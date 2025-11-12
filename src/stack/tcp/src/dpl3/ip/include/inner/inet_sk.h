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

#ifndef INET_SK_H
#define INET_SK_H

#include <netinet/in.h>

#include "sock.h"
#include "netdev.h"
#include "dp_ip.h"
#include "tbm.h"
#include "utils_cksum.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOCK_INET_HEADROOM (128)

#define DP_IPPROTO_MAX  IPPROTO_MAX

#define DP_SOCK_TYPE_MASK 0xf
/* INET socket内存结构
| -- Sock_t -- | -- Inet_t -- | -- Obj(TCP/UDP) -- |
*/

typedef struct {
    union {
        struct {
            uint16_t lport;
            uint16_t pport;
        };
        uint32_t port;
    };
    DP_InAddr_t laddr;
    DP_InAddr_t paddr;

    uint8_t protocol;
    uint8_t resv[3];
} INET_Hashinfo_t;

typedef struct INET_FlowInfo {
    DP_InAddr_t src;
    DP_InAddr_t dst;

    uint8_t  flags;
    uint8_t  reserve;
    uint8_t  protocol;
    uint8_t  flowType;
    uint8_t  tos;
    uint8_t  ttl;
    uint16_t mtu;

    // 如果是广播报文，则这个union结构中dev起作用，否则是rtItem起作用
    union {
        TBM_RtItem_t* rt;
        Netdev_t*     dev;
    };

    TBM_NdItem_t* nd;
} INET_FlowInfo_t;

typedef struct {
    HASH_Node_t node;
    HASH_Node_t connectTblNode;

    INET_Hashinfo_t hashinfo;
    INET_FlowInfo_t flow;

    uint8_t  ttl;
    uint8_t  tos;
    uint16_t mtu;

    union {
        struct {
            uint16_t incHdr : 1; // IP_HDRINCL
            uint16_t tos : 1; // IP_TOS
            uint16_t ttl : 1; // IP_TTL
            uint16_t mtu : 1; // IP_MTU
            // rcv option
            uint16_t pktInfo : 1; // IP_PKTINFO
            uint16_t rcvTos : 1; // IP_RCVTOS
            uint16_t rcvTtl : 1; // IP_RCVTTL
        };
        uint16_t options;
    } options;
    uint16_t flowid;
} InetSk_t;

static inline uint8_t INET_HashinfoEqual(INET_Hashinfo_t* l, INET_Hashinfo_t* r)
{
    return (l->protocol == r->protocol) && (l->port == r->port) && (l->laddr == r->laddr) && (l->paddr == r->paddr);
}

static inline int INET_CheckAddrLen(const struct DP_Sockaddr* addr, DP_Socklen_t addrlen)
{
    if (addr == NULL) {
        return -EFAULT;
    }

    if ((int)addrlen < (int)sizeof(struct DP_Sockaddr)) {
        return -EINVAL;
    }

    return 0;
}

static inline int INET_CheckAddr(const struct DP_Sockaddr* addr, DP_Socklen_t addrlen)
{
    int ret = INET_CheckAddrLen(addr, addrlen);
    if (ret != 0) {
        return ret;
    }

    if (addr->sa_family != DP_AF_INET) {
        return -EAFNOSUPPORT;
    }

    return 0;
}

/**
 * @brief 此接口用于填写hashinfo的local地址和port，协议使用此接口获取到hashinfo，然后进行继续的检查和处理
 *
 */
int INET_Bind(
    Sock_t* sk, InetSk_t* inetSk, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen, INET_Hashinfo_t* hi);

typedef struct {
    INET_Hashinfo_t  hi;
    NETDEV_IfAddr_t* outIfAddr;
} INET_ConnectInfo_t;

int INET_Connect(Sock_t* sk, InetSk_t* inetSk, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen);

enum INET_HASH_MATH_RESULT {
    INET_MATCH_NONE,
    INET_MATCH_EXACT, // 5元组匹配
    INET_MATCH_3TUPLE, // 3元组匹配 (laddr + lport + proto)
    INET_MATCH_2TUPLE, // 2元组匹配 (lport + proto)
};

