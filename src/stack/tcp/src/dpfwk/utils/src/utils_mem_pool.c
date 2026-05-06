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
#include "dp_mp_api.h"

#include "securec.h"

#include "utils_log.h"
#include "utils_base.h"
#include "utils_cfg.h"
#include "utils_atomic.h"
#include "utils_mem_pool.h"

typedef struct {
    DP_Mempool handle;     /**< 内存池handler */
    DP_MempoolCfg_S cfg;   /**< 内存池配置 */
    int64_t mod;             /**< 内存池所属模块 */
    atomic64_t allocCount;   /**< 累计申请次数 */
    atomic64_t freeCount;    /**< 累计释放次数 */
    uint8_t isPrepared;      /**< 内存池是否已经初始化 */
} DP_MemPoolInfo_S;

static uint32_t g_dpMemPoolCount = 0;
static DP_MemPoolInfo_S g_dpMemPoolInfos[DP_MAX_MEM_POOL_INFO_NUM] = { 0 };
static char g_dpMemPoolNames[DP_MAX_MEM_POOL_INFO_NUM][DP_MAX_MEM_POOL_NAME_LEN] = { 0 };

static DP_MempoolHooks_S g_dpMpFns = { 0 };

uint32_t DP_MempoolHookReg(DP_MempoolHooks_S* pHooks)
{
    if (UTILS_IsCfgInited() != 0) {
        DP_LOG_ERR("Mempool hookreg failed, init already!");
        return 1;
    }
    if (g_dpMpFns.mpCreate != NULL) {
        DP_LOG_ERR("Mempool hookreg failed, reg already!");
        return 1;
    }
    if ((pHooks == NULL) ||
        (pHooks->mpAlloc == NULL) ||
        (pHooks->mpCreate == NULL) ||
        (pHooks->mpDestroy == NULL) ||
        (pHooks->mpFree == NULL) ||
        (pHooks->mpConstruct == NULL)) {
        DP_LOG_ERR("Mempool hookreg failed, invalid pHooks!");
        return -1;
    }
    g_dpMpFns.mpCreate = pHooks->mpCreate;
    g_dpMpFns.mpAlloc = pHooks->mpAlloc;
    g_dpMpFns.mpFree = pHooks->mpFree;
    g_dpMpFns.mpDestroy = pHooks->mpDestroy;
    g_dpMpFns.mpConstruct = pHooks->mpConstruct;
    g_dpMpFns.ebufGetseg = pHooks->ebufGetseg;
    g_dpMpFns.ebufRefCntUpdate = pHooks->ebufRefCntUpdate;
    g_dpMpFns.ebufCallback = pHooks->ebufCallback;
    g_dpMpFns.ebufSetRefCnt = pHooks->ebufSetRefCnt;

    return 0;
}

DP_MempoolHooks_S *UTILS_GetMpFunc(void)
{
    return &g_dpMpFns;
}

void* DP_EbufGetNextPbuf(void* ebuf, uint32_t len, uint16_t idx)
{
    if (g_dpMpFns.ebufGetseg != NULL) {
        return g_dpMpFns.ebufGetseg(ebuf, len, idx);
    }
    return NULL;
}

uint16_t DP_EbufRefCntUpdate(void* ptr, int16_t value)
{
    if (g_dpMpFns.ebufRefCntUpdate != NULL) {
        return g_dpMpFns.ebufRefCntUpdate(ptr, value);
    }
    return -1;
}

void DP_EbufCallback(void* ptr)
{
    if (g_dpMpFns.ebufCallback != NULL) {
        g_dpMpFns.ebufCallback(ptr);
    }
}

void DP_EbufSetRefCnt(void* ptr, uint16_t cnt)
{
    if (g_dpMpFns.ebufSetRefCnt != NULL) {
        g_dpMpFns.ebufSetRefCnt(ptr, cnt);
    }
}

