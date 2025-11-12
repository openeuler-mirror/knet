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
#include "utils_atomic.h"

#include "dp_mem_api.h"

DP_MemHooks_t g_memFns = {0};

uint64_t g_memFixCnt[MOD_MAX];
uint64_t g_memFreeCnt[MOD_MAX];

/* 原子操作内存打点值增减 */
#define DP_ATOMIC_MEM_ADD(mod, size, type) \
    ((type) == DP_MEM_FIX) ? ATOMIC64_Add(&(g_memFixCnt[(mod)]), (size)) : ATOMIC64_Add(&(g_memFreeCnt[(mod)]), (size))

#define DP_ATOMIC_MEM_SUB(mod, size, type) \
    ((type) == DP_MEM_FIX) ? ATOMIC64_Sub(&(g_memFixCnt[(mod)]), (size)) : ATOMIC64_Sub(&(g_memFreeCnt[(mod)]), (size))

#define DP_ATOMIC_MEM_GET(mod, type) \
    ((type) == DP_MEM_FIX) ? ATOMIC64_Load(&(g_memFixCnt[(mod)])) : ATOMIC64_Load(&(g_memFreeCnt[(mod)]))

/* 变长内存操作接口注册函数 */
uint32_t DP_MemHookReg(DP_MemHooks_S *memHooks)
{
    if ((memHooks == NULL) || (memHooks->mAlloc == NULL) || (memHooks->mFree == NULL)) {
        DP_LOG_ERR("MemHookReg failed, invalid memHooks!");
        return 1;
    }

    g_memFns.mallocFunc = memHooks->mAlloc;
    g_memFns.freeFunc   = memHooks->mFree;

    UTILS_GetBaseFunc()->memFns = &g_memFns;

    return 0;
}

/* 变长内存申请函数 */
void* DP_MemAlloc(size_t size, uint32_t mod, DP_MemType_t type)
{
    size_t headSize = sizeof(DP_MemInfo_t);

    DP_MemInfo_t *memInfo = UTILS_GetBaseFunc()->memFns->mallocFunc(size + headSize);
    if (memInfo == NULL) {
        return NULL;
    }
    memInfo->mod = mod;
    memInfo->size = size + headSize;
    DP_ATOMIC_MEM_ADD(mod, memInfo->size, type);

    return (void *)((int8_t *)memInfo + headSize);
}

/* 变长内存释放函数 */
void DP_MemFree(void* addr, DP_MemType_t type)
{
    size_t headSize = sizeof(DP_MemInfo_t);
    DP_MemInfo_t *memInfo = (DP_MemInfo_t *)((int8_t *)addr - headSize);
    size_t size = memInfo->size;
    uint32_t mod = memInfo->mod;

    UTILS_GetBaseFunc()->memFns->freeFunc(memInfo);
    DP_ATOMIC_MEM_SUB(mod, size, type);
}

/* 变长内存打点获取函数 */
uint64_t DP_MemCntGet(uint32_t mod, DP_MemType_t type)
{
    return DP_ATOMIC_MEM_GET(mod, type);
}