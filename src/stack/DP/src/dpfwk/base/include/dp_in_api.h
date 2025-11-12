/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: IP地址及选项相关信息
 */

#ifndef DP_IN_API_H
#define DP_IN_API_H

#include <stdint.h>

#include "dp_socket_types_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup in inet_addr
 * @ingroup base
 */

/**
 * @ingroup in
 * Ip地址信息类型
 */
typedef uint32_t DP_InAddr_t;

/**
 * @ingroup in
 * Ip地址信息
 */
struct DP_InAddr {
    DP_InAddr_t s_addr;
};

/**
 * @ingroup in
 * @brief Dummy protocol for TCP
 */
#define DP_IPPROTO_IP   0

/**
 * @ingroup in
 * @brief Internet Control Message Protocol
 */
#define DP_IPPROTO_ICMP 1

/**
 * @ingroup in
 * @brief Transmission Control Protocol
 */
#define DP_IPPROTO_TCP  6

/**
 * @ingroup in
 * @brief User Datagram Protocol
 */
#define DP_IPPROTO_UDP  17

/**
 * @ingroup in
 * @brief IPv6 header
 */
#define DP_IPPROTO_IPV6 41

#define DP_IN_CLASSA(a)     ((((DP_InAddr_t)(a)) & 0x80000000) == 0)
#define DP_IN_CLASSA_NET    0xff000000
#define DP_IN_CLASSA_NSHIFT 24
#define DP_IN_CLASSA_HOST   (0xffffffff & ~IN_CLASSA_NET)
#define DP_IN_CLASSA_MAX    128

#define DP_IN_CLASSB(a)     ((((DP_InAddr_t)(a)) & 0xc0000000) == 0x80000000)
#define DP_IN_CLASSB_NET    0xffff0000
#define DP_IN_CLASSB_NSHIFT 16
#define DP_IN_CLASSB_HOST   (0xffffffff & ~IN_CLASSB_NET)
#define DP_IN_CLASSB_MAX    65536

#define DP_IN_CLASSC(a)     ((((DP_InAddr_t)(a)) & 0xe0000000) == 0xc0000000)
#define DP_IN_CLASSC_NET    0xffffff00
#define DP_IN_CLASSC_NSHIFT 8
#define DP_IN_CLASSC_HOST   (0xffffffff & ~IN_CLASSC_NET)

#define DP_IN_CLASSD(a)    ((((DP_InAddr_t)(a)) & 0xf0000000) == 0xe0000000)
#define DP_IN_MULTICAST(a) IN_CLASSD(a)

#define DP_IN_EXPERIMENTAL(a) ((((DP_InAddr_t)(a)) & 0xe0000000) == 0xe0000000)
#define DP_IN_BADCLASS(a)     ((((DP_InAddr_t)(a)) & 0xf0000000) == 0xf0000000)

/**
 * @ingroup in
 * @brief Address to accept any incoming messages
 */
#define DP_INADDR_ANY ((DP_InAddr_t)0x00000000)

/**
 * @ingroup in
 * @brief Address to send to all hosts
 */
#define DP_INADDR_BROADCAST ((DP_InAddr_t)0xffffffff)

/**
 * @ingroup in
 * @brief Address indicating an error return.
 */
#define DP_INADDR_NONE ((DP_InAddr_t)0xffffffff)

/**
 * @ingroup in
 * Ipv6地址信息
 */
typedef struct DP_In6Addr {
    union {
        uint8_t  addr8[16];
        uint16_t addr16[8];
        uint32_t addr32[4];
    } in6_u;
} DP_In6Addr_t;

// posix compat
#ifndef s6_addr
#define s6_addr __in6_u.__u6_addr8
#endif

/**
 * @ingroup in
 * 端口变量类型
 */
typedef uint16_t DP_InPort_t;

/**
 * @ingroup in
 * sock连接信息，与struct sockaddr_in对齐
 */
struct DP_SockaddrIn {
    DP_SaFamily_t    sin_family;
    DP_InPort_t      sin_port; /* Port number.  */
    struct DP_InAddr sin_addr; /* Internet address.  */
    unsigned char      sin_zero[8]; /* Pad to size of `struct sockaddr'.  */
};

/**
 * @ingroup in
 * sock ipv6连接信息
 */
struct DP_SockaddrIn6 {
    DP_SaFamily_t     sin6_family;
    DP_InPort_t       sin6_port; /* Transport layer port # */
    uint32_t            sin6_flowinfo; /* IPv6 flow information */
    struct DP_In6Addr sin6_addr; /* IPv6 address */
    uint32_t            sin6_scope_id; /* IPv6 scope-id */
};

/**
 * @ingroup in
 * @brief IP option tos
 */
#define DP_IP_TOS          1

/**
 * @ingroup in
 * @brief IP option ttl
 */
#define DP_IP_TTL          2

/**
 * @ingroup in
 * @brief IP option pktinfo
 */
#define DP_IP_PKTINFO      8

// 以下选项暂不支持
#define DP_IP_HDRINCL      3
#define DP_IP_OPTIONS      4
#define DP_IP_ROUTER_ALERT 5
#define DP_IP_RECVOPTS     6
#define DP_IP_RETOPTS      7
#define DP_IP_PKTOPTIONS   9
#define DP_IP_MTU_DISCOVER 10
#define DP_IP_RECVERR      11
#define DP_IP_RECVTTL      12
#define DP_IP_RECVTOS      13
#define DP_IP_MTU          14
#define DP_IP_FREEBIND     15
#define DP_IP_IPSEC_POLICY 16
#define DP_IP_XFRM_POLICY  17
#define DP_IP_PASSSEC      18
#define DP_IP_TRANSPARENT  19

#ifdef __cplusplus
}
#endif
#endif
