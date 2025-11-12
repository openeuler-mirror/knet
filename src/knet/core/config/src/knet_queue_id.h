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

#ifndef __KNET_QUEUE_ID_H__
#define __KNET_QUEUE_ID_H__

#include "knet_config.h"
#include "knet_rpc.h"

enum ConfigRequestType {
    CONF_REQ_TYPE_GET = 0,
    CONF_REQ_TYPE_FREE,
    CONF_REQ_TYPE_LOAD_CONF
};
 
struct ConfigRequest {
    enum ConfigRequestType type;
    int queueId;
};

int KNET_CoreListInit(char *coreListGlobal);
int KNET_GetQueueId(enum KnetProcType procType);
int KNET_FreeQueueId(enum KnetProcType procType, int index);
int KNET_RegConfigRpcHandler(enum KnetProcType procType);
int KNET_GetCoreNum(void);
int KNET_GetCore(int queueId);
int FreeRequestHandler(int clientId, int queueId);
int GetRequestHandler(int clientId, struct KnetRpcMessage *knetRpcResponse);

#endif