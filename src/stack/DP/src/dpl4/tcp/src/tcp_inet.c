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

#include "tcp_inet.h"

#include <securec.h>

#include "dp_tcp.h"
#include "worker.h"
#include "worker.h"
#include "shm.h"
#include "pmgr.h"
#include "utils_sha256.h"
#include "netdev.h"
#include "utils_sha256.h"
#include "utils_cfg.h"
#include "utils_log.h"
#include "utils_spinlock.h"
#include "sock_addr_ext.h"

#include "tcp_timer.h"
#include "tcp_types.h"
#include "tcp_sock.h"
#include "tcp_inethash.h"
#include "tcp_tsq.h"
#include "tcp_in.h"
#include "tcp_cookie.h"
#include "tcp_out.h"
#include "tcp_sack.h"
#include "tcp_inet.h"

#define SHA256_HASH_DIGGESET_LEN 32

#define BACKLOG_QUEUE_DEFAULT 4
#define BACKLOG_QUEUE_MAXCONN 4096

static SOCK_Ops_t g_tcpInetSkOps;

TcpCfgCtx_t g_tcpCfgCtx = {0};

static Pbuf_t* TcpInetGenRstPkt(Pbuf_t* pbuf, DP_TcpHdr_t* origTcpHdr, TcpPktInfo_t* pi);

static inline void TcpInetUpdateMss(TcpSk_t* tcp, Netdev_t* dev)
{
    uint16_t mss = dev->mtu - sizeof(DP_IpHdr_t) - sizeof(DP_TcpHdr_t);

    if (tcp->mss == 0 || tcp->mss > mss) {
        tcp->mss = mss;
    }
}

static int CheckTcpCfgFree(void)
{
    SPINLOCK_Lock(&g_tcpCfgCtx.lock);
    if (g_tcpCfgCtx.freeCnt <= 0) {
        SPINLOCK_Unlock(&g_tcpCfgCtx.lock);
        DP_LOG_INFO("The num of tcpSk exceed tcpCbMax configured.");
        return -1;
    }
    SPINLOCK_Unlock(&g_tcpCfgCtx.lock);
    return 0;
}

static Sock_t* TcpInetAllocSk(Sock_t* parent)
{
    Sock_t *sk;

    size_t  objSize = sizeof(TcpSk_t) + sizeof(InetSk_t);
    objSize = SOCK_GetSkSize(objSize);

    sk = SHM_MALLOC(objSize, MOD_TCP, DP_MEM_FREE);
    if (sk == NULL) {
        DP_LOG_ERR("Malloc memory failed for tcp sk.");
        return NULL;
    }
    SPINLOCK_Lock(&g_tcpCfgCtx.lock);
    g_tcpCfgCtx.freeCnt--;
    SPINLOCK_Unlock(&g_tcpCfgCtx.lock);

    (void)memset_s(sk, objSize, 0, objSize);

    SOCK_InitSk(sk, parent, objSize);

    return sk;
}

static void TcpInetFreeSk(Sock_t* sk)
{
    TcpSk_t *tcp = TcpSK(sk);
    PBUF_ChainClean(&tcp->reassQue);
    PBUF_ChainClean(&tcp->rexmitQue);
    DP_ADD_PKT_STAT(tcp->wid, DP_PKT_SEND_BUF_FREE, tcp->sndQue.pktCnt);
    DP_ADD_PKT_STAT(tcp->wid, DP_PKT_RECV_BUF_FREE, tcp->rcvQue.pktCnt);
    PBUF_ChainClean(&tcp->sndQue);
    PBUF_ChainClean(&tcp->rcvQue);

    TcpDeinitSackInfo(tcp->sackInfo);

    InetSk_t* inetSk = TcpInetSk(tcp);
    INET_DeinitFlow(&inetSk->flow);

    SOCK_DeinitSk(sk);
    SHM_FREE(sk, DP_MEM_FREE);

    SPINLOCK_Lock(&g_tcpCfgCtx.lock);
    g_tcpCfgCtx.freeCnt++;
    SPINLOCK_Unlock(&g_tcpCfgCtx.lock);
}

