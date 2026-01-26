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
#include "shm.h"
#include "pmgr.h"
#include "worker.h"
#include "netdev.h"
#include "sock_addr_ext.h"
#include "utils_cfg.h"
#include "utils_log.h"
#include "utils_base.h"
#include "utils_sha256.h"
#include "utils_spinlock.h"
#include "utils_mem_pool.h"
#include "utils_cb_cnt.h"

#include "tcp_timer.h"
#include "tcp_types.h"
#include "tcp_sock.h"
#include "tcp_inethash.h"
#include "tcp_tsq.h"
#include "tcp_in.h"
#include "tcp_cookie.h"
#include "tcp_out.h"
#include "tcp_sack.h"
#include "tcp_cc.h"
#include "tcp_bbr.h"

char* g_tcpMpName = "DP_TCP_MP";
DP_Mempool g_tcpMemPool = {0};

static Pbuf_t* TcpInetGenRstPkt(Pbuf_t* pbuf, DP_TcpHdr_t* origTcpHdr, TcpPktInfo_t* pi);

static inline void TcpInetUpdateMss(TcpSk_t* tcp, Netdev_t* dev)
{
    uint16_t mss = dev->mtu - sizeof(DP_IpHdr_t) - sizeof(DP_TcpHdr_t);

    if (tcp->mss == 0 || tcp->mss > mss) {
        tcp->mss = mss;
    }
}

static inline void TcpInetSetMaxSegNum(TcpSk_t* tcp, Netdev_t* dev)
{
    tcp->maxSegNum = dev->maxSegNum;
}

static Sock_t* TcpInetAllocSk(Sock_t* parent)
{
    Sock_t *sk;

    size_t  objSize = sizeof(TcpSk_t) + sizeof(InetSk_t);
    objSize = SOCK_GetSkSize(objSize);

    sk = MEMPOOL_ALLOC(g_tcpMemPool);
    if (sk == NULL) {
        DP_LOG_ERR("Malloc memory failed for tcp sk.");
        DP_ADD_ABN_STAT(DP_TCP_CREATE_MEM_ERR);
        return NULL;
    }

    (void)memset_s(sk, objSize, 0, objSize);

    if (SOCK_InitSk(sk, parent, objSize) != 0) {
        MEMPOOL_FREE(g_tcpMemPool, sk);
        return NULL;
    }

    return sk;
}

static void TcpInetFreeSk(Sock_t* sk)
{
    TcpSk_t *tcp = TcpSK(sk);
    DP_ADD_TCP_STAT((tcp)->wid, DP_TCP_DROP_REASS_BYTE, tcp->reassQue.bufLen);
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
    MEMPOOL_FREE(g_tcpMemPool, sk);

    (void)UTILS_DecCbCnt(&g_tcpCbCnt);
}

