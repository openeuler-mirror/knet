/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 
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
#include "utils_cfg.h"
#include "utils_log.h"
#include "utils_atomic.h"
#include "utils_mem_pool.h"
#include "utils_spinlock.h"
#include "utils_statistic.h"
#include "utils_cb_cnt.h"

#include "udp.h"

#define UDP_INET_HASH_MASK_LEN 3 // 1 << 3

#define UDP_MAX_MSG_LEN (65507) // 65535 - 20(IP首部) - 8(UDP首部)

char* g_udpMpName = "DP_UDP_MP";
DP_Mempool g_udpMemPool = {0};

static void* UdpHashAllocTbl()
{
    size_t           objSize;
    UdpHashTbl_t*    tbl;
    HASH_NodeHead_t* nhs;

    objSize = SOCK_ALIGN_SIZE(sizeof(UdpHashTbl_t));
    objSize += HASH_GET_SIZE(UDP_INET_HASH_MASK_LEN);

    tbl = SHM_MALLOC(objSize, MOD_UDP, DP_MEM_FREE);
    if (tbl == NULL) {
        DP_LOG_ERR("Malloc memory failed for udp tbl.");
        return NULL;
    }
    (void)memset_s(tbl, objSize, 0, objSize);

    nhs       = (HASH_NodeHead_t*)PTR_NEXT(tbl, SOCK_ALIGN_SIZE(sizeof(UdpHashTbl_t)));

    if (SPINLOCK_Init(&tbl->lock) != 0) {
        SHM_FREE(tbl, DP_MEM_FREE);
        return NULL;
    }

    HASH_INIT(&tbl->hash, nhs, UDP_INET_HASH_MASK_LEN);

    return tbl;
}