/**
 * @brief
 * 这个接口仅提供精确匹配功能，需要协议自行处理五元组/三元组/二元组匹配关系，如果出现重复的五元组，此接口仅支持查找第一匹配到的socket
 *
 * @param hi
 * @param node
 * @return
 */
InetSk_t* INET_MatchHashinfo(Hash_t* tbl, uint32_t hashVal, INET_Hashinfo_t* hi);

/**
 * @brief 如果设置了reuse，则相同五元组允许重复插入
 *
 * @param sk
 * @param hashNode
 * @param hashVal
 * @return
 */
int INET_InsertHashItem(Hash_t* tbl, HASH_Node_t* hashNode, uint32_t hashVal);

/**
 * @brief 移除hash节点
 *
 * @param tbl
 * @param hashNode
 * @param hashVal
 * @return
 */
void INET_RemoveHashItem(Hash_t* tbl, HASH_Node_t* hashNode, uint32_t hashVal);

/**
 * @brief IP Level选项设置
 *
 * @param inetSk
 * @param optName
 * @param optVal
 * @param optLen
 * @return
 */
int INET_Setsockopt(InetSk_t* inetSk, int optName, const void* optVal, DP_Socklen_t optLen);

int INET_Getsockopt(InetSk_t* inetSk, int optName, void* optVal, DP_Socklen_t* optLen);

typedef struct {
    DP_InAddr_t src;
    DP_InAddr_t dst;
    uint8_t       zero;
    uint8_t       protocol;
    uint16_t      len;
} DP_PACKED INET_PseudoHdr_t;

static inline uint32_t INET_CalcPseudoCksum(INET_Hashinfo_t* hi)
{
    INET_PseudoHdr_t phdr;

    phdr.src      = hi->laddr;
    phdr.dst      = hi->paddr;
    phdr.zero     = 0;
    phdr.protocol = hi->protocol;
    phdr.len      = 0;

    return UTILS_Cksum(0, (uint8_t*)&phdr, sizeof(phdr));
}

int INET_GetAddr(InetSk_t* inetSk, struct DP_Sockaddr* addr, DP_Socklen_t* addrlen, int peer);

#define INET_FLOW_FLAGS_BROADCAST 0x1
#define INET_FLOW_FLAGS_NDCACHED  0x2
#define INET_FLOW_FLAGS_NO_ROUTE  0x4

int INET_InitFlowByDev(Netdev_t* dev, INET_FlowInfo_t* flow, int protocol);

int INET_InitFlowBySk(Sock_t* sk, InetSk_t* inetSk, INET_FlowInfo_t* flow);

int INET_ReinitFlow(DP_InAddr_t dst, NS_Net_t* net, int vrfId, int outIf, INET_FlowInfo_t* flow);

int INET_UpdateFlow(INET_FlowInfo_t* flow);

void INET_DeinitFlow(INET_FlowInfo_t* flow);

int INET_GetMtu(TBM_RtItem_t* rt, uint16_t mtuUser);

static inline Netdev_t* INET_GetDevByFlow(const INET_FlowInfo_t* flow)
{
    return (flow->flags & INET_FLOW_FLAGS_NO_ROUTE) == 0 ? flow->rt->ifaddr->dev : flow->dev;
}

// 计算报文接收校验和
static inline uint16_t INET_CalcCksum(DP_Pbuf_t* pbuf)
{
    uint32_t         cksum;
    INET_PseudoHdr_t phdr;
    DP_IpHdr_t*      ipHdr = (DP_IpHdr_t*)PBUF_GET_L3_HDR(pbuf);

    phdr.src      = ipHdr->src;
    phdr.dst      = ipHdr->dst;
    phdr.zero     = 0;
    phdr.protocol = ipHdr->type;
    phdr.len      = (uint16_t)UTILS_HTONS(PBUF_GET_PKT_LEN(pbuf));

    cksum = UTILS_Cksum(0, (uint8_t*)&phdr, sizeof(phdr));
    cksum += PBUF_CalcCksum(pbuf);

    return UTILS_CksumSwap(cksum);
}

#ifdef __cplusplus
}
#endif
#endif
