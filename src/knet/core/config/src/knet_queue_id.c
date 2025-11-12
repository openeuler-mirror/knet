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

#include "securec.h"
#include "knet_log.h"
#include "knet_utils.h"
#include "knet_queue_id.h"

#define MAX_CORE_NUM 128
#define MAX_PROCESS_NUM 128
#define MAX_STRING_LEN 1024
#define DECIMAL 10

struct QueueIdPool {
    pthread_mutex_t mutex;
    int queueId[MAX_CORE_NUM];
};

struct CqMap {          // 记录clientId与queueId的映射关系
    int clientId;
    int queueId;
};

static struct QueueIdPool g_queueIdPool = {
    PTHREAD_MUTEX_INITIALIZER,
    {0}
};

static int g_coreList[MAX_CORE_NUM] = {0};
static int g_coreListIndex = 0;

static struct CqMap g_processLocalQid[MAX_PROCESS_NUM] = {0};

int Cmp(const void *a, const void *b)
{
    return (*(int *)a - *(int *)b);
}

bool CheckDuplicateCore()
{
    qsort(g_coreList, g_coreListIndex, sizeof(int), Cmp);

    for (int i = 1; i < g_coreListIndex; ++i) {
        if (g_coreList[i] == g_coreList[i - 1]) {
            return true;
        }
    }

    return false;
}

bool CheckAvailableCore(void)
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

int CheckRangeStr(int leftNum, int rightNum)
{
    if (leftNum < 0 || rightNum < 0) {
        KNET_ERR("LeftNum and rightNum must be positive numbers");
        return -1;
    }

    if (leftNum >= rightNum) {
        KNET_ERR("RightNum must be greater than leftNum");
        return -1;
    }

    if (rightNum >= MAX_CORE_NUM) {
        KNET_ERR("RightNum is too large, max num: %d", MAX_CORE_NUM - 1);
        return -1;
    }

    return 0;
}

static int CheckCoreListNum(char *substr)
{
    char *endptr = NULL;
    int base = DECIMAL;

    errno = 0;
    long int num = strtol(substr, &endptr, base);
    if (errno != 0 || *endptr != '\0') {
        KNET_ERR("Invalid substr, errno %d ", errno);
        return -1;
    }

    if (num < 0 || num >= MAX_CORE_NUM) {
        KNET_ERR("Core number must be in range [0, %d]", MAX_CORE_NUM - 1);
        return -1;
    }

    return (int)num;
}

int PhraseRangeStr(char *substr)
{
    char tempStr[MAX_STRVALUE_NUM] = {0};
    int ret = strcpy_s(tempStr, MAX_STRVALUE_NUM, substr);
    if (ret != 0) {
        KNET_ERR("Strcpy faild, ret %d", ret);
        return -1;
    }

    char *hyphen = NULL;
    hyphen = strchr(tempStr, '-');
    if (hyphen == NULL) {
        KNET_ERR("Invalid substr");
        return -1;
    }

    *hyphen = '\0';
    char *leftStr = tempStr;
    char *rightStr = hyphen + 1;
    int leftNum;
    int rightNum;

    leftNum = CheckCoreListNum(leftStr);
    if (leftNum < 0) {
        KNET_ERR("Check left num failed");
        return -1;
    }

    rightNum = CheckCoreListNum(rightStr);
    if (rightNum < 0) {
        KNET_ERR("Check right num failed");
        return -1;
    }

    ret = CheckRangeStr(leftNum, rightNum);
    if (ret != 0) {
        KNET_ERR("Check range string failed");
        return -1;
    }

    for (int n = leftNum; n <= rightNum; ++n) {
        if (g_coreListIndex >= MAX_CORE_NUM) {
            KNET_ERR("Core number is too large, the max num: %d", MAX_CORE_NUM - 1);
            return -1;
        }

        g_coreList[g_coreListIndex] = n;
        ++g_coreListIndex;
    }

    return 0;
}

int PhraseNumStr(char *substr)
{
    if (g_coreListIndex >= MAX_CORE_NUM) {
        KNET_ERR("Core number is too large, the max num: %d", MAX_CORE_NUM - 1);
        return -1;
    }
    
    int num = CheckCoreListNum(substr);
    if (num < 0) {
        KNET_ERR("Check num faild");
        return -1;
    }

    g_coreList[g_coreListIndex] = num;
    ++g_coreListIndex;

    return 0;
}

