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

#include "udp_inet.h"

#include <securec.h>

#include "dp_udp.h"
#include "dp_icmp.h"

#include "shm.h"
#include "pmgr.h"
#include "netdev.h"
#include "utils_statistic.h"
#include "utils_atomic.h"
#include "utils_cfg.h"
#include "utils_log.h"
#include "utils_spinlock.h"

#include "udp.h"

#define UDP_INET_HASH_MASK_LEN 3 // 1 << 3

#define UDP_MAX_MSG_LEN (65507) // 65535 - 20(IP首部) - 8(UDP首部)

/*
UDP端口分配规则：
1. 二元组、三元组使用inetsk中的hash节点，五元组使用udpcb中的节点
2.
*/
typedef struct {
    Spinlock_t   lock;
    atomic32_t   ref;
    Hash_t       hash;
} UdpInetTbl_t;

atomic32_t g_UdpCbCnt;


static void* UdpInetAllocTbl()
{
    size_t           objSize;
    UdpInetTbl_t*    tbl;
    HASH_NodeHead_t* nhs;

    objSize = SOCK_ALIGN_SIZE(sizeof(UdpInetTbl_t));
    objSize += HASH_GET_SIZE(UDP_INET_HASH_MASK_LEN);

    tbl = SHM_MALLOC(objSize, MOD_UDP, DP_MEM_FREE);
    if (tbl == NULL) {
        DP_LOG_ERR("Malloc memory failed for udp tbl.");
        return NULL;
    }
    (void)memset_s(tbl, objSize, 0, objSize);

    nhs       = (HASH_NodeHead_t*)PTR_NEXT(tbl, SOCK_ALIGN_SIZE(sizeof(UdpInetTbl_t)));

    if (SPINLOCK_Init(&tbl->lock) != 0) {
        SHM_FREE(tbl, DP_MEM_FREE);
        return NULL;
    }

    HASH_INIT(&tbl->hash, nhs, UDP_INET_HASH_MASK_LEN);

    return tbl;
}

static void UdpInetFreeTbl(void* ptr)
{
    UdpInetTbl_t* tbl = (UdpInetTbl_t*)ptr;

    SPINLOCK_Deinit(&tbl->lock);

    SHM_FREE(tbl, DP_MEM_FREE);
}

static inline uint32_t CalcHash(INET_Hashinfo_t* hashinfo)
{
    return hashinfo->port + hashinfo->laddr + hashinfo->paddr;
}

static inline uint32_t CalcHashByLport(INET_Hashinfo_t* hashinfo)
{
    return hashinfo->lport;
}

static void UdpInetRefTbl(UdpInetTbl_t* tbl)
{
    ATOMIC32_Inc(&tbl->ref);
}

static void UdpInetDerefTbl(UdpInetTbl_t* tbl)
{
    ATOMIC32_Dec(&tbl->ref);
}

static void UdpInetWaitTblIdle(UdpInetTbl_t* tbl)
{
    while (ATOMIC32_Load(&tbl->ref) != 0) { }
}

static int UdpInetCanConnect(UdpInetTbl_t* tbl, INET_Hashinfo_t* hi, int reuse)
{
    uint32_t     hashVal;
    InetSk_t*    inetSk;
    HASH_Node_t* node;

    hashVal = CalcHash(hi);

    HASH_FOREACH(&tbl->hash, hashVal, node)
    {
        inetSk = (InetSk_t*)node;

        if (INET_HashinfoEqual(hi, &inetSk->hashinfo) == 0) {
            continue;
        }

        if (reuse == 0) {
            return 0;
        }

        if (!SOCK_CAN_REUSE(UdpInetSk2Sk(inetSk))) {
            return 0;
        }
    }

    return 1;
}

static int UdpInetCanBind(UdpInetTbl_t* tbl, INET_Hashinfo_t* hi, int reuse)
{
    uint32_t     hashVal;
    InetSk_t*    inetSk;
    HASH_Node_t* node;

    hashVal = CalcHashByLport(hi);

    HASH_FOREACH(&tbl->hash, hashVal, node)
    {
        inetSk = (InetSk_t*)node;

        if (inetSk->hashinfo.lport != hi->lport) {
            continue;
        }

        // 二元组或三元组匹配
        if (inetSk->hashinfo.laddr == DP_INADDR_ANY || hi->laddr == DP_INADDR_ANY ||
            hi->laddr == inetSk->hashinfo.laddr) {
            if (reuse == 0 || !SOCK_CAN_REUSE(UdpInetSk2Sk(inetSk))) {
                return 0;
            }
        }
    }

    return 1;
}