static int TcpGetDstAddr(Sock_t* sk, Pbuf_t* pbuf, struct DP_Sockaddr* addr, DP_Socklen_t* addrlen)
{
    (void)pbuf;

    if ((int)*addrlen < 0) {
        return -EINVAL;
    }

    return INET_GetAddr(TcpInetSk(sk), addr, addrlen, 1);
}

static int TcpGetAddr(Sock_t* sk, struct DP_Sockaddr* addr, DP_Socklen_t* addrlen, int peer)
{
    return INET_GetAddr(TcpInetSk(sk), addr, addrlen, peer);
}

static int TcpInetBind(Sock_t* sk, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen)
{
    int             ret;
    InetSk_t*       inetSk = TcpInetSk(sk);
    INET_Hashinfo_t cur;
    TcpInetTbl_t*   tbl = TcpInetGetTbl(sk->net);

    if (SOCK_IS_BINDED(sk)) { // TCP不允许重复绑定
        return -EINVAL;
    }

    if (SOCK_IS_SHUTRD(sk) || SOCK_IS_SHUTWR(sk)) {
        return -EINVAL;
    }

    if ((ret = INET_Bind(sk, inetSk, addr, addrlen, &cur)) != 0) {
        return ret;
    }

    TcpInetLockTbl(tbl);

    if (cur.lport == 0) {
        if (TcpInetGenPort(tbl, 1, &cur, 0) == 0) {
            ret = 0;
        } else if (SOCK_CAN_REUSE(sk) && TcpInetGenPort(tbl, 1, &cur, 1) == 0) {
            ret = 0;
        } else {
            ret = -EADDRINUSE;
        }
    } else if (TcpInetCanBind(tbl, &cur, SOCK_CAN_REUSE(sk)) != 0) {
        ret = 0;
    } else {
        ret = -EADDRINUSE;
    }

    if (ret == 0) {
        inetSk->hashinfo = cur;
        TcpInetGlobalInsert(sk);
    }

    TcpInetUnlockTbl(tbl);

    return ret;
}

static int TcpInetListen(Sock_t* sk, int backlog)
{
    if (SOCK_IS_CONNECTED(sk) || SOCK_IS_CONNECTING(sk)) {
        return -EINVAL;
    }

    if (!SOCK_IS_BINDED(sk)) {
        return -EDESTADDRREQ;
    }

    if (SOCK_IS_LISTENED(sk)) {
        return 0;
    }

    SOCK_SET_LISTENED(sk);

    if (backlog < BACKLOG_QUEUE_DEFAULT) {
        TcpSK(sk)->backlog = BACKLOG_QUEUE_DEFAULT;
    } else if (backlog > BACKLOG_QUEUE_MAXCONN) {
        TcpSK(sk)->backlog = BACKLOG_QUEUE_MAXCONN;
    } else {
        TcpSK(sk)->backlog = backlog;
    }

    TcpSetState(TcpSK(sk), TCP_LISTEN);

    TcpInetListenerInsertSafe(sk);

    return 0;
}

static int TcpTryInsertConnectTbl(Sock_t *sk)
{
    TcpInetTbl_t* tbl = TcpInetGetTbl(sk->net);
    InetSk_t* inetSk = TcpInetSk(sk);

    TcpInetLockTbl(tbl);
    if (inetSk->hashinfo.lport == 0) {
        if (TcpInetGenPort(tbl, 0, &inetSk->hashinfo, 0) != 0) {
            TcpInetUnlockTbl(tbl);
            return -EADDRNOTAVAIL;
        }
    } else if (TcpInetCanConnect(tbl, &inetSk->hashinfo) == 0) {
        TcpInetUnlockTbl(tbl);
        return -EADDRINUSE;
    } else {
        TcpInetGlobalRemove(sk);
    }

    TcpSetLport(TcpSK(sk), inetSk->hashinfo.lport);
    TcpSetPport(TcpSK(sk), inetSk->hashinfo.pport);
    TcpInetConnectTblInsert(sk);

    TcpInetUnlockTbl(tbl);
    return 0;
}

static void TcpInitIss(TcpSk_t* tcp)
{
    tcp->iss = TcpInetGenIsn(TcpSk2Sk(tcp));
    tcp->sndUna = tcp->iss;
    tcp->sndNxt = tcp->iss;
    tcp->sndMax = tcp->iss;
}

