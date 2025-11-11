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

#include "fd.h"

#include <securec.h>

#include "dp_errno.h"

#include "utils_base.h"
#include "utils_cfg.h"
#include "utils_log.h"
#include "utils_debug.h"
#include "utils_spinlock.h"

typedef struct FdNode {
    struct FdNode* next;
    Fd_t*          file;
} FdNode_t;

typedef struct {
    int           maxFdCnt; // 表示最大创建的fd的数量
    FdNode_t*     nodes;    // 在实际使用时进行初始化内存
    atomic32_t    ref;

    FdNode_t*      unusedNodes;
    FdNode_t*      unusedNodesTail;
    Spinlock_t     lock;
} FdTbl_t;

int            g_fdOffset = 0;
static FdTbl_t g_fdTbl    = { 0 };

static int InitFdNodes(FdTbl_t* tbl)
{
    int       maxFdCnt;
    FdNode_t* node;

    maxFdCnt = CFG_GET_VAL(DP_CFG_TCPCB_MAX);
    maxFdCnt += CFG_GET_VAL(DP_CFG_UDPCB_MAX);
    maxFdCnt += CFG_GET_VAL(CFG_RAWCB_IP_MAX);
    maxFdCnt += CFG_GET_VAL(CFG_RAWCB_ETH_MAX);

    tbl->maxFdCnt = maxFdCnt;
    tbl->nodes    = MEM_MALLOC((uint32_t)maxFdCnt * sizeof(FdNode_t), MOD_FD, DP_MEM_FIX);
    if (tbl->nodes == NULL) {
        DP_LOG_ERR("Malloc memory failed for fdNodes.");
        return -1;
    }
    (void)memset_s(tbl->nodes, (uint32_t)maxFdCnt * sizeof(FdNode_t), 0, (uint32_t)maxFdCnt * sizeof(FdNode_t));
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

static inline void RefTbl(void)
{
    ATOMIC32_Inc(&g_fdTbl.ref);
}

static inline void DerefTbl(void)
{
    ATOMIC32_Dec(&g_fdTbl.ref);
}

static inline void WaitTblIdle(void)
{
    while (ATOMIC32_Load(&g_fdTbl.ref) != 0) { }
}

static inline int LockFdTbl(void)
{
    return SPINLOCK_Lock(&g_fdTbl.lock);
}

static inline void UnlockFdTbl(void)
{
    return SPINLOCK_Unlock(&g_fdTbl.lock);
}

static FdNode_t* GetUnusedNode(void)
{
    FdNode_t* node;
    FdTbl_t*  tbl = &g_fdTbl;

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

static Fd_t* PutUnusedNodeUnsafe(FdTbl_t* tbl, int fdIdx)
{
    Fd_t*     file;
    FdNode_t* node;

    node = &tbl->nodes[fdIdx];
    if (node->file == NULL) {
        return NULL;
    }

    ASSERT(node->next == NULL);

    file       = node->file;
    node->file = NULL;
    node->next = NULL;

    if (tbl->unusedNodesTail == NULL) {
        tbl->unusedNodes = node;
    } else {
        tbl->unusedNodesTail->next = node;
    }
    tbl->unusedNodesTail = node;

    return file;
}

static Fd_t* PutUnusedNode(int fdIdx)
{
    Fd_t* ret;

    LockFdTbl();

    ret = PutUnusedNodeUnsafe(&g_fdTbl, fdIdx);

    UnlockFdTbl();

    return ret;
}

Fd_t* FD_Alloc(void)
{
    Fd_t*     file;
    FdNode_t* node;

    file = MEM_MALLOC(sizeof(Fd_t), MOD_FD, DP_MEM_FREE);
    if (file == NULL) {
        DP_LOG_ERR("Malloc memory failed for fd file.");
        DP_SET_ERRNO(ENOMEM);
        return NULL;
    }

    (void)memset_s(file, sizeof(Fd_t), 0, sizeof(Fd_t));

    file->ref = 1;

    RefTbl();
    LockFdTbl();

    node = GetUnusedNode();
    if (node != NULL) {
        node->file  = file;
        file->fdIdx = node - g_fdTbl.nodes;
    } else {
        MEM_FREE(file, DP_MEM_FREE);
        DP_SET_ERRNO(EMFILE);
        file = NULL;
    }

    UnlockFdTbl();
    DerefTbl();

    return file;
}

void FD_Free(Fd_t* file)
{
    ASSERT(file->priv == NULL);

    PutUnusedNode(file->fdIdx);
    MEM_FREE(file, DP_MEM_FREE);
}

static int FdClose(int realFd)
{
    Fd_t* file;

    RefTbl();

    file = PutUnusedNode(realFd);
    if (file == NULL) {
        DP_LOG_ERR("PosixClose failed, no unused node.");
        DerefTbl();
        return -EBADF;
    }

    DerefTbl();
    WaitTblIdle();

    FD_Put(file);

    return 0;
}

int FD_Close(int fd)
{
    int realFd = FD_GetRealFd(fd);
    int ret;

    if (realFd < 0 || realFd >= g_fdTbl.maxFdCnt) {
        DP_LOG_ERR("PosixClose failed, invalid fd.");
        return -EBADF;
    }

    ret = FdClose(realFd);

    return ret;
}

int DP_Close(int fd)
{
    return FD_Close(fd);
}

static int FdGet(int realFd, int type, Fd_t** out)
{
    Fd_t* file = g_fdTbl.nodes[realFd].file;
    if (file == NULL) {
        return -EBADF;
    }

    if (file->type != type) {
        if (type == FD_TYPE_SOCKET) {
            return -ENOTSOCK;
        }
        return -EINVAL;
    }

    ATOMIC32_Inc(&file->ref);

    *out = file;

    return 0;
}

int FD_Get(int fd, int type, Fd_t** file)
{
    int realFd = FD_GetRealFd(fd);
    int ret = 0;

    if ((realFd < 0) || (realFd >= g_fdTbl.maxFdCnt) || (g_fdTbl.nodes == NULL)) {
        DP_LOG_ERR("Fd get failed, invalid fd.");
        return -EBADF;
    }

    RefTbl();

    ret = FdGet(realFd, type, file);

    DerefTbl();

    return ret;
}

void FD_Put(Fd_t* file)
{
    uint32_t ref;

    ref = ATOMIC32_Dec(&file->ref);
    if (ref == 0) {
        file->ops->close(file->priv);
        MEM_FREE(file, DP_MEM_FREE);
    }
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
    tbl->ref = 0;
    SPINLOCK_Deinit(&tbl->lock);
}