static InetSk_t* UdpInetLookup(UdpInetTbl_t* tbl, INET_Hashinfo_t* hi, Netdev_t* dev)
{
    uint32_t     hashVal;
    InetSk_t*    inetSk = NULL;
    InetSk_t*    inetSk2 = NULL;
    HASH_Node_t* node;

    hashVal = CalcHash(hi);
    SPINLOCK_Lock(&tbl->lock);
    HASH_FOREACH(&tbl->hash, hashVal, node)
    {
        inetSk = (InetSk_t*)node;

        if (INET_HashinfoEqual(hi, &inetSk->hashinfo) == 0) { // 五元组匹配
            continue;
        }

        // 后续可以在这里扩展socket选择策略
        SPINLOCK_Unlock(&tbl->lock);
        return inetSk;
    }

    hashVal = CalcHashByLport(hi);
    HASH_FOREACH(&tbl->hash, hashVal, node)
    {
        inetSk = (InetSk_t*)node;

        if (inetSk->hashinfo.lport != hi->lport) {
            continue;
        }

        if (inetSk->hashinfo.pport != 0) {
            continue;
        }

        if (UdpInetSk2Sk(inetSk)->dev != NULL && UdpInetSk2Sk(inetSk)->dev != dev) {
            continue;
        }

        // 后续可以在这里扩展socket选择策略，这里优先匹配laddr+lport
        if (inetSk->hashinfo.laddr == hi->laddr) {
            SPINLOCK_Unlock(&tbl->lock);
            return inetSk;
        }

        inetSk2 = inetSk2 == NULL ? inetSk : inetSk2;
    }
    SPINLOCK_Unlock(&tbl->lock);
    return inetSk2;
}

static int UdpInetGenPort(UdpInetTbl_t* tbl, INET_Hashinfo_t* hi, int reuse,
    int (*isPortUsable)(UdpInetTbl_t* tbl, INET_Hashinfo_t* hi, int reuse))
{
    uint16_t port;
    uint16_t minPort;
    uint16_t range;
    uint16_t maxPort;

    port    = UdpGetRsvPort(&minPort, &range);
    maxPort = minPort + range;

    for (uint16_t i = 0; i < range; i++, port++) {
        if (port >= maxPort) {
            port = minPort;
        }

        hi->lport = port;
        if (isPortUsable(tbl, hi, reuse) != 0) {
            return 0;
        }
    }

    return -1;
}

static InetSk_t* UdpInetLookupByPkt(UdpInetTbl_t* tbl, Pbuf_t* pbuf, DP_UdpHdr_t* udpHdr)
{
    DP_IpHdr_t*   ipHdr = (DP_IpHdr_t*)PBUF_GET_L3_HDR(pbuf);
    INET_Hashinfo_t hi;

    hi.lport    = udpHdr->dport;
    hi.pport    = udpHdr->sport;
    hi.laddr    = ipHdr->dst;
    hi.paddr    = ipHdr->src;
    hi.protocol = ipHdr->type;

    return UdpInetLookup(tbl, &hi, (Netdev_t*)PBUF_GET_DEV(pbuf));
}

static void UdpInetInsert(UdpInetTbl_t* tbl, Sock_t* sk, InetSk_t* inetSk)
{
    uint32_t hashVal;

    if (SOCK_IS_CONNECTED(sk)) {
        hashVal = CalcHash(&inetSk->hashinfo);
    } else {
        hashVal = CalcHashByLport(&inetSk->hashinfo);
    }
    INET_InsertHashItem(&tbl->hash, &inetSk->node, hashVal);
}