static int TcpGetDstAddr(Sock_t* sk, Pbuf_t* pbuf, struct DP_Sockaddr* addr, DP_Socklen_t* addrlen)
{
    (void)pbuf;

    if ((int)*addrlen < 0) {
        DP_ADD_ABN_STAT(DP_GET_DST_ADDRLEN_INVAL);
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
    INET_Hashinfo_t cur = {0};
    TcpHashTbl_t*   tbl = TcpHashGetTbl(sk->net);
    void* userData = sk->userData;

    if (SOCK_IS_BINDED(sk)) { // TCP不允许重复绑定
        DP_ADD_ABN_STAT(DP_TCP_BIND_REPEAT);
        return -EINVAL;
    }

    if (SOCK_IS_SHUTRD(sk) || SOCK_IS_SHUTWR(sk)) {
        DP_ADD_ABN_STAT(DP_TCP_BIND_SHUTDOWN);
        return -EINVAL;
    }

    if ((ret = INET_Bind(sk, inetSk, addr, addrlen, &cur)) != 0) {
        DP_ADD_ABN_STAT(DP_TCP_INET_BIND_FAILED);
        return ret;
    }

    TcpHashLockTbl(tbl);

    if (cur.lport == 0) {
        if (TcpInetGenPort(sk->glbHashTblIdx, tbl, 1, &cur, 0, userData) == 0) {
            ret = 0;
        } else {
            DP_ADD_ABN_STAT(DP_TCP_BIND_RAND_PORT_FAILED);
            ret = -EADDRINUSE;
        }
    } else if (TcpInetCanBind(sk->glbHashTblIdx, tbl, &cur, SOCK_CAN_REUSE(sk), userData) != 0) {
        ret = 0;
    } else {
        DP_ADD_ABN_STAT(DP_TCP_BIND_PORT_FAILED);
        ret = -EADDRINUSE;
    }

    if (ret == 0) {
        inetSk->hashinfo = cur;
        TcpInetGlobalInsert(sk);
    }

    TcpHashUnlockTbl(tbl);

    return ret;
}

static int TcpInetListen(Sock_t* sk, int backlog)
{
    if (SOCK_IS_CONNECTED(sk) || SOCK_IS_CONNECTING(sk)) {
        DP_LOG_DBG("TcpInetListen failed, sock called connect before.");
        return -EINVAL;
    }

    if (!SOCK_IS_BINDED(sk)) {
        DP_LOG_DBG("TcpInetListen failed, sock not call connect before.");
        return -EDESTADDRREQ;
    }

    if (SOCK_IS_LISTENED(sk)) {
        DP_LOG_INFO("TcpInetListen sock is listened.");
        return 0;
    }

    SOCK_SET_LISTENED(sk);

    TcpCheckBacklog(sk, backlog);

    TcpSetState(TcpSK(sk), TCP_LISTEN);

    TcpInetListenerInsertSafe(sk);

    return 0;
}

static int TcpTryInsertConnectTbl(Sock_t *sk)
{
    TcpHashTbl_t* tbl = TcpHashGetTbl(sk->net);
    InetSk_t* inetSk  = TcpInetSk(sk);
    void* userData = sk->userData;

    TcpHashLockTbl(tbl);
    if (inetSk->hashinfo.lport == 0) {
        if (TcpInetGenPort(sk->glbHashTblIdx, tbl, 0, &inetSk->hashinfo, 0, userData) != 0) {
            TcpHashUnlockTbl(tbl);
            DP_ADD_ABN_STAT(DP_TCP_CONN_RAND_PORT_FAILED);
            return -EADDRNOTAVAIL;
        }
        TcpInetGlobalInsert(sk);
    } else if (TcpInetCanConnect(sk->glbHashTblIdx, tbl, &inetSk->hashinfo, 1, userData) == 0) {
        TcpHashUnlockTbl(tbl);
        DP_ADD_ABN_STAT(DP_TCP_CONN_PORT_FAILED);
        return -EADDRINUSE;
    }

    TcpSetLport(TcpSK(sk), inetSk->hashinfo.lport);
    TcpSetPport(TcpSK(sk), inetSk->hashinfo.pport);
    TcpInetConnectTblInsert(sk);

    TcpHashUnlockTbl(tbl);
    return 0;
}

static int TcpRxHash(Sock_t* sk, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen)
{
    InetSk_t* inetSk = TcpInetSk(sk);
    NETDEV_IfAddr_t* ifaddr = inetSk->flow.rt->ifaddr;
    int16_t queId;
    struct DP_SockaddrIn lAddr = {
        .sin_family = DP_AF_INET,
        .sin_port = inetSk->hashinfo.lport,
        .sin_addr.s_addr = inetSk->hashinfo.laddr,
    };
    if (ifaddr->dev->rxQueCnt == 1) {
        queId = 0;
    } else if (inetSk->hashinfo.queCnt == 1) {
        queId = inetSk->hashinfo.que;
    } else {
        queId = NETDEV_RxHash(ifaddr->dev, addr, addrlen, (const struct DP_Sockaddr*)&lAddr, sizeof(lAddr));
        if (queId == -1) {
            DP_LOG_ERR("rx hash failed. ifindex = %d", ifaddr->dev->ifindex);
            return -1;
        }
    }

    int16_t wid = NETDEV_GetRxWid(ifaddr->dev, queId);
    if (sk->wid != -1 && sk->wid != wid) {
        // 共线程部署时socket被绑定至worker，若主动散列的队列属于其他worker将导致报文发送异常
        DP_LOG_ERR("rx hash error. ifindex = %d, rx queId = %d, rx wid = %d, sk wid = %d",
            ifaddr->dev->ifindex, queId, wid, sk->wid);
        DP_ADD_ABN_STAT(DP_TCP_RXHASH_WID_ERR);
        return -1;
    }
    TcpSK(sk)->txQueid = queId;
    TcpSK(sk)->wid = wid;
    return 0;
}

static void TcpInitIss(TcpSk_t* tcp)
{
    tcp->iss = TcpInetGenIsn(TcpSk2Sk(tcp));
    tcp->sndUna = tcp->iss;
    tcp->sndNxt = tcp->iss;
    tcp->sndMax = tcp->iss;
    tcp->rttStartSeq = tcp->iss;
}

static void TcpInitHashInfo(InetSk_t* inetSk, const struct DP_Sockaddr* addr, NETDEV_IfAddr_t* ifaddr, Sock_t* sk)
{
    inetSk->hashinfo.pport = ((struct DP_SockaddrIn*)addr)->sin_port;
    inetSk->hashinfo.paddr = inetSk->flow.dst;
    inetSk->hashinfo.laddr = inetSk->flow.src;
    inetSk->hashinfo.ifIndex = ifaddr->dev->ifindex;
    inetSk->hashinfo.wid = (int8_t)sk->wid;
    inetSk->hashinfo.vpnid = sk->vpnid;
}

static inline void TcpInetPreProcConnect(Sock_t* sk, Netdev_t* dev)
{
    TcpInitIss(TcpSK(sk));

    TcpInetUpdateMss(TcpSK(sk), dev);
    TcpInetSetMaxSegNum(TcpSK(sk), dev);

    SOCK_SET_CONNECTING(sk);
    DP_INC_TCP_STAT(TcpSK(sk)->wid, DP_TCP_CONN_ATTEMPT);

    TcpTsqAddLockQue(TcpSK(sk), TCP_TSQ_CONNECT, true);
}

static int TcpInetConnect(Sock_t* sk, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen)
{
    int              ret;
    InetSk_t*        inetSk = TcpInetSk(sk);

    if ((ret = TcpCanConnect(sk)) != 0) {
        DP_ADD_ABN_STAT(DP_CONN_FLAGS_ERR);
        return ret;
    }

    if ((ret = INET_Connect(sk, inetSk, addr, addrlen)) != 0) {
        DP_ADD_ABN_STAT(DP_TCP_INET_CONN_FAILED);
        return ret;
    }

    if (inetSk->flow.rt == NULL) {
        INET_DeinitFlow(&inetSk->flow);
        DP_ADD_ABN_STAT(DP_TCP_CONN_RT_NULL);
        return -EINVAL;
    }

    if (TcpInetDevIsUp(sk) != 0) {
        INET_DeinitFlow(&inetSk->flow);
        DP_ADD_ABN_STAT(DP_TCP_CONN_DEV_DOWN);
        return -ENETDOWN;
    }

    NETDEV_IfAddr_t* ifaddr = inetSk->flow.rt->ifaddr;

    if (TBM_IsVirtualDevRt(inetSk->flow.rt)) {
        if (inetSk->flow.src == DP_INADDR_ANY) {
            INET_DeinitFlow(&inetSk->flow);
            DP_ADD_ABN_STAT(DP_TCP_CONN_VI_ANY);
            return -EADDRNOTAVAIL;
        }
    } else {
        if (inetSk->hashinfo.laddr != DP_INADDR_ANY && ifaddr->local != inetSk->hashinfo.laddr) {
            INET_DeinitFlow(&inetSk->flow);
            DP_ADD_ABN_STAT(DP_TCP_CONN_ADDR_ERR);
            return -EINVAL;
        }
    }

    TcpInitHashInfo(inetSk, addr, ifaddr, sk);

    if ((ret = TcpTryInsertConnectTbl(sk)) != 0) {
        INET_DeinitFlow(&inetSk->flow);
        return ret;
    }

    if (TcpRxHash(sk, addr, addrlen) != 0) {
        INET_DeinitFlow(&inetSk->flow);
        return -EADDRNOTAVAIL;
    }

    TcpSK(sk)->pseudoHdrCksum = INET_CalcPseudoCksum(&inetSk->hashinfo);

    TcpInetPreProcConnect(sk, ifaddr->dev);

    return 0;
}

void TcpShowBaseInfo(TcpSk_t* tcp)
{
    uint32_t offset = 0;
    char output[LEN_INFO] = {0};

    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO, "\r\n-------- TcpInfo --------\n");
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "state = %u\n", tcp->state);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "connType = %u\n", tcp->connType);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "IsNoVerifyCksum = %u\n", tcp->noVerifyCksum);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "IsAckNow = %u\n", tcp->ackNow);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "IsDelayAckEnable = %u\n", tcp->delayAckEnable);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "IsNodelay = %u\n", tcp->nodelay);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "IsRttRecord = %u\n", tcp->rttRecord);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "IsCork = %u\n", tcp->cork);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "IsDeferAccept = %u\n", tcp->deferAccept);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "flags = %u\n", tcp->flags);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "wid = %d\n", tcp->wid);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "txQueid = %d\n", tcp->txQueid);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "childCnt = %d\n", tcp->childCnt);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "backlog = %d\n", tcp->backlog);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "accDataCnt = %u\n", tcp->accDataCnt);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "accDataMax = %u\n", tcp->accDataMax);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "dupAckCnt = %u\n", tcp->dupAckCnt);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "caState = %u\n", tcp->caState);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "cwnd = %u\n", tcp->cwnd);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "ssthresh = %u\n", tcp->ssthresh);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "seqRecover = %u\n", tcp->seqRecover);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "reorderCnt = %u\n", tcp->reorderCnt);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "rttStartSeq = %u\n", tcp->rttStartSeq);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "srtt = %u\n", tcp->srtt);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "rttval = %u\n", tcp->rttval);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "tsVal = %u\n", tcp->tsVal);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "tsEcho = %u\n", tcp->tsEcho);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "lastChallengeAckTime = %u\n", tcp->lastChallengeAckTime);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "fastMode = %u\n", tcp->fastMode);

    DEBUG_SHOW(0, output, offset);
}

