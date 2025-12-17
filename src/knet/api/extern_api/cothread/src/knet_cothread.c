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

#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/unistd.h>

#include "rte_lcore.h"
#include "dp_worker_api.h"

#include "knet_log.h"
#include "knet_config.h"
#include "knet_init.h"
#include "knet_init_tcp.h"
#include "knet_thread.h"
#include "knet_cothread_inner.h"
#include "knet_socket_api.h"

#define MAX_CPU_NUM 128
#define INVALID_WORKER_ID (-1)

__thread int32_t g_currentWorkerId = INVALID_WORKER_ID;
KNET_STATIC bool g_knetCothreadInited = false;

KNET_STATIC volatile int32_t g_nextWorkerId = 0; // 初始化为0，第一个worker id为0

int knet_init(void)
{
    if (KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue == 0) {
        KNET_ERR("Cothread is not enabled, please check the knet_comm.conf");
        return -1;
    }

    int ret = KNET_TrafficResourcesInit();
    if (ret != 0) {
        KNET_ERR("Failed to initialize traffic resources");
        return -1;
    }
    g_knetCothreadInited = true;
    KNET_WARN("K-NET cothread is initialized");
    return 0;
}

KNET_STATIC bool IsCtrlCpuInThread(void)
{
    uint64_t serviceTid = KNET_ThreadId();
    uint16_t cpus[MAX_CPU_NUM] = {0};
    uint32_t cpuNums = MAX_CPU_NUM;

    int32_t ret = KNET_GetThreadAffinity(serviceTid, cpus, &cpuNums);
    if (ret != 0) {
        KNET_ERR("Service cpu get failed, ret %d", ret);
        return false;
    }

    uint16_t core = 0;
    bool found = false;
    for (uint32_t i = 0; i < cpuNums; ++i) {
        core = cpus[i];
        int ctrlVcpuNum = KNET_GetCfg(CONF_COMMON_CTRL_VCPU_NUMS)->intValue;
        const int *ctrlVcpuArr = KNET_GetCfg(CONF_COMMON_CTRL_VCPU_IDS)->intValueArr;
        for (int j = 0; j < ctrlVcpuNum; j++) {
            if (core == ctrlVcpuArr[j]) {
                found = true;
                KNET_ERR("Ctrl cpu %u is used in thread %lu", core, serviceTid);
                break;
            }
        }
    }
    return found;
}

int knet_worker_init(void)
{
    if (!g_knetCothreadInited) {
        KNET_ERR("K-NET cothread is not initialized. Please call knet_init() first");
        return -1;
    }

    if (g_currentWorkerId != INVALID_WORKER_ID) {
        KNET_ERR("Worker has been initialized");
        return -1;
    }

    if (IsCtrlCpuInThread()) {
        KNET_ERR("The worker thread can not run on the control CPU");
        return -1;
    }

    int32_t id = __sync_fetch_and_add_4(&g_nextWorkerId, 1);
    int32_t maxWorkerNum = KNET_GetCfg(CONF_TCP_MAX_WORKER_NUM)->intValue;
    if (id >= maxWorkerNum) {
        KNET_ERR("Too many worker initialization. The worker id is %d(degin from 0), "
            "max number of worker is %d", id, maxWorkerNum);
        return -1;
    }

    RTE_PER_LCORE(_lcore_id) = (unsigned)id;
    g_currentWorkerId = id;
    int ret = KNET_DpdkLcoreMatchDpWorker((unsigned)id); // 此处失败场景为id超出合法范围，无需恢复id
    if (ret != 0) {
        KNET_ERR("Failed to match DPDK lcore to worker");
        return -1;
    }
    return 0;
}

void knet_worker_run(void)
{
    if (g_currentWorkerId == INVALID_WORKER_ID) {
        KNET_ERR("Worker has not been initialized");
        return;
    }
    DP_RunWorkerOnce(g_currentWorkerId);
}

int knet_is_worker_thread(void)
{
    if (KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue == 0) {
        KNET_ERR("Cothread is not enabled, please check the knet_comm.conf");
        return -1;
    }

    if (g_currentWorkerId != INVALID_WORKER_ID) {
        KNET_WARN("In KNET cothread worker thread");
        return 0; // 在共线程中
    }
    KNET_WARN("Not in KNET cothread worker thread");
    return -1; // 不在共线程中
}

bool KNET_IsCothreadGoKernel(void)
{
    if (KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue == 1 && g_currentWorkerId == INVALID_WORKER_ID) {
        return true;
    }
    return false;
}