static int TcpInetConnect(Sock_t* sk, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen)
{
    int              ret;
    NETDEV_IfAddr_t* ifaddr;
    InetSk_t*        inetSk = TcpInetSk(sk);

    if ((ret = TcpCanConnect(sk)) != 0) {
        return ret;
    }

    if ((ret = INET_Connect(sk, inetSk, addr, addrlen)) != 0) {
        return ret;
    }

    if (inetSk->flow.rt == NULL) {
        INET_DeinitFlow(&inetSk->flow);
        return -EINVAL;
    }

    if (TcpInetDevIsUp(sk) != 0) {
        return -ENETDOWN;
    }

    ifaddr = inetSk->flow.rt->ifaddr;

    if (inetSk->hashinfo.laddr != DP_INADDR_ANY && ifaddr->local != inetSk->hashinfo.laddr) {
        INET_DeinitFlow(&inetSk->flow);
        return -EINVAL;
    }

    inetSk->hashinfo.pport = ((struct DP_SockaddrIn*)addr)->sin_port;
    inetSk->hashinfo.paddr = inetSk->flow.dst;
    inetSk->hashinfo.laddr = inetSk->flow.src;

    if ((ret = TcpTryInsertConnectTbl(sk)) != 0) {
        INET_DeinitFlow(&inetSk->flow);
        return ret;
    }

    TcpSK(sk)->txQueid = 0;
    TcpSK(sk)->wid = NETDEV_GetRxWid(ifaddr->dev, 0);
    TcpSK(sk)->pseudoHdrCksum = INET_CalcPseudoCksum(&inetSk->hashinfo);
    TcpInitIss(TcpSK(sk));

    TcpInetUpdateMss(TcpSK(sk), ifaddr->dev);

    SOCK_SET_CONNECTING(sk);
    DP_INC_TCP_STAT(TcpSK(sk)->wid, DP_TCP_CONN_ATTEMPT);

    TcpTsqAddLockQue(TcpSK(sk), TCP_TSQ_CONNECT);

    return 0;
}

static int TcpShutdown(Sock_t* sk, int how)
{
    if (how == DP_SHUT_RD || how == DP_SHUT_RDWR) {
        SOCK_SET_READABLE(sk);
        while (sk->rdSemCnt > 0) {
            SOCK_WakeupRdSem(sk);
        }
    }

    if (how == DP_SHUT_WR || how == DP_SHUT_RDWR) {
        SOCK_SET_WRITABLE(sk);
        while (sk->wrSemCnt > 0) {
            SOCK_WakeupWrSem(sk);
        }
        TcpTsqAddLockQue(TcpSK(sk), TCP_TSQ_DISCONNECT);
    }
    return 0;
}

static SOCK_Ops_t g_tcpInetSkOps = {
    .shutdown   = TcpShutdown,
    .close      = TcpClose,
    .bind       = TcpInetBind,
    .listen     = TcpInetListen,
    .accept     = TcpAccept,
    .connect    = TcpInetConnect,
    .setsockopt = TcpSetSockOpt,
    .getsockopt = TcpGetSockOpt,
    .keepalive  = TcpSetKeepAlive,
    .sendmsg    = TcpSendmsg,
    .recvmsg    = TcpRecvmsg,

    .getDstAddr = TcpGetDstAddr,
    .getAddr    = TcpGetAddr,
};

static int TcpInetCreateSk(NS_Net_t* net, int type, int protocol, Sock_t** out)
{
    Sock_t* sk;

    (void)type;

    if (protocol != 0 && protocol != DP_IPPROTO_TCP) {
        return -EPROTONOSUPPORT;
    }

    if (CheckTcpCfgFree() != 0) {
        return -EMFILE;
    }

    sk = TcpInetAllocSk(NULL);
    if (sk == NULL) {
        return -ENOMEM;
    }

    sk->net    = net;
    sk->ops    = &g_tcpInetSkOps;
    sk->family = DP_AF_INET;
    sk->ref    = 1;

    TcpInetSk(sk)->hashinfo.protocol = DP_IPPROTO_TCP;
    TcpInitTcpSk(TcpSK(sk));

    *out = sk;

    return 0;
}