void TcpShowTransInfo(TcpSk_t* tcp)
{
    uint32_t offset = 0;
    char output[LEN_INFO] = {0};

    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "localPort = %u\n", tcp->lport);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "peerPort = %u\n", tcp->pport);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "synOpt = %u\n", tcp->synOpt);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "negOpt = %u\n", tcp->negOpt);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "rcvWs = %ud\n", tcp->rcvWs);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "sndWs = %u\n", tcp->sndWs);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "rcvMss = %u\n", tcp->rcvMss);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "mss = %u\n", tcp->mss);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "iss = %u\n", tcp->iss);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "irs = %u\n", tcp->irs);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "sndUna = %u\n", tcp->sndUna);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "sndNxt = %u\n", tcp->sndNxt);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "sndMax = %u\n", tcp->sndMax);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "sndWnd = %u\n", tcp->sndWnd);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "sndUp = %u\n", tcp->sndUp);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "sndWl1 = %u\n", tcp->sndWl1);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "rcvNxt = %u\n", tcp->rcvNxt);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "rcvWnd = %u\n", tcp->rcvWnd);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "rcvMax = %u\n", tcp->rcvMax);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "rcvWup = %u\n", tcp->rcvWup);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "idleStart = %u\n", tcp->idleStart);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "keepIdle = %u\n", tcp->keepIdle);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "keepIntvl = %u\n", tcp->keepIntvl);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "keepProbes = %u\n", tcp->keepProbes);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "keepProbeCnt = %u\n", tcp->keepProbeCnt);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "keepIdleLimit = %u\n", tcp->keepIdleLimit);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "keepIdleCnt = %u\n", tcp->keepIdleCnt);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "backoff = %u\n", tcp->backoff);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "maxRexmit = %u\n", tcp->maxRexmit);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "userTimeout = %u\n", tcp->userTimeout);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "userTimeStartFast = %u\n", tcp->userTimeStartFast);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "userTimeStartSlow = %u\n", tcp->userTimeStartSlow);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "fastTimer expiredTick = %u\n", tcp->expiredTick[TCP_TIMERID_FAST]);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "slowTimer expiredTick = %u\n", tcp->expiredTick[TCP_TIMERID_SLOW]);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "delayackTimer expiredTick = %u\n", tcp->expiredTick[TCP_TIMERID_DELAYACK]);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "synRetries = %u\n", tcp->synRetries);
    DEBUG_SHOW(0, output, offset);
}

