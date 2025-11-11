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

/**
 * @brief 默认Fdirhash table的entry大小
 */
#define MAX_ENTRIES 2048
#define INVALID_IP  0xFFFFFFFF
#define INVALID_PORT 0xFFFF
#define DST_IPMASK 0xFFFFFFFF
#define DST_PORTMASK 0xFFFF

struct FDirRequest {
    uint32_t type;
    uint32_t queueId;
    uint32_t dstIp;
    uint32_t dstIpMask;
    uint16_t dstPort;
    uint16_t dstPortMask;
    int32_t  proto;
};

struct Entry {
    uint64_t ip_port;
    struct Map {
        int clientId;
        int count;
        uint32_t queueId;
        char padding[4];   // 填充字节，确保结构体8 字节对齐
        struct rte_flow *flow;
        struct rte_flow *arpFlow;
    } map;
};

enum AccConnectType {
    ACC_CONNECT = 0,
    ACC_DISCONNECT = 1
};

int KNET_EventNotify(uint32_t ip, uint32_t port, int32_t proto, uint32_t type);
int KNET_InitTrans(enum KnetProcType procType);
int KNET_UninitTrans(enum KnetProcType procType);
int KNET_TxBurst(uint16_t queId, struct rte_mbuf** rteMbuf, int cnt, uint32_t portId);
int KNET_RxBurst(uint16_t queId, struct rte_mbuf **rxBuf, int cnt, uint32_t portId);
void KNET_SetFdirHandle(struct rte_hash* fdirHandle);
struct rte_hash* KNET_GetFdirHandle(void);

#endif // __KNET_TRANSMISSION_H__
