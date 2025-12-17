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
#include <stdlib.h>
#include <unistd.h>

#include "rte_config.h"
#include "rte_errno.h"
#include "rte_mempool.h"

#include "securec.h"

#include "knet_config.h"
#include "knet_log.h"
#include "knet_rpc.h"
#include "knet_types.h"
#include "knet_fmm.h"

#define INVALID_CLIENT_ID (-1)
#define FMM_REQUEST_DATA_SIZE 96
#define FMM_POOL_USED 1
#define FMM_POOL_UNUSED 0

enum FmmRequestType {
    FMM_REQ_TYPE_GET = 0,
    FMM_REQ_TYPE_FREE
};

typedef struct {
    struct rte_mempool *mp; /* 报文池地址 */
    char name[KNET_FMM_POOL_NAME_LEN]; /* 报文池名字 */
    uint8_t used;
    int clientId;
    char padding[3];   // 填充字节，确保结构体8 字节对齐
} FmmPoolCtrl;

struct FmmRequest {
    enum FmmRequestType type;
    char padding[4];   // 填充字节，确保结构体8 字节对齐
    uint8_t data[FMM_REQUEST_DATA_SIZE];
};

struct FmmResponse {
    uint32_t poolId;
    char padding[4];   // 填充字节，确保结构体8 字节对齐
};

typedef struct {
    pthread_mutex_t mutex;                           /* 互斥锁 */
    FmmPoolCtrl poolCtrl[KNET_FMM_POOL_MAX_NUM]; /* 内存池控制块数组 */
} FmmModCtrl;

static FmmModCtrl g_knetFmmPoolModCtrl = {
    .mutex = PTHREAD_MUTEX_INITIALIZER
};

static inline struct rte_mempool *FmmGetPool(uint32_t poolId)
{
    return g_knetFmmPoolModCtrl.poolCtrl[poolId].mp;
}

static inline FmmPoolCtrl *FmmGetFmmPoolCtrl(uint32_t poolId)
{
    return &(g_knetFmmPoolModCtrl.poolCtrl[poolId]);
}

static inline void FmmModLock(void)
{
    (void)pthread_mutex_lock(&g_knetFmmPoolModCtrl.mutex);
}

static inline void FmmModUnlock(void)
{
    (void)pthread_mutex_unlock(&g_knetFmmPoolModCtrl.mutex);
}

static bool FmmPoolIsValid(uint32_t poolId)
{
    if (KNET_UNLIKELY(poolId >= KNET_FMM_POOL_MAX_NUM)) {
        return false;
    }

    if (KNET_UNLIKELY(g_knetFmmPoolModCtrl.poolCtrl[poolId].used == 0)) {
        return false;
    }

    return true;
}

static bool FmmPoolIsExist(const char *name)
{
    FmmModCtrl *modCtrl = &g_knetFmmPoolModCtrl;

    uint32_t i = 0;
    for (i = 0; i < KNET_FMM_POOL_MAX_NUM; i++) {
        if (modCtrl->poolCtrl[i].used == 0) {
            continue;
        }

        if (strncmp(name, modCtrl->poolCtrl[i].name, KNET_FMM_POOL_NAME_LEN - 1) == 0) {
            return true;
        }
    }

    return false;
}

static uint32_t FmmCheckPoolName(const char *name)
{
    if (name[0] == '\0') {
        return KNET_FMM_ERROR;
    }

    if (FmmPoolIsExist(name)) {
        return KNET_FMM_ERROR;
    }

    return KNET_OK;
}

static uint32_t FmmAllocPoolId(uint32_t *poolId)
{
    FmmModCtrl *modCtrl = &g_knetFmmPoolModCtrl;

    uint32_t i = 0;
    for (i = 0; i < KNET_FMM_POOL_MAX_NUM; i++) {
        if (modCtrl->poolCtrl[i].used == 1) {
            continue;
        }

        modCtrl->poolCtrl[i].used = 1;
        *poolId = i;
        return KNET_OK;
    }

    return KNET_FMM_ERROR;
}

static void FmmFreePoolId(uint32_t poolId)
{
    FmmModCtrl *ctrl = &g_knetFmmPoolModCtrl;
    ctrl->poolCtrl[poolId].used = 0;
}

void FmmPoolObjInitCallback(struct rte_mempool *mp, void *para, void *obj, unsigned index)
{
    (void)mp;
    (void)index;

    KnetFmmObjInitCb init = (KnetFmmObjInitCb)para;
    init(obj);
}