static void UdpHashFreeTbl(void* ptr)
{
    UdpHashTbl_t* tbl = (UdpHashTbl_t*)ptr;

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

static int UdpInetCanConnect(UdpHashTbl_t* tbl, INET_Hashinfo_t* hi, int reuse)
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

static int UdpInetCanBind(UdpHashTbl_t* tbl, INET_Hashinfo_t* hi, int reuse)
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

static InetSk_t* UdpInetLookup(UdpHashTbl_t* tbl, INET_Hashinfo_t* hi, Netdev_t* dev)
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

static int UdpInetGenPort(UdpHashTbl_t* tbl, INET_Hashinfo_t* hi, int reuse,
    int (*isPortUsable)(UdpHashTbl_t* tbl, INET_Hashinfo_t* hi, int reuse))
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

static InetSk_t* UdpInetLookupByPkt(UdpHashTbl_t* tbl, Pbuf_t* pbuf, DP_UdpHdr_t* udpHdr)
{
    DP_IpHdr_t*   ipHdr = (DP_IpHdr_t*)PBUF_GET_L3_HDR(pbuf);
    INET_Hashinfo_t hi = {0};

    hi.lport    = udpHdr->dport;
    hi.pport    = udpHdr->sport;
    hi.laddr    = ipHdr->dst;
    hi.paddr    = ipHdr->src;
    hi.protocol = ipHdr->type;
    hi.vpnid    = DP_PBUF_GET_VPNID(pbuf);

    return UdpInetLookup(tbl, &hi, (Netdev_t*)PBUF_GET_DEV(pbuf));
}

static void UdpInetInsert(UdpHashTbl_t* tbl, Sock_t* sk, InetSk_t* inetSk)
{
    uint32_t hashVal;

    if (SOCK_IS_CONNECTED(sk)) {
        hashVal = CalcHash(&inetSk->hashinfo);
    } else {
        hashVal = CalcHashByLport(&inetSk->hashinfo);
    }
    HASH_INSERT(&tbl->hash, hashVal, &inetSk->node);
}

static void UdpInetRemove(UdpHashTbl_t* tbl, Sock_t* sk, InetSk_t* inetSk)
{
    uint32_t hashVal;

    if (SOCK_IS_CONNECTED(sk)) {
        hashVal = CalcHash(&inetSk->hashinfo);
    } else {
        hashVal = CalcHashByLport(&inetSk->hashinfo);
    }
    HASH_REMOVE(&tbl->hash, hashVal, &inetSk->node);
}

static int UdpInetBindInner(UdpHashTbl_t* tbl, Sock_t* sk, InetSk_t* inetSk, INET_Hashinfo_t* cur)
{
    int ret = 0;

    SPINLOCK_Lock(&tbl->lock);

    if (cur->lport != 0) { // 用户指定端口，判断能否绑定
        if (UdpInetCanBind(tbl, cur, SOCK_CAN_REUSE(sk)) == 0) {
            ret = -EADDRINUSE;
        }
    } else if (inetSk->hashinfo.lport == 0) { // 生成随机端口
        if (UdpInetGenPort(tbl, cur, 0, UdpInetCanBind) != 0) { // 尝试选择一个未使用的端口
            if (!SOCK_CAN_REUSE(sk)) {
                ret = -EADDRINUSE;
            }
            if (UdpInetGenPort(tbl, cur, 1, UdpInetCanBind) != 0) { // 尝试选择一个可以reuse的端口
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
    INET_Hashinfo_t cur = {0};
    UdpHashTbl_t*   tbl    = UdpHashGetTbl(sk->net);
    InetSk_t*       inetSk = UdpInetSk(sk);

    if (SOCK_IS_BINDED(sk)) {
        return -EINVAL;
    }

    if ((ret = INET_Bind(sk, inetSk, addr, addrlen, &cur)) != 0) {
        return ret;
    }

    return UdpInetBindInner(tbl, sk, inetSk, &cur);
}

static inline void Set4Hashinfo(INET_Hashinfo_t *hi, Sock_t* sk, InetSk_t* inetSk, uint16_t lport, uint16_t pport)
{
    hi->lport    = SOCK_IS_BINDED(sk) ? lport : 0;
    hi->pport    = pport;
    hi->laddr    = inetSk->flow.src;
    hi->paddr    = inetSk->flow.dst;
    hi->protocol = inetSk->hashinfo.protocol;
    hi->vpnid    = inetSk->hashinfo.vpnid;
}

static int UdpInetConnect(Sock_t* sk, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen)
{
    int             ret    = 0;
    UdpHashTbl_t*   tbl    = UdpHashGetTbl(sk->net);
    InetSk_t*       inetSk = UdpInetSk(sk);
    INET_Hashinfo_t tempHi = {0};

    if ((ret = INET_Connect(sk, inetSk, addr, addrlen)) != 0) {
        DP_ADD_ABN_STAT(DP_UDP_INET_CONN_FAILED);
        return ret;
    }

    if (inetSk->flow.src == inetSk->flow.dst) {
        DP_ADD_ABN_STAT(DP_UDP_CONN_SELF);
        ret = -ENETUNREACH;
        goto end;
    }

    Set4Hashinfo(&tempHi, sk, inetSk, inetSk->hashinfo.lport, ((struct DP_SockaddrIn*)addr)->sin_port);

    SPINLOCK_Lock(&tbl->lock);

    if (tempHi.lport == 0) { // 需要随机生成端口
        if (UdpInetGenPort(tbl, &tempHi, 0, UdpInetCanConnect) != 0) { // 先尝试生成一个未使用的端口
            if (!SOCK_CAN_REUSE(sk)) {
                ret = -EADDRNOTAVAIL;
                DP_ADD_ABN_STAT(DP_UDP_CONN_RAND_PORT_FAILED);
            } else if (UdpInetGenPort(tbl, &tempHi, 1, UdpInetCanConnect) != 0) {
                ret = -EADDRNOTAVAIL;
                DP_ADD_ABN_STAT(DP_UDP_CONN_RAND_PORT_FAILED);
            }
        }
    } else if (UdpInetCanConnect(tbl, &tempHi, SOCK_CAN_REUSE(sk)) == 0) { // 用户给的端口可以用于connect
        ret = -EADDRNOTAVAIL;
        DP_ADD_ABN_STAT(DP_UDP_CONN_PORT_FAILED);
    }

    if (ret == 0) {
        if (inetSk->hashinfo.lport != 0) { // 已经随机生成端口，先移除原来的hash节点
            UdpInetRemove(tbl, sk, inetSk);
            UdpHashWaitTblIdle(tbl);
        }

        SOCK_SET_CONNECTED(sk);

        inetSk->hashinfo = tempHi;
        UdpInetInsert(tbl, sk, inetSk);
    }

    SPINLOCK_Unlock(&tbl->lock);
end:
    if (ret != 0) {
        INET_DeinitFlow(&inetSk->flow);
    }
    return ret;
}

static int UdpInetAutoBind(Sock_t* sk, InetSk_t* inetSk)
{
    int             ret = 0;
    UdpHashTbl_t*   tbl = UdpHashGetTbl(sk->net);
    INET_Hashinfo_t tempHi = {0};

    SPINLOCK_Lock(&tbl->lock);

    tempHi = inetSk->hashinfo;

    if (UdpInetGenPort(tbl, &tempHi, 0, UdpInetCanBind) == 0) {
        ret = 0;
    } else if (SOCK_CAN_REUSE(sk) && UdpInetGenPort(tbl, &inetSk->hashinfo, 1, UdpInetCanBind) == 0) {
        ret = 0;
    } else {
        ret = -EAGAIN;
        DP_ADD_ABN_STAT(DP_UDP_AUTO_BIND_FAILED);
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
    cksum += PBUF_CalcCksumAcc(pbuf);

    return UTILS_CksumSwap(cksum);
}

static void UdpInetFillHeader(Sock_t* sk, Pbuf_t* pbuf, uint16_t dport, INET_FlowInfo_t* flow)
{
    DP_UdpHdr_t* udpHdr;

    PBUF_PUT_HEAD(pbuf, sizeof(DP_UdpHdr_t));
    PBUF_SET_L4_OFF(pbuf);
    udpHdr         = PBUF_MTOD(pbuf, DP_UdpHdr_t*);
    udpHdr->sport  = UdpInetSk(sk)->hashinfo.lport;
    udpHdr->dport  = dport;
    udpHdr->len    = UTILS_HTONS((uint16_t)PBUF_GET_PKT_LEN(pbuf));
    udpHdr->chksum = 0;
    udpHdr->chksum = UdpInetCksum(pbuf, flow);
}

static void UdpInetPktFill(Sock_t* sk, INET_FlowInfo_t* flow, uint16_t dport, Pbuf_t* pbuf)
{
    if (flow->rt == NULL) {
        PBUF_SET_DEV(pbuf, sk->dev);
        PBUF_SET_DST_ADDR4(pbuf, flow->dst);
    } else {
        PBUF_SET_DEV(pbuf, INET_GetDevByFlow(flow));
        PBUF_SET_DST_ADDR4(pbuf, flow->rt->nxtHop == DP_INADDR_ANY ? flow->dst : flow->rt->nxtHop);
    }

    if (PBUF_GET_PKT_LEN(pbuf) + sizeof(DP_IpHdr_t) > flow->mtu) {
        PBUF_SET_PKT_FLAGS_BIT(pbuf, PBUF_PKTFLAGS_FRAGMENTED);
    }

    sk->error = 0;
    UdpInetFillHeader(sk, pbuf, dport, flow);
    // UDP报文默认出队列为设备的0号队列
    PBUF_SET_QUE_ID(pbuf, 0);
    DP_PBUF_SET_WID(pbuf, (uint8_t)NETDEV_GetTxWid(PBUF_GET_DEV(pbuf), 0));
    PBUF_SET_PKT_TYPE(pbuf, flow->flowType);
    PBUF_SET_ENTRY(pbuf, PMGR_ENTRY_ROUTE_OUT);
    PBUF_SET_FLOW(pbuf, flow);
}

static int UdpInitFlowWhenSend(Sock_t* sk, INET_FlowInfo_t* flow)
{
    int ret = 0;
    if ((ret = INET_InitFlowBySk(sk, UdpInetSk(sk), flow)) != 0) {
        flow->dst = 0;
        return ret;
    }

    if (flow->flowType == PBUF_PKTTYPE_BROADCAST && sk->broadcast == 0) {
        INET_DeinitFlow(flow);
        DP_ADD_ABN_STAT(DP_UDP_FLOW_BROADCAST);
        ret = -ENETUNREACH;
    }
    return ret;
}

static int UdpUpdateFlowWhenSend(Sock_t* sk, INET_FlowInfo_t* flow)
{
    int ret = INET_UpdateFlow(flow);
    if (flow->flowType == PBUF_PKTTYPE_BROADCAST && sk->broadcast == 0) {
        INET_DeinitFlow(flow);
        DP_ADD_ABN_STAT(DP_UDP_FLOW_BROADCAST);
        return -ENETUNREACH;
    }
    return ret;
}

static int UdpInetFlowFill(Sock_t* sk, const struct DP_Msghdr* msg, INET_FlowInfo_t** flow, int* flowCached)
{
    int ret = 0;
    if (msg->msg_name != NULL) {
        struct DP_SockaddrIn* addrIn = (struct DP_SockaddrIn*)msg->msg_name;

        if ((ret = INET_CheckAddr((struct DP_Sockaddr*)addrIn, msg->msg_namelen)) != 0) {
            DP_ADD_ABN_STAT(DP_UDP_CHECK_DST_ADDR_ERR);
            return ret;
        }

        if (SOCK_IS_CONNECTED(sk)) {
            if (UdpInetSk(sk)->flow.dst != addrIn->sin_addr.s_addr) {
                DP_ADD_ABN_STAT(DP_UDP_SND_ADDR_INVAL);
                return -EINVAL;         // UDP已经connect，sendto传入的地址不能与connect的地址不同
            }
            goto update;
        }
        // 未connect 首次带地址send
        if ((*flow)->dst == 0 && (*flow)->rt == NULL) {
            (*flow)->dst = addrIn->sin_addr.s_addr;
            goto initFlow;
        }
        //  未connect 再次带地址send
        if ((*flow)->dst == addrIn->sin_addr.s_addr) {
            goto update;
        } else {
            INET_DeinitFlow(*flow);
            (*flow)->dst = addrIn->sin_addr.s_addr;
            goto initFlow;
        }
    } else if (SOCK_IS_CONNECTED(sk)) {
        goto update;
    } else {
        DP_ADD_ABN_STAT(DP_UDP_SND_NO_DST);
        return -EDESTADDRREQ;
    }

initFlow:
    return UdpInitFlowWhenSend(sk, *flow);

update:
    *flow       = &UdpInetSk(sk)->flow;
    *flowCached = 1;
    return UdpUpdateFlowWhenSend(sk, *flow);
}

static ssize_t UdpInetSendmsg(Sock_t* sk, const struct DP_Msghdr* msg, int flags, size_t msgDataLen,
                              size_t* index, size_t* offset)
{
    ssize_t          ret;
    InetSk_t*        inetSk = UdpInetSk(sk);
    INET_FlowInfo_t* flow   = &inetSk->flow;
    uint16_t         dport;
    int              flowCached = 0;
    Pbuf_t*          pbuf;

    (void)index;
    (void)offset;

    if (msgDataLen > (size_t)UDP_MAX_MSG_LEN) {
        DP_ADD_ABN_STAT(DP_UDP_SND_LONG);
        return -EMSGSIZE;
    }

    if ((ret = UdpInetFlowFill(sk, msg, &flow, &flowCached)) != 0) {
        return ret;
    }

    if (flow->rt == NULL && sk->bindDev == 0) {
        DP_ADD_ABN_STAT(DP_UDP_SND_NO_RT);
        return -ENETUNREACH;
    }

    if ((INET_GetDevByFlow(flow)->ifflags & DP_IFF_UP) == 0) {
        DP_ADD_ABN_STAT(DP_UDP_SND_DEV_DOWN);
        return -ENETDOWN;
    }

    if (flags != 0 && flags != DP_MSG_DONTWAIT) {
        DP_ADD_ABN_STAT(DP_UDP_SND_FLAGS_NO_SUPPORT);
        return -EOPNOTSUPP;
    }

    if (inetSk->hashinfo.lport == 0) {
        if ((ret = UdpInetAutoBind(sk, inetSk)) != 0) {
            return ret;
        }
    }

    dport = msg->msg_name != NULL ? ((struct DP_SockaddrIn*)(msg->msg_name))->sin_port : inetSk->hashinfo.pport;

    pbuf = SOCK_PbufBuildFromMsg(msg, SOCK_INET_HEADROOM);
    if (pbuf == NULL) {
        ret = -ENOMEM;
        goto err;
    }

    ret = (ssize_t)PBUF_GET_PKT_LEN(pbuf);

    UdpInetPktFill(sk, flow, dport, pbuf);

    DP_INC_PKT_STAT(pbuf->wid, DP_PKT_UDP_OUT);
    PMGR_Dispatch(pbuf);
    return ret;

err:

    if (flowCached == 0) {
        INET_DeinitFlow(flow);
    }

    return ret;
}

static int UdpInetClose(Sock_t* sk)
{
    UdpHashTbl_t* tbl = UdpHashGetTbl(sk->net);

    if (UdpInetSk(sk)->hashinfo.lport != 0) {
        SPINLOCK_Lock(&tbl->lock);
        UdpInetRemove(tbl, sk, UdpInetSk(sk));
        SPINLOCK_Unlock(&tbl->lock);
    }

    UdpHashWaitTblIdle(UdpHashGetTbl(sk->net));

    INET_DeinitFlow(&UdpInetSk(sk)->flow);
    SOCK_Unlock(sk);
    SOCK_DeinitSk(sk);

    MEMPOOL_FREE(g_udpMemPool, sk);
    (void)UTILS_DecCbCnt(&g_udpCbCnt);
    DP_LOG_INFO("Udp socket closed.");

    return 0;
}

int UdpSetSockopt(Sock_t* sk, int level, int optName, const void* optVal, DP_Socklen_t optLen)
{
    if (level == DP_IPPROTO_IP) {
        if (sk->family != DP_AF_INET) {
            return -EINVAL;
        }
        return INET_Setsockopt(UdpInetSk(sk), optName, optVal, optLen);
    }

    return -ENOPROTOOPT;
}

int UdpGetSockopt(Sock_t* sk, int level, int optName, void* optVal, DP_Socklen_t* optLen)
{
    if (level == DP_IPPROTO_IP) {
        if (sk->family != DP_AF_INET) {
            DP_LOG_DBG("UdpGetSockopt failed, level is DP_IPPROTO_IP, sk->family = %d.", sk->family);
            return -EINVAL;
        }
        return INET_Getsockopt(UdpInetSk(sk), optName, optVal, optLen);
    }

    DP_LOG_DBG("UdpGetSockopt failed, level not support, level = %d.", level);
    return -ENOPROTOOPT;
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

static void UdpInetShowInfo(Sock_t* sk)
{
    InetSk_t* inetSk = UdpInetSk(sk);
    INET_ShowInfo(inetSk);
}

static void UdpInetGetState(Sock_t* sk, DP_SocketState_t* state)
{
    InetSk_t* inetSk = UdpInetSk(sk);
    state->pf = DP_PF_INET;
    state->proto = inetSk->hashinfo.protocol;
    state->lAddr4 = inetSk->hashinfo.laddr;
    state->lPort = inetSk->hashinfo.lport;
    state->rAddr4 = inetSk->hashinfo.paddr;
    state->rPort = inetSk->hashinfo.pport;
    state->state = DP_SOCKET_STATE_INVALID;
    state->workerId = (uint32_t)sk->wid;
}

static void UdpInetGetDetails(Sock_t* sk, DP_SockDetails_t* details)
{
    InetSk_t* inetSk = UdpInetSk(sk);
    INET_GetDetails(inetSk, &details->inetDetails);
}

static void UdpCMsgRecvProc(Sock_t* sk, struct DP_Msghdr* msg)
{
    uint32_t offset = 0;
    InetSk_t *inetSk = UdpInetSk(sk);
    if (msg->msg_control == NULL || msg->msg_controllen <= DP_CMSG_ALIGN(sizeof(struct DP_Cmsghdr))) {
        return;
    }
    if (sk->isTimestamp == 1) {
        if ((msg->msg_controllen - offset) < (DP_CMSG_ALIGN(sizeof(struct DP_Cmsghdr)) +
            DP_CMSG_ALIGN(sizeof(struct DP_Timeval)))) {
            return;
        }
        // uint32_t -> DP_Timeval
        struct DP_Timeval timeInfo = {0};
        timeInfo.tv_sec = sk->skTimestamp / MSEC_PER_SEC;
        timeInfo.tv_usec = (sk->skTimestamp % MSEC_PER_SEC) * USEC_PER_MSEC;
        // 构造DP_Cmsghdr头
        struct DP_Cmsghdr cmsgHdr = {0};
        cmsgHdr.cmsg_type = DP_SO_TIMESTAMP;
        cmsgHdr.cmsg_level = DP_SOL_SOCKET;
        cmsgHdr.cmsg_len = DP_CMSG_ALIGN(sizeof(struct DP_Timeval)) + DP_CMSG_ALIGN(sizeof(struct DP_Cmsghdr));
        // 拷贝sk->skTimestamp信息到msg->msg_control内
        (void)memcpy_s((uint8_t *)msg->msg_control + offset, msg->msg_controllen - offset, (uint8_t *)&cmsgHdr,
            sizeof(struct DP_Cmsghdr));
        offset += DP_CMSG_ALIGN(sizeof(struct DP_Cmsghdr));
        (void)memcpy_s((uint8_t *)msg->msg_control + offset, msg->msg_controllen - offset, (uint8_t *)&timeInfo,
            sizeof(struct DP_Timeval));
        offset += DP_CMSG_ALIGN(sizeof(struct DP_Timeval));
    }
    if (inetSk->options.rcvErr == 1) {
        if ((msg->msg_controllen - offset) < (DP_CMSG_ALIGN(sizeof(struct DP_Cmsghdr)) +
            DP_CMSG_ALIGN(sizeof(struct DP_SockExtendedErr)))) {
            return;
        }
        // 构造DP_Cmsghdr头
        struct DP_Cmsghdr cmsgHdr = {0};
        cmsgHdr.cmsg_type = DP_IP_RECVERR;
        cmsgHdr.cmsg_level = DP_SOL_IP;
        cmsgHdr.cmsg_len = DP_CMSG_ALIGN(sizeof(struct DP_SockExtendedErr)) + DP_CMSG_ALIGN(sizeof(struct DP_Cmsghdr));
        (void)memcpy_s((uint8_t *)msg->msg_control + offset, msg->msg_controllen - offset, (uint8_t *)&cmsgHdr,
            sizeof(struct DP_Cmsghdr));
        offset += DP_CMSG_ALIGN(sizeof(struct DP_Cmsghdr));
        (void)memcpy_s((uint8_t *)msg->msg_control + offset, msg->msg_controllen - offset,
                       (uint8_t *)&inetSk->ipErrInfo, sizeof(struct DP_SockExtendedErr));
        offset += DP_CMSG_ALIGN(sizeof(struct DP_SockExtendedErr));
    }
    // 其他CMsg信息获取，待补充
}

static ssize_t UdpRecvmsg(Sock_t* sk, struct DP_Msghdr* msg, int flags, size_t msgDataLen)
{
    ssize_t ret = SOCK_PopRcvBufByPkt(sk, msg, flags, msgDataLen);
    UdpCMsgRecvProc(sk, msg);
    return ret;
}

static SOCK_Ops_t g_udpOps = {
    .type       = DP_SOCK_DGRAM,
    .protocol   = DP_IPPROTO_UDP,
    .shutdown   = NULL,
    .close      = UdpInetClose,
    .bind       = UdpInetBind,
    .listen     = NULL,
    .accept     = NULL,
    .connect    = UdpInetConnect,
    .setsockopt = UdpSetSockopt,
    .getsockopt = UdpGetSockopt,
    .keepalive  = NULL,
    .sendmsg    = UdpInetSendmsg,
    .recvmsg    = UdpRecvmsg,

    .getDstAddr = UdpInetGetDstAddr,
    .getAddr    = UdpInetGetAddr,
    .showInfo   = UdpInetShowInfo,
    .getState   = UdpInetGetState,
    .getDetails = UdpInetGetDetails,
};

static Sock_t* UdpInetAllocSk()
{
    Sock_t* sk;
    size_t  objSize = sizeof(UdpInetSk_t);

    objSize = SOCK_GetSkSize(objSize);

    sk = MEMPOOL_ALLOC(g_udpMemPool);
    if (sk == NULL) {
        DP_LOG_ERR("Malloc memory failed for udp sk.");
        DP_ADD_ABN_STAT(DP_UDP_CREATE_MEM_ERR);
        return NULL;
    }

    (void)memset_s(sk, objSize, 0, objSize);

    if (SOCK_InitSk(sk, NULL, objSize) != 0) {
        MEMPOOL_FREE(g_udpMemPool, sk);
        return NULL;
    }

    return sk;
}

static int UdpInetSkCreate(NS_Net_t* net, int type, int protocol, Sock_t** out)
{
    Sock_t* sk;

    (void)type;

    if ((type != DP_SOCK_DGRAM) || (protocol != 0 && protocol != DP_IPPROTO_UDP)) {
        DP_ADD_ABN_STAT(DP_UDP_CREATE_INVAL);
        return -EPROTONOSUPPORT;
    }

    // 判断UDPCB是否到达上限
    if (UTILS_IncCbCnt(&g_udpCbCnt, (uint32_t)CFG_GET_VAL(DP_CFG_UDPCB_MAX)) != 0) {
        DP_LOG_INFO("The num of udpSk exceed udpCbMax configured.");
        DP_ADD_ABN_STAT(DP_UDP_CREATE_FULL);
        return -EMFILE;
    }

    sk = UdpInetAllocSk();
    if (sk == NULL) {
        (void)UTILS_DecCbCnt(&g_udpCbCnt);
        return -ENOMEM;
    }

    sk->net    = net;
    sk->ops    = &g_udpOps;
    sk->family = DP_AF_INET;
    SOCK_SET_WRITABLE(sk);
    UdpInetSk(sk)->hashinfo.protocol = DP_IPPROTO_UDP;

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

    ret = PBUF_Build((uint8_t*)ip, PBUF_GET_SEG_LEN(orig), SOCK_INET_HEADROOM + sizeof(DP_IcmpHdr_t));
    if (ret == NULL) {
        return NULL;
    }

    PBUF_PUT_HEAD(ret, sizeof(*icmp));
    icmp = PBUF_MTOD(ret, DP_IcmpHdr_t*);

    icmp->type  = DP_ICMP_TYPE_DEST_UNREACH;
    icmp->code  = DP_ICMP_PORT_UNREACH;
    icmp->cksum = 0;
    icmp->resv  = 0;

    icmp->cksum = UTILS_CksumSwap(PBUF_CalcCksumAcc(ret));

    PBUF_SET_DST_ADDR4(ret, ip->src);
    PBUF_SET_FLOW(ret, NULL);
    PBUF_SET_DEV(ret, PBUF_GET_DEV(orig));
    PBUF_SET_ENTRY(ret, PMGR_ENTRY_ROUTE_OUT);
    PBUF_SET_L4_TYPE(ret, DP_IPPROTO_ICMP);

    return ret;
}

int UdpInetErrInput(Pbuf_t* pbuf)
{
    UdpHashTbl_t*   tbl = UdpHashGetTbl(((Netdev_t*)PBUF_GET_DEV(pbuf))->net);
    DP_IpHdr_t*   ipHdr = (DP_IpHdr_t*)PBUF_GET_L3_HDR(pbuf);
    DP_UdpHdr_t*  udpHdr;
    INET_Hashinfo_t hi = {0};
    InetSk_t*       inetSk;

    if (PBUF_GET_SEG_LEN(pbuf) < sizeof(DP_UdpHdr_t)) {
        DP_INC_PKT_STAT(DP_PBUF_GET_WID(pbuf), DP_UDP_ICMP_UNREACH_SHORT);
        return -1;
    }
    udpHdr = PBUF_MTOD(pbuf, DP_UdpHdr_t*);

    hi.lport    = udpHdr->sport;
    hi.pport    = udpHdr->dport;
    hi.laddr    = ipHdr->src;
    hi.paddr    = ipHdr->dst;
    hi.protocol = DP_IPPROTO_UDP;
    hi.vpnid    = DP_PBUF_GET_VPNID(pbuf);

    UdpHashRefTbl(tbl);

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

    UdpHashDerefTbl(tbl);

    return 0;
}

static Pbuf_t* UdpInetInput(Pbuf_t* pbuf)
{
    Pbuf_t*        ret = NULL;
    DP_UdpHdr_t* udpHdr = PBUF_MTOD(pbuf, DP_UdpHdr_t*);
    Sock_t*        sk;
    InetSk_t*      inetSk;
    Netdev_t*      dev = (Netdev_t*)PBUF_GET_DEV(pbuf);
    UdpHashTbl_t*  tbl = UdpHashGetTbl(dev->net);

    if (PreCheckUdp(pbuf, udpHdr) != 0) {
        NET_DEV_ADD_RX_ERRS(NETDEV_GetRxQue(dev, PBUF_GET_QUE_ID(pbuf)), 1);
        goto drop;
    }

    UdpHashRefTbl(tbl);

    inetSk = UdpInetLookupByPkt(tbl, pbuf, udpHdr);
    UdpHashDerefTbl(tbl);
    if (inetSk == NULL) {
        ret = UdpGenPortUnreachable(pbuf);
        goto drop;
    }
    sk = UdpInetSk2Sk(inetSk);
    // 时间戳选项开启
    if (sk->isTimestamp == 1) {
        sk->skTimestamp = UTILS_TimeNow();
    }

    if (PBUF_GET_PKT_TYPE(pbuf) == PBUF_PKTTYPE_BROADCAST) {
        if (sk->broadcast == 0) { // 如果是广播报文
            goto drop;
        }
    }

    PBUF_SET_L4_OFF(pbuf);
    PBUF_CUT_HEAD(pbuf, sizeof(*udpHdr));
    PBUF_SET_IFINDEX(pbuf, dev->ifindex);
    if (SOCK_PushRcvBufSafe(sk, pbuf) < 0) {
        NET_DEV_ADD_RX_DROP(NETDEV_GetRxQue(dev, PBUF_GET_QUE_ID(pbuf)), 1);
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

static int UdpMemPoolInit(void)
{
    size_t objSize = sizeof(UdpInetSk_t);
    objSize = SOCK_GetSkSize(objSize);

    DP_MempoolCfg_S mpCfg = {0};
    mpCfg.name = g_udpMpName;
    mpCfg.count = (uint32_t)CFG_GET_VAL(DP_CFG_UDPCB_MAX);
    mpCfg.size = objSize;
    mpCfg.type = DP_MEMPOOL_TYPE_FIXED_MEM;

    int32_t mod = MOD_UDP;
    DP_MempoolAttr_S attr = (void*)&mod;
    return MEMPOOL_CREATE(&mpCfg, &attr, &g_udpMemPool);
}

int UdpInetInit(int slave)
{
    (void)slave;

    SOCK_AddProto(DP_AF_INET, &g_udpProtoOps);
    PMGR_AddEntry(PMGR_ENTRY_UDP_IN, UdpInetInput);
    g_icmpErrMsg.icmpUnreach = UdpInetErrInput;

    NS_SetNetOps(NS_NET_UDP, UdpHashAllocTbl, UdpHashFreeTbl);

    ATOMIC32_Store(&g_udpCbCnt, 0);

    /* DP_MEMPOOL_TYPE_FIXED_MEM 类型的内存池创建失败时会使用MEM_MALLOC代替 */
    if (UdpMemPoolInit() != 0) {
        DP_LOG_INFO("Udp mempool init failed.");
    }

    return 0;
}

void UdpInetDeinit(int slave)
{
    (void)slave;

    ATOMIC32_Store(&g_udpCbCnt, 0);

    if (g_udpMemPool != NULL) {
        MEMPOOL_DESTROY(g_udpMemPool);
        g_udpMemPool = NULL;
    }
}
