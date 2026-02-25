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


#ifndef __KNET_CONFIG_RPC_H__
#define __KNET_CONFIG_RPC_H__

#include "knet_rpc.h"
#include "knet_config_core_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

enum ConfigRequestType {
    CONF_REQ_TYPE_GET = 0,
    CONF_REQ_TYPE_FREE,
    CONF_REQ_TYPE_LOAD_CONF
};

struct ConfigRequest {
    enum ConfigRequestType type;
    int queueId;
    pid_t pid;
};

int KnetGetRequestHandler(int clientId, struct KNET_RpcMessage *knetRpcRequest,
                          struct KNET_RpcMessage *knetRpcResponse);
int KnetFreeRequestHandler(int clientId, int queueId, pid_t pid);
int KnetGetQueueIdFromPrimary(void);
int KnetFreeQueueIdFromPrimary(int index);
int KnetRegConfigRpcHandler(enum KNET_ProcType procType);

#ifdef __cplusplus
}
#endif
#endif