static void UdpInetRemove(UdpInetTbl_t* tbl, Sock_t* sk, InetSk_t* inetSk)
{
    uint32_t hashVal;

    if (SOCK_IS_CONNECTED(sk)) {
        hashVal = CalcHash(&inetSk->hashinfo);
    } else {
        hashVal = CalcHashByLport(&inetSk->hashinfo);
    }
    INET_RemoveHashItem(&tbl->hash, &inetSk->node, hashVal);
}

static inline UdpInetTbl_t* UdpInetGetTbl(NS_Net_t* net)
{
    return NS_GET_TBL(net, NS_NET_UDP);
}

static int UdpInetBindInner(UdpInetTbl_t* tbl, Sock_t* sk, InetSk_t* inetSk, INET_Hashinfo_t* cur)
{
    int ret = 0;

    SPINLOCK_Lock(&tbl->lock);

    if (cur->lport != 0) { // 用户指定端口，判断能否绑定
        if (UdpInetCanBind(tbl, cur, SOCK_CAN_REUSE(sk)) == 0) {
            ret = -EADDRINUSE;
        }
    } else if (inetSk->hashinfo.lport == 0) { // 生成随机端口
        if (UdpInetGenPort(tbl, cur, 0, UdpInetCanBind) != 0) { // 尝试选择一个未使用的端口
            if (SOCK_CAN_REUSE(sk) && UdpInetGenPort(tbl, cur, 1, UdpInetCanBind) != 0) { // 尝试选择一个可以reuse的端口
                ret = -EADDRINUSE;
            }
        }
    }

    if (ret == 0) {
        inetSk->hashinfo.laddr = cur->laddr;
        if (inetSk->hashinfo.lport == 0) { // 已经生成的随机端口，不需要处理hash表
            inetSk->hashinfo.lport = cur->lport;
            UdpInetInsert(tbl, sk, UdpInetSk(sk));
        }
        SOCK_SET_BINDED(sk);
    }

    SPINLOCK_Unlock(&tbl->lock);

    return ret;
}

static int UdpInetBind(Sock_t* sk, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen)
{
    int             ret;
    INET_Hashinfo_t cur;
    UdpInetTbl_t*   tbl    = UdpInetGetTbl(sk->net);
    InetSk_t*       inetSk = UdpInetSk(sk);

    if (SOCK_IS_BINDED(sk)) {
        return -EINVAL;
    }

    if ((ret = INET_Bind(sk, inetSk, addr, addrlen, &cur)) != 0) {
        return ret;
    }

    return UdpInetBindInner(tbl, sk, inetSk, &cur);
}

static int UdpInetConnect(Sock_t* sk, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen)
{
    int             ret    = 0;
    UdpInetTbl_t*   tbl    = UdpInetGetTbl(sk->net);
    InetSk_t*       inetSk = UdpInetSk(sk);
    INET_Hashinfo_t tempHi;

    if ((ret = INET_Connect(sk, inetSk, addr, addrlen)) != 0) {
        return ret;
    }

    if (inetSk->flow.src == inetSk->flow.dst) {
        return -ENETUNREACH;
    }

    tempHi.lport    = SOCK_IS_BINDED(sk) ? inetSk->hashinfo.lport : 0;
    tempHi.pport    = ((struct DP_SockaddrIn*)addr)->sin_port;
    tempHi.laddr    = inetSk->flow.src;
    tempHi.paddr    = inetSk->flow.dst;
    tempHi.protocol = inetSk->hashinfo.protocol;

    SPINLOCK_Lock(&tbl->lock);

    if (tempHi.lport == 0) { // 需要随机生成端口
        if (UdpInetGenPort(tbl, &tempHi, 0, UdpInetCanConnect) == 0) { // 先尝试生成一个未使用的端口
            if (SOCK_CAN_REUSE(sk) &&
                UdpInetGenPort(tbl, &tempHi, 1, UdpInetCanConnect) == 0) { // 尝试生成一个可以resue的端口
                ret = -EADDRNOTAVAIL;
            }
        }
    } else if (UdpInetCanConnect(tbl, &tempHi, SOCK_CAN_REUSE(sk)) == 0) { // 用户给的端口可以用于connect
        ret = -EADDRNOTAVAIL;
    }

    if (ret == 0) {
        if (inetSk->hashinfo.lport != 0) { // 已经随机生成端口，先移除原来的hash节点
            UdpInetRemove(tbl, sk, inetSk);
            UdpInetWaitTblIdle(tbl);
        }

        SOCK_SET_CONNECTED(sk);

        inetSk->hashinfo = tempHi;
        UdpInetInsert(tbl, sk, inetSk);
    }

    SPINLOCK_Unlock(&tbl->lock);

    return ret;
}

