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
    PMGR_ENTRY_UDP_IN,
    PMGR_ENTRY_UDP_ERR_IN,
    PMGR_ENTRY_UDP6_IN,
    PMGR_ENTRY_ROUTE_OUT,
    PMGR_ENTRY_ND_OUT,
    PMGR_ENTRY_ND_SOLICIT, // arp 探测
    PMGR_ENTRY_VLAN_OUT,
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

#define ATTR_WEAK __attribute__((weak))

#ifndef DPL2_ETH
#define ETH_INIT_ATTR ATTR_WEAK
#else
#define ETH_INIT_ATTR
#endif

#ifndef DPL3_IP
#define IP_INIT_ATTR ATTR_WEAK
#else
#define IP_INIT_ATTR
#endif

#ifndef DPL3_IP6
#define IP6_INIT_ATTR ATTR_WEAK
#else
#define IP6_INIT_ATTR
#endif

#ifndef DPL4_UDP
#define UDP_INIT_ATTR ATTR_WEAK
#else
#define UDP_INIT_ATTR
#endif

#ifndef DPL4_TCP
#define TCP_INIT_ATTR ATTR_WEAK
#else
#define TCP_INIT_ATTR
#endif

// 以下协议初始声明，由各协议实现，在pmgr.c中通过模块宏控进行裁剪
extern int ETH_Init(int slave) ETH_INIT_ATTR;
extern int IP_Init(int slave) IP_INIT_ATTR;
extern int IP6_Init(int slave) IP6_INIT_ATTR;
extern int UDP_Init(int slave) UDP_INIT_ATTR;
extern int TCP_Init(int slave) TCP_INIT_ATTR;

extern void ETH_Deinit(int slave) ETH_INIT_ATTR;
extern void IP_Deinit(int slave) IP_INIT_ATTR;
extern void IP6_Deinit(int slave) IP6_INIT_ATTR;
extern void UDP_Deinit(int slave) UDP_INIT_ATTR;
extern void TCP_Deinit(int slave) TCP_INIT_ATTR;

#ifdef __cplusplus
}
#endif
#endif
