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
#include "ip.h"

#include <securec.h>

#include "dp_ip.h"
#include "dp_icmp.h"
#include "dp_ethernet.h"

#include "ip_reass.h"
#include "inet_sk.h"
#include "ip_out.h"

#include "utils_cksum.h"
#include "utils_statistic.h"
#include "utils_log.h"

#include "shm.h"
#include "pbuf.h"
#include "tbm.h"
#include "netdev.h"
#include "pmgr.h"
#ifdef DPFWK_NF
#include "nf.h"
#endif

IpId_t* g_ipIdTbl;

static inline uint8_t GetDstEntry(uint8_t hdrType)
{
    switch (hdrType) {
        case DP_IPHDR_TYPE_TCP:
            return PMGR_ENTRY_TCP_IN;
        case DP_IPHDR_TYPE_UDP:
            return PMGR_ENTRY_UDP_IN;
        default:
            return PMGR_ENTRY_NONE;
    }
}

static inline uint16_t IpVerifyCksum(Pbuf_t* pbuf, DP_IpHdr_t* ipHdr, uint8_t hdrLen)
{
    uint32_t cksum;

    if ((DP_PBUF_GET_OLFLAGS(pbuf) & DP_PBUF_OLFLAGS_RX_IP_CKSUM_GOOD) != 0) {
        return 0;
    }

    if ((DP_PBUF_GET_OLFLAGS(pbuf) & DP_PBUF_OLFLAGS_RX_IP_CKSUM_BAD) != 0) {
        return -1;
    }

    cksum = UTILS_Cksum(0, (uint8_t*)ipHdr, hdrLen);

    return UTILS_CksumSwap(cksum);
}

// icmp报文校验和不需要伪首部（icmpv6需要伪首部）
static int IpIcmpVerifyCksum(Pbuf_t* pbuf)
{
    uint32_t cksum = PBUF_CalcCksumAcc(pbuf);

    return UTILS_CksumSwap(cksum);
}

static inline int IsMcAddr(DP_InAddr_t addr)
{
    return (addr & UTILS_HTONL(0xf0000000)) == UTILS_HTONL(0xe0000000);
}

static inline int CheckPeerAddr(DP_InAddr_t addr)
{
    if (addr == DP_INADDR_ANY) {
        return -1;
    }

    if (addr == DP_INADDR_BROADCAST) {
        return -1;
    }

    if (IsMcAddr(addr)) {
        return -1;
    }

    return 0;
}