static int UdpInetAutoBind(Sock_t* sk, InetSk_t* inetSk)
{
    int             ret = 0;
    UdpInetTbl_t*   tbl = UdpInetGetTbl(sk->net);
    INET_Hashinfo_t tempHi;

    SPINLOCK_Lock(&tbl->lock);

    tempHi = inetSk->hashinfo;

    if (UdpInetGenPort(tbl, &tempHi, 0, UdpInetCanBind) == 0) {
        ret = 0;
    } else if (SOCK_CAN_REUSE(sk) && UdpInetGenPort(tbl, &inetSk->hashinfo, 1, UdpInetCanBind) == 0) {
        ret = 0;
    } else {
        ret = -EAGAIN;
    }

    if (ret == 0) {
        inetSk->hashinfo = tempHi;
        UdpInetInsert(tbl, sk, inetSk);
    }

    SPINLOCK_Unlock(&tbl->lock);

    return ret;
}

static inline uint32_t UdpInetPseudoCksum(Pbuf_t* pbuf, INET_FlowInfo_t* flow)
{
    INET_PseudoHdr_t phdr;
    uint32_t         cksum;
    uint16_t         len = (uint16_t)PBUF_GET_PKT_LEN(pbuf);

    phdr.src      = flow->src;
    phdr.dst      = flow->dst;
    phdr.zero     = 0;
    phdr.protocol = DP_IPPROTO_UDP;
    phdr.len      = UTILS_HTONS(len);
    cksum         = UTILS_Cksum(0, (uint8_t*)&phdr, sizeof(phdr));

    return cksum;
}

static uint16_t UdpInetCksum(Pbuf_t* pbuf, INET_FlowInfo_t* flow)
{
    uint32_t         cksum;
    Netdev_t* dev = PBUF_GET_DEV(pbuf);

    if (dev != NULL && NETDEV_TX_UDP_CKSUM_ENABLED(dev) &&
        ((PBUF_GET_PKT_FLAGS(pbuf) & PBUF_PKTFLAGS_FRAGMENTED) == 0)) {
        DP_PBUF_SET_OLFLAGS_BIT(pbuf, DP_PBUF_OLFLAGS_TX_UDP_CKSUM);

        if (NETDEV_TX_L4_CKSUM_PARTIAL(dev)) {
            cksum = UdpInetPseudoCksum(pbuf, flow);
            return UTILS_CksumAdd(cksum);
        }
        return 0;
    }

    cksum = UdpInetPseudoCksum(pbuf, flow);
    cksum += PBUF_CalcCksum(pbuf);

    return UTILS_CksumSwap(cksum);
}

static void UdpInetFillHeader(Sock_t* sk, Pbuf_t* pbuf, uint16_t dport, INET_FlowInfo_t* flow)
{
    DP_UdpHdr_t* udpHdr;

    PBUF_PUT_HEAD(pbuf, sizeof(DP_UdpHdr_t));

    udpHdr         = PBUF_MTOD(pbuf, DP_UdpHdr_t*);
    udpHdr->sport  = UdpInetSk(sk)->hashinfo.lport;
    udpHdr->dport  = dport;
    udpHdr->len    = UTILS_HTONS((uint16_t)PBUF_GET_PKT_LEN(pbuf));
    udpHdr->chksum = 0;
    udpHdr->chksum = UdpInetCksum(pbuf, flow);
}