uint32_t FmmCreatePool(KNET_FmmPoolCfg *cfg, uint32_t *poolId)
{
    if (cfg == NULL || poolId == NULL) {
        KNET_ERR("Fmm pool create failed, parameter is null");
        return KNET_FMM_ERROR;
    }

    FmmModLock();

    uint32_t ret = FmmCheckPoolName(cfg->name);
    if (ret != KNET_OK) {
        FmmModUnlock();
        KNET_ERR("Fmm pool create failed, name is invalid. ret %u, name %s", ret, cfg->name);
        return ret;
    }

    uint32_t index = 0;
    ret = FmmAllocPoolId(&index);
    if (ret != 0) {
        FmmModUnlock();
        KNET_ERR("Fmm pool create failed, get pool id failed. ret %u, name %s", ret, cfg->name);
        return ret;
    }

    struct rte_mempool *mp = rte_mempool_create(
        cfg->name, cfg->eltNum, cfg->eltSize, cfg->cacheSize, 0, NULL, NULL, NULL, NULL, cfg->socketId, 0);
    if (mp == NULL) {
        FmmFreePoolId(index);
        FmmModUnlock();
        KNET_ERR("Fmm pool create failed. Name %s, ObjNum %u, ObjSize %u, CacheSize %u, NumaId %u, ErrNo %d",
            cfg->name, cfg->eltNum, cfg->eltSize, cfg->cacheSize, cfg->socketId, rte_errno);
        return KNET_FMM_ERROR;
    }

    FmmPoolCtrl *ctrl = FmmGetFmmPoolCtrl(index);
    ctrl->mp = mp;
    (void)strncpy_s(ctrl->name, KNET_FMM_POOL_NAME_LEN, cfg->name, KNET_FMM_POOL_NAME_LEN - 1);

    *poolId = index;
    FmmModUnlock();

    if (cfg->objInit != NULL) {
        rte_mempool_obj_iter(mp, FmmPoolObjInitCallback, cfg->objInit);
    }
    return KNET_OK;
}

static int GetCfgNew(KNET_FmmPoolCfg *cfgNew, KNET_FmmPoolCfg *cfg)
{
    int ret = memcpy_s(cfgNew, sizeof(KNET_FmmPoolCfg), (char *)cfg, sizeof(KNET_FmmPoolCfg));
    if (ret != 0) {
        KNET_ERR("Cfg new memcpy_s failed, ret %d", ret);
        return KNET_ERROR;
    }

    int qid = KNET_GetCfg(CONF_INNER_QID)->intValue;
    int len = sprintf_s(cfgNew->name, KNET_FMM_POOL_NAME_LEN, "%s_%d", cfg->name, qid);
    if ((len < 0) || len > KNET_FMM_POOL_NAME_LEN) {
        KNET_ERR("Get cfg new name err, ret %d", len);
        return KNET_ERROR;
    }

    if (rte_mempool_lookup(cfgNew->name) != NULL) {
        KNET_ERR("Fmm pool already exist. name %s", cfgNew->name);
        return KNET_ERROR;
    }

    return KNET_OK;
}

static int FmmCreatePoolPostProcess(KNET_FmmPoolCfg *cfgNew, uint32_t *poolId,
    struct KNET_RpcMessage *knetRpcResponse)
{
    if (knetRpcResponse->ret != 0) {
        KNET_ERR("Fmm pool create failed name %s, request ret %d",
            cfgNew->name, knetRpcResponse->ret);
        return KNET_ERROR;
    }

    FmmModLock();
    struct FmmResponse *resp = (struct FmmResponse *)knetRpcResponse->fixedLenData;
    if (resp->poolId >= KNET_FMM_POOL_MAX_NUM) {
        FmmModUnlock();
        return KNET_ERROR;
    }

    FmmPoolCtrl *ctrl = FmmGetFmmPoolCtrl(resp->poolId);
    ctrl->mp = rte_mempool_lookup(cfgNew->name);
    if (ctrl->mp == NULL) {
        KNET_ERR("Fmm pool create failed, name %s", cfgNew->name);
        FmmModUnlock();
        return KNET_ERROR;
    }

    *poolId = resp->poolId;
    ctrl->used = FMM_POOL_USED;
    int ret = memcpy_s(ctrl->name, KNET_FMM_POOL_NAME_LEN, (char *)cfgNew->name, KNET_FMM_POOL_NAME_LEN);
    if (ret != 0) {
        KNET_ERR("Memcpy request failed, ret %d", ret);
        FmmModUnlock();
        return KNET_ERROR;
    }

    if (cfgNew->objInit != NULL) {
        rte_mempool_obj_iter(ctrl->mp, FmmPoolObjInitCallback, cfgNew->objInit);
    }
    FmmModUnlock();

