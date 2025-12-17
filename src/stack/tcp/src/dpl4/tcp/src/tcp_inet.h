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
#ifndef TCP_INET_H
#define TCP_INET_H

#include "tcp_types.h"
#include "tcp_sack.h"
#include "dp_tcp.h"

#include "utils_log.h"
#include "utils_mem_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TcpInetSk(sk)           ((InetSk_t*)(((TcpSk_t*)(sk)) + 1))
#define TcpInetSk2Sk(inetSk)    (Sock_t*)((uint8_t*)(inetSk) - sizeof(TcpSk_t))
#define TcpInetTcpSK(inetSk)    TcpSK(TcpInetSk2Sk(inetSk))

#define TcpInet6Sk(sk)          ((Inet6Sk_t*)(((TcpSk_t*)(sk)) + 1))
#define TcpInet6Sk2Sk(inet6Sk)  (Sock_t*)((uint8_t*)(inet6Sk) - sizeof(TcpSk_t))
#define TcpInet6TcpSK(inet6Sk)  TcpSK(TcpInet6Sk2Sk(inet6Sk))

#define SHA256_HASH_DIGGESET_LEN 32
#define BACKLOG_QUEUE_DEFAULT 4
#define BACKLOG_QUEUE_MAXCONN 35767

#define DP_TCP_STAT_RCV(tcp, _pktLen)                                           \
    do {                                                                        \
        if ((tcp)->connType == TCP_ACTIVE) {                                    \
            DP_ADD_TCP_STAT((tcp)->wid, DP_TCP_ACTIVE_RCV_BYTE, (_pktLen));     \
        } else {                                                                \
            DP_ADD_TCP_STAT((tcp)->wid, DP_TCP_PASSIVE_RCV_BYTE, (_pktLen));    \
        }                                                                       \
    } while (0)

extern char* g_tcpMpName;
extern DP_Mempool g_tcpMemPool;

static inline int TcpInetDevIsUp(Sock_t* sk)
{
    INET_FlowInfo_t* flow = &TcpInetSk(sk)->flow;
    if ((INET_GetDevByFlow(flow)->ifflags & DP_IFF_UP) == 0) {
        return 1;
    }
    return 0;
}

static inline uint16_t TcpGetTsoSize(Netdev_t* dev, uint16_t mss)
{
    if (dev == NULL || !NETDEV_TSO_ENABLED(dev)) {
        return mss;
    }

    // TSO 开启，返回 tsoSize
    return (uint16_t)UTILS_MAX(mss, dev->tsoSize);
}

static inline void TcpCheckBacklog(Sock_t* sk, int backlog)
{
    if (backlog < BACKLOG_QUEUE_DEFAULT) {
        TcpSK(sk)->backlog = BACKLOG_QUEUE_DEFAULT;
    } else if (backlog > BACKLOG_QUEUE_MAXCONN) {
        TcpSK(sk)->backlog = BACKLOG_QUEUE_MAXCONN;
    } else {
        TcpSK(sk)->backlog = backlog;
    }
}

/* 这里是因为在触发cookie的时候已经生成过了一遍ISS 需要使用对端回复报文的ACK字段来对iss进行更新 */
static inline void TcpFillCookieSk(TcpSk_t* newTcp, TcpPktInfo_t* pi, uint16_t mss)
{
    newTcp->iss    = pi->ack - 1;
    newTcp->sndUna = newTcp->iss;
    newTcp->sndNxt = newTcp->iss + 1;
    newTcp->rttStartSeq = newTcp->sndMax = newTcp->iss + 1;

    newTcp->irs    = pi->seq - 1;
    newTcp->rcvNxt = newTcp->irs + 1;
    newTcp->rcvAdvertise = newTcp->irs + 1;
    newTcp->rcvWup = newTcp->rcvNxt;
    newTcp->mss    = mss;
    newTcp->rcvWnd = TcpGetRcvSpace(newTcp); // 更新通告窗口
}

#define TCP_OPTLEN_SACKBLOCK_MAX (DP_TCPOLEN_MAX - TCP_OPTLEN_SACK_APPA) // 按tcp选项长度计算的最大sack块数量

static inline uint16_t TcpGetEffectiveMss(TcpSk_t* tcp)
{
    if (TCP_SACK_AVAILABLE(tcp) && tcp->sackInfo->sackBlockNum != 0) {
        if (TcpNegTs(tcp)) { // 有时间戳时的对齐
            return tcp->mss - (uint16_t)(TCP_OPTLEN_SACK_PERBLOCK * UTILS_MIN(tcp->sackInfo->sackBlockNum,
                (TCP_OPTLEN_SACKBLOCK_MAX - DP_TCPOLEN_TSTAMP_APPA) / TCP_OPTLEN_SACK_PERBLOCK)) - TCP_OPTLEN_SACK_APPA;
        } else {             // 无时间戳时的对齐
            return tcp->mss - (uint16_t)(TCP_OPTLEN_SACK_PERBLOCK * UTILS_MIN(tcp->sackInfo->sackBlockNum,
                TCP_OPTLEN_SACKBLOCK_MAX / TCP_OPTLEN_SACK_PERBLOCK)) - TCP_OPTLEN_SACK_APPA;
        }
    }
    return tcp->mss;
}

uint32_t TcpInetGenIsn(Sock_t* sk);
uint32_t TcpInet6GenIsn(Sock_t* sk);

int TcpInet6IcmpTooBig(Pbuf_t* pbuf, uint32_t info);

void TcpShowInfo(TcpSk_t* tcp);

void TcpGetDetails(TcpSk_t* tcp, DP_TcpDetails_t* details);

int TcpInetInit(void);
int TcpInet6Init(void);

void TcpInetDeinit(void);

#ifdef __cplusplus
}
#endif
#endif
