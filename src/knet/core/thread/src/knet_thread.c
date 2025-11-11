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
#define _GNU_SOURCE

#include <pthread.h>
#include <sched.h>

#include "knet_log.h"
#include "knet_thread.h"

int32_t KNET_CreateThread(uint64_t *thread, void *(*func)(void *), void *arg)
{
    int32_t ret;
    ret = pthread_create(thread, NULL, func, arg);
    return ret;
}

int32_t KNET_SetThreadAffinity(uint64_t threadId, const uint16_t *cpus, uint32_t len)
{
    int32_t ret;
    uint32_t i;
    cpu_set_t cpuSet;

    if (cpus == NULL) {
        KNET_ERR("Set affinity failed, invalid params");
        return -1;
    }
    CPU_ZERO(&cpuSet);
    for (i = 0; i < len; ++i) {
        if (cpus[i] >= CPU_SETSIZE) {
            KNET_ERR("Set affinity failed, invalid cpu");
            return -1;
        }
        CPU_SET(cpus[i], &cpuSet);
    }
    ret = pthread_setaffinity_np(threadId, sizeof(cpu_set_t), &cpuSet);
    return ret;
}

int32_t KNET_GetThreadAffinity(uint64_t threadId, uint16_t *cpus, uint32_t *len)
{
    int32_t ret;
    uint32_t i;
    uint32_t cnt = 0;
    cpu_set_t cpuSet;

    if (cpus == NULL || len == NULL) {
        KNET_ERR("Get affinity failed, invalid params");
        return -1;
    }
    CPU_ZERO(&cpuSet);
    ret = pthread_getaffinity_np(threadId, sizeof(cpu_set_t), &cpuSet);
    if (ret != 0) {
        KNET_ERR("Get affinity failed: %d", ret);
        return ret;
    }

    for (i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET(i, &cpuSet) && cnt < *len) {
            cpus[cnt++] = i;
        }
    }

    *len = cnt;

    return 0;
}

int32_t KNET_ThreadNameSet(uint64_t threadId, const char *name)
{
    return pthread_setname_np(threadId, name);
}

int32_t KNET_JoinThread(uint64_t threadId, void **ret)
{
    int32_t result;
    result = pthread_join(threadId, ret);
    return result;
}

inline uint64_t KNET_ThreadId(void)
{
    return pthread_self();
}