static int IpPreCheckProc(Pbuf_t* pbuf, DP_IpHdr_t* ipHdr)
{
    uint16_t        firstLen = PBUF_GET_SEG_LEN(pbuf);
    uint16_t        pktLen   = (uint16_t)PBUF_GET_PKT_LEN(pbuf);

    // 不支持首部分片
    if (firstLen < sizeof(DP_IpHdr_t)) {
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_NET_TOO_SHORT);
        return -1;
    }

    if (ipHdr->version != DP_IP_VERSION_IPV4) {
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_NET_BAD_VER);
        return -1;
    }

    uint16_t hdrLen = DP_GET_IP_HDR_LEN(ipHdr);
    if (hdrLen > firstLen || hdrLen < sizeof(DP_IpHdr_t) || pktLen == hdrLen) {
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_NET_BAD_HEAD_LEN);
        return -1;
    }

    uint16_t dataLen = UTILS_NTOHS(ipHdr->totlen);
    // 不支持处理纯IP报文
    if (dataLen <= hdrLen || dataLen > pktLen) {
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_NET_BAD_LEN);
        return -1;
    }

    // 当前只支持处理这几种协议，其他的要直接丢弃
    if (ipHdr->type != DP_IPPROTO_ICMP && ipHdr->type != DP_IPPROTO_UDP &&
        ipHdr->type != DP_IPPROTO_TCP && ipHdr->type != DP_IPPROTO_IP) {
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_NET_NO_PROTO);
        return -1;
    }

    if (dataLen < pktLen) {
        PBUF_CutTailData(pbuf, (uint16_t)(pktLen - dataLen));
    }

    if (CheckPeerAddr(ipHdr->src) != 0) {
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_NET_CHECK_ADDR_FAIL);
        return -1;
    }

    /* 如果偏移量为全1,即:0x1fff,则应为最后一个分片,即MF标志位不应被设置,如果MF被设置,为错误报文 */
    /* 注意:
     *  1. 偏移量为0x1fff肯定是最后一个分片,因为这是最大的偏移量
     *  2. 最后一个分片的偏移量不一定是0x1fff
     */
    uint16_t curOffset = UTILS_NTOHS(ipHdr->off);
    if (!DP_IS_LAST_FRAG(curOffset) && ((uint16_t)(curOffset << 0x3) == 0xfff8)) {
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_IP_BAD_OFFSET);
        return -1;
    }

    /*
     * 根据协议，以下分片报文标志组合是非法的：
     *   1. DF = 1, 而 MF = 1
     *   2. DF = 1, 而 offset != 0
     *   3. DF = 0, 而 offset + 报文长度 > 65535
     * 考虑到协议栈的宽进严出，仅认为以下分片标志位的组合是非法的：
     *   1. DF = 0, 而 offset + 报文长度 > 65535
     * 即，主机可以接收 DF = 1 的分片报文
     */
    curOffset          = DP_IP_FRAG_OFFSET(curOffset);
    if ((uint32_t)curOffset + (uint32_t)dataLen > 65535) { // offset + 报文长度 > 65535 则认为是非法报文，需要丢弃
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_IP_LEN_OVER_LIMIT);
        return -1;
    }

    if (IpVerifyCksum(pbuf, ipHdr, (uint8_t)hdrLen) != 0) {
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_NET_BAD_SUM);
        return -1;
    }

    return 0;
}

void IpFillHdr(Pbuf_t* pbuf, const INET_FlowInfo_t* flow)
{
    uint8_t hdrLen = sizeof(DP_IpHdr_t);

    PBUF_PUT_HEAD(pbuf, sizeof(DP_IpHdr_t));
    DP_IpHdr_t* ipHdr = PBUF_MTOD(pbuf, DP_IpHdr_t*);
    uint16_t    len   = (uint16_t)PBUF_GET_PKT_LEN(pbuf);

    ipHdr->hdrlen  = hdrLen >> 2; // 根据协议，首部长度传输时需要除以4，所以这里右移2位
    ipHdr->version = DP_IP_VERSION_IPV4;
    ipHdr->tos     = flow->tos;
    ipHdr->totlen  = UTILS_HTONS(len);
    if (flow->protocol == DP_IPPROTO_TCP) {
        ipHdr->ipid = IpGetId(DP_PBUF_GET_WID(pbuf));
    } else {
        ipHdr->ipid = IpGetGlobalId();
    }
    ipHdr->off     = 0;
    ipHdr->ttl     = flow->ttl;
    ipHdr->type    = flow->protocol;
    ipHdr->chksum  = 0;
    ipHdr->src     = flow->src;
    ipHdr->dst     = flow->dst;
    ipHdr->chksum  = IpCalcCksum(pbuf, INET_GetDevByFlow(flow), ipHdr, hdrLen);
}

