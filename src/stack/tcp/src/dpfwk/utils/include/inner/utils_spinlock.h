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
#ifndef UTILS_SPINLOCK_H
#define UTILS_SPINLOCK_H

#include <stdbool.h>

#include "utils_atomic.h"
#include "utils_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPIN_INITIALIZER    {0}


typedef struct {
    atomic32_t locked;
} Spinlock_t;

static inline void SPINLOCK_CpuRelax(void)
{
#if defined(__x86_64__)
    __asm__ __volatile__("pause": : :"memory");
#elif defined(__aarch64__) || defined(_M_ARM64)
    __asm__ __volatile__("yield": : :"memory");
#else
    return;
#endif
}

static inline int SPINLOCK_Init(Spinlock_t* lock)
{
    lock->locked = 0;
    return 0;
}

static inline void SPINLOCK_Deinit(Spinlock_t* lock)
{
    (void)lock;
}

static inline int SPINLOCK_Lock(Spinlock_t* lock)
{
    if (CFG_GET_VAL(CFG_NOLOCK) == DP_ENABLE) {
        return 0;
    }

    while (ATOMIC32_Cas(&lock->locked, 0, 1) != true) {
        SPINLOCK_CpuRelax();
    }
    return 0;
}

static inline int SPINLOCK_DoLock(Spinlock_t* lock)
{
    while (ATOMIC32_Cas(&lock->locked, 0, 1) != true) {
        SPINLOCK_CpuRelax();
    }
    return 0;
}

static inline int SPINLOCK_TryLock(Spinlock_t* lock)
{
    if (CFG_GET_VAL(CFG_NOLOCK) == DP_ENABLE) {
        return 0;
    }

    if (ATOMIC32_Cas(&lock->locked, 0, 1) != true) {
        return -1;
    }
    return 0;
}

static inline void SPINLOCK_Unlock(Spinlock_t* lock)
{
    if (CFG_GET_VAL(CFG_NOLOCK) == DP_ENABLE) {
        return;
    }

    ATOMIC32_Store(&lock->locked, 0);
}

static inline void SPINLOCK_DoUnlock(Spinlock_t* lock)
{
    ATOMIC32_Store(&lock->locked, 0);
}

#ifdef __cplusplus
}
#endif
#endif
