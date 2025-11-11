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

#ifndef __KNET_RPC_H__
#define __KNET_RPC_H__

// RPC_MESSAGE_SIZE 修改前为128，为了适配knet.conf的传输，改为2560
#define RPC_MESSAGE_SIZE 2560

enum ConnectEvent {
    KNET_CONNECT_EVENT_NEW = 0,
    KNET_CONNECT_EVENT_REQUEST,
    KNET_CONNECT_EVENT_DISCONNECT,
    KNET_CONNECT_EVENT_EXCEPTION,
    KNET_CONNECT_EVENT_MAX
};

enum KnetModType {
    KNET_MOD_CONF = 0,
    KNET_MOD_FDIR,
    KNET_MOD_FMM,
    KNET_MOD_HASH,
    KNET_MOD_MAX
};

struct KnetRpcMessage {
    uint8_t data[RPC_MESSAGE_SIZE];
    size_t len;
    int ret;
    char padding[4];   // 填充字节，确保结构体8 字节对齐
};

typedef int (*RpcHandler)(int id, struct KnetRpcMessage *knetRpcRequest, struct KnetRpcMessage *knetRpcMessage);

int KNET_RpcRun(void);

int KNET_RpcClient(enum KnetModType mod, struct KnetRpcMessage *knetRpcRequest, struct KnetRpcMessage *knetRpcResponse);

int KNET_RegServer(enum ConnectEvent event, enum KnetModType mod, RpcHandler handler);

void KNET_DesServer(enum ConnectEvent event, enum KnetModType mod);

#endif  // __KNET_RPC_H__