static Pbuf_t* TryReplyIcmpEcho(Pbuf_t* pbuf, DP_IpHdr_t* ip)
{
    Pbuf_t*    ret;
    DP_IcmpHdr_t* src = PBUF_MTOD(pbuf, DP_IcmpHdr_t*);
    DP_IcmpHdr_t* dst;
    uint32_t        cksum;

    PBUF_CUT_HEAD(pbuf, sizeof(DP_IcmpHdr_t));

    ret = PBUF_BuildFromPbuf(pbuf, (uint16_t)PBUF_GET_PKT_LEN(pbuf), IP_RSV_HEADROOM + sizeof(DP_IcmpHdr_t));
    if (ret == NULL) {
        PBUF_Free(pbuf);
        return ret;
    }

    cksum = PBUF_CalcCksumAcc(pbuf);

    PBUF_PUT_HEAD(ret, sizeof(DP_IcmpHdr_t));
    dst = PBUF_MTOD(ret, DP_IcmpHdr_t*);

    dst->type     = DP_ICMP_TYPE_ECHOREPLY;
    dst->code     = src->code;
    dst->cksum    = 0;
    dst->echo.id  = src->echo.id;
    dst->echo.seq = src->echo.seq;

    cksum      = UTILS_Cksum(cksum, (uint8_t*)dst, sizeof(DP_IcmpHdr_t));
    dst->cksum = UTILS_CksumSwap(cksum);

    PBUF_SET_QUE_ID(ret, NETDEV_GetTxQueid(PBUF_GET_DEV(pbuf), PBUF_GET_QUE_ID(pbuf)));
    DP_PBUF_SET_WID(ret, DP_PBUF_GET_WID(pbuf));
    PBUF_SET_PKT_FLAGS(ret, 0);
    PBUF_SET_ENTRY(ret, PMGR_ENTRY_ROUTE_OUT);
    PBUF_SET_FLOW(ret, NULL);
    PBUF_SET_L4_TYPE(ret, DP_IPPROTO_ICMP);
    PBUF_SET_DEV(ret, PBUF_GET_DEV(pbuf));
    DP_PBUF_SET_VPNID(ret, DP_PBUF_GET_VPNID(pbuf));
    PBUF_SET_DST_ADDR4(ret, ip->src);
    INET_TX_CB(ret)->src = ip->dst;

    PBUF_Free(pbuf);
    return ret;
}

static Pbuf_t* IpProcIcmpUnreach(Pbuf_t* pbuf, DP_IcmpHdr_t* icmp)
{
    DP_IpHdr_t* srcIpHdr;

    if (icmp->code != DP_ICMP_PORT_UNREACH) { // 仅处理端口不可达消息
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_ICMP_NOT_PORT_UNREACH);
        PBUF_Free(pbuf);
        return NULL;
    }

    if (PBUF_GET_SEG_LEN(pbuf) <= sizeof(DP_IcmpHdr_t) + sizeof(DP_IpHdr_t)) {
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_ICMP_UNREACH_TOO_SHORT);
        PBUF_Free(pbuf);
        return NULL;
    }

    PBUF_CUT_HEAD(pbuf, sizeof(DP_IcmpHdr_t));
    /* 设置三层偏移，方便udp处理中获取信息
     * | IP首部 | ICMP首部 | IP首部 | UDP首部 | 数据... |
     *                   | 原始报文开始
     */
    PBUF_SET_L3_OFF(pbuf);
    srcIpHdr = PBUF_MTOD(pbuf, DP_IpHdr_t*);

    if (srcIpHdr->type != DP_IPPROTO_UDP) { // 仅支持UDP的端口不可达消息
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_ICMP_UNREACH_TYPE_ERR);
        PBUF_Free(pbuf);
        return NULL;
    }

    if (PBUF_GET_SEG_LEN(pbuf) <= DP_GET_IP_HDR_LEN(srcIpHdr)) {    // 补充IP携带选项场景的长度判断
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_ICMP_UNREACH_TOO_SHORT);
        PBUF_Free(pbuf);
        return NULL;
    }

    PBUF_CUT_HEAD(pbuf, DP_GET_IP_HDR_LEN(srcIpHdr));

    int ret = g_icmpErrMsg.icmpUnreach(pbuf);
    if (ret != 0) {
        DP_LOG_DBG("IP icmpUnreach failed.");
    }

    PBUF_Free(pbuf);
    return NULL;
}