static ssize_t UdpInetPktFill(Sock_t* sk, const struct DP_Msghdr* msg, INET_FlowInfo_t* flow,
    uint16_t dport, Pbuf_t* pbuf)
{
    ssize_t ret = SOCK_PbufAppendMsg(pbuf, msg);
    if (ret == 0) {
        return -1;
    }

    if (flow->rt == NULL && sk->bindDev == 1) {
        PBUF_SET_DEV(pbuf, sk->dev);
        PBUF_SET_DST_ADDR(pbuf, flow->dst);
    } else {
        PBUF_SET_DEV(pbuf, INET_GetDevByFlow(flow));
        PBUF_SET_DST_ADDR(pbuf, flow->rt->nxtHop == DP_INADDR_ANY ? flow->dst : flow->rt->nxtHop);
    }

    if (PBUF_GET_PKT_LEN(pbuf) + sizeof(DP_IpHdr_t) > flow->mtu) {
        PBUF_SET_PKT_FLAGS_BIT(pbuf, PBUF_PKTFLAGS_FRAGMENTED);
    }

    sk->error = 0;
    UdpInetFillHeader(sk, pbuf, dport, flow);
    // UDP报文默认出队列为设备的0号队列
    PBUF_SET_QUE_ID(pbuf, 0);
    PBUF_SET_WID(pbuf, (uint8_t)NETDEV_GetTxWid(PBUF_GET_DEV(pbuf), 0));
    PBUF_SET_PKT_TYPE(pbuf, flow->flowType);
    PBUF_SET_ENTRY(pbuf, PMGR_ENTRY_ROUTE_OUT);
    PBUF_SET_FLOW(pbuf, flow);
    return ret;
}

static ssize_t UdpInetFlowFill(Sock_t* sk, const struct DP_Msghdr* msg, INET_FlowInfo_t** flow, int* flowCached)
{
    ssize_t ret = 0;
    if (msg->msg_name != NULL) {
        struct DP_SockaddrIn* addrIn = (struct DP_SockaddrIn*)msg->msg_name;

        if ((ret = INET_CheckAddr((struct DP_Sockaddr*)addrIn, msg->msg_namelen)) != 0) {
            return ret;
        }

        if (SOCK_IS_CONNECTED(sk)) {
            if (UdpInetSk(sk)->flow.dst != addrIn->sin_addr.s_addr) {
                return -EINVAL;         // UDP已经connect，sendto传入的地址不能与connect的地址不同
            }
        }

        (*flow)->dst = addrIn->sin_addr.s_addr;
        if ((ret = INET_InitFlowBySk(sk, UdpInetSk(sk), *flow)) != 0) {
            return ret;
        }

        if ((*flow)->flowType == PBUF_PKTTYPE_BROADCAST && sk->broadcast == 0) {
            INET_DeinitFlow(*flow);
            return -ENETUNREACH;
        }
    } else if (SOCK_IS_CONNECTED(sk)) {
        *flow       = &UdpInetSk(sk)->flow;
        *flowCached = 1;
        if ((ret = INET_UpdateFlow(*flow)) != 0) {
            return ret;
        }
    } else {
        return -EDESTADDRREQ;
    }
    return ret;
}

static ssize_t UdpInetSendmsg(Sock_t* sk, const struct DP_Msghdr* msg, int flags, size_t msgDataLen,
    size_t* index, size_t* offset)
{
    ssize_t          ret;
    INET_FlowInfo_t  tempFlow;
    InetSk_t*        inetSk = UdpInetSk(sk);
    INET_FlowInfo_t* flow   = &tempFlow;
    uint16_t         dport;
    int              flowCached = 0;
    Pbuf_t*          pbuf;

    (void)flags;
    (void)index;
    (void)offset;

    if (msgDataLen > (size_t)UDP_MAX_MSG_LEN) {
        return -EMSGSIZE;
    }

    if ((ret = UdpInetFlowFill(sk, msg, &flow, &flowCached)) != 0) {
        return ret;
    }

    if ((INET_GetDevByFlow(flow)->ifflags & DP_IFF_UP) == 0) {
        return -ENETDOWN;
    }

    if (flags != 0 && flags != DP_MSG_DONTWAIT) {
        return -EOPNOTSUPP;
    }

    if (inetSk->hashinfo.lport == 0) {
        if ((ret = UdpInetAutoBind(sk, inetSk)) != 0) {
            return ret;
        }
    }

    dport = msg->msg_name != NULL ? ((struct DP_SockaddrIn*)(msg->msg_name))->sin_port : inetSk->hashinfo.pport;

    pbuf = PBUF_Alloc(SOCK_INET_HEADROOM, (uint16_t)msgDataLen);
    if (pbuf == NULL) {
        ret = -ENOMEM;
        goto err;
    }

    ret = UdpInetPktFill(sk, msg, flow, dport, pbuf);
    if (ret == -1) {
        ret = -ENOMEM;
        goto err;
    }

    DP_INC_PKT_STAT(pbuf->wid, DP_PKT_UDP_OUT);
    PMGR_Dispatch(pbuf);

err:

    if (flowCached == 0) {
        INET_DeinitFlow(flow);
    }

    return ret;
}

