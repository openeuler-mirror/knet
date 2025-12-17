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

#include "dp_in_api.h"
#include "dp_inet.h"

#include "tbm.h"
#ifdef DPFWK_NF
#include "nf.h"
#endif
#include "utils_base.h"
#include "utils_log.h"
#include "utils_statistic.h"

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
    {
        .type     = DP_SOCK_RAW,
        .protocol = 0,
        .create   = NULL,
    },
};
static RawInputFn_t g_inetRawInput = NULL;

void AddRawFn(RawInputFn_t op)
{
    if (op == NULL || g_inetRawInput != NULL) {
        return;
    }
    g_inetRawInput = op;
}

void CallRawInput(Pbuf_t* pbuf, DP_IpHdr_t* ipHdr)
{
    if (g_inetRawInput != NULL) {
        g_inetRawInput(pbuf, ipHdr);
    }
}

static int IsAddrLocal(Sock_t* sk, DP_InAddr_t addr, int32_t* ifIndex)
{
    int ret = 0;
    if (addr == DP_INADDR_BROADCAST) {
        return ret;
    }

    TBM_RtItem_t* rtItem = TBM_GetRtItem(sk->net, sk->vrfId, addr);
    if (rtItem == NULL) {
        return ret;
    }

    if (rtItem->ifaddr->local == DP_INADDR_LOOPBACK || TBM_IsVirtualDevRt(rtItem) || rtItem->ifaddr->local == addr) {
        ret = 1;
        *ifIndex = rtItem->ifaddr->dev->ifindex;
    }
    TBM_PutRtItem(rtItem);
    return ret;
}

static void AddInetOps(const SOCK_ProtoOps_t* ops)
{
    for (int i = 0; i < (int)DP_ARRAY_SIZE(g_protoOps); i++) {
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
        DP_ADD_ABN_STAT(DP_SOCKET_TYPE_WITH_FLAGS);
        return -EINVAL;
    }

    if (type != DP_SOCK_STREAM && type != DP_SOCK_DGRAM && type != DP_SOCK_RAW) {
        DP_ADD_ABN_STAT(DP_SOCKET_TYPE_ERR);
        return -EPROTOTYPE;
    }

    if (proto < 0 || proto >= DP_IPPROTO_MAX) {
        DP_ADD_ABN_STAT(DP_SOCKET_PROTO_INVAL);
        return -EINVAL;
    }

    for (int i = 0; i < (int)DP_ARRAY_SIZE(g_protoOps); i++, ops++) {
        if (MatchOps(ops, type, proto) != 0) {
            if (ops->create == NULL) {
                break;
            }
            *hook = ops->create;
            return 0;
        }
    }
    DP_ADD_ABN_STAT(DP_SOCKET_NOSUPP);
    return -EPROTONOSUPPORT;
}

INET_Handler g_icmpErrMsg = { 0 };

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

    for (int i = 0; i < (int)DP_ARRAY_SIZE(g_protoOps); i++) {
        g_protoOps[i].create = NULL;
        g_protoOps[i].protocol = 0;
    }
    (void)memset_s(&g_icmpErrMsg, sizeof(g_icmpErrMsg), 0, sizeof(g_icmpErrMsg));
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

static int InetSetOptIpTtl(InetSk_t* inetSk, const void* optVal, DP_Socklen_t optLen)
{
    if (optVal == NULL) {
        return -EFAULT;
    }
    if (optLen != TTL_OPT_LEN) {
        return -EINVAL;
    }
    int val = *(int*)optVal;
    if (val <= 0 || val > TTL_MAX_VAL) {
        return -EINVAL;
    }
    inetSk->ttl         = (uint8_t)val;
    inetSk->options.ttl = 1;
    return 0;
}

static int InetSetOptIpTos(InetSk_t* inetSk, const void* optVal, DP_Socklen_t optLen)
{
    if (optVal == NULL) {
        return -EFAULT;
    }
    if (optLen != TOS_OPT_LEN) {
        return -EINVAL;
    }
    unsigned int val = *(unsigned int*)optVal;
    if (inetSk->hashinfo.protocol == DP_IPPROTO_TCP) {
        val &= ~INET_ECN_MASK;
        val |= inetSk->tos & INET_ECN_MASK;
    }
    inetSk->tos         = (uint8_t)val;
    inetSk->options.tos = 1;
    return 0;
}

static int InetSetOptIpPktInfo(InetSk_t* inetSk, const void* optVal, DP_Socklen_t optLen)
{
    if (optVal == NULL) {
        return -EFAULT;
    }
    if ((int)optLen < (int)sizeof(DP_Socklen_t)) {
        return -EINVAL;
    }
    inetSk->options.pktInfo = *(int *)optVal == 0 ? 0 : 1;
    return 0;
}