static Pbuf_t* IpTryProcIcmp(Pbuf_t* pbuf, DP_IpHdr_t* ip)
{
    NetdevQue_t* rxQue = NETDEV_GetRxQue(PBUF_GET_DEV(pbuf), PBUF_GET_QUE_ID(pbuf));
    // 长度不满足条件直接丢弃
    if (PBUF_GET_SEG_LEN(pbuf) < sizeof(DP_IcmpHdr_t)) {
        NET_DEV_ADD_RX_ERRS(rxQue, 1);
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_ICMP_PKT_LEN_SHORT);
        PBUF_Free(pbuf);
        return NULL;
    }

    if (IpIcmpVerifyCksum(pbuf) != 0) {
        NET_DEV_ADD_RX_ERRS(rxQue, 1);
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_ICMP_PKT_BAD_SUM);
        PBUF_Free(pbuf);
        return NULL;
    }

    DP_INC_PKT_STAT(pbuf->wid, DP_PKT_ICMP_IN);
    DP_IcmpHdr_t* icmp = PBUF_MTOD(pbuf, DP_IcmpHdr_t*);
    if (icmp->type == DP_ICMP_TYPE_ECHO) {
        return TryReplyIcmpEcho(pbuf, ip);
    }

    if (icmp->type == DP_ICMP_TYPE_DEST_UNREACH) {
        return IpProcIcmpUnreach(pbuf, icmp);
    }

    PBUF_PUT_HEAD(pbuf, DP_GET_IP_HDR_LEN(ip));
    DP_INC_PKT_STAT(pbuf->wid, DP_PKT_ICMP_FORWARD);
    PBUF_SET_ENTRY(pbuf, PMGR_ENTRY_ICMP_IN);
    return pbuf;
}

static int PreProcIpAddr(Pbuf_t* pbuf, DP_IpHdr_t* ipHdr)
{
    NETDEV_IfAddr_t* ifaddr;
    const Netdev_t*  dev = PBUF_GET_DEV(pbuf);
    NetdevQue_t*     rxQue = NETDEV_GetRxQue(dev, PBUF_GET_QUE_ID(pbuf));

    if (IpPreCheckProc(pbuf, ipHdr) != 0) {
        NET_DEV_ADD_RX_ERRS(rxQue, 1);
        return IP_POLICY_DROP;
    }
#ifdef DPFWK_NF
    if (NF_PreRouting(pbuf, DP_PF_INET) != NF_ACCEPT) {
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_NF_PREROUTING_DROP);
        return IP_POLICY_DROP;
    }
#endif
    if (PBUF_GET_PKT_TYPE(pbuf) == PBUF_PKTTYPE_BROADCAST) { // 二层广播报文
        if (ipHdr->dst == DP_INADDR_BROADCAST) {
            DP_INC_PKT_STAT(pbuf->wid, DP_PKT_IP_BCAST_DELIVER);
            NET_DEV_ADD_RX_MULT(rxQue, 1);
            return IP_POLICY_HOST;
        }
        ifaddr = NETDEV_GetBroadcastIfaddr(dev, ipHdr->dst);
        if (ifaddr != NULL) {
            NET_DEV_ADD_RX_MULT(rxQue, 1);
            return IP_POLICY_HOST;
        }
    } else {
        ifaddr = NETDEV_GetLocalIfaddr(dev, ipHdr->dst);
        if (ifaddr != NULL) {
            return IP_POLICY_HOST;
        }
    }

    if (dev->devType == DP_NETDEV_TYPE_VI) {
        return IP_POLICY_HOST;
    }

    DP_INC_PKT_STAT(pbuf->wid, DP_PKT_NET_DEV_TYPE_NOT_MATCH);
    // 不支持转发，直接 DROP
    return IP_POLICY_DROP;
}

static int IsBroadcastAddress(uint32_t ipAddr, NETDEV_IfAddr_t* broadIfAddr)
{
    // 有限广播地址(255.255.255.255)
    if (ipAddr == DP_INADDR_BROADCAST) {
        return 1;
    }

    // netdev设置子网掩码后，也能丢弃子网定向广播地址(子网号.全1)
    return (broadIfAddr != NULL);
}