static int TcpInitInetSk(DP_TcpHdr_t* tcpHdr, DP_IpHdr_t* ipHdr, Sock_t* newsk)
{
    InetSk_t* inetSk = TcpInetSk(newsk);
    inetSk->hashinfo.lport    = tcpHdr->dport;
    inetSk->hashinfo.pport    = tcpHdr->sport;
    inetSk->hashinfo.laddr    = ipHdr->dst;
    inetSk->hashinfo.paddr    = ipHdr->src;
    inetSk->hashinfo.protocol = ipHdr->type;

    inetSk->flow.dst       = inetSk->hashinfo.paddr;
    if (INET_InitFlowBySk(newsk, inetSk, &inetSk->flow) != 0 || inetSk->flow.src != inetSk->hashinfo.laddr) {
        return -1;
    }

    return 0;
}

static TcpSk_t* TcpInetCreateChildSk(Sock_t* parent, Pbuf_t* pbuf, DP_TcpHdr_t* tcpHdr, uint8_t *isNeedRst)
{
    DP_IpHdr_t* ipHdr = (DP_IpHdr_t*)PBUF_GET_L3_HDR(pbuf);
    Netdev_t*     dev   = (Netdev_t*)PBUF_GET_DEV(pbuf);

    if (TcpSK(parent)->backlog <= TcpSK(parent)->childCnt) {
        return NULL;
    }

    // 通知地址信息，失败时不能建链
    struct DP_SockaddrIn addrIn = {
        .sin_family = DP_AF_INET,
        .sin_addr.s_addr = ipHdr->dst,
        .sin_port = tcpHdr->dport
    };
    DP_LOG_DBG("TCP addr notify create.");

    // 内核行为，如果已经达到socket资源的最大值，则服务端会回复RST报文，且返回EMFILE错误
    if (CheckTcpCfgFree() != 0) {
        parent->error = EMFILE;
        SOCK_WakeupRdSem(parent);
        *isNeedRst = 1;
        return NULL;
    }

    if (SOCK_AddrEventNotify(DP_ADDR_EVENT_CREATE, DP_IPPROTO_TCP,
        (struct DP_Sockaddr*)&addrIn, sizeof(addrIn)) != 0) {
        return NULL;
    }

    Sock_t* newsk = TcpInetAllocSk(parent);
    if (newsk == NULL) {
        return NULL;
    }

    TcpSk_t* newtcp = TcpSK(newsk);
    TcpInitChildTcpSk(newsk, parent, pbuf, tcpHdr);
    if (TcpInitInetSk(tcpHdr, ipHdr, newsk) != 0) {
        TcpInetFreeSk(newsk);
        return NULL;
    }

    TcpSetLport(newtcp, tcpHdr->dport);
    TcpSetPport(newtcp, tcpHdr->sport);
    TcpSetState(newtcp, TCP_SYN_RECV);

    TcpInitIss(newtcp);
    newtcp->pseudoHdrCksum = INET_CalcPseudoCksum(&TcpInetSk(newtcp)->hashinfo);

    // 调整mss
    TcpInetUpdateMss(newtcp, dev);
    if (TcpInetPerWorkerTblInsert(newsk) != 0 || TcpInetConnectTblInsertSafe(newsk) != 0) {
        TcpInetFreeSk(newsk);
        return NULL;
    }

    LIST_INSERT_TAIL(&TcpSK(parent)->uncomplete, newtcp, childNode);
    TcpSK(parent)->childCnt++;
    SOCK_Ref(TcpSk2Sk(parent));

    return newtcp;
}