int KNET_CoreListInit(char *coreListGlobal)
{
    if (coreListGlobal == NULL) {
        KNET_ERR("Core list global is null");
        return -1;
    }

    char coreListStr[MAX_STRING_LEN] = {0};

    int memcpyRet = memcpy_s(coreListStr, sizeof(coreListStr), coreListGlobal, strlen(coreListGlobal) + 1);
    if (memcpyRet != 0) {
        KNET_ERR("Core list init failed by memcpy: %d", memcpyRet);
        return -1;
    }

    int ret;
    char *substr = NULL;
    char *nextSubStr = NULL;
    for (substr = strtok_s(coreListStr, ",", &nextSubStr); substr != NULL; substr =  strtok_s(NULL, ",", &nextSubStr)) {
        if (strchr(substr, '-') == NULL) {
            ret = PhraseNumStr(substr);
            if (ret != 0) {
                KNET_ERR("Phrase num str faild");
                return -1;
            }
        } else {
            ret = PhraseRangeStr(substr);
            if (ret != 0) {
                KNET_ERR("Phrase range str faild");
                return -1;
            }
        }
    }

    if (CheckDuplicateCore()) {
        KNET_ERR("There are duplicate cores in core list");
        return -1;
    }
    
    if (!CheckAvailableCore()) {
        KNET_ERR("Some cores in core list are not available");
        return -1;
    }

    for (int i = 0; i < MAX_PROCESS_NUM; ++i) {
        g_processLocalQid[i].clientId = -1;
        g_processLocalQid[i].queueId = -1;
    }
    
    return 0;
}

int GetProcessLocalQid(int clientId)
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

int SetProcessLocalQid(int clientId, int queueId)
{
    if (clientId < 0) {
        KNET_ERR("Invalid clientId");
        return -1;
    }
    for (int i = 0; i < MAX_PROCESS_NUM; ++i) {
        if (g_processLocalQid[i].clientId == -1) {
            g_processLocalQid[i].clientId = clientId;
            g_processLocalQid[i].queueId = queueId;
            return 0;
        }
    }

    KNET_ERR("Process number is too large");
    return -1;
}

int DelProcessLocalQid(int clientId)
{
    if (clientId < 0) {
        KNET_ERR("Invalid clientId");
        return -1;
    }
    for (int i = 0; i < MAX_PROCESS_NUM; ++i) {
        if (g_processLocalQid[i].clientId == clientId) {
            g_processLocalQid[i].clientId = -1;
            g_processLocalQid[i].queueId = -1;
            return 0;
        }
    }

    KNET_ERR("Map is not exist, clientId %d", clientId);
    return -1;
}