static Pbuf_t* IpInput(Pbuf_t* pbuf)
{
    DP_INC_PKT_STAT(pbuf->wid, DP_PKT_NET_IN);
    DP_IpHdr_t* ipHdr = PBUF_MTOD(pbuf, DP_IpHdr_t*);
    int policy = PreProcIpAddr(pbuf, ipHdr);
    if (policy < 0) {
        goto drop;
    }

    uint8_t hdrLen = DP_GET_IP_HDR_LEN(ipHdr);

    PBUF_SET_ENTRY(pbuf, GetDstEntry(ipHdr->type));
    PBUF_SET_DST_ADDR4(pbuf, ipHdr->src);
    PBUF_SET_L3_TYPE(pbuf, DP_ETH_P_IP);
    PBUF_SET_L3_OFF(pbuf);

    if (IsFragPkt(ipHdr)) {
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_FRAG_IN);
        pbuf = IpReass(pbuf, ipHdr, hdrLen);
        if (pbuf == NULL) {
            return NULL;
        }
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_REASS_OUT_REASS_PKT);
        ipHdr  = PBUF_MTOD(pbuf, DP_IpHdr_t*);
        hdrLen = DP_GET_IP_HDR_LEN(ipHdr);
        PBUF_SET_PKT_FLAGS_BIT(pbuf, PBUF_PKTFLAGS_FRAGMENTED);
    } else {
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_NON_FRAG_DELIVER);
    }
#ifdef DPFWK_NF
    if (NF_LocalIn(pbuf, DP_PF_INET) != NF_ACCEPT) {
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_NF_LOCALIN_DROP);
        goto drop;
    }
#endif

    CallRawInput(pbuf, ipHdr);

    PBUF_CUT_HEAD(pbuf, hdrLen);

    if (ipHdr->type == DP_IPHDR_TYPE_ICMP) {
        /* 目的地址广播ICMP或子网广播ICMP会被丢弃 */
        NETDEV_IfAddr_t* broadIfAddr = NETDEV_GetBroadcastIfaddr((Netdev_t*)PBUF_GET_DEV(pbuf), ipHdr->dst);
        if (IsBroadcastAddress(ipHdr->dst, broadIfAddr)) {
            DP_INC_PKT_STAT(pbuf->wid, DP_PKT_ICMP_ADDR_NOT_MATCH);
            goto drop;
        }
        return IpTryProcIcmp(pbuf, ipHdr);
    }

    return pbuf;

drop:
    PBUF_Free(pbuf);

    return NULL;
}

static void IpFragSetPbuf(Pbuf_t *cur, Pbuf_t *pbuf, uint8_t hdrLen)
{
    PBUF_PUT_HEAD(cur, hdrLen);
    PBUF_SET_DST_ADDR4(cur, PBUF_GET_DST_ADDR4(pbuf));
    PBUF_SET_FLOW(cur, PBUF_GET_FLOW(pbuf));
    PBUF_SET_DEV(cur, PBUF_GET_DEV(pbuf));
    PBUF_SET_ND(cur, PBUF_GET_ND(pbuf));
    PBUF_SET_L3_TYPE(cur, DP_ETH_P_IP);
    PBUF_SET_L3_OFF(cur);
}