void TcpShowInfo(TcpSk_t* tcp)
{
    TcpShowBaseInfo(tcp);
    TcpShowTransInfo(tcp);
    if ((tcp->caMeth->algId == TCP_CAMETH_BBR) && (tcp->state >= TCP_ESTABLISHED)) {
        TcpShowBBRInfo(tcp);
    }
}

static void TcpInetShowInfo(Sock_t* sk)
{
    InetSk_t* inetSk = TcpInetSk(sk);
    INET_ShowInfo(inetSk);

    TcpShowInfo(TcpSK(sk));
}

void TcpGetBaseDetails(TcpSk_t* tcp, DP_TcpBaseDetails_t* details)
{
    details->state = tcp->state;
    details->connType = tcp->connType;
    details->options = tcp->options;
    details->flags = tcp->flags;
    details->wid = tcp->wid;
    details->txQueid = tcp->txQueid;
    details->childCnt = tcp->childCnt;
    details->backlog = tcp->backlog;
    details->accDataCnt = tcp->accDataCnt;
    details->accDataMax = tcp->accDataMax;
    details->dupAckCnt = tcp->dupAckCnt;
    details->caAlgId = tcp->caMeth == NULL ? -1 : tcp->caMeth->algId;
    details->caState = tcp->caState;
    details->cwnd = tcp->cwnd;
    details->ssthresh = tcp->ssthresh;
    details->seqRecover = tcp->seqRecover;
    details->reorderCnt = tcp->reorderCnt;
    details->rttStartSeq = tcp->rttStartSeq;
    details->srtt = tcp->srtt;
    details->rttval = tcp->rttval;
    details->maxRtt = tcp->maxRtt;
    details->tsVal = tcp->tsVal;
    details->tsEcho = tcp->tsEcho;
    details->lastChallengeAckTime = tcp->lastChallengeAckTime;
    details->fastMode = tcp->fastMode;
    details->sndQueSize = tcp->sndQue.bufLen;
    details->rcvQueSize = tcp->rcvQue.bufLen;
    details->rexmitQueSize = tcp->rexmitQue.bufLen;
    details->reassQueSize = tcp->reassQue.bufLen;
}

void TcpGetTransDetails(TcpSk_t* tcp, DP_TcpTransDetails_t* details)
{
    details->lport = tcp->lport;
    details->pport = tcp->pport;
    details->synOpt = tcp->synOpt;
    details->negOpt = tcp->negOpt;
    details->rcvWs = tcp->rcvWs;
    details->sndWs = tcp->sndWs;
    details->rcvMss = tcp->rcvMss;
    details->mss = tcp->mss;
    details->iss = tcp->iss;
    details->irs = tcp->irs;
    details->sndUna = tcp->sndUna;
    details->sndNxt = tcp->sndNxt;
    details->sndMax = tcp->sndMax;
    details->sndWnd = tcp->sndWnd;
    details->sndUp = tcp->sndUp;
    details->sndWl1 = tcp->sndWl1;
    details->rcvNxt = tcp->rcvNxt;
    details->rcvWnd = tcp->rcvWnd;
    details->rcvMax = tcp->rcvMax;
    details->rcvWup = tcp->rcvWup;
    details->idleStart = tcp->idleStart;
    details->keepIdle = (uint16_t)tcp->keepIdle;
    details->keepIntvl = (uint16_t)tcp->keepIntvl;
    details->keepProbes = tcp->keepProbes;
    details->keepProbeCnt = tcp->keepProbeCnt;
    details->keepIdleLimit = tcp->keepIdleLimit;
    details->keepIdleCnt = tcp->keepIdleCnt;
    details->backoff = tcp->backoff;
    details->maxRexmit = tcp->maxRexmit;
    details->rexmitCnt = tcp->rexmitCnt;
    details->userTimeout = tcp->userTimeout;
    details->userTimeStartFast = tcp->userTimeStartFast;
    details->userTimeStartSlow = tcp->userTimeStartSlow;
    details->fastTimeoutTick = tcp->expiredTick[TCP_TIMERID_FAST];
    details->slowTimeoutTick = tcp->expiredTick[TCP_TIMERID_SLOW];
    details->delayAckTimoutTick = tcp->expiredTick[TCP_TIMERID_DELAYACK];
    details->synRetries = tcp->synRetries;
}

void TcpGetDetails(TcpSk_t* tcp, DP_TcpDetails_t* details)
{
    TcpGetBaseDetails(tcp, &details->baseDetails);
    TcpGetTransDetails(tcp, &details->transDetails);
}

static void TcpInetGetDetails(Sock_t* sk, DP_SockDetails_t* details)
{
    InetSk_t* inetSk = TcpInetSk(sk);
    INET_GetDetails(inetSk, &details->inetDetails);

    TcpGetDetails(TcpSK(sk), &details->tcpDetails);
}

static void TcpInetGetState(Sock_t* sk, DP_SocketState_t* state)
{
    InetSk_t* inetSk = TcpInetSk(sk);
    state->pf = DP_PF_INET;
    state->proto = inetSk->hashinfo.protocol;
    state->lAddr4 = inetSk->hashinfo.laddr;
    state->lPort = inetSk->hashinfo.lport;
    state->rAddr4 = inetSk->hashinfo.paddr;
    state->rPort = inetSk->hashinfo.pport;
    state->state = TcpSK(sk)->state;
    // 共线程模式下worker id在创建socket时确定，返回sk->wid；非共线程模式下返回建链时使用的worker id
    state->workerId = (uint32_t)(sk->wid == -1 ? (int32_t)TcpSK(sk)->wid : sk->wid);
}

