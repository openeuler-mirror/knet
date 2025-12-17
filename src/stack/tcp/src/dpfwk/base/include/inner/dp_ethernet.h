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
/**
 * @file dp_ethernet.h
 * @brief 二层协议首部及相关类型定义
 */

#ifndef DP_ETHERNET_H
#define DP_ETHERNET_H

#include "dp_ether_api.h"

#include "dp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DP_ETH_P_ARP        0x0806 //!< ARP报文类型
#define DP_ETH_P_IP         0x0800 //!< IP报文类型
#define DP_ETH_P_IPV6       0x86dd //!< IPv6报文类型
#define DP_ETH_P_VLAN       0x8100 //!< ETH VLAN报文类型
#define DP_ETH_P_PPPOE_DISC 0x8863 //!< PPPoE Discory
#define DP_ETH_P_PPPOE_SES  0x8864 //!< PPPoE Session
#define DP_ETH_P_ALL        0x0003 //!< Every packet

/** 以太首部 */
typedef struct DP_EthHdr {
    DP_EthAddr_t dst;
    DP_EthAddr_t src;
    uint16_t       type;
} DP_PACKED DP_EthHdr_t;

#define DP_MAC_COPY(dst, src)           \
    do {                                 \
        (dst)->addr[0] = (src)->addr[0]; \
        (dst)->addr[1] = (src)->addr[1]; \
        (dst)->addr[2] = (src)->addr[2]; \
        (dst)->addr[3] = (src)->addr[3]; \
        (dst)->addr[4] = (src)->addr[4]; \
        (dst)->addr[5] = (src)->addr[5]; \
    } while (0)

#define DP_MAC_IS_EQUAL(dst, src)                                                                                      \
    (((dst)->addr[0] == (src)->addr[0]) && ((dst)->addr[1] == (src)->addr[1]) && ((dst)->addr[2] == (src)->addr[2]) && \
        ((dst)->addr[3] == (src)->addr[3]) && ((dst)->addr[4] == (src)->addr[4]) &&                                    \
        ((dst)->addr[5] == (src)->addr[5]))

#define DP_MAC_SET_BROADCAST(mac) \
    do {                          \
        (mac)->addr[0] = 0xFF;    \
        (mac)->addr[1] = 0xFF;    \
        (mac)->addr[2] = 0xFF;    \
        (mac)->addr[3] = 0xFF;    \
        (mac)->addr[4] = 0xFF;    \
        (mac)->addr[5] = 0xFF;    \
    } while (0)

#define DP_MAC_SET_DUMMY(mac)     \
    do {                          \
        (mac)->addr[0] = 0x00;    \
        (mac)->addr[1] = 0x00;    \
        (mac)->addr[2] = 0x00;    \
        (mac)->addr[3] = 0x00;    \
        (mac)->addr[4] = 0x00;    \
        (mac)->addr[5] = 0x00;    \
    } while (0)

#define DP_MAC_IS_BROADCAST(mac)                                                                             \
    ((mac)->addr[0] == 0xFF && (mac)->addr[1] == 0xFF && (mac)->addr[2] == 0xFF && (mac)->addr[3] == 0xFF && \
        (mac)->addr[4] == 0xFF && (mac)->addr[5] == 0xFF)

#define DP_MAC_IS_MUTICAST(mac) ((((uint8_t*)(mac))[0] & 0x1) == 0x1 && !DP_MAC_IS_BROADCAST(mac)) || \
                                (((uint8_t*)(mac))[0] == 0x33 && ((uint8_t*)(mac))[1] == 0x33)

#define DP_MAC_IS_UNICAST(mac) ((((uint8_t*)(mac))[0] & 0x1) == 0)

#define DP_MAC_IS_DUMMY(mac)                                                                             \
    ((mac)->addr[0] == 0 && (mac)->addr[1] == 0 && (mac)->addr[2] == 0 && (mac)->addr[3] == 0 && \
        (mac)->addr[4] == 0 && (mac)->addr[5] == 0)

#ifdef __cplusplus
}
#endif
#endif
