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

#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include "utils_base.h"
#include "utils_dft_fns.h"

static uint32_t SemInit(DP_Sem_t sem, int32_t flag, uint32_t value)
{
    if (sem == NULL) {
        return (uint32_t)-1;
    }
    (void)flag;
    (void)value;

    if (sem_init(sem, 0, 0) != 0) {
        return (uint32_t)-1;
    }

    return 0;
}

static void SemDeinit(DP_Sem_t sem)
{
    if (sem == NULL) {
        return;
    }

    sem_destroy(sem);
}

static uint32_t SemWait(DP_Sem_t sem, int timeout)
{
    int nsec;
    int sec;
    struct timespec ts;

    if (timeout < 0) {
        if (sem_wait((sem_t*)sem) != 0) {
            return errno;
        }
        return 0;
    }

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return (uint32_t)errno;
    }

    sec  = (timeout >= MSEC_PER_SEC) ? (timeout / MSEC_PER_SEC) : 0;
    nsec = (timeout - sec * MSEC_PER_SEC) * (int)NSEC_PER_MSEC;

    ts.tv_sec += sec;
    ts.tv_nsec += nsec;
    while (ts.tv_nsec > NSEC_PER_SEC) {
        ts.tv_sec += 1;
        ts.tv_nsec -= NSEC_PER_SEC;
    }

    if (sem_timedwait((sem_t*)sem, &ts) != 0) {
        return (uint32_t)errno;
    }

    return 0;
}

static uint32_t SemSignal(DP_Sem_t sem)
{
    return (uint32_t)sem_post((sem_t*)sem);
}

static DP_SemHooks_S g_semFns = {
    .size   = sizeof(sem_t),
    .init   = SemInit,
    .deinit = SemDeinit,
    .timeWait   = SemWait,
    .post = SemSignal,
};

DP_SemHooks_S* GetDefaultSemFns(void)
{
    return &g_semFns;
}

static uint32_t TimeNow(DP_ClockId_E clockId, int64_t *seconds, int64_t *nanoseconds)
{
    struct timespec tp;
    clock_gettime(clockId, &tp);

    *seconds = tp.tv_sec;
    *nanoseconds = tp.tv_nsec;
    return 0;
}

DP_ClockGetTimeHook GetTimeFns(void)
{
    return TimeNow;
}

DP_MemHooks_t* GetDefaultMemfns(void)
{
    static DP_MemHooks_t memFns = {
        .mallocFunc = malloc,
        .freeFunc   = free,
    };

    return &memFns;
}
