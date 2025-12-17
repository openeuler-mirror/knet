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
#ifndef UTILS_ATOMIC_H
#define UTILS_ATOMIC_H

#include <stdbool.h>

#include "dp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef volatile uint16_t atomic16_t;
typedef volatile uint32_t atomic32_t;
typedef volatile uint64_t atomic64_t;

static inline uint16_t ATOMIC16_Load(volatile uint16_t* dst)
{
    return __atomic_load_n(dst, __ATOMIC_SEQ_CST);
}

static inline void ATOMIC16_Store(volatile uint16_t* dst, uint16_t val)
{
    __atomic_store_n(dst, val, __ATOMIC_SEQ_CST);
}

static inline uint16_t ATOMIC16_Inc(volatile uint16_t* dst)
{
    return __atomic_add_fetch(dst, 1, __ATOMIC_SEQ_CST);
}

static inline uint16_t ATOMIC16_Dec(volatile uint16_t* dst)
{
    return __atomic_sub_fetch(dst, 1, __ATOMIC_SEQ_CST);
}

static inline uint16_t ATOMIC16_Add(volatile uint16_t* dst, uint16_t val)
{
    return __atomic_add_fetch(dst, val, __ATOMIC_SEQ_CST);
}

static inline uint16_t ATOMIC16_Sub(volatile uint16_t* dst, uint16_t val)
{
    return __atomic_sub_fetch(dst, val, __ATOMIC_SEQ_CST);
}

static inline bool ATOMIC16_Cas(volatile uint16_t* dst, uint16_t exp, uint16_t src)
{
    return __atomic_compare_exchange_n(dst, &exp, src, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline uint16_t ATOMIC16_Exchange(volatile uint16_t* dst, uint16_t val)
{
    return __atomic_exchange_n(dst, val, __ATOMIC_SEQ_CST);
}

static inline uint32_t ATOMIC32_Load(volatile uint32_t* dst)
{
    return __atomic_load_n(dst, __ATOMIC_SEQ_CST);
}

static inline void ATOMIC32_Store(volatile uint32_t* dst, uint32_t val)
{
    __atomic_store_n(dst, val, __ATOMIC_SEQ_CST);
}

static inline uint32_t ATOMIC32_Inc(volatile uint32_t* dst)
{
    return __atomic_add_fetch(dst, 1, __ATOMIC_SEQ_CST);
}

static inline uint32_t ATOMIC32_Dec(volatile uint32_t* dst)
{
    return __atomic_sub_fetch(dst, 1, __ATOMIC_SEQ_CST);
}

static inline uint32_t ATOMIC32_Add(volatile uint32_t* dst, uint32_t val)
{
    return __atomic_add_fetch(dst, val, __ATOMIC_SEQ_CST);
}

static inline uint32_t ATOMIC32_Sub(volatile uint32_t* dst, uint32_t val)
{
    return __atomic_sub_fetch(dst, val, __ATOMIC_SEQ_CST);
}

static inline bool ATOMIC32_Cas(volatile uint32_t* dst, uint32_t exp, uint32_t src)
{
    return __atomic_compare_exchange_n(dst, &exp, src, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline uint32_t ATOMIC32_Exchange(volatile uint32_t* dst, uint32_t val)
{
    return __atomic_exchange_n(dst, val, __ATOMIC_SEQ_CST);
}

static inline uint64_t ATOMIC64_Load(volatile uint64_t* dst)
{
    return __atomic_load_n(dst, __ATOMIC_SEQ_CST);
}

static inline void ATOMIC64_Store(volatile uint64_t* dst, uint64_t val)
{
    __atomic_store_n(dst, val, __ATOMIC_SEQ_CST);
}

static inline uint64_t ATOMIC64_Inc(volatile uint64_t* dst)
{
    return __atomic_add_fetch(dst, 1, __ATOMIC_SEQ_CST);
}

static inline uint64_t ATOMIC64_Dec(volatile uint64_t* dst)
{
    return __atomic_sub_fetch(dst, 1, __ATOMIC_SEQ_CST);
}

static inline uint64_t ATOMIC64_Add(volatile uint64_t* dst, uint64_t val)
{
    return __atomic_add_fetch(dst, val, __ATOMIC_SEQ_CST);
}

static inline uint64_t ATOMIC64_Sub(volatile uint64_t* dst, uint64_t val)
{
    return __atomic_sub_fetch(dst, val, __ATOMIC_SEQ_CST);
}

static inline bool ATOMIC64_Cas(volatile uint64_t* dst, uint64_t exp, uint64_t src)
{
    return __atomic_compare_exchange_n(dst, &exp, src, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline uint64_t ATOMIC64_Exchange(volatile uint64_t* dst, uint64_t val)
{
    return __atomic_exchange_n(dst, val, __ATOMIC_SEQ_CST);
}

#ifdef __cplusplus
}
#endif
#endif