static Pbuf_t* TcpGenCookieSynAckPkt(Pbuf_t* pbuf, TcpSk_t* parent, TcpPktInfo_t* pi, TcpSynOpts_t* synOpts,
                                     TcpCookieInetHashInfo_t* ci)
{
    Pbuf_t*         ret;
    DP_TcpHdr_t*    tcpHdr;
    INET_Hashinfo_t hi;
    DP_IpHdr_t*     ipHdr = (DP_IpHdr_t*)PBUF_GET_L3_HDR(pbuf);
    uint32_t        cksum;

    ret = TcpGenCookieSynAckPktByPkt(pbuf, parent, pi, synOpts, ci);
    if (ret == NULL) {
        return ret;
    }

    PBUF_SET_L4_TYPE(ret, DP_IPPROTO_TCP);
    PBUF_SET_ENTRY(ret, PMGR_ENTRY_ROUTE_OUT);
    PBUF_SET_WID(ret, PBUF_GET_WID(pbuf));
    PBUF_SET_QUE_ID(ret, NETDEV_GetTxQueid(PBUF_GET_DEV(pbuf), PBUF_GET_QUE_ID(pbuf)));
    PBUF_SET_PKT_TYPE(ret, PBUF_PKTTYPE_HOST);
    PBUF_SET_DEV(ret, PBUF_GET_DEV(pbuf));
    PBUF_SET_FLOW(ret, NULL);
    PBUF_SET_DST_ADDR(ret, PBUF_GET_DST_ADDR(pbuf));

    hi.laddr    = ipHdr->dst;
    hi.paddr    = ipHdr->src;
    hi.protocol = DP_IPPROTO_TCP;
    cksum = INET_CalcPseudoCksum(&hi);

    tcpHdr         = PBUF_MTOD(ret, DP_TcpHdr_t*);
    tcpHdr->chksum = TcpCalcTxCksum(cksum, ret);

    return ret;
}

static Pbuf_t* TcpInetProcCookie(TcpSk_t* tcp, Pbuf_t* pbuf, DP_TcpHdr_t* tcpHdr, TcpPktInfo_t* pi,
                                 TcpSynOpts_t* synOpts)
{
    TcpCookieInetHashInfo_t info = { 0 };
    uint16_t mss;
    DP_IpHdr_t* ipHdr = (DP_IpHdr_t*)PBUF_GET_L3_HDR(pbuf);

    info.laddr = ipHdr->src;
    info.paddr = ipHdr->dst;
    info.sport = tcpHdr->sport;
    info.dport = tcpHdr->dport;

    if ((synOpts->rcvSynOpt & TCP_SYN_OPT_MSS) == 0) {
        mss = TCP_DEF4_MSS;
    } else {
        mss = g_TcpMssTable[TcpCookieGetMssIndex((synOpts->mss < tcp->mss) ? synOpts->mss : tcp->mss)];
    }

    TcpCookieCalcInetIss(pi, &info, mss);

    synOpts->mss = mss;
    Pbuf_t* ret = TcpGenCookieSynAckPkt(pbuf, tcp, pi, synOpts, &info);

    PBUF_Free(pbuf);

    return ret;
}

static Pbuf_t* TcpInetProcChild(TcpSk_t* tcp, Pbuf_t* pbuf, DP_TcpHdr_t* tcpHdr, TcpPktInfo_t* pi)
{
    TcpSk_t*  child;
    TcpSynOpts_t synOpts;
    Pbuf_t *ret = NULL;
    uint8_t isNeedRst = 0;

    PBUF_SET_L4_OFF(pbuf);
    PBUF_CUT_HEAD(pbuf, pi->hdrLen);

    if ((pi->thFlags & DP_TH_RST) != 0) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_DROP_CONTROL_PKTS);
        goto drop;
    }

    if ((pi->thFlags & DP_TH_ACK) != 0) {
        ret = TcpInetGenRstPkt(pbuf, tcpHdr, pi);
        goto drop;
    }

    if (PBUF_GET_PKT_LEN(pbuf) != 0 || (pi->thFlags & 0x3F) != DP_TH_SYN) { // 暂不支持带数据的syn报文
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_DROP_CONTROL_PKTS);
        goto drop;
    }

    if (TcpParseSynOpts((uint8_t*)(tcpHdr + 1), pi->hdrLen - sizeof(DP_TcpHdr_t), &synOpts) != 0) {
        goto drop;
    }

    SOCK_Lock(TcpSk2Sk(tcp));

    if (CFG_GET_TCP_VAL(DP_CFG_TCP_COOKIE) == DP_ENABLE && TcpCheckCookie(tcp)) {
        ret = TcpInetProcCookie(tcp, pbuf, tcpHdr, pi, &synOpts);
        SOCK_Unlock(TcpSk2Sk(tcp));
        return ret;
    }

    child = TcpInetCreateChildSk(TcpSk2Sk(tcp), pbuf, tcpHdr, &isNeedRst);

    SOCK_Unlock(TcpSk2Sk(tcp));

    if (isNeedRst != 0) {
        ret = TcpInetGenRstPkt(pbuf, tcpHdr, pi);
        goto drop;
    } else if (child == NULL) {
        goto drop;
    }

    ret = TcpProcListen(child, &synOpts, pi);