static int InetSetOptIpRecvErr(InetSk_t* inetSk, const void* optVal, DP_Socklen_t optLen)
{
    if (optVal == NULL) {
        return -EFAULT;
    }
    if ((int)optLen < (int)sizeof(DP_Socklen_t)) {
        return -EINVAL;
    }
    inetSk->options.rcvErr = *(int *)optVal == 0 ? 0 : 1;
    return 0;
}

#ifdef DPFWK_NF
static int InetSetOptIpTableReplace(InetSk_t* inetSk, const void* optVal, DP_Socklen_t optLen)
{
    return DP_IptSetsockopt(inetSk, DP_SOL_IP, DP_IPT_SO_SET_REPLACE, optVal, optLen);
}
#endif

#ifdef DPFWK_NF
static int InetSetOptIpTableAddCounter(InetSk_t* inetSk, const void* optVal, DP_Socklen_t optLen)
{
    return DP_IptSetsockopt(inetSk, DP_SOL_IP, DP_IPT_SO_SET_ADD_COUNTERS, optVal, optLen);
}
#endif

typedef struct {
    int optname;
    int (*set)(InetSk_t* inetSk, const void* optVal, DP_Socklen_t optLen);
} SockSetOptOps_t;

static SockSetOptOps_t g_inetSetOptOps[] = {
    {DP_IP_TTL, InetSetOptIpTtl},
    {DP_IP_TOS, InetSetOptIpTos},
    {DP_IP_PKTINFO, InetSetOptIpPktInfo},
    {DP_IP_RECVERR, InetSetOptIpRecvErr},
#ifdef DPFWK_NF
    {DP_IPT_SO_SET_REPLACE, InetSetOptIpTableReplace},
    {DP_IPT_SO_SET_ADD_COUNTERS, InetSetOptIpTableAddCounter},
#endif
};

int INET_Setsockopt(InetSk_t* inetSk, int optName, const void* optVal, DP_Socklen_t optLen)
{
    for (size_t i = 0; i < DP_ARRAY_SIZE(g_inetSetOptOps); i++) {
        if (g_inetSetOptOps[i].optname != optName) {
            continue;
        }
        if (g_inetSetOptOps[i].set == NULL) {
            return -ENOPROTOOPT;
        }
        return g_inetSetOptOps[i].set(inetSk, optVal, optLen);
    }
    DP_LOG_DBG("Inet setOpt failed, invalid optname, optname = %d.", optName);
    return -ENOPROTOOPT;
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
        case DP_IP_MTU:
            *optLen       = sizeof(int);
            *(int*)optVal = (int)inetSk->mtu; // 当前没有set场景，恒为0
            break;
#ifdef DPFWK_NF
        case DP_IPT_SO_GET_INFO:
        case DP_IPT_SO_GET_ENTRIES:
        case DP_IPT_SO_GET_REVISION_MATCH:
        case DP_IPT_SO_GET_REVISION_TARGET:
            return DP_IptGetsockopt(inetSk, DP_SOL_IP, optName, optVal, optLen);
#endif
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
        DP_ADD_ABN_STAT(DP_INET_BIND_ADDR_INVAL);
        return ret;
    }

    if (SOCK_IS_CONNECTING(sk) || SOCK_IS_CONNECTED(sk)) {
        DP_ADD_ABN_STAT(DP_INET_BIND_CONNECTED);
        return -EISCONN;
    }

    int32_t ifIndex = -1;
    if (addrIn->sin_addr.s_addr != DP_INADDR_ANY && !IsAddrLocal(sk, addrIn->sin_addr.s_addr, &ifIndex)) {
        DP_ADD_ABN_STAT(DP_INET_BIND_ADDR_ERR);
        return -EADDRNOTAVAIL;
    }

    *hi       = inetSk->hashinfo;
    hi->laddr = addrIn->sin_addr.s_addr;
    hi->lport = addrIn->sin_port;
    hi->lportMask = PORT_MASK_DEFAULT;
    hi->ifIndex = ifIndex;
    hi->wid = (int8_t)sk->wid;
    hi->vpnid = sk->vpnid;

    return 0;
}

