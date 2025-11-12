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

#include <securec.h>

#include "inet_sk.h"

#include "dp_inet.h"

#include "tbm.h"

#include "utils_base.h"

#define InetSk(sk) SOCK_Sk2Obj((sk))

#define TTL_MAX_VAL 255
#define TTL_OPT_LEN 4

#define TOS_OPT_LEN 4

#define INET_ECN_MASK 3

static SOCK_ProtoOps_t g_protoOps[] = {
    {
        .type     = DP_SOCK_DGRAM,
        .protocol = 0,
        .create   = NULL,
    },
    {
        .type     = DP_SOCK_STREAM,
        .protocol = 0,
        .create   = NULL,
    },
};

static inline int MatchOps(SOCK_ProtoOps_t* ops, int type, int proto)
{
    if (type != ops->type) {
        return 0;
    }

    if (proto == 0) {
        return 1;
    }

    if (ops->protocol == 0) {
        return 1;
    }

    return ops->protocol == proto ? 1 : 0;
}

static int IsAddrLocal(Sock_t* sk, DP_InAddr_t addr)
{
    int ret = 0;
    if (addr == DP_INADDR_BROADCAST) {
        return ret;
    }

    TBM_RtItem_t* rtItem = TBM_GetRtItem(sk->net, sk->vrfId, addr);
    if (rtItem == NULL) {
        return ret;
    }

    if (rtItem->ifaddr->local == DP_INADDR_LOOPBACK || rtItem->ifaddr->local == addr) {
        ret = 1;
    }
    TBM_PutRtItem(rtItem);
    return ret;
}

static void AddInetOps(const SOCK_ProtoOps_t* ops)
{
    for (int i = 0; i < (int)ARRAY_SIZE(g_protoOps); i++) {
        if (MatchOps(&g_protoOps[i], ops->type, ops->protocol) != 0) {
            ASSERT(ops->create != NULL);
            g_protoOps[i].create = ops->create;
            return;
        }
    }

    ASSERT(0);
}

static int GetInetCreateHook(int type, int proto, SOCK_CreateSkFn_t* hook)
{
    SOCK_ProtoOps_t* ops = g_protoOps;

    // 当前不支持type中带有flags
    if (((uint32_t)type & ~DP_SOCK_TYPE_MASK) != 0) {
        return -EINVAL;
    }

    if (type != DP_SOCK_STREAM && type != DP_SOCK_DGRAM) {
        return -EPROTOTYPE;
    }

    if (proto < 0 || proto >= DP_IPPROTO_MAX) {
        return -EINVAL;
    }

    for (int i = 0; i < (int)ARRAY_SIZE(g_protoOps); i++, ops++) {
        if (MatchOps(ops, type, proto) != 0) {
            if (ops->create == NULL) {
                break;
            }
            *hook = ops->create;
            return 0;
        }
    }

    return -EPROTONOSUPPORT;
}

int INET_Init(int slave)
{
    SOCK_FamilyOps_t ops = {
        .family = DP_AF_INET,
        .add    = AddInetOps,
        .lookup = GetInetCreateHook,
    };

    SOCK_AddFamilyOps(&ops);

    (void)slave;

    return 0;
}

void INET_Deinit(int slave)
{
    (void)slave;

    for (int i = 0; i < (int)ARRAY_SIZE(g_protoOps); i++) {
        g_protoOps[i].create = NULL;
        g_protoOps[i].protocol = 0;
    }
}

InetSk_t* INET_MatchHashinfo(Hash_t* tbl, uint32_t hashVal, INET_Hashinfo_t* hi)
{
    HASH_Node_t* node = NULL;

    HASH_FOREACH(tbl, hashVal, node)
    {
        InetSk_t* inetSk = (InetSk_t*)node;

        if (INET_HashinfoEqual(hi, &inetSk->hashinfo) != 0) {
            return inetSk;
        }
    }

    return NULL;
}

int INET_InsertHashItem(Hash_t* tbl, HASH_Node_t* hashNode, uint32_t hashVal)
{
    HASH_INSERT(tbl, hashVal, hashNode);

    return 0;
}

void INET_RemoveHashItem(Hash_t* tbl, HASH_Node_t* hashNode, uint32_t hashVal)
{
    HASH_REMOVE(tbl, hashVal, hashNode);
}

