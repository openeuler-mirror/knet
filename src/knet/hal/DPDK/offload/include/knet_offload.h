/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 硬件卸载相关操作
 */
#ifndef __KNET_OFFLOAD_H__
#define __KNET_OFFLOAD_H__

#include "rte_ethdev.h"


#ifdef __cplusplus
extern "C" {
#endif

enum DPDK_PORT_TYPE {
    DPDK_PORT_NORMAL = 0,
    DPDK_PORT_BOND,
    DPDK_PORT_INVALID
};

#define KNET_MAX_QUEUES_PER_PORT 1024 /* 同步dpdk约束 RTE_MAX_QUEUES_PER_PORT */
#define MAX_TRANS_PATTERN_NUM 4
#define MAX_ACTION_NUM 2
/* telemetry 流规则信息协议匹配和动作 */
struct KNET_FlowTeleInfo {
    struct rte_flow_action action[MAX_ACTION_NUM];
    struct rte_flow_item pattern[MAX_TRANS_PATTERN_NUM];
};

/* 流规则 */
struct KNET_FlowCfg {
    int flowEnable;  /* 流规则使能 */
    uint16_t rxQueueId[KNET_MAX_QUEUES_PER_PORT];   /* 接收队列ID */
    uint16_t rxQueueIdSize;   /* 接收队列ID长度 */
    uint32_t srcIp;       /* 源IP */
    uint32_t srcIpMask;   /* 源IP掩码 */
    uint32_t dstIp;       /* 目的IP */
    uint32_t dstIpMask;   /* 目的IP掩码 */
    uint16_t srcPort;     /* 源端口 */
    uint16_t srcPortMask; /* 源端口掩码 */
    uint16_t dstPort;     /* 目的端口 */
    uint16_t dstPortMask; /* 目的端口掩码 */
    int proto;            /* 协议类型 */
};

/**
 * @brief 开启LRO
 *
 * @param devInfo 设备信息
 * @param portConf 端口配置信息
 * @param dpdkPortType dpdk端口类型
 * @return int32_t 返回值，0成功，-1失败
 */
int32_t KNET_SetLRO(const struct rte_eth_dev_info *devInfo, struct rte_eth_conf *portConf, int dpdkPortType);

/**
 * @brief 开启TSO
 *
 * @param devInfo 设备信息
 * @param portConf 端口配置信息
 * @return int32_t 返回值，0成功，-1失败
 */
int32_t KNET_SetTSO(const struct rte_eth_dev_info *devInfo, struct rte_eth_conf *portConf);

/**
 * @brief 创建一个 IPv4 和 TCP/UDP 流量规则，并将其应用到指定 DPDK网络端口上
 *
 * @param portId 端口ID
 * @param flowCfg 流量配置
 * @param flow 返回的流规则指针
 * @return int32_t 返回值，0成功，-1失败
 */
int32_t KNET_GenerateIpv4Flow(uint16_t portId, struct KNET_FlowCfg *flowCfg, struct rte_flow **flow,
                              struct KNET_FlowTeleInfo *flowTele);

/**
 * @brief 创建一个 ARP 流量规则，并将其应用到指定的 DPDK 网络端口上
 *
 * @param portId 端口ID
 * @param queueId 队列ID
 * @param flow 返回的流规则指针
 * @return int32_t 返回值，0成功，-1失败
 */
int32_t KNET_GenerateArpFlow(uint16_t portId, uint32_t queueId, struct rte_flow **flow);

/**
 * @brief 删除一个流规则
 *
 * @param portId 端口ID
 * @param flow 流规则指针
 */
int32_t KNET_DeleteFlowRule(uint16_t portId, struct rte_flow *flow);

uint32_t KNET_GetMaxEntryId(void);

#ifdef __cplusplus
}
#endif
#endif