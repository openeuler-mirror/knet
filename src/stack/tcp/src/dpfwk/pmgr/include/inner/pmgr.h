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

#ifndef PMGR_H
#define PMGR_H

#include "pbuf.h"
#include "utils_debug.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PMGR_ENTRY_ETH_IN,
    PMGR_ENTRY_ETH_OUT,
    PMGR_ENTRY_IP_IN,
    PMGR_ENTRY_IP_OUT,
    PMGR_ENTRY_ICMP_IN,
    PMGR_ENTRY_IP6_IN,
    PMGR_ENTRY_IP6_OUT,
    PMGR_ENTRY_ARP_IN,
    PMGR_ENTRY_TCP_IN,
    PMGR_ENTRY_TCP6_IN,
    PMGR_ENTRY_UDP_IN,
    PMGR_ENTRY_UDP6_IN,
    PMGR_ENTRY_ROUTE_OUT,
    PMGR_ENTRY_ROUTE6_OUT,
    PMGR_ENTRY_ND_OUT,
    PMGR_ENTRY_ND_SOLICIT, // arp 探测
    PMGR_ENTRY_VLAN_OUT,
    PMGR_ENTRY_DELAY_KERNEL_IN, // 内核转发特殊场景的延后处理
    PMGR_ENTRY_BUTT,
} PMGR_Entry_t;

#define PMGR_ENTRY_NONE PMGR_ENTRY_BUTT

typedef Pbuf_t* (*PMGR_ProtoEntry_t)(Pbuf_t* pbuf);

void PMGR_Dispatch(Pbuf_t* pbuf);

PMGR_ProtoEntry_t PMGR_Get(PMGR_Entry_t entry);

void PMGR_AddEntry(PMGR_Entry_t id, PMGR_ProtoEntry_t entry);

int PMGR_Init(int slave);

void PMGR_Deinit(int slave);

PMGR_ProtoEntry_t* GetProtoEntrys(void);

#ifdef __cplusplus
}
#endif
#endif
