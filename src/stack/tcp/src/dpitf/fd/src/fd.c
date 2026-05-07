/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include "dp_fd.h"

#include <securec.h>

#include "dp_errno.h"

#include "utils_base.h"
#include "utils_cfg.h"
#include "utils_log.h"
#include "utils_spinlock.h"
#include "dp_socket_types_api.h"

typedef struct FdNode {
    struct FdNode* next;
    Fd_t*          file;
    Spinlock_t     lock;
    bool           isClosed;
} ALIGNED_TO(CACHE_LINE) FdNode_t;

typedef struct {
    int           maxFdCnt; // 表示最大创建的fd的数量
    FdNode_t*     nodes;    // 在实际使用时进行初始化内存
    FdNode_t*     unusedNodes;
    FdNode_t*     unusedNodesTail;
    Spinlock_t    lock;
} FdTbl_t;

int            g_fdOffset = 0;
static FdTbl_t g_fdTbl    = { 0 };
static Fd_t* g_zcopyFile[DP_HIGHLIMIT_TCPCB_MAX + DP_HIGHLIMIT_UDPCB_MAX + DP_DEFAULT_ARP_MAX];

static int InitFdNodes(FdTbl_t* tbl)
{
    int       maxFdCnt;
    FdNode_t* node;
    size_t    allocSize;

    maxFdCnt = CFG_GET_VAL(DP_CFG_TCPCB_MAX);
    maxFdCnt += CFG_GET_VAL(DP_CFG_UDPCB_MAX);
    maxFdCnt += CFG_GET_VAL(DP_CFG_EPOLLCB_MAX);

    allocSize = (uint32_t)maxFdCnt * sizeof(FdNode_t);

    tbl->maxFdCnt = maxFdCnt;
    tbl->nodes    = MEM_MALLOC_ALIGN(allocSize, CACHE_LINE, MOD_FD, DP_MEM_FIX);
    if (tbl->nodes == NULL) {
        DP_LOG_ERR("Malloc memory failed for fdNodes.");
        return -1;
    }

    (void)memset_s(tbl->nodes, allocSize, 0, allocSize);

    node = &tbl->nodes[0];
    for (int i = 0; i < maxFdCnt - 1; i++, node++) {
        node->next = (node + 1);
    }

    tbl->unusedNodes     = &tbl->nodes[0];
    tbl->unusedNodesTail = &tbl->nodes[tbl->maxFdCnt - 1];

    return 0;
}

int FD_Init(void)
{
    FdTbl_t* tbl = &g_fdTbl;

    if (tbl->nodes != NULL) {
        DP_LOG_ERR("FD_Init failed for g_fdTbl->nodes not NULL.");
        return -1;
    }

    if (SPINLOCK_Init(&tbl->lock) != 0) {
        return -1;
    }

    if (InitFdNodes(tbl) != 0) {
        return -1;
    }

    g_fdOffset = CFG_GET_VAL(CFG_FD_OFFSET);

    return 0;
}

static inline int LockFdTbl(void)
{
    return SPINLOCK_DoLock(&g_fdTbl.lock);
}

static inline void UnlockFdTbl(void)
{
    return SPINLOCK_DoUnlock(&g_fdTbl.lock);
}

static inline void LockFdNode(FdNode_t* node)
{
    SPINLOCK_Lock(&node->lock);
}

static inline void UnlockFdNode(FdNode_t* node)
{
    SPINLOCK_Unlock(&node->lock);
}

static FdNode_t* GetUnusedNode(FdTbl_t* tbl)
{
    FdNode_t* node;

    node = tbl->unusedNodes;
    if (node == NULL) {
        return NULL;
    }

    tbl->unusedNodes = node->next;
    node->next       = NULL;
    if (tbl->unusedNodes == NULL) {
        tbl->unusedNodesTail = NULL;
    }

    return node;
}

static void PutUnusedNode(FdTbl_t* tbl, FdNode_t* node)
{
    if (tbl->unusedNodesTail == NULL) {
        tbl->unusedNodes = node;
    } else {
        tbl->unusedNodesTail->next = node;
    }
    tbl->unusedNodesTail = node;
}