static int GetQueueId()
{
    (void)pthread_mutex_lock(&g_queueIdPool.mutex);

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

static int GetQueueIdFromPrimary(void)
{
    int ret;
    struct KnetRpcMessage knetRpcRequest = {0};
    struct KnetRpcMessage knetRpcReponse = {0};

    struct ConfigRequest req = {0};
    req.type = CONF_REQ_TYPE_GET;

    ret = memcpy_s(knetRpcRequest.data, RPC_MESSAGE_SIZE, (char *)&req, sizeof(struct ConfigRequest));
    if (ret != 0) {
        KNET_ERR("Memcpy faild, ret %d", ret);
        return -1;
    }
    knetRpcRequest.len = sizeof(struct ConfigRequest);

    ret = KNET_RpcClient(KNET_MOD_CONF, &knetRpcRequest, &knetRpcReponse);
    if (ret != 0) {
        KNET_ERR("Rpc request failed, ret %d", ret);
        return -1;
    }

    ret = knetRpcReponse.ret;
    if (ret != 0) {
        KNET_ERR("Get queue id failed, ret %d", ret);
        return -1;
    }

    int queueId = *(int *)knetRpcReponse.data;

    return queueId;
}

int KNET_GetQueueId(enum KnetProcType procType)
{
    int queueId;

    if (procType == KNET_PROC_TYPE_PRIMARY) {
        queueId = GetQueueId();
        if (queueId == -1) {
            KNET_ERR("Get queueId faild, queueId %d", queueId);
            return -1;
        }
    } else {
        queueId = GetQueueIdFromPrimary();
        if (queueId == -1) {
            KNET_ERR("Get queueId from primary faild, queueId %d", queueId);
            return -1;
        }
    }

    return queueId;
}

int FreeQueueId(int index)
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

static int FreeQueueIdFromPrimary(int queueId)
{
    int ret;
    struct KnetRpcMessage knetRpcRequest = {0};
    struct KnetRpcMessage knetRpcReponse = {0};

    struct ConfigRequest req = {0};
    req.type = CONF_REQ_TYPE_FREE;
    req.queueId = queueId;

    ret = memcpy_s(knetRpcRequest.data, RPC_MESSAGE_SIZE, (char *)&req, sizeof(struct ConfigRequest));
    if (ret != 0) {
        KNET_ERR("Memcpy faild, ret %d", ret);
        return -1;
    }
    knetRpcRequest.len = sizeof(struct ConfigRequest);

    ret = KNET_RpcClient(KNET_MOD_CONF, &knetRpcRequest, &knetRpcReponse);
    if (ret != 0) {
        KNET_ERR("Rpc request failed, ret %d", ret);
        return -1;
    }

    ret = knetRpcReponse.ret;
    if (ret != 0) {
        KNET_ERR("Free Queue Id failed, ret %d", ret);
        return -1;
    }

    return 0;
}

int KNET_FreeQueueId(enum KnetProcType procType, int index)
{
    int ret;

    if (procType == KNET_PROC_TYPE_PRIMARY) {
        ret = FreeQueueId(index);
        if (ret != 0) {
            KNET_ERR("Free queue id faild, ret %d", ret);
            return -1;
        }
    } else {
        ret = FreeQueueIdFromPrimary(index);
        if (ret != 0) {
            KNET_ERR("Free queue id faild, ret %d", ret);
            return -1;
        }
    }

    return 0;
}

int GetRequestHandler(int clientId, struct KnetRpcMessage *knetRpcResponse)
{
    int ret = 0;

    int queueId = GetQueueId();
    if (queueId == -1) {
        KNET_ERR("Get queue id faild, queueId %d", queueId);
        return -1;
    }

    ret = SetProcessLocalQid(clientId, queueId);  // 主进程保留从进程和queueId之间的映射关系
    if (ret != 0) {
        FreeQueueId(queueId);
        DelProcessLocalQid(clientId);
        KNET_ERR("Set process local queue id faild, ret %d", ret);
        return -1;
    }

    ret = memcpy_s(knetRpcResponse->data, RPC_MESSAGE_SIZE, &queueId, sizeof(int));
    if (ret != 0) {
        FreeQueueId(queueId);
        DelProcessLocalQid(clientId);
        KNET_ERR("Memcpy faild, ret %d", ret);
        return -1;
    }

    return 0;
}

int FreeRequestHandler(int clientId, int queueId)
{
    int ret = 0;

    ret = FreeQueueId(queueId);
    if (ret != 0) {
        KNET_ERR("Free queue id faild, ret %d", ret);
        return -1;
    }

    ret = DelProcessLocalQid(clientId);
    if (ret != 0) {
        KNET_ERR("Delete process local queue id faild, ret %d", ret);
        return -1;
    }

    return 0;
}

int ConfigDisconnetHandler(int clientId, struct KnetRpcMessage *knetRpcRequest, struct KnetRpcMessage *knetRpcResponse)
{
    int queueId = GetProcessLocalQid(clientId);
    if (queueId == -1) {
        KNET_ERR("Get process local queue id faild, queueId %d", queueId);
        return -1;
    }
    (void)knetRpcRequest;
    (void)knetRpcResponse;

    FreeQueueId(queueId);
    DelProcessLocalQid(clientId);

    return 0;
}

int KNET_RegConfigRpcHandler(enum KnetProcType procType)
{
    int ret = 0;

    if (procType == KNET_PROC_TYPE_PRIMARY) {
        ret = KNET_RegServer(KNET_CONNECT_EVENT_DISCONNECT, KNET_MOD_CONF, ConfigDisconnetHandler);
        if (ret != 0) {
            KNET_ERR("Register handler faild, ret %d", ret);
            return -1;
        }
    }

    return ret;
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

int KNET_GetCore(int queueId)
{
    if (queueId < 0 || queueId >= g_coreListIndex) {
        KNET_ERR("Invalid queueId,  index must be in range [0, %d]", g_coreListIndex - 1);
        return -1;
    }

    return g_coreList[queueId];
}

/**
 * 判断 queueId 是否被使用
 * queueId 被使用返回1，空闲或不可用返回0
*/
int KNET_IsQueueIdUsed(int queueId)
{
    if (queueId < 0 || queueId >= g_coreListIndex) {
        return 0;
    }
    return g_queueIdPool.queueId[queueId];
}

int KNET_GetCoreNum(void)
{
    return g_coreListIndex;
}
