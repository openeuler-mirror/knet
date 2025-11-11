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

#include "knet_hash_rpc.h"
#include "rte_hash.h"
#include "knet_log.h"
#include "knet_rpc.h"

#define HASH_TABLE_HANDLE_MAX 1024

struct HandleClientMap {          // 记录clientId与handle的映射关系
    struct rte_hash *handle;
    uint8_t initFlag;
    int clientID;
    char padding[3];   // 填充字节，确保结构体8 字节对齐
};

struct HandleClientMap g_hcMap[HASH_TABLE_HANDLE_MAX] = {0};

# define HASH_TABLE_QUEUE_NAME_LEN_MAX_MULTIPLE 64

struct HashRequest {
    char name [HASH_TABLE_QUEUE_NAME_LEN_MAX_MULTIPLE];
    uint32_t entries;
    uint32_t reserved;
    uint32_t keyLen;
    uint32_t hashFuncInitval;
    int socketId;
    uint8_t extraFlag;
    uint8_t type;
    char padding[2];   // 填充字节，确保结构体8 字节对齐
};

struct rte_hash *KnetCreateHashTblMultiple(uint32_t entries, uint32_t keySize, char *queueName, int queueNameSize)
{
    int32_t ret;
    struct rte_hash *handle;
    struct KnetRpcMessage req = {0};
    struct KnetRpcMessage res = {0};
    struct HashRequest *hr = (struct HashRequest*)req.data;

    ret = strcpy_s(hr->name, HASH_TABLE_QUEUE_NAME_LEN_MAX_MULTIPLE, queueName);
    if (ret != 0) {
        KNET_ERR("Strcpy hrName failed, ret %d", ret);
        return NULL;
    }
    hr->type = HASH_CREATE;
    hr->entries = entries;
    hr->keyLen = keySize;
    hr->hashFuncInitval = 0;
    hr->socketId = SOCKET_ID_ANY;

    req.len = sizeof(struct HashRequest);

    ret = KNET_RpcClient(KNET_MOD_HASH, &req, &res);
    if (ret != 0) {
        KNET_ERR("Rpc client failed: %d.", ret);
        return NULL;
    }
    ret = res.ret;
    if (ret != 0) {
        return NULL;
    }

    handle = rte_hash_find_existing(queueName);
    return handle;
}

int KnetDestroyHashTblMultiple(void)
{
    int32_t ret;
    struct KnetRpcMessage req = {0};
    struct KnetRpcMessage res = {0};
    struct HashRequest *hr = (struct HashRequest*)req.data;

    hr->type = HASH_DESTROY;
    req.len = sizeof(struct HashRequest);

    ret = KNET_RpcClient(KNET_MOD_HASH, &req, &res);
    if (ret != 0) {
        KNET_ERR("Rpc client failed: %d.", ret);
        return -1;
    }
    ret = res.ret;
    return ret;
}

// 从进程对hash表的请求处理
static int HashRequestHandler(int id, struct KnetRpcMessage *knetRpcRequest, struct KnetRpcMessage *knetRpcResponse)
{
    int32_t ret;
    struct rte_hash_parameters params = {0};
    struct HashRequest *hr = NULL;
    struct rte_hash *handle;
    hr = (struct HashRequest*)(knetRpcRequest->data);

    if (hr->type != HASH_CREATE && hr->type != HASH_DESTROY) {
        ret = -1;
        KNET_ERR("Hash request type err");
        knetRpcResponse->ret = ret;
        return ret;
    }

    if (hr->type == HASH_DESTROY) {
        for (int i = 0; i < HASH_TABLE_HANDLE_MAX; ++i) {
            if (g_hcMap[i].clientID == id) {
                rte_hash_free(g_hcMap[i].handle);
                g_hcMap[i].clientID = -1;
                g_hcMap[i].handle = NULL;
            }
        }
        ret = 0;
        knetRpcResponse->ret = ret;
        return ret;
    }

    params.name = hr->name;
    params.entries = hr->entries;
    params.key_len = hr->keyLen;
    params.hash_func = NULL;
    params.hash_func_init_val = hr->hashFuncInitval;
    params.socket_id = hr->socketId;

    handle = rte_hash_create(&params);
    if (handle == NULL) {
        ret = -1;
        KNET_ERR("The primary process Create hash table failed");
    } else {
        for (int i = 0; i < HASH_TABLE_HANDLE_MAX; ++i) {
            if (g_hcMap[i].clientID == -1) {
                g_hcMap[i].clientID = id;
                g_hcMap[i].handle = handle;
                break;
            }
        }
        ret = 0;
    }

    knetRpcResponse->ret = ret;
    return ret;
}

// 从进程断链后对hash表的处理
static int HashDisconnectHandler(int id, struct KnetRpcMessage *knetRpcRequest, struct KnetRpcMessage *knetRpcResponse)
{
    if (id == -1) {
        KNET_ERR("Invalid id");
        return -1;
    }

    for (int i = 0; i < HASH_TABLE_HANDLE_MAX; ++i) {
        if (g_hcMap[i].clientID == id) {
            KNET_INFO("Hash disconnect handler, client id: %d", id);
            rte_hash_free(g_hcMap[i].handle);
            g_hcMap[i].clientID = -1;
            g_hcMap[i].handle = NULL;
        }
    }
    return 0;
}

int KNET_InitHash(enum KnetProcType procType)
{
    int ret;

    if (procType != KNET_PROC_TYPE_PRIMARY) {
        return KNET_ERROR;
    }

    if (g_hcMap[0].initFlag == 0) {
        g_hcMap[0].initFlag = 1; // 只初始化一次
        for (int i = 0; i < HASH_TABLE_HANDLE_MAX; ++i) {
            g_hcMap[i].handle = NULL;
            g_hcMap[i].clientID = -1;
        }
    }

    ret = KNET_RegServer(KNET_CONNECT_EVENT_REQUEST, KNET_MOD_HASH, HashRequestHandler);
    if (ret != 0) {
        KNET_ERR("Init hash Register request handler failed");
        return -1;
    }
    ret = KNET_RegServer(KNET_CONNECT_EVENT_DISCONNECT, KNET_MOD_HASH, HashDisconnectHandler);
    if (ret != 0) {
        KNET_ERR("Init hash Register disconnect handler failed");
        return -1;
    }
    return 0;
}

int KNET_UninitHash(enum KnetProcType procType)
{
    KNET_DesServer(KNET_CONNECT_EVENT_REQUEST, KNET_MOD_HASH);
    KNET_DesServer(KNET_CONNECT_EVENT_DISCONNECT, KNET_MOD_HASH);

    return 0;
}