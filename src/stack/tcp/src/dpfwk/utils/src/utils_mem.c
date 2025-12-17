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
#include "utils_base.h"
#include "utils_log.h"
#include "utils_cfg.h"
#include "utils_atomic.h"

#include "dp_mem_api.h"
// #include "dp_mem.h"

DP_MemHooks_t g_memFns = {0};

uint64_t g_memFixCnt[MOD_MAX];
uint64_t g_memFreeCnt[MOD_MAX];
uint64_t g_memZSendCnt[DP_HIGHLIMIT_WORKER_MAX];
uint64_t g_memZRecvCnt[DP_HIGHLIMIT_WORKER_MAX];

static uint64_t* g_memCntTable[DP_MEM_MAX] = {
    [DP_MEM_FIX]        = g_memFixCnt,
    [DP_MEM_FREE]       = g_memFreeCnt,
    [DP_MEM_ZCOPY_SEND] = g_memZSendCnt,
    [DP_MEM_ZCOPY_RECV] = g_memZRecvCnt,
};

/* 原子操作内存打点值增减 */
#define DP_ATOMIC_MEM_ADD(mod, size, type) \
    ATOMIC64_Add(&(g_memCntTable[(type)][(mod)]), (size))

#define DP_ATOMIC_MEM_SUB(mod, size, type) \
    ATOMIC64_Sub(&(g_memCntTable[(type)][(mod)]), (size))

#define DP_ATOMIC_MEM_GET(mod, type) \
    ATOMIC64_Load(&(g_memCntTable[(type)][(mod)]))

    /* 变长内存操作接口注册函数 */
uint32_t DP_MemHookReg(DP_MemHooks_S *memHooks)
{
    if (UTILS_IsCfgInited() != 0) {
        DP_LOG_ERR("MemHookReg failed, init already!");
        return 1;
    }
    if (UTILS_GetBaseFunc()->memFns != NULL) {
        DP_LOG_ERR("MemHookReg failed, reg already!");
        return 1;
    }
    if ((memHooks == NULL) || (memHooks->mAlloc == NULL) || (memHooks->mFree == NULL)) {
        DP_LOG_ERR("MemHookReg failed, invalid memHooks!");
        return 1;
    }

    g_memFns.mAlloc = memHooks->mAlloc;
    g_memFns.mFree   = memHooks->mFree;

    UTILS_GetBaseFunc()->memFns = &g_memFns;

    return 0;
}

/* 非原子操作内存打点值增减 */
#define DP_ADD_MEM_STAT(wid, size, type)    g_memCntTable[(type)][(wid)] += (size)
#define DP_SUB_MEM_STAT(wid, size, type)    g_memCntTable[(type)][(wid)] -= (size)

/* 变长内存申请函数 */
void* DP_MemAlloc(size_t size, uint32_t mod, DP_MemType_t type)
{
    size_t headSize = sizeof(DP_MemInfo_t);
    if (SIZE_MAX - headSize < size) {
        return NULL;
    }

    DP_MemInfo_t *memInfo = UTILS_GetBaseFunc()->memFns->mAlloc(size + headSize);
    if (memInfo == NULL) {
        return NULL;
    }
    memInfo->mod = mod;
    memInfo->padLen = 0;
    memInfo->size = size + headSize;
    DP_ATOMIC_MEM_ADD(mod, memInfo->size, type);

    return (void *)((int8_t *)memInfo + headSize);
}

/* 变长内存申请函数，对齐到 align */
void* DP_MemAllocAlign(size_t size, size_t align, uint32_t mod, DP_MemType_t type)
{
    size_t alignedSize = SIZE_ALIGNED(size, align);
    size_t headSize = SIZE_ALIGNED(sizeof(DP_MemInfo_t), align);
    size_t padLen = headSize - sizeof(DP_MemInfo_t);

    if (SIZE_MAX - headSize < alignedSize) {
        return NULL;
    }

    uint8_t* ptr = UTILS_GetBaseFunc()->memFns->mAlloc(alignedSize + headSize);
    if (ptr == NULL) {
        return NULL;
    }

    DP_MemInfo_t* memInfo = (DP_MemInfo_t*)(ptr + padLen);
    memInfo->mod = mod;
    memInfo->padLen = (uint32_t)padLen;
    memInfo->size = alignedSize + headSize;
    DP_ATOMIC_MEM_ADD(mod, memInfo->size, type);

    return (void*)(ptr + headSize);
}

/* 变长内存释放函数 */
void DP_MemFree(void* addr, DP_MemType_t type)
{
    size_t headSize = sizeof(DP_MemInfo_t);
    DP_MemInfo_t *memInfo = (DP_MemInfo_t *)((int8_t *)addr - headSize);
    size_t size = memInfo->size;
    uint32_t mod = memInfo->mod;
    void* ptr = (void*)((int8_t*)memInfo - memInfo->padLen);

    UTILS_GetBaseFunc()->memFns->mFree(ptr);
    DP_ATOMIC_MEM_SUB(mod, size, type);
}

/* 零拷贝内存打点增加函数 */
void DP_ZcopyMemCntAdd(uint32_t wid, size_t size, DP_MemType_t type)
{
    if (CFG_GET_VAL(DP_CFG_DEPLOYMENT) == DP_DEPLOYMENT_CO_THREAD) {
        DP_ADD_MEM_STAT(wid, size, type);
        return;
    }
    DP_ATOMIC_MEM_ADD(0, size, type);       // 零拷贝内存计数中，DP_ATOMIC_MEM_ADD对应mod字段对应为wid，非共线程记录在worker0中
}

/* 零拷贝内存打点减少函数 */
void DP_ZcopyMemCntSub(uint32_t wid, size_t size, DP_MemType_t type)
{
    if (CFG_GET_VAL(DP_CFG_DEPLOYMENT) == DP_DEPLOYMENT_CO_THREAD) {
        DP_SUB_MEM_STAT(wid, size, type);
        return;
    }
    DP_ATOMIC_MEM_SUB(0, size, type);       // 零拷贝内存计数中，DP_ATOMIC_MEM_SUB对应mod字段对应为wid，非共线程记录在worker0中
}

/* 内存打点增加函数 */
void DP_MemCntAdd(uint32_t mod, size_t size, DP_MemType_t type)
{
    DP_ATOMIC_MEM_ADD(mod, size, type);
}

/* 内存打点减少函数 */
void DP_MemCntSub(uint32_t mod, size_t size, DP_MemType_t type)
{
    DP_ATOMIC_MEM_SUB(mod, size, type);
}

/* 内存打点获取函数 */
uint64_t DP_MemCntGet(uint32_t mod, DP_MemType_t type)
{
    return DP_ATOMIC_MEM_GET(mod, type);
}