static inline int RefFd(Fd_t* file)
{
    uint32_t ref;

    while (1) {
        ref = ATOMIC32_Load(&file->ref);
        if (ref == 0) {
            return -1;
        }
        if (ATOMIC32_Cas(&file->ref, ref, ref + 1)) {
            return 0;
        }
    }
}

Fd_t* FD_Alloc(void)
{
    Fd_t*     file;
    FdNode_t* node;

    file = MEM_MALLOC(sizeof(Fd_t), MOD_FD, DP_MEM_FREE);
    if (file == NULL) {
        DP_LOG_ERR("Malloc memory failed for fd file.");
        DP_ADD_ABN_STAT(DP_FD_MEM_ERR);
        DP_SET_ERRNO(ENOMEM);
        return NULL;
    }

    (void)memset_s(file, sizeof(Fd_t), 0, sizeof(Fd_t));

    file->ref = 1;

    LockFdTbl();

    node = GetUnusedNode(&g_fdTbl);
    if (node != NULL) {
        node->file  = file;
        file->fdIdx = node - g_fdTbl.nodes;
        node->isClosed = false;
        g_zcopyFile[FD_GetUserFd(file)] = file;
    } else {
        MEM_FREE(file, DP_MEM_FREE);
        DP_ADD_ABN_STAT(DP_FD_NODE_FULL);
        DP_SET_ERRNO(EMFILE);
        file = NULL;
    }

    UnlockFdTbl();
    return file;
}

void FD_Free(Fd_t* file)
{
    FdNode_t* node = &g_fdTbl.nodes[file->fdIdx];

    LockFdTbl();

    // 调用 FD_Free 时，node 不会被外界持有，无需加锁
    node->file = NULL;
    node->isClosed = true;
    g_zcopyFile[FD_GetUserFd(file)] = NULL;

    PutUnusedNode(&g_fdTbl, node);

    UnlockFdTbl();

    MEM_FREE(file, DP_MEM_FREE);
}

static int FdClose(int realFd)
{
    Fd_t* file;
    FdNode_t* node = &g_fdTbl.nodes[realFd];

    LockFdNode(node);

    file = node->file;
    if (file == NULL || node->isClosed || (file->ops->canClose(file->priv) != 1)) {
        UnlockFdNode(node);

        DP_LOG_DBG("FdClose failed, fd is invalid.");
        return -EBADF;
    }

    node->isClosed = true;

    UnlockFdNode(node);

    FD_Put(file);

    return 0;
}

int FD_Close(int fd)
{
    int realFd = FD_GetRealFd(fd);
    int ret;

    if (realFd < 0 || realFd >= g_fdTbl.maxFdCnt) {
        DP_LOG_DBG("PosixClose failed, invalid fd.");
        return -EBADF;
    }

    ret = FdClose(realFd);
    if ((ret != 0)) {
        DP_SET_ERRNO(-ret);
        return -1;
    }
    g_zcopyFile[fd] = NULL;

    return ret;
}

int DP_Close(int fd)
{
    return FD_Close(fd);
}

static int FdGet(int realFd, int type, Fd_t** out, int refOp)
{
    int ret = 0;
    Fd_t* file;
    FdNode_t* node = &g_fdTbl.nodes[realFd];

    if (refOp != DP_FD_OP_NOREF) {
        LockFdNode(node);
    }

    file = node->file;

    if (node->isClosed || file == NULL) {
        DP_ADD_ABN_STAT(DP_FD_GET_CLOSED);
        ret = -EBADF;
        goto end;
    }

    if (type != FD_TYPE_INVALID && file->type != type) {
        ret = (type == FD_TYPE_SOCKET) ? -ENOTSOCK : -EINVAL;
        DP_ADD_ABN_STAT(DP_FD_GET_INVAL_TYPE);
        goto end;
    }

    if (refOp == DP_FD_OP_DEFAULT && RefFd(file) != 0) {
        DP_ADD_ABN_STAT(DP_FD_GET_REF_ERR);
        ret = -EBADF;
        goto end;
    }

    *out = file;

end:
    if (refOp != DP_FD_OP_NOREF) {
        UnlockFdNode(node);
    }
    return ret;
}

