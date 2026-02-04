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

#include "knet_types.h"

enum KNET_ConfModule {
    CONF_COMMON,
    CONF_INTERFACE,
    CONF_HW,
    CONF_TCP,
    CONF_DPDK,
    CONF_INNER,
    CONF_MAX,
};
 
#define MAX_CONF_NUM_PER_INDEX_BITS 20
#define CONF_INDEX_LOWER_MASK ((1 << MAX_CONF_NUM_PER_INDEX_BITS) - 1)

#define MAX_STRVALUE_NUM 64
#define MAX_STRVALUE_COUNT 2
#define MAX_INT_COUNT 8
#define MAX_VCPU_NUMS 8
#define MAX_RING_PER_VCPU 8
#define INVALID_CONF_INT_VALUE (-1)

enum KNET_BifurCfg {
    BIFUR_FORBID = 0,
    BIFUR_ENABLE,
    KERNEL_FORWARD_ENABLE,
    BIFUR_CFG_MAX
};

enum KNET_ConfKey {
    // common配置项起始位置
    CONF_COMMON_MIN = CONF_COMMON << MAX_CONF_NUM_PER_INDEX_BITS,
    CONF_COMMON_MODE = CONF_COMMON_MIN,
    CONF_COMMON_LOG_LEVEL,
    CONF_COMMON_CTRL_VCPU_NUMS,
    CONF_COMMON_CTRL_RING_PER_VCPU,
    CONF_COMMON_CTRL_VCPU_IDS,
    CONF_COMMON_ZERO_COPY,
    CONF_COMMON_COTHREAD,
    CONF_COMMON_MAX,

    // interface配置项起始位置
    CONF_INTERFACE_MIN = CONF_INTERFACE << MAX_CONF_NUM_PER_INDEX_BITS,
    CONF_INTERFACE_BOND_ENABLE = CONF_INTERFACE_MIN,
    CONF_INTERFACE_BOND_MODE,
    CONF_INTERFACE_BDF_NUMS,
    CONF_INTERFACE_MAC,
    CONF_INTERFACE_IP,
    CONF_INTERFACE_NETMASK,
    CONF_INTERFACE_GATEWAY,
    CONF_INTERFACE_MTU,
    CONF_INTERFACE_MAX,

    // hw_offload配置项起始位置
    CONF_HW_MIN = CONF_HW << MAX_CONF_NUM_PER_INDEX_BITS,
    CONF_HW_TSO = CONF_HW_MIN,
    CONF_HW_LRO,
    CONF_HW_TCP_CHECKSUM,
    CONF_HW_BIFUR_ENABLE, // 1: 开启流量分叉 2: 开启内核转发
    CONF_HW_MAX,

    // proto_stack配置项起始位置
    CONF_TCP_MIN = CONF_TCP << MAX_CONF_NUM_PER_INDEX_BITS,
    CONF_TCP_MAX_MBUF = CONF_TCP_MIN,
    CONF_TCP_MAX_WORKER_NUM,
    CONF_TCP_MAX_ROUTE,
    CONF_TCP_MAX_ARP,
    CONF_TCP_MAX_TCPCB,
    CONF_TCP_MAX_UDPCB,
    CONF_TCP_TCP_SACK,
    CONF_TCP_TCP_DACK,
    CONF_TCP_MSL_TIME,
    CONF_TCP_FIN_TIMEOUT,
    CONF_TCP_MIN_PORT,
    CONF_TCP_MAX_PORT,
    CONF_TCP_MAX_SENDBUF,
    CONF_TCP_DEF_SENDBUF,
    CONF_TCP_MAX_RECVBUF,
    CONF_TCP_DEF_RECVBUF,
    CONF_TCP_TCP_COOKIE,
    CONF_TCP_REASS_MAX,
    CONF_TCP_REASS_TIMEOUT,
    CONF_TCP_SYNACK_RETRIES,
    CONF_TCP_SGE_LEN,
    CONF_TCP_SGE_NUM,
    CONF_TCP_EPOLL_DATA,
    CONF_TCP_MAX,
 