    return KNET_OK;
}

// 从主进程获取分配的fmm内存池
uint32_t FmmCreatePoolFromPrimary(KNET_FmmPoolCfg *cfg, uint32_t *poolId)
{
    struct KNET_RpcMessage knetRpcRequest = {0};
    struct FmmRequest *req = (struct FmmRequest *)knetRpcRequest.fixedLenData;
    KNET_FmmPoolCfg *cfgNew = (KNET_FmmPoolCfg *)req->data;

    if (cfg == NULL || poolId == NULL) {
        KNET_ERR("Fmm pool create failed, parameter is null");
        return KNET_ERROR;
    }

    int ret = 0;
    ret = GetCfgNew(cfgNew, cfg);
    if (ret != 0) {
        KNET_ERR("Fmm cfg create failed");
        return KNET_ERROR;
    }

    req->type = FMM_REQ_TYPE_GET;
    knetRpcRequest.dataLen = sizeof(struct FmmRequest);
    knetRpcRequest.dataType = RPC_MSG_DATA_TYPE_FIXED_LEN;
    struct KNET_RpcMessage knetRpcReponse = {0};
    ret = KNET_RpcCall(KNET_RPC_MOD_FMM, &knetRpcRequest, &knetRpcReponse);
    if (ret != 0) {
        KNET_ERR("Fmm pool create rpc request failed, ret %d", ret);
        return KNET_ERROR;
    }

    ret = FmmCreatePoolPostProcess(cfgNew, poolId, &knetRpcReponse);
    if (ret != KNET_OK) {
        KNET_ERR("Fmm pool create post process failed");
        return KNET_ERROR;
    }

    KNET_INFO("Fmm pool create success.  Name %s, poolId %u",
        cfgNew->name, *poolId);

    return KNET_OK;
}

uint32_t KNET_FmmCreatePool(KNET_FmmPoolCfg *cfg, uint32_t *poolId)
{
    int procType = KNET_GetCfg(CONF_INNER_PROC_TYPE)->intValue;
    if (procType == (int)KNET_PROC_TYPE_PRIMARY) {
        return FmmCreatePool(cfg, poolId);
    }

    return FmmCreatePoolFromPrimary(cfg, poolId);
}

uint32_t FmmDestroyPool(uint32_t poolId, enum KNET_ProcType procType)
{
    FmmModLock();
    if (!FmmPoolIsValid(poolId)) {
        FmmModUnlock();
        KNET_ERR("Fmm pool destroy failed, poolId is invalid. PoolId %u", poolId);
        return KNET_FMM_ERROR;
    }

    if (procType == KNET_PROC_TYPE_PRIMARY) {
        struct rte_mempool *mp = FmmGetPool(poolId);
        rte_mempool_free(mp);
    }
    FmmFreePoolId(poolId);

    FmmPoolCtrl *ctrl = FmmGetFmmPoolCtrl(poolId);
    ctrl->mp = NULL;
    ctrl->name[0] = '\0';
    ctrl->clientId = INVALID_CLIENT_ID;

    FmmModUnlock();
    return KNET_OK;
}

uint32_t FmmDestroyPoolFromPrimary(uint32_t poolId)
{
    struct KNET_RpcMessage knetRpcRequest = {0};
    struct FmmRequest *req = (struct FmmRequest *)knetRpcRequest.fixedLenData;

    req->type = FMM_REQ_TYPE_FREE;
    int ret = 0;
    ret = memcpy_s(req->data, FMM_REQUEST_DATA_SIZE, (char *)&poolId, sizeof(uint32_t));
    if (ret != 0) {
        KNET_ERR("Memcpy request failed, ret %d", ret);
        return KNET_ERROR;
    }

    knetRpcRequest.dataLen = sizeof(struct FmmRequest);
    knetRpcRequest.dataType = RPC_MSG_DATA_TYPE_FIXED_LEN;
    struct KNET_RpcMessage knetRpcReponse = {0};
    ret = KNET_RpcCall(KNET_RPC_MOD_FMM, &knetRpcRequest, &knetRpcReponse);
    if (ret != 0) {
        KNET_ERR("Fmm pool delete rpc request failed, ret %d", ret);
        return KNET_ERROR;
    }

    if (knetRpcReponse.ret != 0) {
        KNET_ERR("Fmm pool delete failed, poolId %u ret %d", poolId, knetRpcReponse.ret);
        return KNET_ERROR;
    }

    return FmmDestroyPool(poolId, KNET_PROC_TYPE_SECONDARY);
}

