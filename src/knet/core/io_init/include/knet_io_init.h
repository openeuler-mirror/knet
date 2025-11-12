/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: DPDK初始化
 */

#ifndef __KNET_IO_INIT_H__
#define __KNET_IO_INIT_H__

#include "rte_flow.h"
#include "knet_config.h"
#include "knet_lock.h"

#define CACHE_LINE 64

typedef struct {
    uint16_t portId;
    char padding[6];   // 填充字节，确保结构体8 字节对齐
} DpdkNetdevCtx;

typedef struct {
    DpdkNetdevCtx netdevCtx;
    uint32_t pktPoolId;
    char padding[4];   // 填充字节，确保结构体8 字节对齐
} DpdkDevInfo;

typedef struct {
    KNET_SpinLock lock;
    uint32_t workerId;  // 协议栈workerId
    uint32_t lcoreId;   // dpdk lcoreId
} __attribute__((__aligned__((CACHE_LINE)))) DpWorkerInfo;

DpdkNetdevCtx *KNET_GetNetDevCtx(void);
DpWorkerInfo *KNET_DpWorkerInfoGet(uint32_t dpWorkerId);
uint32_t KNET_DpMaxWorkerIdGet(void);

int32_t KNET_ACC_WorkerGetSelfId(void);

int32_t KNET_InitDpdk(int procType, int processMode);
int32_t KNET_UninitDpdk(int procType, int processMode);

int32_t KnetGenerateIpv4TcpPortFlow(struct KnetFlowCfg *flowCfg, struct rte_flow **flow);
int32_t KnetGenerateIpv4UdpPortFlow(struct KnetFlowCfg *flowCfg, struct rte_flow **flow);
int32_t KnetGenerateCtlArpFlow(uint32_t queueId, struct rte_flow **flow);
int KNET_DeleteFlowRule(struct rte_flow *flow);

#endif // __KNET_IO_INIT_H__