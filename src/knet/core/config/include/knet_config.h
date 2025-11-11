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

#ifndef __KNET_CONFIG_H__
#define __KNET_CONFIG_H__

#define MAX_STRVALUE_NUM 64

#include "knet_types.h"

enum ConfModule {
    CONF_COMMON,
    CONF_INTERFACE,
    CONF_HW,
    CONF_DP,
    CONF_DPDK,
    CONF_INNER,
    CONF_MAX,
};

enum KnetConfKey {
    // common配置项起始位置
    CONF_COMMON_MIN = 0,
    CONF_COMMON_MODE = CONF_COMMON_MIN,
    CONF_COMMON_LOG_LEVEL,
    CONF_COMMON_CTRL_VCPU_ID,
    CONF_COMMON_MAX,

    // interface配置项起始位置
    CONF_INTERFACE_MIN = 1000,
    CONF_INTERFACE_BDF_NUM = CONF_INTERFACE_MIN,
    CONF_INTERFACE_MAC,
    CONF_INTERFACE_IP,
    CONF_INTERFACE_NETMASK,
    CONF_INTERFACE_GATEWAY,
    CONF_INTERFACE_MTU,
    CONF_INTERFACE_MAX,

    // hw_offload配置项起始位置
    CONF_HW_MIN = 2000,
    CONF_HW_TSO = CONF_HW_MIN,
    CONF_HW_LRO,
    CONF_HW_TCP_CHECKSUM,
    CONF_HW_MAX,

    // proto_stack配置项起始位置
    CONF_DP_MIN = 3000,
    CONF_DP_MAX_MBUF = CONF_DP_MIN,
    CONF_DP_MAX_WORKER_NUM,
    CONF_DP_MAX_ROUTE,
    CONF_DP_MAX_ARP,
    CONF_DP_MAX_TCPCB,
    CONF_DP_MAX_UDPCB,
    CONF_DP_TCP_SACK,
    CONF_DP_TCP_DACK,
    CONF_DP_MSL_TIME,
    CONF_DP_FIN_TIMEOUT,
    CONF_DP_MIN_PORT,
    CONF_DP_MAX_PORT,
    CONF_DP_MAX_SENDBUF,
    CONF_DP_DEF_SENDBUF,
    CONF_DP_MAX_RECVBUF,
    CONF_DP_DEF_RECVBUF,
    CONF_DP_TCP_COOKIE,
    CONF_DP_REASS_MAX,
    CONF_DP_REASS_TIMEOUT,
    CONF_DP_MAX,
 
    // dpdk配置项起始位置
    CONF_DPDK_MIN = 4000,
    CONF_DPDK_CORE_LIST_GLOBAL = CONF_DPDK_MIN,
    CONF_DPDK_TX_CACHE_SIZE,
    CONF_DPDK_RX_CACHE_SIZE,
    CONF_DPDK_SOCKET_MEM,
    CONF_DPDK_SOCKET_LIM,
    CONF_DPDK_EXTERNAL_DRIVER,
    CONF_DPDK_TELEMETRY,
    CONF_DPDK_HUGE_DIR,
    CONF_DPDK_MAX,

    // 内部配置项起始位置
    CONF_INNER_MIN = 5000,
    CONF_INNER_QID = CONF_INNER_MIN,
    CONF_INNER_CORE_LIST,
    CONF_INNER_PROC_TYPE,
    CONF_INNER_TX_QUEUE_NUM,
    CONF_INNER_RX_QUEUE_NUM,
    CONF_INNER_MAX,
};

enum KnetProcType {
    KNET_PROC_TYPE_PRIMARY = 0,
    KNET_PROC_TYPE_SECONDARY
};

enum KnetRunMode {
    KNET_RUN_MODE_SINGLE = 0,
    KNET_RUN_MODE_MULTIPLE,
    KNET_RUN_MODE_INVALID
};

union CfgValue {
    int32_t intValue;
    char strValue[MAX_STRVALUE_NUM];
};

#define INVALID_CONF_VALUE 0xFFFFFFFF
#define MAC_ADDR_LEN 6

/* 流规则 */
struct KnetFlowCfg {
    uint32_t flowEnable;  /* 流规则使能 */
    uint32_t rxQueueId;   /* 接收队列ID */
    uint32_t srcIp;       /* 源IP */
    uint32_t srcIpMask;   /* 源IP掩码 */
    uint32_t dstIp;       /* 目的IP */
    uint32_t dstIpMask;   /* 目的IP掩码 */
    uint16_t srcPort;     /* 源端口 */
    uint16_t srcPortMask; /* 源端口掩码 */
    uint16_t dstPort;     /* 目的端口 */
    uint16_t dstPortMask; /* 目的端口掩码 */
};

int KNET_InitCfg(enum KnetProcType outterProcType);
union CfgValue KNET_GetCfg(enum KnetConfKey key);
int KNET_CtrlVcpuCheck(void);

int KNET_FindCoreInList(int index);
int KNET_IsQueueIdUsed(int queueId);
#endif