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


#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <regex.h>

#include "securec.h"
#include "knet_log.h"
#include "knet_utils.h"
#include "knet_config_setter.h"
#include "knet_config_core_queue.h"

#define MAX_PROCESS_NUM 128

struct QueueIdPool {
    pthread_mutex_t mutex;
    int queueId[MAX_CORE_NUM];
};

struct CqMap {          // 记录clientId与queueId的映射关系
    int clientId;
    int queueId;
    pid_t pid; // 记录进程id
};

static struct QueueIdPool g_queueIdPool = {
    PTHREAD_MUTEX_INITIALIZER,
    {0}
};

static int g_coreList[MAX_CORE_NUM] = {0};
KNET_STATIC int g_coreListIndex = 0; // 初始化结束后表示core与queue的数量

static struct CqMap g_processLocalQid[MAX_PROCESS_NUM] = {0};

int Cmp(const void *a, const void *b)
{
    return (*(int *)a - *(int *)b);
}

bool KnetCheckDuplicateCore(void)
{
    qsort(g_coreList, g_coreListIndex, sizeof(int), Cmp);

    for (int i = 1; i < g_coreListIndex; ++i) {
        if (g_coreList[i] == g_coreList[i - 1]) {
            return true;
        }
    }

    return false;
}

bool KnetCheckAvailableCore(void)
{
    for (int i = 0; i < g_coreListIndex; ++i) {
        int ret = KNET_CpuDetected(g_coreList[i]);
        if (ret < 0) {
            KNET_ERR("Core %d is not a available core", g_coreList[i]);
            return false;
        }
    }
    return true;
}

void KnetSetQueueNum(int num)
{
    g_coreListIndex = num;
}

int KnetCoreListAppend(int num)
{
    if (g_coreListIndex >= MAX_CORE_NUM) {
        KNET_ERR("Core number is too large, the max num %d", MAX_CORE_NUM - 1);
        return -1;
    }

    if (num < 0 || num >= MAX_CORE_NUM) {
        KNET_ERR("Core number must be in range [0, %d]", MAX_CORE_NUM - 1);
        return -1;
    }

    g_coreList[g_coreListIndex] = num;
    ++g_coreListIndex;
    return 0;
}


int KnetGetProcessLocalQid(int clientId)
{
    if (clientId < 0) {
        KNET_ERR("Invalid clientId");
        return -1;
    }
    for (int i = 0; i < MAX_PROCESS_NUM; ++i) {
        if (g_processLocalQid[i].clientId == clientId) {
            return g_processLocalQid[i].queueId;
        }
    }

    KNET_ERR("Map is not exist, clientId %d", clientId);
    return -1;
}

int KnetSetProcessLocalQid(int clientId, int queueId, pid_t pid)
{
    if (clientId < 0) {
        KNET_ERR("Invalid clientId");
        return -1;
    }
    for (int i = 0; i < MAX_PROCESS_NUM; ++i) {
        if (g_processLocalQid[i].clientId == -1) {
            g_processLocalQid[i].clientId = clientId;
            g_processLocalQid[i].queueId = queueId;
            g_processLocalQid[i].pid = pid;
            return 0;
        }
    }

    KNET_ERR("Process number is too large, the max num %d", MAX_PROCESS_NUM - 1);
    return -1;
}

int KnetDelProcessLocalQid(int clientId)
{
    if (clientId < 0) {
        KNET_ERR("Invalid clientId in del lcoal qid");
        return -1;
    }
    for (int i = 0; i < MAX_PROCESS_NUM; ++i) {
        if (g_processLocalQid[i].clientId == clientId) {
            g_processLocalQid[i].clientId = -1;
            g_processLocalQid[i].queueId = -1;
            g_processLocalQid[i].pid = -1;
            return 0;
        }
    }

    KNET_ERR("Can not find queue id by client id %d", clientId);
    return -1;
}

KNET_STATIC int SetEnvQueueId(int requeseQueueId)
{
    if (requeseQueueId == KNET_QUEUE_ID_INVALID) {
        KNET_ERR("Invalid KNET_QUEUE_ID please check input");
        return -1;
    }

    if (requeseQueueId >= g_coreListIndex || requeseQueueId < 0) {
        KNET_ERR("SetEnvQueueId Invalid index, index must be in range [0, %d]", g_coreListIndex - 1);
        return -1;
    }

    if (g_queueIdPool.queueId[requeseQueueId] == 0) {
        g_queueIdPool.queueId[requeseQueueId] = 1;
        return requeseQueueId;
    } else {
        KNET_ERR("Queue id %d is already in used ", requeseQueueId);
        return -1;
    }

    return -1;
}

int KnetGetQueueIdFromPool(int requeseQueueId)
{
    (void)pthread_mutex_lock(&g_queueIdPool.mutex);

    if (KNET_GetCfg(CONF_HW_BIFUR_ENABLE)->intValue == BIFUR_ENABLE
        && requeseQueueId != KNET_QUEUE_ID_NULL) {
        int envQueueId = SetEnvQueueId(requeseQueueId);
        (void)pthread_mutex_unlock(&g_queueIdPool.mutex);
        return envQueueId;
    }

    for (int i = 0; i < g_coreListIndex; ++i) {
        if (g_queueIdPool.queueId[i] == 0) {
            g_queueIdPool.queueId[i] = 1;
            (void)pthread_mutex_unlock(&g_queueIdPool.mutex);
            return i;
        }
    }

    (void)pthread_mutex_unlock(&g_queueIdPool.mutex);
    return -1;
}

int KnetFreeQueueIdInPool(int index)
{
    if (index < 0 || index >= g_coreListIndex) {
        KNET_ERR("Invalid index, index must be in range [0, %d]", g_coreListIndex - 1);
        return -1;
    }

    (void)pthread_mutex_lock(&g_queueIdPool.mutex);
    g_queueIdPool.queueId[index] = 0;
    (void)pthread_mutex_unlock(&g_queueIdPool.mutex);
    return 0;
}

int KnetGetCoreByQueueId(int queueId)
{
    if (queueId < 0 || queueId >= g_coreListIndex) {
        KNET_ERR("Invalid queueId,  index must be in range [0, %d]", g_coreListIndex - 1);
        return -1;
    }

    return g_coreList[queueId];
}

int KnetGetCoreNum(void)
{
    return g_coreListIndex;
}

void KnetQueueInit(void)
{
    for (int i = 0; i < MAX_PROCESS_NUM; ++i) {
        g_processLocalQid[i].clientId = -1;
        g_processLocalQid[i].queueId = -1;
        g_processLocalQid[i].pid = -1;
    }
}

int KNET_IsQueueIdUsed(int queueId)
{
    if (queueId < 0 || queueId >= g_coreListIndex) {
        return 0;
    }
    return g_queueIdPool.queueId[queueId];
}

int KNET_FindCoreInList(int index)
{
    for (int i = 0; i < g_coreListIndex; ++i) {
        if (g_coreList[i] == index) {
            return i;
        }
    }
    
    return -1;
}