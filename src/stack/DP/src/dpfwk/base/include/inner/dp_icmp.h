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

#ifndef DP_ICMP_H
#define DP_ICMP_H

#include "dp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DP_IcmpHdr {
    uint8_t  type; /* 消息类型 */
    uint8_t  code; /* 消息代码 */
    uint16_t cksum; /* 检验和 */
    union {
        struct {
            uint16_t id;
            uint16_t seq;
        } echo;
        uint32_t gw;
        struct {
            uint16_t unused;
            uint16_t mtu;
        } frag;
        uint32_t resv;
    };
} DP_PACKED DP_IcmpHdr_t;

#define DP_ICMP_TTL 0x40

#define DP_ICMP_TYPE_ECHOREPLY    0 /* Echo Reply                   */
#define DP_ICMP_TYPE_DEST_UNREACH 3 /* Destination Unreachable      */
#define DP_ICMP_TYPE_ECHO         8 /* Echo Request                 */

/* Codes for UNREACH. */
#define DP_ICMP_CODE_NET_UNREACH 0 /* Network Unreachable          */
#define DP_ICMP_PROT_UNREACH     2 /* Protocol Unreachable         */
#define DP_ICMP_PORT_UNREACH     3 /* Port Unreachable             */

#ifdef __cplusplus
}
#endif
#endif