int INET_Connect(Sock_t* sk, InetSk_t* inetSk, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen)
{
    struct DP_SockaddrIn* addrIn = (struct DP_SockaddrIn*)addr;

    int ret = INET_CheckAddr(addr, addrlen);
    if (ret != 0) {
        DP_ADD_ABN_STAT(DP_INET_CONN_ADDR_INVAL);
        return ret;
    }

    // UDP非首次connect 且对端相同
    if (SOCK_IS_CONNECTED(sk) && inetSk->flow.dst == addrIn->sin_addr.s_addr) {
        return INET_UpdateFlow(&inetSk->flow);
    }

    if (SOCK_IS_CONNECTED(sk)) {    // UDP非首次connect 且对端不同，将上次取得的flow信息清空
        INET_DeinitFlow(&inetSk->flow);
    } else {                        // 首次connect
        inetSk->flow.src = inetSk->hashinfo.laddr;
        inetSk->flow.dst = addrIn->sin_addr.s_addr;
    }

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

void INET_ShowInfo(InetSk_t* inetSk)
{
    uint32_t offset = 0;
    char output[LEN_INFO] = {0};

    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO, "\r\n-------- InetSkInfo --------\n");
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "ttl = %u\n", inetSk->ttl);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "tos = %u\n", inetSk->tos);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "mtu = %u\n", inetSk->mtu);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "IsIncHdr = %u\n", inetSk->options.incHdr);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "IsTos = %u\n", inetSk->options.tos);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "IsTtl = %u\n", inetSk->options.ttl);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "IsMtu = %u\n", inetSk->options.mtu);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "IsPktInfo = %u\n", inetSk->options.pktInfo);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "IsRcvTos = %u\n", inetSk->options.rcvTos);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "IsRcvTtl = %u\n", inetSk->options.rcvTtl);

    DEBUG_SHOW(0, output, offset);
}

void INET_GetDetails(InetSk_t* inetSk, DP_InetDetails_t* details)
{
    details->ttl = inetSk->ttl;
    details->tos = inetSk->tos;
    details->mtu = inetSk->mtu;
    details->options.options = inetSk->options.options;
}

static void InitBroadcastFlow(Netdev_t* dev, INET_FlowInfo_t* flow, int protocol)
{
    if (dev->devType != DP_NETDEV_TYPE_VI) {
        flow->src      = DP_GetFirstIpv4Addr(dev);
    }
    flow->flags    = INET_FLOW_FLAGS_NO_ROUTE;
    flow->flowType = PBUF_PKTTYPE_BROADCAST;
    flow->protocol = (uint8_t)protocol;
    flow->tos      = DP_IPHDR_TOS;
    flow->ttl      = DP_IPHDR_TTL;
    flow->mtu      = dev->mtu;
    flow->dev      = dev;
    flow->nd       = NULL;
    (void)NETDEV_RefDev(dev);
}

static int InetInitFlowBySk(Sock_t* sk, InetSk_t* inetSk, INET_FlowInfo_t* flow)
{
    flow->rt = TBM_GetRtItem(sk->net, sk->vrfId, flow->dst);
    if (flow->rt == NULL) {
        if (sk->bindDev == 0) {
            DP_LOG_DBG("Can't find the rtItem");
            DP_ADD_ABN_STAT(DP_INIT_FLOW_RT_FAILED);
            return -ENETUNREACH;
        }

        Netdev_t* dev  = sk->dev;
        if (dev->devType != DP_NETDEV_TYPE_VI) {
            flow->src      = DP_GetFirstIpv4Addr(dev);
        }
        flow->flowType = flow->dst == DP_INADDR_BROADCAST ? PBUF_PKTTYPE_BROADCAST : PBUF_PKTTYPE_HOST;
        flow->protocol = inetSk->hashinfo.protocol;
        flow->tos      = inetSk->tos == 0 ? DP_IPHDR_TOS : inetSk->tos;
        flow->ttl      = inetSk->ttl == 0 ? DP_IPHDR_TTL : inetSk->ttl;
        flow->mtu      = dev->mtu;
        flow->nd       = NULL;
        return 0;
    }

    if (TBM_IsVirtualDevRt(flow->rt)) {
        flow->flags |= INET_FLOW_FLAGS_NO_ND;
    } else {
        flow->src      = flow->rt->ifaddr->local;
        flow->flags    = 0;
    }
    flow->protocol = inetSk->hashinfo.protocol;
    flow->tos      = inetSk->tos == 0 ? DP_IPHDR_TOS : inetSk->tos;
    flow->ttl      = inetSk->ttl == 0 ? DP_IPHDR_TTL : inetSk->ttl;
    flow->mtu      = (uint16_t)INET_GetMtu(flow->rt, inetSk->mtu);

    if (TBM_IsBroadcastRt(flow->rt) != 0) {
        flow->flowType = PBUF_PKTTYPE_BROADCAST;
        flow->nd = NULL;
    } else {
        flow->flowType = PBUF_PKTTYPE_HOST;
        TBM_IpAddr_t addr = {.ipv4 = TBM_IsDirectRt(flow->rt) != 0 ? flow->dst : flow->rt->nxtHop};
        flow->nd = TBM_GetNdItem(sk->net, sk->vrfId, addr);
    }

    return 0;
}