uint32_t KNET_FmmDestroyPool(uint32_t poolId)
{
    int procType = KNET_GetCfg(CONF_INNER_PROC_TYPE)->intValue;
    if (procType == (int)KNET_PROC_TYPE_PRIMARY) {
        return FmmDestroyPool(poolId, KNET_PROC_TYPE_PRIMARY);
    }

    return FmmDestroyPoolFromPrimary(poolId);
}

static int FmmGetPoolId(KNET_FmmPoolCfg *cfg, uint32_t *poolId)
{
    int ret = (int)FmmCheckPoolName(cfg->name);
    if (ret != KNET_OK) {
        KNET_ERR("Fmm pool create failed, name is invalid. ret %u, name %s", ret, cfg->name);
        return ret;
    }

    ret = (int)FmmAllocPoolId(poolId);
    if (ret != 0) {
        KNET_ERR("Fmm pool create failed, get pool id failed. ret %u, name %s", ret, cfg->name);
        return ret;
    }

    return KNET_OK;
}

// FMM资源申请
static int FmmGetRequestHandler(int clientId, KNET_FmmPoolCfg *cfg, struct KNET_RpcMessage *knetRpcResponse)
{
    uint32_t poolId = 0;
    FmmModLock();
    int ret = FmmGetPoolId(cfg, &poolId);
    if (ret != KNET_OK) {
        FmmModUnlock();
        KNET_ERR("Fmm pool alloc pool id failed");
        return KNET_ERROR;
    }

    struct rte_mempool *mp = rte_mempool_create(
        cfg->name, cfg->eltNum, cfg->eltSize, cfg->cacheSize, 0, NULL, NULL, NULL, NULL, cfg->socketId, 0);
    if (mp == NULL) {
        KNET_ERR("Fmm pool create failed. Name %s, ObjNum %u, ObjSize %u, CacheSize %u, NumaId %u, ErrNo %d",
            cfg->name, cfg->eltNum, cfg->eltSize, cfg->cacheSize, cfg->socketId, rte_errno);
        FmmFreePoolId(poolId);
        FmmModUnlock();
        return KNET_ERROR;
    }

    FmmPoolCtrl *ctrl = FmmGetFmmPoolCtrl(poolId);
    ctrl->mp = mp;
    ctrl->clientId = clientId;
    ctrl->used = FMM_POOL_USED;
    (void)strncpy_s(ctrl->name, KNET_FMM_POOL_NAME_LEN, cfg->name, KNET_FMM_POOL_NAME_LEN - 1);

    struct FmmResponse *resp = (struct FmmResponse *)knetRpcResponse->fixedLenData;
    resp->poolId = poolId;
    KNET_INFO("Fmm pool create success mp %s poolId %d clientId %d", ctrl->name, poolId, ctrl->clientId);

    FmmModUnlock();
    return KNET_OK;
}

// 主进程FMM资源回收
static int FmmFreeRequestHandler(int clientId, uint32_t poolId)
{
    int ret = (int)FmmDestroyPool(poolId, KNET_PROC_TYPE_PRIMARY);
    if (ret != KNET_OK) {
        KNET_ERR("Free fmm pool failed, clientId %d, poolId %u", clientId, poolId);
    }

    return ret;
}

// FMM请求回调函数
int FmmRequestHandler(int clientId, struct KNET_RpcMessage *knetRpcRequest, struct KNET_RpcMessage *knetRpcResponse)
{
    knetRpcResponse->dataType = RPC_MSG_DATA_TYPE_FIXED_LEN;
    knetRpcResponse->dataLen = 0;
    struct FmmRequest *req = (struct FmmRequest *)knetRpcRequest->fixedLenData;
    
    int ret = 0;
    switch (req->type) {
        case FMM_REQ_TYPE_GET: {
            KNET_FmmPoolCfg *cfg = (KNET_FmmPoolCfg *)req->data;
            ret = FmmGetRequestHandler(clientId, cfg, knetRpcResponse);
            if (ret != KNET_OK) {
                KNET_ERR("Get fmm pool request failed");
            }
            break;
        }
        case FMM_REQ_TYPE_FREE: {
            uint32_t poolId = *(uint32_t *)req->data;
            ret = FmmFreeRequestHandler(clientId, poolId);
            if (ret != KNET_OK) {
                KNET_ERR("Free fmm pool request failed");
            }
            break;
        }
        default: {
            ret = KNET_ERROR;
            KNET_ERR("Free fmm pool request type err");
            break;
        }
    }

    knetRpcResponse->ret = ret;
    return ret;
}

