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

#ifndef __KNET_TRANSMISSION_H__
#define __KNET_TRANSMISSION_H__

#include "rte_hash.h"
#include "rte_flow.h"
#include "knet_config.h"
#include "knet_types.h"
#include "knet_offload.h"

#define INVALID_IP  0xFFFFFFFF
#define PORT_MAX 65535U
#define MAX_CPD_NAME_LEN 32

struct KNET_FDirRequest {
    uint32_t type;
    uint32_t dstIp;
    uint32_t dstIpMask;
    uint16_t dstPort;
    uint16_t dstPortMask;
    int32_t  proto;
    uint16_t queueIdSize;
    uint16_t queueId[KNET_MAX_QUEUES_PER_PORT]; // 同步offload中queue数组大小
};

/**
 * @brief queId和pid/tid的映射关系表
 * @attention 多进程的每个从进程都是用queIDMapPidTid[0]记录，因此多进程的queId没有实际意义。
 * @attention 单进程都是从queIDMapPidTid[0]开始记录， 表示第一个队列，有实际意义
 */
typedef struct {
    uint32_t pid;
    uint32_t tid;
    uint32_t lcoreId;
    uint32_t workerId;
} KNET_QueIdMapPidTid_t;

/**
 * @brief 协议栈建链和断链时的通知和处理函数
 *
 * @param fdir [IN] 参数类型struct KNET_FDirRequest。协议栈传输的流规则。
 * @return int 协议栈建链和断链时在KNET处理结果，成功返回0，失败返回-1。
 */
int KNET_EventNotify(struct KNET_FDirRequest *fdir);

/**
 * @brief 初始化管理流规则的hash表，多进程情况下RPC服务注册
 *
 * @param procType [IN] 参数类型enum KNET_ProcType。进程类型。
 * @return int 初始化结果。成功返回0，失败返回-1。
 */
int KNET_InitTrans(enum KNET_ProcType procType);

/**
 * @brief 删除流规则管理hash表，多进程情况下RPC服务注销
 *
 * @param procType [IN] 参数类型enum KNET_ProcType。进程类型。
 * @return int 资源销毁结果。成功返回0，失败返回-1。
 */
int KNET_UninitTrans(enum KNET_ProcType procType);

/**
 * @brief 报文发送接口
 *
 * @param queId [IN] 参数类型uint16_t。队列ID。
 * @param rteMbuf [IN] 参数类型struct rte_mbuf**。数据缓冲区。
 * @param cnt [IN] 参数类型int。报文个数。
 * @param portId [IN] 参数类型uint32_t。网卡端口号
 * @return int 报文发送结果。成功返回发送报文个数，失败返回错误码。
 */
int KNET_TxBurst(uint16_t queId, struct rte_mbuf** rteMbuf, int cnt, uint32_t portId);

/**
 * @brief 报文接收接口
 *
 * @param queId [IN] 参数类型uint16_t。队列ID。
 * @param rxBuf [IN] 参数类型struct rte_mbuf**。数据缓冲区。
 * @param cnt [IN] 参数类型int。报文个数。
 * @param portId [IN] 参数类型uint32_t。网卡端口号
 * @return int 报文接收结果。成功返回发送报文个数，失败返回错误码
 */
int KNET_RxBurst(uint16_t queId, struct rte_mbuf **rxBuf, int cnt, uint32_t portId);

/**
 * @brief 查找对应的ip port是否有流表，以及对应的队列ID
 *
 * @param dstIp [IN] 参数类型uint32_t。目标IP地址。
 * @param dstPort [IN] 参数类型uint16_t。目标端口号。
 * @param queueId [OUT] 参数类型uint16_t*。队列ID。
 * @return int -1: 没有流表，正数：流表中queue个数
 */
int KNET_FindFdirQue(uint32_t dstIp, uint16_t dstPort, uint16_t *queueId);

/**
 * @brief 拿到queueId到pid和tid映射关系表，该表是一个hash表，通过queueId下标获取queueId对应pid/tid/lcoreId
 * @attention queId和pid/tid的映射关系表, 多进程的每个从进程都是用queIDMapPidTid[0]记录，因此多进程的queId没有实际意义。
 * @attention 单进程都是从queIDMapPidTid[0]开始记录， 表示第一个队列，有实际意义
 */
KNET_QueIdMapPidTid_t* KNET_GetQueIdMapPidTidLcoreInfo(void);

/**
 * @brief 设置queueId到pid和tid映射关系表
 * @param input queId 设置的队列号[0,MAX_QUEUE_NUM)
 * @param input pid 设置队列被使用的进程号
 * @param input tid 设置队列被使用的线程号
 * @param input lcoreId 设置队列被使用的lcoreId
 */
int KNET_SetQueIdMapPidTidLcoreInfo(uint32_t queId, uint32_t pid, uint32_t tid, uint32_t lcoreId, uint32_t workerId);
#endif // __KNET_TRANSMISSION_H__