static void IpFragOutput(Pbuf_t* pbuf, uint16_t mtu)
{
    PBUF_Chain_t  chain;
    uint16_t      fragSize;
    DP_IpHdr_t* ipHdr;
    DP_IpHdr_t* fragHdr;
    uint8_t      hdrLen;
    Pbuf_t*  cur;
    Pbuf_t*  nxt;
    uint16_t      offset = 0;
    uint16_t      dataLen;

    ipHdr = PBUF_MTOD(pbuf, DP_IpHdr_t*);

    // 先还原首部
    hdrLen = DP_GET_IP_HDR_LEN(ipHdr);
    PBUF_CUT_HEAD(pbuf, hdrLen);

    // 计算分片长度
    fragSize = (uint16_t)(mtu - hdrLen) & (~(0x7U)); // 0x7: 分片长度必须为 8 的倍数

    PBUF_ChainInit(&chain);

    if (PBUF_ChainWriteFromPbuf(&chain, pbuf, fragSize, PBUF_GET_HEADROOM(pbuf)) != PBUF_GET_PKT_LEN(pbuf)) {
        PBUF_ChainClean(&chain);
        return;
    }

    cur = PBUF_CHAIN_FIRST(&chain);

    while (cur != NULL) {
        nxt = PBUF_CHAIN_NEXT(cur);

        fragHdr = (DP_IpHdr_t*)(PBUF_MTOD(cur, uint8_t*) - hdrLen);

        dataLen = (uint16_t)PBUF_GET_PKT_LEN(cur);

        (void)memcpy_s(fragHdr, hdrLen, ipHdr, hdrLen);

        fragHdr->off    = (offset >> 3) | ((nxt != NULL) ? DP_IP_FRAG_MF : 0); // 3: IP偏移量：offset / 8，并根据是否为最后一片标记MF
        fragHdr->off    = UTILS_HTONS(fragHdr->off);
        fragHdr->totlen = UTILS_HTONS(dataLen + hdrLen);
        fragHdr->chksum = 0;
        fragHdr->chksum = IpCalcCksum(pbuf, PBUF_GET_DEV(pbuf), fragHdr, hdrLen);

        IpFragSetPbuf(cur, pbuf, hdrLen);

        DP_INC_PKT_STAT(cur->wid, DP_PKT_FRAG_OUT);

        Netdev_t *dev = (Netdev_t *)PBUF_GET_DEV(pbuf);
        if (dev->dstEntry == PMGR_ENTRY_BUTT) { // 三层设备
            NETDEV_XmitPbuf(cur);
        } else {
            PBUF_SET_ENTRY(cur, PMGR_ENTRY_ND_OUT);
            PMGR_Dispatch(cur);
        }

        offset += dataLen;
        cur = nxt;
    }
}

// 从AF_INET/AF_INET6使用flow概念进行发包，inet_sk里可以维护一个固定的flow，raw/udp可以使用临时flow发包
// IpOutput主要处理post route过滤及分片处理
// 为了和网络各表项解耦，定义流表，流采集路由、nd、dev等在栈上下文所需的信息， 暂放到inet_sk/inet6_sk中保存，不使用流表
// 内部转发暂时不用流表，通过路由查找实现
static Pbuf_t* IpOutput(Pbuf_t* pbuf)
{
    DP_INC_PKT_STAT(pbuf->wid, DP_PKT_NET_OUT);
    Netdev_t* dev = (Netdev_t*)PBUF_GET_DEV(pbuf);

    PBUF_SET_L3_TYPE(pbuf, DP_ETH_P_IP);
#ifdef DPFWK_NF
    if (NF_PostRouting(pbuf, DP_PF_INET) != NF_ACCEPT) {
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_NF_POSTROUTING_DROP);
        PBUF_Free(pbuf);
        return NULL;
    }
#endif
    if (PBUF_GET_PKT_TYPE(pbuf) == PBUF_PKTTYPE_LOCAL) { // 本机报文
        return pbuf;
    }

    if (PBUF_GET_PKT_LEN(pbuf) > IpTxCb(pbuf)->mtu &&
        ((DP_PBUF_GET_OLFLAGS(pbuf) & DP_PBUF_OLFLAGS_TX_TSO) == 0)) {
        IpFragOutput(pbuf, IpTxCb(pbuf)->mtu);
        PBUF_Free(pbuf);
        return NULL;
    }

    PBUF_SET_L3_OFF(pbuf);

    if (dev->dstEntry == PMGR_ENTRY_BUTT) { // 三层设备
        NETDEV_XmitPbuf(pbuf);
        return NULL;
    }

    PBUF_SET_ENTRY(pbuf, dev->in.ndEntry);

    return pbuf;
}