drop:
    PBUF_Free(pbuf);

    return ret;
}

static TcpSk_t* TcpInetLookupByPkt(Pbuf_t* pbuf, DP_TcpHdr_t* tcpHdr)
{
    DP_IpHdr_t*     ipHdr;
    Netdev_t*       dev = PBUF_GET_DEV(pbuf);
    INET_Hashinfo_t hi;

    ipHdr = (DP_IpHdr_t*)PBUF_GET_L3_HDR(pbuf);

    hi.protocol = DP_IPPROTO_TCP;
    hi.paddr    = ipHdr->src;
    hi.laddr    = ipHdr->dst;
    hi.pport    = tcpHdr->sport;
    hi.lport    = tcpHdr->dport;

    return TcpInetLookup(dev->net, PBUF_GET_WID(pbuf), &hi);
}

static Pbuf_t* TcpInetGenRstPkt(Pbuf_t* pbuf, DP_TcpHdr_t* origTcpHdr, TcpPktInfo_t* pi)
{
    Pbuf_t*         ret;
    DP_TcpHdr_t*    tcpHdr;
    INET_Hashinfo_t hi;
    DP_IpHdr_t*     ipHdr = (DP_IpHdr_t*)PBUF_GET_L3_HDR(pbuf);
    uint32_t        cksum;

    ret = TcpGenRstPktByPkt(origTcpHdr, pi);
    if (ret == NULL) {
        return ret;
    }

    PBUF_SET_L4_TYPE(ret, DP_IPPROTO_TCP);
    PBUF_SET_ENTRY(ret, PMGR_ENTRY_ROUTE_OUT);
    PBUF_SET_QUE_ID(ret, NETDEV_GetTxQueid(PBUF_GET_DEV(pbuf), PBUF_GET_QUE_ID(pbuf)));
    PBUF_SET_WID(ret, PBUF_GET_WID(pbuf));
    PBUF_SET_PKT_TYPE(ret, PBUF_PKTTYPE_HOST);
    PBUF_SET_DEV(ret, PBUF_GET_DEV(pbuf));
    PBUF_SET_FLOW(ret, NULL);
    PBUF_SET_DST_ADDR(ret, PBUF_GET_DST_ADDR(pbuf));

    DP_INC_TCP_STAT(PBUF_GET_WID(pbuf), DP_TCP_SND_RST);
    DP_INC_TCP_STAT(PBUF_GET_WID(pbuf), DP_TCP_SND_CONTROL);
    DP_INC_TCP_STAT(PBUF_GET_WID(pbuf), DP_TCP_SND_TOTAL);

    hi.laddr    = ipHdr->dst;
    hi.paddr    = ipHdr->src;
    hi.protocol = DP_IPPROTO_TCP;
    cksum = INET_CalcPseudoCksum(&hi);

    tcpHdr         = PBUF_MTOD(ret, DP_TcpHdr_t*);
    tcpHdr->chksum = TcpCalcTxCksum(cksum, ret);

    return ret;
}