int INET_Setsockopt(InetSk_t* inetSk, int optName, const void* optVal, DP_Socklen_t optLen)
{
    switch (optName) {
        case DP_IP_TTL:
            if (optVal == NULL) {
                return -EFAULT;
            } else if (optLen != TTL_OPT_LEN) {
                return -EINVAL;
            } else {
                int val = *(int*)optVal;
                if (val <= 0 || val > TTL_MAX_VAL) {
                    return -EINVAL;
                }
                inetSk->ttl         = (uint8_t)val;
                inetSk->options.ttl = 1;
            }
            break;
        case DP_IP_TOS:
            if (optVal == NULL) {
                return -EFAULT;
            } else if (optLen != TOS_OPT_LEN) {
                return -EINVAL;
            } else {
                unsigned int val = *(unsigned int*)optVal;
                if (inetSk->hashinfo.protocol == DP_IPPROTO_TCP) {
                    val &= ~INET_ECN_MASK;
                    val |= inetSk->tos & INET_ECN_MASK;
                }
                inetSk->tos         = (uint8_t)val;
                inetSk->options.tos = 1;
            }
            break;
        case DP_IP_PKTINFO:
            if (optVal == NULL) {
                return -EFAULT;
            }
            if ((int)optLen < (int)sizeof(DP_Socklen_t)) {
                return -EINVAL;
            }
            inetSk->options.pktInfo = *(int *)optVal == 0 ? 0 : 1;
            break;
        default:
            return -ENOPROTOOPT;
    }

    return 0;
}

int INET_Getsockopt(InetSk_t* inetSk, int optName, void* optVal, DP_Socklen_t* optLen)
{
    if ((optVal == NULL) || (optLen == NULL)) {
        return -EFAULT;
    }
    if ((int)*optLen < (int)sizeof(int)) {
        return -EINVAL;
    }
    switch (optName) {
        case DP_IP_TTL:
            *optLen       = sizeof(int);
            *(int*)optVal = inetSk->ttl == 0 ? DP_IPHDR_TTL : inetSk->ttl;
            break;
        case DP_IP_TOS:
            *optLen       = sizeof(int);
            *(int*)optVal = inetSk->tos;
            break;
        case DP_IP_PKTINFO:
            *optLen       = sizeof(int);
            *(int*)optVal = inetSk->options.pktInfo;
            break;
        default:
            return -ENOPROTOOPT;
    }

    return 0;
}

int INET_Bind(
    Sock_t* sk, InetSk_t* inetSk, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen, INET_Hashinfo_t* hi)
{
    struct DP_SockaddrIn* addrIn = (struct DP_SockaddrIn*)addr;

    int ret = INET_CheckAddr(addr, addrlen);
    if (ret != 0) {
        return ret;
    }

    if (SOCK_IS_CONNECTED(sk)) {
        return -EISCONN;
    }

    if (addrIn->sin_addr.s_addr != DP_INADDR_ANY && !IsAddrLocal(sk, addrIn->sin_addr.s_addr)) {
        return -EADDRNOTAVAIL;
    }

    *hi       = inetSk->hashinfo;
    hi->laddr = addrIn->sin_addr.s_addr;
    hi->lport = addrIn->sin_port;

    return 0;
}

int INET_Connect(Sock_t* sk, InetSk_t* inetSk, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen)
{
    struct DP_SockaddrIn* addrIn = (struct DP_SockaddrIn*)addr;

    int ret = INET_CheckAddr(addr, addrlen);
    if (ret != 0) {
        return ret;
    }

    inetSk->flow.dst = addrIn->sin_addr.s_addr;

    return INET_InitFlowBySk(sk, inetSk, &inetSk->flow);
}

int INET_GetAddr(InetSk_t* inetSk, struct DP_Sockaddr* addr, DP_Socklen_t* addrlen, int peer)
{
    struct DP_SockaddrIn* addrIn = (struct DP_SockaddrIn*)addr;

    struct DP_SockaddrIn tempAddrIn = {.sin_family = DP_AF_INET};
    if (peer == 1) {
        tempAddrIn.sin_port        = inetSk->hashinfo.pport;
        tempAddrIn.sin_addr.s_addr = inetSk->hashinfo.paddr;
    } else {
        tempAddrIn.sin_port        = inetSk->hashinfo.lport;
        tempAddrIn.sin_addr.s_addr = inetSk->hashinfo.laddr;
    }
    uint32_t cpyLen = UTILS_MIN(*addrlen, (uint32_t)sizeof(struct DP_SockaddrIn));
    (void)memcpy_s(addrIn, cpyLen, &tempAddrIn, cpyLen);        // 按入参长度截断赋值addrIn
    *addrlen                = sizeof(*addrIn);

    return 0;
}

static void InitBroadcastFlow(Netdev_t* dev, INET_FlowInfo_t* flow, int protocol)
{
    flow->src      = DP_GetFirstIpv4Addr(dev);
    flow->flags    = INET_FLOW_FLAGS_NO_ROUTE;
    flow->flowType = PBUF_PKTTYPE_BROADCAST;
    flow->protocol = (uint8_t)protocol;
    flow->tos      = DP_IPHDR_TOS;
    flow->ttl      = DP_IPHDR_TTL;
    flow->mtu      = dev->mtu;
    flow->dev      = dev;
    flow->nd       = NULL;
}