static int InetInitFlowByDev(Netdev_t* dev, INET_FlowInfo_t* flow, int protocol)
{
    if (flow->dst == DP_INADDR_BROADCAST) {
        InitBroadcastFlow(dev, flow, protocol);
        return 0;
    }

    TBM_RtItem_t* tempRt = TBM_GetRtItem(dev->net, dev->vrfId, flow->dst);
    if (tempRt == NULL) {
        return -ENETUNREACH;
    }
    flow->rt = tempRt;

    if (TBM_IsVirtualDevRt(flow->rt)) {
        flow->flags = INET_FLOW_FLAGS_NO_ND;
    } else {
        flow->src   = flow->rt->ifaddr->local;
        flow->flags = 0;
    }
    flow->protocol = (uint8_t)protocol;
    flow->tos      = DP_IPHDR_TOS;
    flow->ttl      = DP_IPHDR_TTL;
    flow->mtu      = (uint16_t)INET_GetMtu(flow->rt, 0);

    if (TBM_IsBroadcastRt(flow->rt) != 0) {
        flow->flowType = PBUF_PKTTYPE_BROADCAST;
        flow->nd = NULL;
    } else {
        flow->flowType = PBUF_PKTTYPE_HOST;
        TBM_IpAddr_t addr = {.ipv4 = TBM_IsDirectRt(flow->rt) != 0 ? flow->dst : flow->rt->nxtHop};
        flow->nd = TBM_GetNdItem(dev->net, dev->vrfId, addr);
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
    if ((flow->rt != NULL) && (flow->flags & INET_FLOW_FLAGS_NO_ROUTE) == 0) {
        TBM_PutRtItem(flow->rt);
    }

    if (flow->nd != NULL) {
        TBM_PutNdItem(flow->nd);
    }

    if (flow->dev != NULL && (flow->flags & INET_FLOW_FLAGS_NO_ROUTE) != 0) {
        NETDEV_PutDev(flow->dev);
    }

    flow->rt = NULL;
    flow->nd = NULL;
}

static void INET_FlowRevertItem(INET_FlowInfo_t* flow, TBM_RtItem_t* oldRt, TBM_NdItem_t* oldNd)
{
    TBM_RtItem_t *tempRt = flow->rt;
    TBM_NdItem_t *tempNd = flow->nd;

    flow->rt = oldRt;
    flow->nd = oldNd;

    if (tempRt != NULL) {
        TBM_PutRtItem(tempRt);
    }

    if (tempNd != NULL) {
        TBM_PutNdItem(tempNd);
    }
}

int INET_UpdateFlow(INET_FlowInfo_t* flow)
{
    if ((flow->flags & INET_FLOW_FLAGS_NO_ROUTE) != 0) {
        return 0;
    }

    if (flow->rt == NULL) {
        return 0;
    }

    if (flow->rt->valid != 0) {
        if ((flow->flags & INET_FLOW_FLAGS_NO_ND) != 0) {
            return 0;
        }
        if (flow->nd != NULL && flow->nd->valid != 0) {
            return 0;
        }
    }

    Netdev_t* dev = flow->rt->ifaddr->dev;
    DP_InAddr_t src = flow->src;

    TBM_RtItem_t* oldRt = flow->rt;
    TBM_NdItem_t* oldNd = flow->nd;

    int ret = InetInitFlowByDev(dev, flow, flow->protocol);
    if (ret != 0) {
        DP_ADD_ABN_STAT(DP_UPDATE_FLOW_RT_FAILED);
        return ret;
    }

    /* 更新表项异常，则使用原本的表项 */
    if (flow->src != src) {
        INET_FlowRevertItem(flow, oldRt, oldNd);
        DP_ADD_ABN_STAT(DP_UPDATE_FLOW_WRONG_ADDR);
        return -EADDRNOTAVAIL;
    }

    /* 更新无异常后，将原本的表项释放 */
    if (oldRt != NULL) {
        TBM_PutRtItem(oldRt);
    }

    if (oldNd != NULL) {
        TBM_PutNdItem(oldNd);
    }
    return 0;
}