static TcpSk_t* TcpTryCreateCookieSk(TcpSk_t* tcp, Pbuf_t* pbuf, DP_TcpHdr_t* tcpHdr, TcpPktInfo_t* pi)
{
    TcpSk_t* newTcp;
    TcpSynOpts_t opts;
    uint16_t mss;
    DP_IpHdr_t* ipHdr = (DP_IpHdr_t*)PBUF_GET_L3_HDR(pbuf);
    uint8_t isNeedRst = 0;

    if (CFG_GET_TCP_VAL(DP_CFG_TCP_COOKIE) == DP_DISABLE ||
        (pi->thFlags & (DP_TH_ACK | DP_TH_SYN | DP_TH_RST)) != DP_TH_ACK) {
        // 当 cookie 关闭，报文中携带 SYN, 报文中携带 RST ，报文中没有 ACK ，不处理
        return NULL;
    }

    TcpCookieInetHashInfo_t info = { 0 };
    info.laddr = ipHdr->src;
    info.paddr = ipHdr->dst;
    info.sport = tcpHdr->sport;
    info.dport = tcpHdr->dport;

    if (!TcpCookieVerifyInetIss(pi, &info, &mss)) {
        return NULL;
    }

    if (TcpParseSynOpts((uint8_t*)(tcpHdr + 1), pi->hdrLen - sizeof(DP_TcpHdr_t), &opts) != 0) {
        return NULL;
    }

    // return 创建子socket
    SOCK_Lock(TcpSk2Sk(tcp));
    newTcp = TcpInetCreateChildSk(TcpSk2Sk(tcp), pbuf, tcpHdr, &isNeedRst);
    SOCK_Unlock(TcpSk2Sk(tcp));

    if (newTcp == NULL) {
        return NULL;
    }

    /* 这里是因为在触发cookie的时候已经生成过了一遍ISS 需要使用对端回复报文的ACK字段来对iss进行更新 */
    newTcp->iss    = pi->ack - 1;
    newTcp->sndUna = newTcp->iss;
    newTcp->sndNxt = newTcp->iss + 1;
    newTcp->sndMax = newTcp->iss + 1;

    newTcp->irs    = pi->seq - 1;
    newTcp->rcvNxt = newTcp->irs + 1;
    newTcp->rcvWup = newTcp->rcvNxt;
    newTcp->mss    = mss;
    newTcp->rcvWnd = TcpGetRcvSpace(newTcp); // 更新通告窗口
    // 从时间戳中解析选项
    if ((opts.rcvSynOpt & TCP_SYN_OPT_TIMESTAMP) != 0) {
        TcpCookieSetOpts(newTcp, &opts);
    }

    return newTcp;
}

static Pbuf_t* TcpInetInput(Pbuf_t* pbuf)
{
    DP_TcpHdr_t* tcpHdr;
    TcpPktInfo_t   pi;
    TcpSk_t*       tcp;
    Pbuf_t*        ret = NULL;

    DP_INC_PKT_STAT(PBUF_GET_WID(pbuf), DP_PKT_TCP_IN);
    tcpHdr = PBUF_MTOD(pbuf, DP_TcpHdr_t*);
    if (TcpInitPktInfo(pbuf, tcpHdr, &pi) != 0) {
        goto drop;
    }

    tcp = TcpInetLookupByPkt(pbuf, tcpHdr);
    if (tcp == NULL) {
        int wid = NETDEV_GetRxQueWid(PBUF_GET_DEV(pbuf), 0);
        // wid不同且没查找到，可能是主动建链场景，syn|ack报文被分流到其他worker
        if (PBUF_GET_WID(pbuf) != wid) {
            TcpTsqInetInsertBacklog(wid, pbuf);
        } else if ((pi.thFlags & DP_TH_RST) == 0) {
            // 无监听端口，且非RST报文，返回RST报文。否则直接丢弃
            ret = TcpInetGenRstPkt(pbuf, tcpHdr, &pi);
        }
        goto drop;
    }

    if (tcp->state == TCP_LISTEN) {
        TcpSk_t* newTcp = TcpTryCreateCookieSk(tcp, pbuf, tcpHdr, &pi);
        if (newTcp != NULL) { // 处理cookie socket
            ret = TcpProcSynRecv(newTcp, tcpHdr, pbuf, &pi);
            TcpAdjustCookieRtt(newTcp, &pi);
            PBUF_Free(pbuf);
        } else {
            ret = TcpInetProcChild(tcp, pbuf, tcpHdr, &pi);
        }
        SOCK_Deref(TcpSk2Sk(tcp));
        return ret;
    }
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_TOTAL);
    return TcpInput(tcp, pbuf, tcpHdr, &pi);

drop:
    PBUF_Free(pbuf);

    return ret;
}

static inline uint16_t TcpInetGetEffectiveMss(TcpSk_t* tcp)
{
    // 当前不支持 sack 和 path mtu ，直接返回协商的 mss
    return tcp->mss;
}