static int InetInitFlowBySk(Sock_t* sk, InetSk_t* inetSk, INET_FlowInfo_t* flow)
{
    flow->rt = TBM_GetRtItem(sk->net, sk->vrfId, flow->dst);
    if (flow->rt == NULL) {
        if (sk->bindDev == 0) {
            DP_LOG_DBG("Can't find the rtItem");
            return -ENETUNREACH;
        }

        Netdev_t* dev  = sk->dev;
        flow->src      = DP_GetFirstIpv4Addr(dev);
        flow->flowType = flow->dst == DP_INADDR_BROADCAST ? PBUF_PKTTYPE_BROADCAST : PBUF_PKTTYPE_HOST;
        flow->protocol = inetSk->hashinfo.protocol;
        flow->tos      = inetSk->tos == 0 ? DP_IPHDR_TOS : inetSk->tos;
        flow->ttl      = inetSk->ttl == 0 ? DP_IPHDR_TTL : inetSk->ttl;
        flow->mtu      = dev->mtu;
        flow->nd       = NULL;
        return 0;
    }

    flow->src      = flow->rt->ifaddr->local;
    flow->flags    = 0;
    flow->protocol = inetSk->hashinfo.protocol;
    flow->tos      = inetSk->tos == 0 ? DP_IPHDR_TOS : inetSk->tos;
    flow->ttl      = inetSk->ttl == 0 ? DP_IPHDR_TTL : inetSk->ttl;
    flow->mtu      = (uint16_t)INET_GetMtu(flow->rt, inetSk->mtu);

    if (TBM_IsBroadcastRt(flow->rt) != 0) {
        flow->flowType = PBUF_PKTTYPE_BROADCAST;
        flow->nd = NULL;
    } else {
        flow->flowType = PBUF_PKTTYPE_HOST;
        flow->nd = TBM_GetNdItem(sk->net, sk->vrfId,
                                 TBM_IsDirectRt(flow->rt) != 0 ? flow->dst : flow->rt->nxtHop);
    }

    return 0;
}

static int InetInitFlowByDev(Netdev_t* dev, INET_FlowInfo_t* flow, int protocol)
{
    if (flow->dst == DP_INADDR_BROADCAST) {
        InitBroadcastFlow(dev, flow, protocol);
        return 0;
    }

    flow->rt = TBM_GetRtItem(dev->net, dev->vrfId, flow->dst);
    if (flow->rt == NULL) {
        return -ENETUNREACH;
    }

    flow->src      = flow->rt->ifaddr->local;
    flow->flags    = 0;
    flow->protocol = (uint8_t)protocol;
    flow->tos      = DP_IPHDR_TOS;
    flow->ttl      = DP_IPHDR_TTL;
    flow->mtu      = (uint16_t)INET_GetMtu(flow->rt, 0);

    if (TBM_IsBroadcastRt(flow->rt) != 0) {
        flow->flowType = PBUF_PKTTYPE_BROADCAST;
        flow->nd = NULL;
    } else {
        flow->flowType = PBUF_PKTTYPE_HOST;
        flow->nd = TBM_GetNdItem(dev->net, dev->vrfId,
                                 TBM_IsDirectRt(flow->rt) != 0 ? flow->dst : flow->rt->nxtHop);
    }

    return 0;
}

int INET_InitFlowByDev(Netdev_t* dev, INET_FlowInfo_t* flow, int protocol)
{
    NS_RefNet(dev->net);

    int ret = InetInitFlowByDev(dev, flow, protocol);

    NS_DerefNet(dev->net);

    return ret;
}

int INET_InitFlowBySk(Sock_t* sk, InetSk_t* inetSk, INET_FlowInfo_t* flow)
{
    NS_RefNet(sk->net);

    int ret = InetInitFlowBySk(sk, inetSk, flow);

    NS_DerefNet(sk->net);

    return ret;
}

void INET_DeinitFlow(INET_FlowInfo_t* flow)
{
    if (flow->rt != NULL) {
        TBM_PutRtItem(flow->rt);
    }

    if (flow->nd != NULL) {
        TBM_PutNdItem(flow->nd);
    }

    flow->rt = NULL;
    flow->nd = NULL;
}

int INET_UpdateFlow(INET_FlowInfo_t* flow)
{
    if ((flow->flags & INET_FLOW_FLAGS_NO_ROUTE) != 0) {
        return 0;
    }

    if (flow->rt == NULL) {
        return 0;
    }

    if (flow->rt->valid != 0 && flow->nd != NULL && flow->nd->valid != 0) {
        return 0;
    }

    Netdev_t* dev = flow->rt->ifaddr->dev;
    DP_InAddr_t src = flow->src;

    TBM_PutRtItem(flow->rt);
    if (flow->nd != NULL) {
        TBM_PutNdItem(flow->nd);
    }

    int ret = InetInitFlowByDev(dev, flow, flow->protocol);
    if (ret != 0) {
        return ret;
    }

    if (flow->src != src) {
        INET_DeinitFlow(flow);
        return -EADDRNOTAVAIL;
    }

    return 0;
}

int INET_GetMtu(TBM_RtItem_t* rt, uint16_t mtuUser)
{
    if (mtuUser == 0) {
        return rt->ifaddr->dev->mtu;
    }

    return rt->ifaddr->dev->mtu > mtuUser ? mtuUser : rt->ifaddr->dev->mtu;
}
