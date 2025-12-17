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
 * @file dp_ip.h
 * @brief IP首部相关定义
 */

#ifndef DP_IP_H
#define DP_IP_H

#include "dp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DP_IPHDR_TYPE_ICMP 1 /* Internet Control Message Protocol.  */
#define DP_IPHDR_TYPE_IGMP 2 /* Internet Group Management Protocol. */
#define DP_IPHDR_TYPE_IPIP 4 /* IPIP tunnels (older KA9Q tunnels use 94).  */
#define DP_IPHDR_TYPE_TCP  6 /* Transmission Control Protocol.  */
#define DP_IPHDR_TYPE_UDP  17 /* User Datagram Protocol.  */
#define DP_IPHDR_TYPE_IPV6 41 /* IPv6 header.  */
#define DP_IPHDR_TYPE_RAW  255 /* Raw IP packets.  */

#define DP_IPHDR_TOS 0x10 /* *< 默认TOS值 */

#ifndef DP_IPHDR_TTL
#define DP_IPHDR_TTL 0xFE /* *< 默认TTL值，为了与Linux区分，这里设定为254 */
#endif

#define DP_IP_FRAG_DF         (1 << 14) /**< dont frag */
#define DP_IP_FRAG_MF         (1 << 13) /**< more frag */
#define DP_IS_FRAG(offset)     (((offset) & DP_IP_FRAG_DF) == 0) /**< 判断是否分片 */
#define DP_IS_LAST_FRAG(offset) (((offset) & DP_IP_FRAG_MF) == 0) /**< 判断最后一片 */
#define DP_IP_FRAG_OFFSET(off)  (((off) & ((1 << 13) - 1)) << 3) /**< 计算IP offset */

#define DP_IP_VERSION_IPV4 4

/* !< IPv4首部 */
typedef struct DP_IpHdr {
#if DP_BYTE_ORDER == DP_LITTLE_ENDIAN
    uint8_t hdrlen : 4;
    uint8_t version : 4;
#else
    uint8_t version : 4;
    uint8_t hdrlen : 4;
#endif
    uint8_t  tos;
    uint16_t totlen;
    uint16_t ipid;
    uint16_t off;
    uint8_t  ttl;

    uint8_t  type;
    uint16_t chksum;
    uint32_t src;
    uint32_t dst;
} DP_PACKED DP_IpHdr_t;

#define DP_GET_IP_HDR_LEN(ipHdr) ((ipHdr)->hdrlen << 2)

#ifdef __cplusplus
}
#endif
#endif
