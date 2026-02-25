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

#ifdef __cplusplus
extern "C" {
#endif

#define KNET_SPIN_UNLOCKED_VALUE         (0)
#define KNET_SPIN_LOCKED_VALUE           (1)

#define KNET_RWLOCK_INIT_VALUE           (0)
#define KNET_RWLOCK_WRITE_VALUE          (0xFFFFFFFF)

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

/**
 * @brief 获取线程等待策略
 *
 * @return KNET_WaitPolicy 获取到的线程等待策略
 */
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

/**
 * @brief 线程等待函数，根据当前线程等待策略选择忙等待(spin wait)还是让出cpu (yield wait)
 *
 */
static inline void KNET_HalWait(void)
{
    KNET_WaitPolicy policy = KNET_HalGetWaitPolicy();
    if (policy == KNET_SPIN_WAIT) {
        KNET_HalCpuRelax();
    } else if (policy == KNET_YIELD_WAIT) {
        (void)sched_yield();
    }
}

/**
 * @brief 获取自旋锁
 *
 * @param spinlock [IN] 参数类型 KNET_SpinLock*。指向自旋锁结构体的指针
 */
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

/**
 * @brief 释放自旋锁
 *
 * @param spinlock [IN] 参数类型 KNET_SpinLock*。指向自旋锁结构体的指针
 */
static inline void KNET_SpinlockUnlock(KNET_SpinLock *spinlock)
{
    __atomic_store_n(&spinlock->value, KNET_SPIN_UNLOCKED_VALUE, __ATOMIC_RELEASE);
}


/**
 * @brief 初始化读写锁，将读写锁的value设为初始值
 *
 * @param rwlock [IN] 参数类型 KNET_RWLock*。指向读写锁结构体的指针
 */
static inline void KNET_RwlockInit(KNET_RWLock *rwlock)
{
    rwlock->value = KNET_RWLOCK_INIT_VALUE;
}

/**
 * @brief 尝试获取读锁。如果当前有写锁被持有，那么线程将进入等待状态，直到写锁被释放
 *
 * @param rwlock [IN] 参数类型 KNET_RWLock*。指向读写锁结构体的指针
 */
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

/**
 * @brief 尝试获取写锁。如果当前有任何锁（读锁或写锁）被持有，那么线程将进入等待状态，直到所有锁都被释放
 *
 * @param rwlock [IN] 参数类型 KNET_RWLock*。指向读写锁结构体的指针
 */
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

/**
 * @brief 释放读锁
 *
 * @param pstRwlock [IN] 参数类型 KNET_RWLock*。指向读写锁结构体的指针
 */
static inline void KNET_RwlockReadUnlock(KNET_RWLock *pstRwlock)
{
    __atomic_fetch_sub(&pstRwlock->value, 1, __ATOMIC_RELEASE);
}

/**
 * @brief 释放写锁
 *
 * @param pstRwlock [IN] 参数类型 KNET_RWLock*。指向读写锁结构体的指针
 */
static inline void KNET_RwlockWriteUnlock(KNET_RWLock *pstRwlock)
{
    __atomic_fetch_add(&(pstRwlock)->value, 1, __ATOMIC_RELEASE);
}

#ifdef __cplusplus
}
#endif
#endif // K_NET_KNET_LOCK_H