static SOCK_Ops_t g_tcpInetSkOps = {
    .type       = DP_SOCK_STREAM,
    .protocol   = DP_IPPROTO_TCP,
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
    .showInfo   = TcpInetShowInfo,
    .getState   = TcpInetGetState,
    .getDetails = TcpInetGetDetails,
};

static int TcpInetCreateSk(NS_Net_t* net, int type, int protocol, Sock_t** out)
{
    Sock_t* sk;

    (void)type;

    if (protocol != 0 && protocol != DP_IPPROTO_TCP) {
        DP_ADD_ABN_STAT(DP_TCP_CREATE_INVAL);
        return -EPROTONOSUPPORT;
    }

    if (UTILS_IncCbCnt(&g_tcpCbCnt, (uint32_t)CFG_GET_VAL(DP_CFG_TCPCB_MAX)) != 0) {
        DP_ADD_ABN_STAT(DP_TCP_CREATE_FULL);
        return -EMFILE;
    }

    sk = TcpInetAllocSk(NULL);
    if (sk == NULL) {
        (void)UTILS_DecCbCnt(&g_tcpCbCnt);
        return -ENOMEM;
    }

    sk->net    = net;
    sk->ops    = &g_tcpInetSkOps;
    sk->family = DP_AF_INET;

    TcpInetSk(sk)->hashinfo.protocol = DP_IPPROTO_TCP;
    TcpInitTcpSk(TcpSK(sk));

    *out = sk;

    return 0;
}

static int TcpInitInetSk(Sock_t* newsk, Sock_t* parent, DP_TcpHdr_t* tcpHdr, DP_IpHdr_t* ipHdr)
{
    InetSk_t* inetSk = TcpInetSk(newsk);
    inetSk->hashinfo.lport     = tcpHdr->dport;
    inetSk->hashinfo.pport     = tcpHdr->sport;
    inetSk->hashinfo.laddr     = ipHdr->dst;
    inetSk->hashinfo.paddr     = ipHdr->src;
    inetSk->hashinfo.protocol  = ipHdr->type;
    inetSk->hashinfo.vpnid     = TcpInetSk(parent)->hashinfo.vpnid;
    inetSk->hashinfo.lportMask = TcpInetSk(parent)->hashinfo.lportMask;
    inetSk->hashinfo.wid       = (int8_t)newsk->wid;
    if (TcpInetPortRef(newsk->net, inetSk->hashinfo.lport, inetSk->hashinfo.lportMask, inetSk->hashinfo.wid) != 0) {
        return -1;
    }

    inetSk->flow.src          = inetSk->hashinfo.laddr;
    inetSk->flow.dst          = inetSk->hashinfo.paddr;
    if (INET_InitFlowBySk(newsk, inetSk, &inetSk->flow) != 0 || inetSk->flow.src != inetSk->hashinfo.laddr) {
        return -1;
    }
    inetSk->hashinfo.ifIndex = inetSk->flow.rt->ifaddr->dev->ifindex;

    return 0;
}