// 连接断开时执行资源回收
int FmmDisconnetHandler(int clientId, struct KNET_RpcMessage *knetRpcRequest, struct KNET_RpcMessage *knetRpcResponse)
{
    int ret = KNET_OK;
    for (int i = 0; i < KNET_FMM_POOL_MAX_NUM; i++) {
        FmmPoolCtrl *poolCtrl = &g_knetFmmPoolModCtrl.poolCtrl[i];
        if (poolCtrl->clientId != clientId || poolCtrl->used == FMM_POOL_UNUSED) {
            continue;
        }

        KNET_INFO("Fmm disconnect uninit, clientId %d, poolName %s poolId %d , is used %d",
            poolCtrl->clientId, poolCtrl->name, i, poolCtrl->used);
        uint32_t delRet = FmmDestroyPool(i, KNET_PROC_TYPE_PRIMARY);
        if (delRet != KNET_OK) {
            KNET_ERR("Fmm disconnect uninit, failed clientId %d, poolName %s poolId %d , is used %d",
                poolCtrl->clientId, poolCtrl->name, i, poolCtrl->used);
            ret = KNET_ERROR;
        }
    }
    return ret;
}

// 主进程初始化注册FMM相关请求回调函数
int KNET_InitFmm(enum KNET_ProcType procType)
{
    if (procType != KNET_PROC_TYPE_PRIMARY) {
        return KNET_ERROR;
    }

    int ret = 0;
    ret = KNET_RpcRegServer(KNET_RPC_EVENT_REQUEST, KNET_RPC_MOD_FMM, FmmRequestHandler);
    if (ret != 0) {
        KNET_ERR("Register fmm request handler failed");
        return KNET_ERROR;
    }

    ret = KNET_RpcRegServer(KNET_RPC_EVENT_DISCONNECT, KNET_RPC_MOD_FMM, FmmDisconnetHandler);
    if (ret != 0) {
        KNET_ERR("Register fmm disconnect handler failed");
        return KNET_ERROR;
    }

    return KNET_OK;
}

int KNET_UnInitFmm(enum KNET_ProcType procType)
{
    int ret = KNET_OK;

    for (int i = 0; i < KNET_FMM_POOL_MAX_NUM; i++) {
        FmmPoolCtrl *poolCtrl = &g_knetFmmPoolModCtrl.poolCtrl[i];
        if (poolCtrl->used == 0) {
            continue;
        }

        KNET_INFO("Fmm uninit, clientId %d, poolName %s poolId %d , is used %d",
            poolCtrl->clientId, poolCtrl->name, i, poolCtrl->used);
        uint32_t delRet = FmmDestroyPool(i, procType);
        if (delRet != KNET_OK) {
            KNET_ERR("Fmm uninit, failed clientId %d, poolName %s poolId %d , is used %d",
                poolCtrl->clientId, poolCtrl->name, i, poolCtrl->used);
            ret = KNET_ERROR;
        }
    }

    KNET_RpcDesServer(KNET_RPC_EVENT_REQUEST, KNET_RPC_MOD_FMM);
    KNET_RpcDesServer(KNET_RPC_EVENT_DISCONNECT, KNET_RPC_MOD_FMM);

    return ret;
}

uint32_t KNET_FmmAlloc(uint32_t poolId, void **obj)
{
    if (KNET_UNLIKELY(obj == NULL)) {
        KNET_ERR("Fmm pool alloc failed, obj is null.");
        return KNET_FMM_ERROR;
    }

    if (!FmmPoolIsValid(poolId)) {
        KNET_ERR("Fmm pool alloc failed, poolId is invalid. PoolId %u", poolId);
        return KNET_FMM_ERROR;
    }

    struct rte_mempool *mp = FmmGetPool(poolId);
    int ret = rte_mempool_get(mp, obj);
    if (ret != 0) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fmm pool alloc failed, get memory failed. ret %d, PoolId %u, ErrNo %d",
            ret, poolId, rte_errno);
        return KNET_FMM_ERROR;
    }

    return KNET_OK;
}

uint32_t KNET_FmmFree(uint32_t poolId, void *obj)
{
    if (KNET_UNLIKELY(obj == NULL)) {
        KNET_ERR("Fmm pool alloc failed, obj is null.");
        return KNET_FMM_ERROR;
    }

    if (!FmmPoolIsValid(poolId)) {
        KNET_ERR("Fmm pool free failed, poolId is invalid. PoolId %u", poolId);
        return KNET_FMM_ERROR;
    }

    struct rte_mempool *mp = FmmGetPool(poolId);
    rte_mempool_put(mp, obj);

    return KNET_OK;
}