static int UdpInetClose(Sock_t* sk)
{
    UdpInetTbl_t* tbl = UdpInetGetTbl(sk->net);

    if (UdpInetSk(sk)->hashinfo.lport != 0) {
        SPINLOCK_Lock(&tbl->lock);
        UdpInetRemove(tbl, sk, UdpInetSk(sk));
        SPINLOCK_Unlock(&tbl->lock);
    }

    UdpInetWaitTblIdle(UdpInetGetTbl(sk->net));

    INET_DeinitFlow(&UdpInetSk(sk)->flow);
    SOCK_Unlock(sk);
    SOCK_DeinitSk(sk);
    SHM_FREE(sk, DP_MEM_FREE);

    (void)ATOMIC32_Dec(&g_UdpCbCnt);

    return 0;
}

static int UdpInetSetSockopt(Sock_t* sk, int level, int optName, const void* optVal, DP_Socklen_t optLen)
{
    int ret = -ENOPROTOOPT;

    if (level == DP_IPPROTO_IP) {
        ret = INET_Setsockopt(UdpInetSk(sk), optName, optVal, optLen);
    }

    return ret;
}

static int UdpInetGetSockopt(Sock_t* sk, int level, int optName, void* optVal, DP_Socklen_t* optLen)
{
    int ret = -ENOPROTOOPT;

    if (level == DP_IPPROTO_IP) {
        ret = INET_Getsockopt(UdpInetSk(sk), optName, optVal, optLen);
    }

    return ret;
}

static int UdpInetGetDstAddr(Sock_t* sk, Pbuf_t* pbuf, struct DP_Sockaddr* addr, DP_Socklen_t* addrlen)
{
    DP_UdpHdr_t*          udpHdr;
    DP_IpHdr_t*           ipHdr;
    struct DP_SockaddrIn* addrIn = (struct DP_SockaddrIn*)addr;

    (void)sk;

    if (pbuf == NULL || (int)*addrlen < 0) {
        return -EINVAL;
    }

    udpHdr = (DP_UdpHdr_t*)PBUF_GET_L4_HDR(pbuf);
    ipHdr  = (DP_IpHdr_t*)PBUF_GET_L3_HDR(pbuf);

    struct DP_SockaddrIn tempAddrIn = {.sin_family      = DP_AF_INET,
                                       .sin_port        = udpHdr->sport,
                                       .sin_addr.s_addr = ipHdr->src};
    uint32_t cpyLen = UTILS_MIN(*addrlen, (uint32_t)sizeof(struct DP_SockaddrIn));
    (void)memcpy_s(addrIn, cpyLen, &tempAddrIn, cpyLen);        // 按入参长度截断赋值addrIn
    *addrlen                = sizeof(*addrIn);

    return 0;
}

static int UdpInetGetAddr(Sock_t* sk, struct DP_Sockaddr* addr, DP_Socklen_t* addrlen, int peer)
{
    return INET_GetAddr(UdpInetSk(sk), addr, addrlen, peer);
}

static SOCK_Ops_t g_udpOps = {
    .shutdown   = NULL,
    .close      = UdpInetClose,
    .bind       = UdpInetBind,
    .listen     = NULL,
    .accept     = NULL,
    .connect    = UdpInetConnect,
    .setsockopt = UdpInetSetSockopt,
    .getsockopt = UdpInetGetSockopt,
    .keepalive  = NULL,
    .sendmsg    = UdpInetSendmsg,
    .recvmsg    = SOCK_PopRcvBufByPkt,

    .getDstAddr = UdpInetGetDstAddr,
    .getAddr    = UdpInetGetAddr,
};