static TcpSk_t* TcpInetCreateChildSk(Sock_t* parent, Pbuf_t* pbuf, DP_TcpHdr_t* tcpHdr, uint8_t *isNeedRst)
{
    DP_IpHdr_t* ipHdr = (DP_IpHdr_t*)PBUF_GET_L3_HDR(pbuf);
    Netdev_t*     dev   = (Netdev_t*)PBUF_GET_DEV(pbuf);

    if (TcpSK(parent)->backlog <= TcpSK(parent)->childCnt) {
        DP_INC_TCP_STAT(DP_PBUF_GET_WID(pbuf), DP_TCP_OVER_BACKLOG);
        return NULL;
    }

    // 内核行为，如果已经达到socket资源的最大值，则服务端会回复RST报文，且返回EMFILE错误
    if (UTILS_IncCbCnt(&g_tcpCbCnt, (uint32_t)CFG_GET_VAL(DP_CFG_TCPCB_MAX)) != 0) {
        DP_INC_TCP_STAT(DP_PBUF_GET_WID(pbuf), DP_TCP_PAEST_OVER_MAXCB);
        parent->error = EMFILE;
        SOCK_WakeupRdSem(parent);
        *isNeedRst = 1;
        return NULL;
    }

    Sock_t* newsk = TcpInetAllocSk(parent);
    if (newsk == NULL) {
        (void)UTILS_DecCbCnt(&g_tcpCbCnt);
        return NULL;
    }

    TcpSk_t* newtcp = TcpSK(newsk);
    newsk->family = DP_AF_INET;
    TcpInitChildTcpSk(newsk, parent, pbuf, tcpHdr);
    if (TcpInitInetSk(newsk, parent, tcpHdr, ipHdr) != 0) {
        TcpInetFreeSk(newsk);
        return NULL;
    }

    if (TcpInetCanUseAddr(&TcpInetSk(newsk)->hashinfo) != 1) {
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
    TcpInetSetMaxSegNum(newtcp, dev);
    TcpInetPerWorkerTblInsert(newsk);
    TcpInetConnectTblInsertSafe(newsk);

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
    INET_Hashinfo_t hi = {0};
    DP_IpHdr_t*     ipHdr = (DP_IpHdr_t*)PBUF_GET_L3_HDR(pbuf);
    uint32_t        cksum;

    ret = TcpGenCookieSynAckPktByPkt(pbuf, parent, pi, synOpts, ci->iss);
    if (ret == NULL) {
        return ret;
    }

    PBUF_SET_L4_TYPE(ret, DP_IPPROTO_TCP);
    PBUF_SET_ENTRY(ret, PMGR_ENTRY_ROUTE_OUT);
    DP_PBUF_SET_WID(ret, DP_PBUF_GET_WID(pbuf));
    PBUF_SET_QUE_ID(ret, NETDEV_GetTxQueid(PBUF_GET_DEV(pbuf), PBUF_GET_QUE_ID(pbuf)));
    PBUF_SET_PKT_TYPE(ret, PBUF_PKTTYPE_HOST);
    PBUF_SET_DEV(ret, PBUF_GET_DEV(pbuf));
    PBUF_SET_FLOW(ret, NULL);
    DP_PBUF_SET_VPNID(ret, DP_PBUF_GET_VPNID(pbuf));
    PBUF_SET_DST_ADDR4(ret, PBUF_GET_DST_ADDR4(pbuf));
    INET_TX_CB(ret)->src = ipHdr->dst;

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
    Pbuf_t* ret;

    TcpNotifyEvent(TcpSk2Sk(tcp), SOCK_EVENT_RCVSYN, tcp->tsqNested);
    if (SOCK_IS_CLOSED(TcpSk2Sk(tcp))) {
        DP_INC_TCP_STAT(pbuf->wid, DP_TCP_COOKIE_AFTER_CLOSED);
        ret = TcpInetGenRstPkt(pbuf, tcpHdr, pi);
        goto out;
    }

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
    ret = TcpGenCookieSynAckPkt(pbuf, tcp, pi, synOpts, &info);

out:
    PBUF_Free(pbuf);
    return ret;
}

static Pbuf_t* TcpInetProcChild(TcpSk_t* tcp, Pbuf_t* pbuf, DP_TcpHdr_t* tcpHdr, TcpPktInfo_t* pi)
{
    TcpSk_t*  child = NULL;
    TcpSynOpts_t synOpts = {0};
    Pbuf_t *ret = NULL;
    uint8_t isNeedRst = 0;
    uint32_t reason = 0;

    PBUF_SET_L4_OFF(pbuf);
    PBUF_CUT_HEAD(pbuf, pi->hdrLen);

    if (TcpPreProcChild(tcp, pbuf, pi, &isNeedRst, &reason) != 0 && isNeedRst == 0) {
        goto drop;
    } else if (isNeedRst == 1) {
        ret = TcpInetGenRstPkt(pbuf, tcpHdr, pi);
        goto drop;
    }

    if (TcpParseSynOpts((uint8_t*)(tcpHdr + 1), pi->hdrLen - sizeof(DP_TcpHdr_t), &synOpts) != 0) {
        DP_INC_TCP_STAT(DP_PBUF_GET_WID(pbuf), DP_TCP_RCV_ERR_SYN_OPT);
    }

    SOCK_Lock(TcpSk2Sk(tcp));

    if (SOCK_IS_CLOSED(TcpSk2Sk(tcp))) {
        SOCK_Unlock(TcpSk2Sk(tcp));
        reason = DP_TCP_PARENT_CLOSED;
        ret = TcpInetGenRstPkt(pbuf, tcpHdr, pi);
        goto drop;
    }

    if (tcp->cookie != 0 && TcpCheckCookie(tcp)) {
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
    if (reason != 0) {
        DP_INC_TCP_STAT(DP_PBUF_GET_WID(pbuf), reason);
        DP_INC_TCP_STAT(DP_PBUF_GET_WID(pbuf), DP_TCP_DROP_CONTROL_PKTS);
        ATOMIC32_Inc(&tcp->rcvDrops);
    }

    PBUF_Free(pbuf);
    return ret;
}

TcpSk_t* TcpInetLookupByPkt(Pbuf_t* pbuf, TcpPktInfo_t* pi)
{
    DP_IpHdr_t*     ipHdr;
    DP_TcpHdr_t*    tcpHdr;
    Netdev_t*       dev = PBUF_GET_DEV(pbuf);
    INET_Hashinfo_t hi = {0};
    TcpSk_t* tcp = NULL;

    ipHdr = (DP_IpHdr_t*)PBUF_GET_L3_HDR(pbuf);
    tcpHdr = PBUF_MTOD(pbuf, DP_TcpHdr_t*);

    hi.protocol = DP_IPPROTO_TCP;
    hi.paddr    = ipHdr->src;
    hi.laddr    = ipHdr->dst;
    hi.pport    = tcpHdr->sport;
    hi.lport    = tcpHdr->dport;
    hi.vpnid    = DP_PBUF_GET_VPNID(pbuf);

    tcp = TcpInetLookupPerWorker(DP_PBUF_GET_WID(pbuf), dev->net, &hi);
    if (tcp != NULL) {
        if (tcp->state != TCP_TIME_WAIT) {
            return tcp;
        }

        // 支持复用TIMEWAIT状态socket
        /* rfc9293 3.10.7.4. For the TIME-WAIT state, new connections can
            be accepted if the Timestamp Option is used and meets
            expectations (per [40]).
        */
        tcp = TcpReuse(tcp, tcpHdr, pi);
        if (tcp != NULL) {
            return tcp;
        }
    }

    return TcpInetLookupListener(DP_PBUF_GET_WID(pbuf), dev->net, &hi);
}

static Pbuf_t* TcpInetGenRstPkt(Pbuf_t* pbuf, DP_TcpHdr_t* origTcpHdr, TcpPktInfo_t* pi)
{
    Pbuf_t*         ret;
    DP_TcpHdr_t*    tcpHdr;
    INET_Hashinfo_t hi = {0};
    DP_IpHdr_t*     ipHdr = (DP_IpHdr_t*)PBUF_GET_L3_HDR(pbuf);
    uint32_t        cksum;

    ret = TcpGenRstPktByPkt(origTcpHdr, pi);
    if (ret == NULL) {
        return ret;
    }

    PBUF_SET_L4_TYPE(ret, DP_IPPROTO_TCP);
    PBUF_SET_ENTRY(ret, PMGR_ENTRY_ROUTE_OUT);
    PBUF_SET_QUE_ID(ret, NETDEV_GetTxQueid(PBUF_GET_DEV(pbuf), PBUF_GET_QUE_ID(pbuf)));
    DP_PBUF_SET_WID(ret, DP_PBUF_GET_WID(pbuf));
    PBUF_SET_PKT_TYPE(ret, PBUF_PKTTYPE_HOST);
    PBUF_SET_DEV(ret, PBUF_GET_DEV(pbuf));
    PBUF_SET_FLOW(ret, NULL);
    DP_PBUF_SET_VPNID(ret, DP_PBUF_GET_VPNID(pbuf));
    PBUF_SET_DST_ADDR4(ret, PBUF_GET_DST_ADDR4(pbuf));
    INET_TX_CB(ret)->src = ipHdr->dst;

    DP_INC_TCP_STAT(DP_PBUF_GET_WID(pbuf), DP_TCP_SND_RST);
    DP_INC_TCP_STAT(DP_PBUF_GET_WID(pbuf), DP_TCP_SND_CONTROL);
    DP_INC_TCP_STAT(DP_PBUF_GET_WID(pbuf), DP_TCP_SND_TOTAL);

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
    TcpSk_t* newTcp = NULL;
    TcpSynOpts_t opts = {0};
    uint16_t mss;
    DP_IpHdr_t* ipHdr = (DP_IpHdr_t*)PBUF_GET_L3_HDR(pbuf);
    uint8_t isNeedRst = 0;

    if (tcp->cookie == 0 ||
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
        DP_INC_TCP_STAT(DP_PBUF_GET_WID(pbuf), DP_TCP_RCV_ACK_ERR_COOKIE);
        return NULL;
    }

    if (TcpParseSynOpts((uint8_t*)(tcpHdr + 1), pi->hdrLen - sizeof(DP_TcpHdr_t), &opts) != 0) {
        DP_INC_TCP_STAT(DP_PBUF_GET_WID(pbuf), DP_TCP_RCV_ERR_SYN_OPT);
    }

    // return 创建子socket
    SOCK_Lock(TcpSk2Sk(tcp));
    newTcp = TcpInetCreateChildSk(TcpSk2Sk(tcp), pbuf, tcpHdr, &isNeedRst);
    SOCK_Unlock(TcpSk2Sk(tcp));

    if (newTcp == NULL) {
        DP_INC_TCP_STAT(DP_PBUF_GET_WID(pbuf), DP_TCP_COOKIE_CREATE_FAILED);
        return NULL;
    }

    TcpFillCookieSk(newTcp, pi, mss);

    // 从时间戳中解析选项
    if ((opts.rcvSynOpt & TCP_SYN_OPT_TIMESTAMP) != 0) {
        TcpCookieSetOpts(newTcp, &opts);
    }

    return newTcp;
}

static Pbuf_t* TcpProcWithoutTcp(Pbuf_t* pbuf, DP_TcpHdr_t* tcpHdr, TcpPktInfo_t* pi)
{
    Pbuf_t* ret = NULL;
    int wid = NETDEV_GetRxQueWid(PBUF_GET_DEV(pbuf), 0);
    // wid不同且没查找到，可能是主动建链场景，syn|ack报文被分流到其他worker
    if ((CFG_GET_VAL(DP_CFG_DEPLOYMENT) != DP_DEPLOYMENT_CO_THREAD) && (CFG_GET_VAL(CFG_NOLOCK) != DP_ENABLE) &&
        (DP_PBUF_GET_WID(pbuf) != wid)) {
        TcpTsqInsertBacklog(wid, pbuf);
        return NULL;
    }

    if (PMGR_Get(PMGR_ENTRY_DELAY_KERNEL_IN) != NULL) {
        /* 队列钩子初始化完整，执行延后处理逻辑 */
        uint16_t putBackHead = pbuf->offset - pbuf->l3Off;
        PBUF_PUT_HEAD(pbuf, putBackHead);
        PBUF_SET_ENTRY(pbuf, PMGR_ENTRY_DELAY_KERNEL_IN);
        return pbuf;
    }

    if ((pi->thFlags & DP_TH_RST) == 0) {
        // 无监听端口，且非RST报文，返回RST报文。否则直接丢弃
        DP_INC_TCP_STAT(DP_PBUF_GET_WID(pbuf), DP_TCP_RCV_NON_RST);
        ret = TcpInetGenRstPkt(pbuf, tcpHdr, pi);
    }
    DP_INC_TCP_STAT(DP_PBUF_GET_WID(pbuf), DP_TCP_RCV_WITHOUT_LISTENER);
    PBUF_Free(pbuf);
    return ret;
}

static Pbuf_t* TcpInetInput(Pbuf_t* pbuf)
{
    DP_TcpHdr_t* tcpHdr;
    TcpPktInfo_t   pi;
    TcpSk_t*       tcp;
    Pbuf_t*        ret = NULL;
    Sock_t*        sk;

    DP_INC_PKT_STAT(DP_PBUF_GET_WID(pbuf), DP_PKT_TCP_IN);
    tcpHdr = PBUF_MTOD(pbuf, DP_TcpHdr_t*);
    if (UTILS_UNLIKELY(TcpInitPktInfo(pbuf, tcpHdr, &pi) != 0)) {
        NET_DEV_ADD_RX_ERRS(NETDEV_GetRxQue(PBUF_GET_DEV(pbuf), PBUF_GET_QUE_ID(pbuf)), 1);
        PBUF_Free(pbuf);
        return NULL;
    }

    tcp = TcpInetLookupByPkt(pbuf, &pi);
    if (tcp == NULL) {
        return TcpProcWithoutTcp(pbuf, tcpHdr, &pi);
    }
    sk = TcpSk2Sk(tcp);
    // 时间戳选项开启
    if (sk->isTimestamp == 1) {
        sk->skTimestamp = UTILS_TimeNow();
    }

    // 状态为CLOSED之后，不处理
    if (UTILS_UNLIKELY(TcpState(tcp) == TCP_CLOSED)) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_AFTER_CLOSED);
        PBUF_Free(pbuf);
        return NULL;
    }

    pi.tsVal = tcp->tsEcho;
    DP_INC_TCP_STAT(DP_PBUF_GET_WID(pbuf), DP_TCP_RCV_TOTAL);
    if (tcp->state == TCP_LISTEN) {
        DP_ADD_TCP_STAT(DP_PBUF_GET_WID(pbuf), DP_TCP_PASSIVE_RCV_BYTE, pi.dataLen);
        TcpSk_t* newTcp = TcpTryCreateCookieSk(tcp, pbuf, tcpHdr, &pi);
        if (newTcp != NULL) { // 处理cookie socket
            ret = TcpProcSynRecv(newTcp, tcpHdr, pbuf, &pi);
            TcpAdjustCookieRtt(newTcp, &pi);
            PBUF_Free(pbuf);
        } else {
            ret = TcpInetProcChild(tcp, pbuf, tcpHdr, &pi);
        }
        if (SOCK_Deref(TcpSk2Sk(tcp)) == 0) {
            TcpFreeSk(TcpSk2Sk(tcp));
        }
        return ret;
    }
    DP_TCP_STAT_RCV(tcp, pi.dataLen);
    return TcpInput(tcp, pbuf, tcpHdr, &pi);
}