    // dpdk配置项起始位置
    CONF_DPDK_MIN = CONF_DPDK << MAX_CONF_NUM_PER_INDEX_BITS,
    CONF_DPDK_CORE_LIST_GLOBAL = CONF_DPDK_MIN,
    CONF_DPDK_QUEUE_NUM,
    CONF_DPDK_TX_CACHE_SIZE,
    CONF_DPDK_RX_CACHE_SIZE,
    CONF_DPDK_SOCKET_MEM,
    CONF_DPDK_SOCKET_LIM,
    CONF_DPDK_EXTERNAL_DRIVER,
    CONF_DPDK_TELEMETRY,
    CONF_DPDK_HUGE_DIR,
    CONF_DPDK_BASE_VIRTADDR,
    CONF_DPDK_MAX,

    // 内部配置项起始位置
    CONF_INNER_MIN = CONF_INNER << MAX_CONF_NUM_PER_INDEX_BITS,
    CONF_INNER_QID = CONF_INNER_MIN,
    CONF_INNER_CORE_LIST,
    CONF_INNER_PROC_TYPE,
    CONF_INNER_QUEUE_NUM,
    CONF_INNER_PORT_STEP,
    CONF_INNER_HW_TYPE,
    CONF_INNER_NEED_STOP_QUEUE,
    CONF_INNER_KERNEL_BOND_NAME,
    CONF_INNER_MAX,
};

enum KNET_ProcType {
    KNET_PROC_TYPE_PRIMARY = 0,
    KNET_PROC_TYPE_SECONDARY
};

enum KNET_RunMode {
    KNET_RUN_MODE_SINGLE = 0,
    KNET_RUN_MODE_MULTIPLE,
    KNET_RUN_MODE_INVALID
};

enum KNET_HwType {
    KNET_HW_TYPE_TM280 = 0,
    KNET_HW_TYPE_SP670,
    KNET_HW_TYPE_MAX
};

enum KNE_StopQueueType {
    KNET_NOT_STOP_QUEUE = 0,
    KNET_STOP_QUEUE
};

struct KNET_CfgNode {
    void *data;
    size_t dataSize;
    struct KNET_CfgNode *next;
};

union KNET_CfgValue {
    int32_t intValue;
    uint64_t uint64Value;
    int32_t intValueArr[MAX_INT_COUNT];
    char strValue[MAX_STRVALUE_NUM];
    char strValueArr[MAX_STRVALUE_COUNT][MAX_STRVALUE_NUM];
};

typedef void (*KnetRpcRequestHandle)(int, pid_t);
struct KnetRpcReqNotifyTelemetry {
    KnetRpcRequestHandle addNewProcess;
    KnetRpcRequestHandle delOldProcess;
};

/**
 * @brief 注册RPC遥测通知函数
 *
 * @param notifyFunc [IN] 遥测通知回调函数指针,包含不同的事件函数
 * @return int 0表示成功，-1表示失败
 */
int KNET_RpcRegTelemetryNotifyFunc(struct KnetRpcReqNotifyTelemetry* notifyFunc);

/**
 * @brief 配置文件初始化
 *
 * @param outterProcType [IN] 参数类型 enum KNET_ProcType。指定进程模式
 * @return int 0表示成功，-1表示失败
 */
int KNET_InitCfg(enum KNET_ProcType outterProcType);

/**
 * @brief 释放配置文件主从一致性校验所占资源
 *
 */
void KNET_UninitCfg(void);

/**
 * @brief 获取配置项的值，模块内接口，且性能敏感，不检查参数有效性。建议使用前确认参数有效性。
 *
 * @param key [IN] 参数类型 enum ConfKey。指定配置项
 * @return union 配置项值的联合体
 */
const union KNET_CfgValue *KNET_GetCfg(enum KNET_ConfKey key);

/**
 * @brief 查找 cpu 是否在绑核列表中
 *
 * @param index cpu索引编号
 * @return int -1表示不在列表中，其他返回在列表中的索引
 */
int KNET_FindCoreInList(int index);

/**
 * @brief 判断 queueId 是否被使用
 *
 * @param queueId queue编号
 * @return int 1表示被使用，0表示空闲或不可用
 */
int KNET_IsQueueIdUsed(int queueId);

/**
 * @brief 判断是否需要下流表, 无需下发的场景为: 单进程 且 关流量分叉 且 (关闭共线程 或 max_worker_num为 1)
 *
 * @return int 0 : 无需下发
 *         int 1 : 需要下发
 */
int KNET_IsNeedFlowRule(void);
#endif