static Sock_t* UdpInetAllocSk()
{
    Sock_t* sk;
    size_t  objSize = sizeof(UdpInetSk_t);

    objSize = SOCK_GetSkSize(objSize);

    sk = SHM_MALLOC(objSize, MOD_UDP, DP_MEM_FREE);
    if (sk == NULL) {
        DP_LOG_ERR("Malloc memory failed for udp sk.");
        return NULL;
    }

    (void)memset_s(sk, objSize, 0, objSize);

    SOCK_InitSk(sk, NULL, objSize);

    return sk;
}

static int UdpInetSkCreate(NS_Net_t* net, int type, int protocol, Sock_t** out)
{
    Sock_t* sk;

    (void)type;

    if ((type != DP_SOCK_DGRAM) || (protocol != 0 && protocol != DP_IPPROTO_UDP)) {
        return -EPROTONOSUPPORT;
    }

    // 判断UDPCB是否到达上限
    if (ATOMIC32_Load(&g_UdpCbCnt) >= (uint32_t)CFG_GET_VAL(DP_CFG_UDPCB_MAX)) {
        DP_LOG_INFO("The num of udpSk exceed udpCbMax configured.");
        return -EMFILE;
    }

    sk = UdpInetAllocSk();
    if (sk == NULL) {
        return -ENOMEM;
    }

    sk->net    = net;
    sk->ops    = &g_udpOps;
    sk->family = DP_AF_INET;
    SOCK_SET_WRITABLE(sk);
    UdpInetSk(sk)->hashinfo.protocol = DP_IPPROTO_UDP;

    (void)ATOMIC32_Inc(&g_UdpCbCnt);

    *out = sk;

    return 0;
}

static int UdpVerifyCksum(Pbuf_t* pbuf)
{
    if ((DP_PBUF_GET_OLFLAGS(pbuf) & DP_PBUF_OLFLAGS_RX_L4_CKSUM_GOOD) != 0) {
        return 0;
    }

    return INET_CalcCksum(pbuf);
}

static int PreCheckUdp(Pbuf_t* pbuf, DP_UdpHdr_t* udpHdr)
{
    if (PBUF_GET_SEG_LEN(pbuf) <= sizeof(*udpHdr)) {
        return -1;
    }

    if (udpHdr->chksum == 0) { // 忽略cksum校验
        return 0;
    }

    if (UdpVerifyCksum(pbuf) != 0) {
        return -1;
    }

    return 0;
}

static Pbuf_t* UdpGenPortUnreachable(Pbuf_t* orig)
{
    DP_IpHdr_t* ip = (DP_IpHdr_t*)PBUF_GET_L3_HDR(orig);
    Pbuf_t* ret;
    DP_IcmpHdr_t* icmp;

    PBUF_SET_HEAD(orig, PBUF_GET_L3_OFF(orig));

    ret = PBUF_Alloc(SOCK_INET_HEADROOM + sizeof(DP_IcmpHdr_t), (uint16_t)PBUF_GET_PKT_LEN(orig));
    if (ret == NULL) {
        return NULL;
    }

    PBUF_Append(ret, (uint8_t*)ip, PBUF_GET_SEG_LEN(orig));

    PBUF_PUT_HEAD(ret, sizeof(*icmp));
    icmp = PBUF_MTOD(ret, DP_IcmpHdr_t*);

    icmp->type  = DP_ICMP_TYPE_DEST_UNREACH;
    icmp->code  = DP_ICMP_PORT_UNREACH;
    icmp->cksum = 0;
    icmp->resv  = 0;

    icmp->cksum = UTILS_CksumSwap(PBUF_CalcCksum(ret));

    PBUF_SET_DST_ADDR(ret, ip->src);
    PBUF_SET_FLOW(ret, NULL);
    PBUF_SET_DEV(ret, PBUF_GET_DEV(orig));
    PBUF_SET_ENTRY(ret, PMGR_ENTRY_ROUTE_OUT);
    PBUF_SET_L4_TYPE(ret, DP_IPPROTO_ICMP);

    return ret;
}