static int TcpInetGetXmitInfo(Sock_t* sk, TcpXmitInfo_t* info)
{
    TcpSk_t* tcp = TcpSK(sk);
    INET_FlowInfo_t* flow = &TcpInetSk(sk)->flow;
    Netdev_t* dev = NULL;
    uint16_t rawMss = tcp->mss;

    if (INET_UpdateFlow(flow) != 0) {
        return -1;
    }

    if (flow->rt != NULL) {
        dev = flow->rt->ifaddr->dev;
    } else {
        dev = sk->dev;
    }

    if (dev == NULL) {
        DP_LOG_ERR("TcpInetGetXmitInfo! dev == NULL.");
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_XMIT_GET_DEV_NULL);
        return -1;
    }

    info->pentry = PMGR_ENTRY_ROUTE_OUT;
    info->mss = TcpGetEffectiveMss(tcp, rawMss);

    if (NETDEV_TSO_ENABLED(dev) && (dev->tsoSize > (sizeof(DP_IpHdr_t) + sizeof(DP_TcpHdr_t)))) {
        rawMss = (uint16_t)UTILS_MAX(rawMss, dev->tsoSize - sizeof(DP_IpHdr_t) - sizeof(DP_TcpHdr_t));
    }
    info->tsoSize = TcpGetEffectiveMss(tcp, rawMss);
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

