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

#ifndef __KNET_DPDK_CFG_H__
#define __KNET_DPDK_CFG_H__

#include "knet_io_init.h"

#define KNET_DPDK_PRIM_ARGC 14
#define KNET_DPDK_ARG_MAX_LEN 127

#define KNET_PKT_POOL_DEFAULT_CACHENUM 128
#define KNET_PKT_POOL_DEFAULT_CACHESIZE 512
#define KNET_PKT_POOL_DEFAULT_PRIVATE_SIZE 256
#define KNET_PKT_POOL_DEFAULT_HEADROOM_SIZE 128
#define KNET_PKT_POOL_DEFAULT_CREATE_ALG KNET_PKTPOOL_ALG_RING_MP_MC

#define MAX_WORKER_ID 512
#define INVALID_WORKER_ID UINT32_MAX
#define MAX_LRO_SEG 32768

#define MAX_TRANS_PATTERN_NUM 4
#define MAX_ARP_PATTERN_NUM 2
#define MAX_ACTION_NUM 2


typedef struct {
    DpWorkerInfo workerInfo[MAX_WORKER_ID];
    uint32_t coreIdToWorkerId[MAX_WORKER_ID];
    uint32_t maxWorkerId;
    char padding[4];   // 填充字节，确保结构体8 字节对齐
} DpWorkerIdTable; // data plane worker id table

typedef enum {
    ETH_PATTERN_INDEX = 0,
    IPV4_PATTERN_INDEX,
    TCP_PATTERN_INDEX,
    END_PATTERN_INDEX
} PatternIndex;

#endif // __KNET_DPDK_CFG_H__