static Pbuf_t* UdpInetErrInput(Pbuf_t* pbuf)
{
    UdpInetTbl_t*   tbl = UdpInetGetTbl(((Netdev_t*)PBUF_GET_DEV(pbuf))->net);
    DP_IpHdr_t*   ipHdr = (DP_IpHdr_t*)PBUF_GET_L3_HDR(pbuf);
    DP_UdpHdr_t*  udpHdr;
    INET_Hashinfo_t hi;
    DP_IcmpHdr_t* icmp;
    InetSk_t*       inetSk;

    icmp   = (DP_IcmpHdr_t*)((uint8_t*)ipHdr + (ipHdr->hdrlen << 2)); // 左移2位
    ipHdr  = (DP_IpHdr_t*)(icmp + 1);
    udpHdr = (DP_UdpHdr_t*)((uint8_t*)ipHdr + (ipHdr->hdrlen << 2)); // 左移2位

    hi.lport    = udpHdr->sport;
    hi.pport    = udpHdr->dport;
    hi.laddr    = ipHdr->src;
    hi.paddr    = ipHdr->dst;
    hi.protocol = DP_IPPROTO_UDP;

    UdpInetRefTbl(tbl);

    inetSk = UdpInetLookup(tbl, &hi, (Netdev_t*)PBUF_GET_DEV(pbuf));
    if (inetSk != NULL) {
        Sock_t* sk = UdpInetSk2Sk(inetSk);
        SOCK_Lock(sk);

        if (SOCK_IS_CONNECTED(sk)) { // 如果处于链接状态
            sk->error = ECONNREFUSED;
            SOCK_WakeupRdSem(sk);
        }

        // 这里处理err事件

        SOCK_Unlock(sk);
    }

    UdpInetDerefTbl(tbl);

    PBUF_Free(pbuf);

    return NULL;
}

static Pbuf_t* UdpInetInput(Pbuf_t* pbuf)
{
    Pbuf_t*        ret = NULL;
    DP_UdpHdr_t* udpHdr = PBUF_MTOD(pbuf, DP_UdpHdr_t*);
    Sock_t*        sk;
    InetSk_t*      inetSk;
    Netdev_t*      dev = (Netdev_t*)PBUF_GET_DEV(pbuf);
    UdpInetTbl_t*  tbl = UdpInetGetTbl(dev->net);

    if (PreCheckUdp(pbuf, udpHdr) != 0) {
        goto drop;
    }

    UdpInetRefTbl(tbl);

    inetSk = UdpInetLookupByPkt(tbl, pbuf, udpHdr);
    UdpInetDerefTbl(tbl);
    if (inetSk == NULL) {
        ret = UdpGenPortUnreachable(pbuf);
        goto drop;
    }
    sk = UdpInetSk2Sk(inetSk);

    if (PBUF_GET_PKT_TYPE(pbuf) == PBUF_PKTTYPE_BROADCAST) {
        if (sk->broadcast == 0) { // 如果是广播报文
            goto drop;
        }
    }

    PBUF_SET_L4_OFF(pbuf);
    PBUF_CUT_HEAD(pbuf, sizeof(*udpHdr));
    PBUF_SET_IFINDEX(pbuf, dev->ifindex);
    if (SOCK_PushRcvBufSafe(sk, pbuf) < 0) {
        goto drop;
    }
    DP_INC_PKT_STAT(pbuf->wid, DP_PKT_UDP_IN);
    return NULL;

drop:
    PBUF_Free(pbuf);

    return ret;
}

SOCK_ProtoOps_t g_udpProtoOps = {
    .type     = DP_SOCK_DGRAM,
    .protocol = 0,
    .create   = UdpInetSkCreate,
};

int UdpInetInit(int slave)
{
    (void)slave;

    SOCK_AddProto(DP_AF_INET, &g_udpProtoOps);
    PMGR_AddEntry(PMGR_ENTRY_UDP_IN, UdpInetInput);
    PMGR_AddEntry(PMGR_ENTRY_UDP_ERR_IN, UdpInetErrInput);

    NS_SetNetOps(NS_NET_UDP, UdpInetAllocTbl, UdpInetFreeTbl);

    ATOMIC32_Store(&g_UdpCbCnt, 0);

    return 0;
}

void UdpInetDeinit(int slave)
{
    (void)slave;

    ATOMIC32_Store(&g_UdpCbCnt, 0);
}