static inline uint16_t TcpInetGetTsoSize(Netdev_t* dev, uint16_t mss)
{
    if (dev == NULL || !NETDEV_TSO_ENABLED(dev)) {
        return mss;
    }

    // TSO 开启，返回 tsoSize
    return (uint16_t)UTILS_MAX(mss, dev->tsoSize);
}

static int TcpInetGetXmitInfo(Sock_t* sk, TcpXmitInfo_t* info)
{
    TcpSk_t* tcp = TcpSK(sk);
    INET_FlowInfo_t* flow = &TcpInetSk(sk)->flow;
    Netdev_t* dev = NULL;

    if (INET_UpdateFlow(flow) != 0) {
        return -1;
    }

    if (flow->rt != NULL) {
        dev = flow->rt->ifaddr->dev;
    } else {
        dev = sk->dev;
    }

    info->pentry = PMGR_ENTRY_ROUTE_OUT;
    info->mss = TcpInetGetEffectiveMss(tcp);
    info->tsoSize = TcpInetGetTsoSize(dev, info->mss);
    info->flow = flow;
    info->dev = dev;
    return 0;
}

static uint32_t TcpGenInetIsn(DP_InAddr_t laddr, DP_InAddr_t paddr, uint16_t sport, uint16_t dport)
{
    struct {
        DP_InAddr_t laddr;
        DP_InAddr_t paddr;
        uint16_t sport;
        uint16_t dport;
    } hashinfo = {
        .laddr = laddr, .paddr = paddr, .sport = sport, .dport = dport
    };

    uint32_t offset = sizeof(hashinfo);
    uint32_t digestBuffer[SHA256_HASH_DIGGESET_LEN] = {0};

    SHA256GenHash((const uint8_t *)&hashinfo, offset, (uint8_t *)digestBuffer, SHA256_HASH_DIGGESET_LEN);
    return digestBuffer[1] + WORKER_GetTime();
}

uint32_t TcpInetGenIsn(Sock_t* sk)
{
    InetSk_t* inetSk = TcpInetSk(sk);

    return TcpGenInetIsn(inetSk->hashinfo.laddr, inetSk->hashinfo.paddr,
        inetSk->hashinfo.lport, inetSk->hashinfo.pport);
}

int TcpInetInit(void)
{
    SOCK_ProtoOps_t skOps = {
        .type     = DP_SOCK_STREAM,
        .protocol = 0,
        .create   = TcpInetCreateSk,
    };
    static TcpFamilyOps_t inetOps = {
        .hash       = TcpInetPerWorkerTblInsert,
        .unhash     = TcpInetPerWorkerTblRemove,
        .getXmitInfo = TcpInetGetXmitInfo,
        .freeFunc       = TcpInetFreeSk,
        .waitIdle   = TcpInetWaitIdle,
        .listenerInsert = TcpInetListenerInsertSafe,
        .listenerRemove = TcpInetListenerRemoveSafe,
        .connectTblInsert = TcpInetConnectTblInsert,
        .connectTblRemove = TcpInetConnectTblRemoveSafe,
        .globalInsert = TcpInetGlobalInsert,
        .globalRemove = TcpInetGlobalRemoveSafe,
    };

    g_tcpInetOps = &inetOps;
    g_tcpCfgCtx.freeCnt = CFG_GET_VAL(DP_CFG_TCPCB_MAX);
    if (SPINLOCK_Init(&g_tcpCfgCtx.lock) != 0) {
        return -1;
    }

    SOCK_AddProto(DP_AF_INET, &skOps);

    PMGR_AddEntry(PMGR_ENTRY_TCP_IN, TcpInetInput);

    NS_SetNetOps(NS_NET_TCP, TcpInetAllocHash, TcpInetFreeHash);

    return 0;
}

void TcpInetDeinit(void)
{
    SPINLOCK_Deinit(&g_tcpCfgCtx.lock);
}

int DP_SocketCountGet(int type)
{
    (void)type;
    int tcpSocketCount;
    SPINLOCK_Lock(&g_tcpCfgCtx.lock);
    tcpSocketCount = g_tcpCfgCtx.freeCnt;
    SPINLOCK_Unlock(&g_tcpCfgCtx.lock);
    return CFG_GET_VAL(DP_CFG_TCPCB_MAX) - tcpSocketCount;
}