static void IpRouteSetPbuf(Pbuf_t* pbuf, const INET_FlowInfo_t* flow)
{
    PBUF_SET_PKT_TYPE(pbuf, flow->flowType);
    PBUF_SET_ENTRY(pbuf, PMGR_ENTRY_IP_OUT);
    PBUF_SET_ND(pbuf, flow->nd);

    if (flow->nd == NULL) {
        if (flow->rt != NULL && TBM_IsDirectRt(flow->rt) == 0) {
            PBUF_SET_DST_ADDR4(pbuf, flow->rt->nxtHop);
        } else {
            PBUF_SET_DST_ADDR4(pbuf, flow->dst);
        }
    }

    IpTxCb(pbuf)->mtu = flow->mtu;
}

static Pbuf_t* IpRouteOutputSlow(Pbuf_t* pbuf)
{
    INET_FlowInfo_t  tempFlow = {0};
    Netdev_t*        dev  = PBUF_GET_DEV(pbuf);
    INET_FlowInfo_t* flow = &tempFlow;

    if (dev == NULL || NETDEV_IsDevUsed(dev) == 0) {
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_NET_NO_ROUTE);
        PBUF_Free(pbuf);
        return NULL;
    }

    flow->src = INET_TX_CB(pbuf)->src;
    flow->dst = PBUF_GET_DST_ADDR4(pbuf);
    if (INET_InitFlowByDev(dev, flow, PBUF_GET_L4_TYPE(pbuf)) != 0) {
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_NET_NO_ROUTE);
        PBUF_Free(pbuf);
        return NULL;
    }

    IpRouteSetPbuf(pbuf, flow);
    IpFillHdr(pbuf, flow);

    INET_DeinitFlow(flow);

    return pbuf;
}

// 查找路由，如果设置flowinfo，则通过flowinfo信息填写源目的地址
static Pbuf_t* IpRouteOutput(Pbuf_t* pbuf)
{
    Pbuf_t* ret = NULL;
    const INET_FlowInfo_t* flow = PBUF_GET_FLOW(pbuf);

    if (flow != NULL && flow->rt != NULL) {
        IpRouteSetPbuf(pbuf, flow);
        IpFillHdr(pbuf, flow);
        ret = pbuf;
    } else {
        // 内部转发流程会走这个流程
        ret = IpRouteOutputSlow(pbuf);
    }

    if (ret == NULL) {
        return ret;
    }
#ifdef DPFWK_NF
    if (NF_LocalOut(ret, DP_PF_INET) != NF_ACCEPT) {
        DP_INC_PKT_STAT(ret->wid, DP_PKT_NF_LOCALOUT_DROP);
        PBUF_Free(ret);
        return NULL;
    }
#endif
    return ret;
}

int IP_Init(int slave)
{
    PMGR_AddEntry(PMGR_ENTRY_IP_IN, IpInput);
    PMGR_AddEntry(PMGR_ENTRY_IP_OUT, IpOutput);
    PMGR_AddEntry(PMGR_ENTRY_ROUTE_OUT, IpRouteOutput);

    if (slave != 0) {
        return 0;
    }

    int tblCnt = CFG_GET_VAL(DP_CFG_WORKER_MAX);
    g_ipIdTbl = MEM_MALLOC_ALIGN(sizeof(IpId_t) * tblCnt, CACHE_LINE, MOD_IP, DP_MEM_FIX);
    if (g_ipIdTbl == NULL) {
        DP_LOG_ERR("Malloc IpId tbl failed\n");
        return -1;
    }

    for (int i = 0; i < tblCnt; i++) {
        g_ipIdTbl[i].ipid = RAND_GEN();
    }

    if (IpReassInit() != 0) {
        SHM_FREE(g_ipIdTbl, DP_MEM_FIX);
        g_ipIdTbl = NULL;
        DP_LOG_ERR("Ip init ipReass failed.");
        return -1;
    }

    return 0;
}

void IP_Deinit(int slave)
{
    if (slave != 0) {
        return;
    }

    IpReassDeinit();

    if (g_ipIdTbl != NULL) {
        SHM_FREE(g_ipIdTbl, DP_MEM_FIX);
        g_ipIdTbl = NULL;
    }
}
