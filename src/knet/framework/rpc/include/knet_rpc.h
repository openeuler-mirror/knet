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

#define RPC_MESSAGE_SIZE 128
// 此宏定义为rpc通信时约束的动态申请空间的最大值，理论最大值是配置文件传输MAX_CFG_SIZE
#define RPC_MESSAGE_SIZE_MAX 8000

enum KNET_RpcEventType {
    KNET_RPC_EVENT_CONNECT = 0,
    KNET_RPC_EVENT_REQUEST,
    KNET_RPC_EVENT_DISCONNECT,
    KNET_RPC_EVENT_EXCEPTION,
    KNET_RPC_EVENT_MAX
};

enum KNET_RpcModType {
    KNET_RPC_MOD_CONF = 0,
    KNET_RPC_MOD_FDIR,
    KNET_RPC_MOD_FMM,
    KNET_RPC_MOD_HASH,
    KNET_RPC_MOD_MAX
};

// KNET_RpcMessage内部data类型：固定长度(对应fixedLenData数组), 可变长度(对应variableLenData)
enum RpcMsgDataType {
    RPC_MSG_DATA_TYPE_FIXED_LEN = 0,
    RPC_MSG_DATA_TYPE_VARIABLE_LEN,
    RPC_MSG_DATA_TYPE_MAX
};

// KNET_RpcMessage支持传递两种类型数据中的一种，固定长度(实际长度dataLen限制为0-128字节)和可变长度(实际长度dataLen限制为1-20000字节)
struct KNET_RpcMessage {
    enum RpcMsgDataType dataType;
    char padding[4]; // 后续数组8字节对齐
    uint8_t fixedLenData[RPC_MESSAGE_SIZE];
    void* variableLenData;
    size_t dataLen;
    int ret;
};

/**
 * @brief 封装rpc服务的函数指针
 * @attention 在handler内部要设置KNET_RpcMessage使用可变长类型数据时，在handler内进行申请内存操作，遇到异常场景需要free
 */
typedef int (*RpcHandler)(int id, struct KNET_RpcMessage *knetRpcRequest, struct KNET_RpcMessage *knetRpcResponse);

/**
 * @brief 服务端(daemon)创建 socket和epoll实例，等待客户端(从进程)请求并处理，运行会阻塞在while循环
 * @return int 0成功，非0失败
 */
int KNET_RpcRun(void);

/**
 * @brief 客户端(从进程)创建socket建连，发送服务请求，接收执行结果
 * @param mod 请求所在模块
 * @param knetRpcRequest 存放请求信息的结构体指针
 * @param knetRpcResponse 存放结果信息的结构体指针
 * @return int 0成功，非0失败
 * @attention 涉及到可变长数据类型的KNET_RpcMessage时，需要在使用完成后进行free
 */
int KNET_RpcCall(enum KNET_RpcModType mod, struct KNET_RpcMessage *knetRpcRequest,
                 struct KNET_RpcMessage *knetRpcResponse);

/**
 * @brief 注册服务
 * @param event 服务所属事件
 * @param mod 服务所属模块
 * @param handler 待注册的服务(函数指针)
 * @return int 	0成功，非0失败
 */
int KNET_RpcRegServer(enum KNET_RpcEventType event, enum KNET_RpcModType mod, RpcHandler handler);

/**
 * @brief 注销服务
 * @param event 服务所属事件
 * @param mod 服务所属模块
 */
void KNET_RpcDesServer(enum KNET_RpcEventType event, enum KNET_RpcModType mod);

#endif  // __KNET_RPC_H__