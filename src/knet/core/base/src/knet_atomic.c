/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: eal atomic
 */

#include "knet_atomic.h"
#include "rte_config.h"
#include "rte_atomic.h"

#ifdef __cplusplus
extern "C" {
#endif

int KNET_HalAtomicTestSet64(KNET_ATOMIC64_T *value)
{
    return rte_atomic64_test_and_set((rte_atomic64_t *)value);
}

uint64_t KNET_HalAtomicRead64(KNET_ATOMIC64_T *value)
{
    return rte_atomic64_read((rte_atomic64_t *)value);
}

void KNET_HalAtomicSet64(KNET_ATOMIC64_T *value, uint64_t newVal)
{
    rte_atomic64_set((rte_atomic64_t *)value, (int64_t)newVal);
}

void KNET_HalAtomicAdd64(KNET_ATOMIC64_T *value, uint64_t addVal)
{
    rte_atomic64_add((rte_atomic64_t *)value, (int64_t)addVal);
}

void KNET_HalAtomicSub64(KNET_ATOMIC64_T *value, uint64_t subVal)
{
    rte_atomic64_sub((rte_atomic64_t *)value, (int64_t)subVal);
}

#ifdef __cplusplus
}
#endif /* __cpluscplus */