int DP_Fcntl(int fd, int cmd, int val)
{
    Fd_t*   file;
    int     ret = 0;
    ret = FD_Get(fd, FD_TYPE_INVALID, &file);
    if (ret != 0) {
        return ret;
    }

    if ((CFG_GET_VAL(DP_CFG_DEPLOYMENT) == DP_DEPLOYMENT_CO_THREAD) || (CFG_GET_VAL(CFG_NOLOCK) == DP_ENABLE))  {
        if (file->ops->canFcntl(file->priv) != 1) {
            FD_Put(file);
            DP_LOG_ERR("Fcntl failed, fd is invalid.");
            DP_SET_ERRNO(EBADF);
            return -1;
        }
    }

    switch (cmd) {
        case DP_F_GETFD:
            ret = (int)file->flags;
            break;
        case DP_F_SETFD:
            file->flags |= (uint32_t)val;
            break;
        case DP_F_GETFL:
        case DP_F_SETFL:
            if (file->ops->fcntl != NULL) {
                ret = file->ops->fcntl(file->priv, cmd, val);
            } else {
                ret = -EINVAL;
                DP_LOG_DBG("Sock fcntl failed, DP_F_SETFL failed.");
            }
            break;
        default:
            ret = -EINVAL;
            DP_LOG_DBG("Sock fcntl failed, cmd %d not support", cmd);
            break;
    }

    FD_Put(file);
    if (ret < 0) {
        DP_SET_ERRNO(-ret);
    }

    return ret < 0 ? -1 : ret;
}

int FD_Get(int fd, int type, Fd_t** file)
{
    int realFd = FD_GetRealFd(fd);
    int ret = 0;

    if ((realFd < 0) || (realFd >= g_fdTbl.maxFdCnt) || (g_fdTbl.nodes == NULL)) {
        DP_LOG_DBG("Fd get failed, invalid fd.");
        DP_ADD_ABN_STAT(DP_FD_GET_INVAL);
        return -EBADF;
    }

    ret = FdGet(realFd, type, file, DP_FD_OP_DEFAULT);

    return ret;
}

int FD_GetOptRef(int fd, int type, Fd_t** file)
{
    int realFd = FD_GetRealFd(fd);
    int ret = 0;
    int refOp = DP_FD_OP_DEFAULT;

    if ((realFd < 0) || (realFd >= g_fdTbl.maxFdCnt) || (g_fdTbl.nodes == NULL)) {
        DP_LOG_DBG("Fd get failed, invalid fd.");
        DP_ADD_ABN_STAT(DP_FD_GET_INVAL);
        return -EBADF;
    }

    if (CFG_GET_VAL(DP_CFG_DEPLOYMENT) == DP_DEPLOYMENT_CO_THREAD) {
        refOp = DP_FD_OP_NOREF;
    }
    ret = FdGet(realFd, type, file, refOp);

    return ret;
}

void FD_Put(Fd_t* file)
{
    uint32_t ref;

    ref = ATOMIC32_Dec(&file->ref);
    if (ref == 0) {
        file->ops->close(file->priv);
    }
}

void FD_PutOptRef(Fd_t* file)
{
    if (CFG_GET_VAL(DP_CFG_DEPLOYMENT) != DP_DEPLOYMENT_CO_THREAD) {
        FD_Put(file);
    }
}

Fd_t* FD_GetFileOpt(int fd)
{
    return g_zcopyFile[fd];
}

int FD_GetFileLimit(void)
{
    return g_fdTbl.maxFdCnt;
}

void FD_Deinit(void)
{
    FdTbl_t *tbl = &g_fdTbl;

    if (tbl->nodes != NULL) {
        MEM_FREE(tbl->nodes, DP_MEM_FIX);
        tbl->nodes = NULL;
    }

    tbl->unusedNodes = NULL;
    tbl->unusedNodesTail = NULL;
    SPINLOCK_Deinit(&tbl->lock);
}
