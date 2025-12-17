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

#include <unistd.h>
#include "knet_log.h"
#include "knet_config_setter.h"
#include "knet_utils.h"         // RegMatch
#include "knet_config.h"

#include "knet_config_rpc.h"

int ConfigDisconnetHandler(int clientId, struct KNET_RpcMessage *knetRpcRequest,
                           struct KNET_RpcMessage *knetRpcResponse)
{
    int queueId = KnetGetProcessLocalQid(clientId);
    if (queueId == -1) {
        KNET_ERR("Get process local queue id failed, queueId %d, cliendId %d", queueId, clientId);
        return -1;
    }
    (void)knetRpcRequest;
    (void)knetRpcResponse;

    KnetFreeQueueIdInPool(queueId);
    KnetDelProcessLocalQid(clientId);

    return 0;
}

int KnetRegConfigRpcHandler(enum KNET_ProcType procType)
{
    int ret = 0;

    if (procType == KNET_PROC_TYPE_PRIMARY) {
        ret = KNET_RpcRegServer(KNET_RPC_EVENT_DISCONNECT, KNET_RPC_MOD_CONF, ConfigDisconnetHandler);
        if (ret != 0) {
            KNET_ERR("Register handler failed in primary, ret %d", ret);
            return -1;
        }
    }

    return ret;
}


int KnetGetRequestHandler(int clientId, struct KNET_RpcMessage *knetRpcRequest, struct KNET_RpcMessage *knetRpcResponse)
{
    struct ConfigRequest *req = (struct ConfigRequest *)knetRpcRequest->fixedLenData;
    int queueId = KnetGetQueueIdFromPool(req->queueId);
    if (queueId == -1) {
        KNET_ERR("Get queue id failed , queueId %d, clienId %d", queueId, clientId);
        return -1;
    }

    int ret = KnetSetProcessLocalQid(clientId, queueId);  // 主进程保留从进程和queueId之间的映射关系
    if (ret != 0) {
        KnetFreeQueueIdInPool(queueId);
        KnetDelProcessLocalQid(clientId);
        KNET_ERR("Set process local queue id failed, client %d, ret %d", clientId, ret);
        return -1;
    }

    knetRpcResponse->dataType = RPC_MSG_DATA_TYPE_FIXED_LEN;
    knetRpcResponse->dataLen = sizeof(int);
    ret = memcpy_s(knetRpcResponse->fixedLenData, RPC_MESSAGE_SIZE, &queueId, sizeof(int));
    if (ret != 0) {
        KnetFreeQueueIdInPool(queueId);
        KnetDelProcessLocalQid(clientId);
        KNET_ERR("Memcpy failed, client %d, ret %d", clientId, ret);
        return -1;
    }
    return 0;
}

int KnetFreeRequestHandler(int clientId, int queueId)
{
    int ret = KnetFreeQueueIdInPool(queueId);
    if (ret != 0) {
        KNET_ERR("Free queue id failed, ret %d, client %d, queueId %d", ret, clientId, queueId);
        return -1;
    }

    ret = KnetDelProcessLocalQid(clientId);
    if (ret != 0) {
        KNET_ERR("Delete process local queue id failed, ret %d, client %d", ret, clientId);
        return -1;
    }
    return 0;
}

KNET_STATIC int GetEnvQueueId(void)
{
    char *strEnvQueueId = getenv("KNET_QUEUE_ID");
    if (strEnvQueueId == NULL) {
        return KNET_QUEUE_ID_NULL;
    }

    // 匹配 0-127 的数字 MAX_CORE_NUM为128
    const char *pattern = "^([0-9]|[1-9][0-9]|1[0-1][0-9]|12[0-7])$";
    if (!KNET_RegMatch(pattern, strEnvQueueId)) {
        KNET_ERR("Env KNET_QUEUE_ID which should be in range 0-127 is invalid");
        return KNET_QUEUE_ID_INVALID;
    }
    int queueId = 0;
    if (sscanf_s(strEnvQueueId, "%d", &queueId) == -1) {
        // 前面正则保证id是0-127的数字
        KNET_ERR("Failed to convert env queue id to int, env %s", strEnvQueueId);
        return KNET_QUEUE_ID_INVALID;
    }
    return queueId;
}

static int GetQueueIdFromPrimary(void)
{
    struct ConfigRequest req = {0};
    req.type = CONF_REQ_TYPE_GET;

    if (KNET_GetCfg(CONF_HW_BIFUR_ENABLE)->intValue == BIFUR_ENABLE) {
        req.queueId = GetEnvQueueId();
    }

    struct KNET_RpcMessage knetRpcRequest = {0};
    int ret = memcpy_s(knetRpcRequest.fixedLenData, RPC_MESSAGE_SIZE, (char *)&req, sizeof(struct ConfigRequest));
    if (ret != 0) {
        KNET_ERR("Memcpy failed, ret %d", ret);
        return -1;
    }
    knetRpcRequest.dataLen = sizeof(struct ConfigRequest);
    knetRpcRequest.dataType = RPC_MSG_DATA_TYPE_FIXED_LEN;

    struct KNET_RpcMessage knetRpcReponse = {0};
    ret = KNET_RpcCall(KNET_RPC_MOD_CONF, &knetRpcRequest, &knetRpcReponse);
    if (ret != 0) {
        KNET_ERR("Rpc request failed, ret %d", ret);
        return -1;
    }

    ret = knetRpcReponse.ret;
    if (ret != 0) {
        KNET_ERR("Get queue id failed, ret %d", ret);
        return -1;
    }

    int queueId = *(int *)knetRpcReponse.fixedLenData;

    return queueId;
}

int KnetGetQueueIdFromPrimary(void)
{
    int queueId = GetQueueIdFromPrimary();
    if (queueId == -1) {
        KNET_ERR("Get queueId from primary failed, queueId %d", queueId);
        return -1;
    }

    return queueId;
}

static int FreeQueueIdFromPrimary(int queueId)
{
    struct ConfigRequest req = {0};
    req.type = CONF_REQ_TYPE_FREE;
    req.queueId = queueId;

    struct KNET_RpcMessage knetRpcRequest = {0};
    int ret = memcpy_s(knetRpcRequest.fixedLenData, RPC_MESSAGE_SIZE, (char *)&req, sizeof(struct ConfigRequest));
    if (ret != 0) {
        KNET_ERR("Memcpy failed in free queue id, ret %d", ret);
        return -1;
    }
    knetRpcRequest.dataLen = sizeof(struct ConfigRequest);
    knetRpcRequest.dataType = RPC_MSG_DATA_TYPE_FIXED_LEN;

    struct KNET_RpcMessage knetRpcReponse = {0};
    ret = KNET_RpcCall(KNET_RPC_MOD_CONF, &knetRpcRequest, &knetRpcReponse);
    if (ret != 0) {
        KNET_ERR("Rpc request failed in free queue id, ret %d", ret);
        return -1;
    }

    ret = knetRpcReponse.ret;
    if (ret != 0) {
        KNET_ERR("Free Queue Id failed, ret %d", ret);
        return -1;
    }

    return 0;
}

int KnetFreeQueueIdFromPrimary(int index)
{
    int ret = FreeQueueIdFromPrimary(index);
    if (ret != 0) {
        KNET_ERR("Free queue id failed, ret %d, index %d", ret, index);
        return -1;
    }

    return 0;
}