int32_t DP_MempoolCreate(const DP_MempoolCfg_S* cfg, const DP_MempoolAttr_S* attr, DP_Mempool* handler)
{
    /* 绑定本文件维护的内存池信息 */
    if (g_dpMemPoolCount >= DP_MAX_MEM_POOL_INFO_NUM) {
        DP_LOG_ERR("Mempool num out of limit!");
        return -1;
    }
    DP_MemPoolInfo_S* info = &g_dpMemPoolInfos[g_dpMemPoolCount];
    info->cfg = *cfg;
    info->cfg.name = g_dpMemPoolNames[g_dpMemPoolCount];
    if (attr != NULL) {
        info->mod = *((int32_t*)(*attr));
    } else {
        info->mod = MOD_SHM; /* 默认模块 */
    }
    *handler = (DP_Mempool)info;
    g_dpMemPoolCount++;

    /* 拷贝内存池名称 */
    int32_t ret = strcpy_s(info->cfg.name, DP_MAX_MEM_POOL_NAME_LEN, cfg->name);
    if (ret != 0) {
        DP_LOG_ERR("mempool name copy failed,maybe mempool name len out of limit.");
        return ret;
    }

    /* 触发回调 */
    ret = -1;
    DP_Mempool tempHandler = NULL;
    if (g_dpMpFns.mpCreate != NULL) {
        ret = g_dpMpFns.mpCreate(cfg, attr, &tempHandler);
    }
    if (ret == 0) {
        info->handle = tempHandler;
        info->isPrepared = 1;
    }
    return ret;
}

void* DP_MempoolAlloc(DP_Mempool mp)
{
    /* 通过handle获取对应的info */
    DP_MemPoolInfo_S* info = (DP_MemPoolInfo_S*)mp;
    if (info == NULL) {
        return NULL;
    }

    void* ret = NULL;
    /* pbuf不可以直接malloc */
    if (info->isPrepared == 0 && info->cfg.type != DP_MEMPOOL_TYPE_PBUF) {
        ret = MEM_MALLOC(info->cfg.size, info->mod, DP_MEM_FREE);
    } else if (g_dpMpFns.mpAlloc != NULL) {
        ret = g_dpMpFns.mpAlloc(info->handle);
    }

    return ret;
}

void DP_MempoolFree(DP_Mempool mp, void* ptr)
{
    /* 通过handle获取对应的info */
    DP_MemPoolInfo_S* info = (DP_MemPoolInfo_S*)mp;
    if (info == NULL) {
        return;
    }

    if (info->isPrepared == 0 && info->cfg.type != DP_MEMPOOL_TYPE_PBUF) {
        MEM_FREE(ptr, DP_MEM_FREE);
    } else if (g_dpMpFns.mpFree != NULL) {
        g_dpMpFns.mpFree(info->handle, ptr);
    }
}

void DP_MempoolDestory(DP_Mempool mp)
{
    DP_MemPoolInfo_S* info = (DP_MemPoolInfo_S*)mp;
    if (info == NULL) {
        return;
    }

    if (g_dpMpFns.mpDestroy != NULL) {
        g_dpMpFns.mpDestroy(info->handle);
    }
}

void* DP_MempoolConstruct(DP_Mempool mp, void* addr, uint64_t offset, uint16_t len)
{
    DP_MemPoolInfo_S* info = (DP_MemPoolInfo_S*)mp;
    if (info == NULL) {
        return NULL;
    }

    if (g_dpMpFns.mpConstruct != NULL) {
        return g_dpMpFns.mpConstruct(info->handle, addr, offset, len);
    }
    return NULL;
}

static inline void ResetMemPoolInfos(void)
{
    for (uint32_t i = 0; i < DP_MAX_MEM_POOL_INFO_NUM; i++) {
        g_dpMemPoolInfos[i] = (DP_MemPoolInfo_S){0};
        (void)memset_s(g_dpMemPoolNames[i], sizeof(g_dpMemPoolNames[i]), 0, sizeof(g_dpMemPoolNames[i]));
    }
    g_dpMemPoolCount = 0;
}

void MempoolHookClr(void)
{
    (void)memset_s(&g_dpMpFns, sizeof(DP_MempoolHooks_S), 0, sizeof(DP_MempoolHooks_S));
    ResetMemPoolInfos();
}