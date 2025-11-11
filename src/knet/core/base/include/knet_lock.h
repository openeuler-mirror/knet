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

#ifndef K_NET_KNET_LOCK_H
#define K_NET_KNET_LOCK_H

#include <stdint.h>
#include <pthread.h>

#define KNET_SPIN_UNLOCKED_VALUE         (0)
#define KNET_SPIN_LOCKED_VALUE           (1)

typedef struct {
    volatile uint32_t value;   /**< spinlock锁加锁标志 */
    char padding[4];   // 填充字节，确保结构体8 字节对齐
} KNET_SpinLock;


/* 线程的等待策略 */
typedef enum {
    KNET_SPIN_WAIT = 0,   /* 默认线程等待策略为死转 */
    KNET_YIELD_WAIT = 1,  /* 线程等待策略为sched_yield */
    KNET_WAIT_MAX
} KNET_WaitPolicy;

typedef struct {
    volatile uint32_t value;     /**< 读写锁加锁标志 */
    char padding[4];   // 填充字节，确保结构体8 字节对齐
} KNET_RWLock;

#define KNET_RWLOCK_INIT_VALUE           (0)
#define KNET_RWLOCK_WRITE_VALUE          (0xFFFFFFFF)

void KNET_HalSetWaitPolicy(KNET_WaitPolicy waitPolicy);

KNET_WaitPolicy KNET_HalGetWaitPolicy(void);

#ifdef __x86_64__
    static inline void KNET_HalCpuRelax(void)
    {
        __asm__ __volatile__("pause \n" ::: "memory");
    }
#else
    static inline void KNET_HalCpuRelax(void)
    {
        __asm__ __volatile__("yield" ::: "memory");
    }
#endif

/* 线程的等待策略 */
static inline void KNET_HalWait(void)
{
    KNET_WaitPolicy policy = KNET_HalGetWaitPolicy();
    if (policy == KNET_SPIN_WAIT) {
        KNET_HalCpuRelax();
    } else if (policy == KNET_YIELD_WAIT) {
        (void)sched_yield();
    }
}

static inline void KNET_SpinlockLock(KNET_SpinLock *spinlock)
{
    uint32_t exp = KNET_SPIN_UNLOCKED_VALUE;
    while (!__atomic_compare_exchange_n(&spinlock->value, &exp, KNET_SPIN_LOCKED_VALUE,
                                        0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        while (__atomic_load_n(&spinlock->value, __ATOMIC_RELAXED) != KNET_SPIN_UNLOCKED_VALUE) {
            KNET_HalWait();
        }
        exp = KNET_SPIN_UNLOCKED_VALUE;
    }
}

static inline void KNET_SpinlockUnlock(KNET_SpinLock *spinlock)
{
    __atomic_store_n(&spinlock->value, KNET_SPIN_UNLOCKED_VALUE, __ATOMIC_RELEASE);
}


/* read/write lock */
static inline void KNET_RwlockInit(KNET_RWLock *rwlock)
{
    rwlock->value = KNET_RWLOCK_INIT_VALUE;
}

static inline void KNET_RwlockReadLock(KNET_RWLock *rwlock)
{
    uint32_t t, res = 0;

    while (res == 0) {
        t = __atomic_load_n(&rwlock->value, __ATOMIC_RELAXED);
        if (t == KNET_RWLOCK_WRITE_VALUE) { /* write lock is held */
            KNET_HalWait();
            continue;
        }

        res = __atomic_compare_exchange_n(&rwlock->value, &t, t + 1, 1,
            __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
    }
}

static inline void KNET_RwlockWriteLock(KNET_RWLock *rwlock)
{
    uint32_t t, res = 0;

    while (res == 0) {
        t = __atomic_load_n(&rwlock->value, __ATOMIC_RELAXED);
        if (t != 0) { /* a lock is held */
            KNET_HalWait();
            continue;
        }

        res = __atomic_compare_exchange_n(&rwlock->value, &t, KNET_RWLOCK_WRITE_VALUE, 1,
            __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
    }
}

static inline void KNET_RwlockReadUnlock(KNET_RWLock *pstRwlock)
{
    __atomic_fetch_sub(&pstRwlock->value, 1, __ATOMIC_RELEASE);
}

static inline void KNET_RwlockWriteUnlock(KNET_RWLock *pstRwlock)
{
    __atomic_fetch_add(&(pstRwlock)->value, 1, __ATOMIC_RELEASE);
}

#endif // K_NET_KNET_LOCK_H