static int TcpMemPoolInit(void)
{
    size_t objSize = sizeof(InetSk_t);
    objSize += sizeof(TcpSk_t);
    objSize = SOCK_GetSkSize(objSize);

    DP_MempoolCfg_S mpCfg = {0};
    mpCfg.name = g_tcpMpName;
    mpCfg.count = (uint32_t)CFG_GET_VAL(DP_CFG_TCPCB_MAX);
    mpCfg.size = objSize;
    mpCfg.type = DP_MEMPOOL_TYPE_FIXED_MEM;

    int32_t mod = MOD_TCP;
    DP_MempoolAttr_S attr = (void*)&mod;
    return MEMPOOL_CREATE(&mpCfg, &attr, &g_tcpMemPool);
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
        .mFree       = TcpInetFreeSk,
        .waitIdle   = TcpInetWaitIdle,
        .listenerInsert = TcpInetListenerInsertSafe,
        .listenerRemove = TcpInetListenerRemoveSafe,
        .connectTblInsert = TcpInetConnectTblInsert,
        .connectTblRemove = TcpInetConnectTblRemoveSafe,
        .globalInsert = TcpInetGlobalInsert,
        .globalRemove = TcpInetGlobalRemoveSafe,
    };

    g_tcpInetOps = &inetOps;

    ATOMIC32_Store(&g_tcpCbCnt, 0);

    SOCK_AddProto(DP_AF_INET, &skOps);

    PMGR_AddEntry(PMGR_ENTRY_TCP_IN, TcpInetInput);

    NS_SetNetOps(NS_NET_TCP, TcpHashAlloc, TcpHashFree);

    /* DP_MEMPOOL_TYPE_FIXED_MEM 类型的内存池创建失败时会使用MEM_MALLOC代替 */
    if (TcpMemPoolInit() != 0) {
        DP_LOG_INFO("Tcp mempool init failed.");
    }

    return 0;
}

void TcpInetDeinit(void)
{
    ATOMIC32_Store(&g_tcpCbCnt, 0);

    if (g_tcpMemPool != NULL) {
        MEMPOOL_DESTROY(g_tcpMemPool);
        g_tcpMemPool = NULL;
    }
}
