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

#include "tcp_sock.h"
#include "tcp_tsq.h"
#include "tcp_cc.h"
#include "tcp_timer.h"
#include "tcp_out.h"
#include "tcp_inet.h"
#include "tcp_reass.h"

#include "dp_tcp.h"

#include "sock.h"

#include "utils_statistic.h"
#include "utils_base.h"
#include "utils_debug.h"
#include "utils_cfg.h"

#include <securec.h>

// TCP MSS配置最小值
#define TCP_MSS_SPEC_MIN 88
// TCP MSS配置最大值
#define TCP_MSS_SPEC_MAX 32767
// 最大重试次数
#define TCP_MAX_REXMIT_TIMES 255
// 默认保活探测次数
#define TCP_DEFAULT_KEEP_CNT 9
// 默认保活时间间隔2h
#define TCP_DEFAULT_KEEP_IDLE TCP_SEC2_TICK(120 * 60)
// 默认保活探测时间间隔 75s
#define TCP_DEFAULT_KEEP_INTVL TCP_SEC2_TICK(75)

TcpFamilyOps_t* g_tcpInetOps  = NULL;
TcpFamilyOps_t* g_tcpInet6Ops = NULL;

uint32_t TcpGetRcvMax(TcpSk_t* tcp)
{
    Pbuf_t* lastSeg;
    TcpReassInfo_t* lastSegInfo;
    lastSeg = PBUF_CHAIN_TAIL(&tcp->reassQue);
    if (lastSeg == NULL) {
        return tcp->rcvNxt;
    }

    lastSegInfo = TcpReassGetInfo(lastSeg);
    return lastSegInfo->endSeq;
}

int TcpCanConnect(Sock_t* sk)
{
    if (SOCK_IS_LISTENED(sk)) {
        DP_ADD_ABN_STAT(DP_CONN_BY_LISTEN_SK);
        return -EOPNOTSUPP;
    }

    // TCP不允许重复connect
    if (SOCK_IS_CONNECTED(sk)) {
        DP_ADD_ABN_STAT(DP_CONNED_SK_REPEAT);
        return -EISCONN;
    }

    if (SOCK_IS_CONNECTING(sk)) {
        if (SOCK_IS_CONN_REFUSED(sk)) {
            DP_ADD_ABN_STAT(DP_CONN_REFUSED);
            return -ECONNREFUSED;
        }
        DP_ADD_ABN_STAT(DP_CONN_IN_PROGRESS);
        return -EALREADY;
    }

    return 0;
}

int TcpDisconnect(Sock_t* sk)
{
    TcpSk_t* tcp = TcpSK(sk);

    // 尚未建链直接关闭
    if (tcp->state == TCP_SYN_SENT) {
        TcpCleanUp(tcp);
        TcpSetState(tcp, TCP_CLOSED);
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_CLOSED);
        TcpFreeSk(sk);
        return -1;
    }

    // 调用close且开启linger则直接断链, 如果用户调用shutdown则进行正常断链
    if (tcp->parent != NULL || (SOCK_IS_CLOSED(sk) && (sk->lingerOnoff != 0))) {
        TcpXmitRstAckPkt(tcp);
        TcpCleanUp(tcp);
        if (tcp->parent != NULL) {
            TcpRemoveFromParentList(tcp);
        }
        TcpFreeSk(sk);
        return -1;
    }

    if (TcpState(tcp) == TCP_SYN_RECV || TcpState(tcp) == TCP_ESTABLISHED) {
        TcpSetState(tcp, TCP_FIN_WAIT1);
    } else if (TcpState(tcp) == TCP_CLOSE_WAIT) { // TCP_CLOSE_WAIT
        TcpSetState(tcp, TCP_LAST_ACK);
    } else {
        return 0;
    }

    if (tcp->sndQue.bufLen == 0 && sk->sndBuf.bufLen == 0) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_FIN);
        TcpXmitFinAckPkt(tcp);
    } else {
        TcpTsqAddQue(tcp, TCP_TSQ_SEND_DATA);
    }

    if (SOCK_IS_CLOSED(sk)) {
        PBUF_ChainClean(&tcp->reassQue);
        DP_ADD_PKT_STAT(tcp->wid, DP_PKT_RECV_BUF_FREE, tcp->rcvQue.pktCnt);
        PBUF_ChainClean(&tcp->rcvQue);
        PBUF_ChainClean(&sk->rcvBuf);
    }
    return 0;
}

void TcpDoConnecting(Sock_t* sk)
{
    TcpSk_t*        tcp = TcpSK(sk);
    TcpFamilyOps_t* ops;

    ops = sk->family == DP_AF_INET ? g_tcpInetOps : g_tcpInet6Ops;
    ASSERT(ops != NULL);

    SOCK_Lock(sk);

    sk->error = (uint16_t)ops->hash(sk);
    if (sk->error != 0 || sk->nonblock > 0) {
        SOCK_WakeupWrSem(sk);
    }

    SOCK_Unlock(sk);

    tcp->sndUp  = 0;
    tcp->sndWl1 = 0;

    tcp->negOpt = tcp->synOpt;

    TcpSetState(tcp, TCP_SYN_SENT);

    TcpActiveConKeepTimer(tcp);
    TcpActiveInitialRexmitTimer(tcp);

    TcpXmitSynPkt(tcp);
}

int TcpAccept(Sock_t* sk, struct DP_Sockaddr* addr, DP_Socklen_t* addrlen, Sock_t** newSk)
{
    TcpSk_t* tcp = TcpSK(sk);
    TcpSk_t* childTcp;

    if (SOCK_IS_CLOSED(sk)) {
        return -ECONNABORTED;
    }

    if (!SOCK_IS_LISTENED(sk)) {
        return -EINVAL;
    }

    childTcp = LIST_FIRST(&tcp->complete);
    if (childTcp == NULL) {
        DP_ADD_ABN_STAT(DP_ACCEPT_NO_CHILD);
        return -EAGAIN;
    }

    if (addr != NULL) {
        int ret = sk->ops->getDstAddr(TcpSk2Sk(childTcp), NULL, addr, addrlen);
        if (ret != 0) {
            return -ECONNABORTED;
        }
    }

    LIST_REMOVE_HEAD(&tcp->complete, childTcp, childNode);
    SOCK_Deref(sk);

    tcp->childCnt--;
    childTcp->parent = NULL;

    if (LIST_IS_EMPTY(&tcp->complete)) {
        SOCK_UnsetState(sk, SOCK_STATE_READ);
    }

    SOCK_SET_CONNECTED(TcpSk2Sk(childTcp));
    *newSk = (Sock_t*)(childTcp);
    return 0;
}

static int TcpSetKeepIdleVal(uint32_t val, TcpSk_t* tcp)
{
    if (val < 1 || val > MAX_TCP_KEEPIDLE) {
        DP_ADD_ABN_STAT(DP_SETOPT_KPID_INVAL);
        return -EINVAL;
    }

    tcp->keepIdle = (uint16_t)TCP_SEC2_TICK(val);
    return 0;
}

static int TcpSetKeepIntvlVal(uint32_t val, TcpSk_t* tcp)
{
    if (val < 1 || val > MAX_TCP_KEEPINTVL) {
        DP_ADD_ABN_STAT(DP_SETOPT_KPIN_INVAL);
        return -EINVAL;
    }
    tcp->keepIntvl = (uint16_t)TCP_SEC2_TICK(val);
    return 0;
}

static int TcpSetKeepCntVal(uint32_t val, TcpSk_t* tcp)
{
    if (val < 1 || val > MAX_TCP_KEEPCNT) {
        DP_ADD_ABN_STAT(DP_SETOPT_KPCN_INVAL);
        return -EINVAL;
    }
    tcp->keepProbes = (uint8_t)val;
    return 0;
}

static int TcpSetNoDelay(uint32_t val, TcpSk_t* tcp)
{
    tcp->nodelay = val == 0 ? 0 : 1;
    return 0;
}

static int TcpSetCork(uint32_t val, TcpSk_t* tcp)
{
    // 开启CORK后又关闭场景需要发送数据报文避免小数据报文长期阻塞
    if (tcp->cork != 0 && val == 0) {
        tcp->cork = 0;
        Sock_t *sk = TcpSk2Sk(tcp);
        // 正在建链或者已经建链或者关闭状态下需要添加发送时间
        if (SOCK_IS_CONNECTING(sk) || SOCK_IS_CONNECTED(sk) || SOCK_IS_CLOSED(sk)) {
            TcpTsqAddLockQue(tcp, TCP_TSQ_SEND_DATA);
        }
    } else {
        tcp->cork = val == 0 ? 0 : 1;
    }
    return 0;
}

static int TcpSetMaxSeg(uint32_t val, TcpSk_t* tcp)
{
    // 配置超出范围，则本次配置不生效
    if (val < TCP_MSS_SPEC_MIN || val > TCP_MSS_SPEC_MAX) {
        DP_ADD_ABN_STAT(DP_SETOPT_MAXSEG_INVAL);
        return -EINVAL;
    }

    // 只在CLOSED以及LISTEN状态下设置才生效
    if (tcp->state == TCP_CLOSED || tcp->state == TCP_LISTEN) {
        tcp->mss = val;
        return 0;
    }

    DP_ADD_ABN_STAT(DP_SETOPT_MAXSEG_STAT);
    return -EISCONN;
}

static uint8_t TcpSecsTransToMaxRexmit(uint64_t seconds, uint32_t timeOut, uint32_t rtoMax)
{
    uint8_t ret = 0;
    if (seconds > 0) {
        uint64_t temp = timeOut;
        uint32_t calcTime = timeOut;
        ret = 1;
        while (seconds > temp && ret < TCP_MAX_REXMIT_TIMES) {
            ret++;

            calcTime <<= 1;
            if (calcTime > rtoMax) {
                calcTime = rtoMax;
            }

            temp += calcTime;
        }
    }

    return ret;
}

static uint32_t TcpMaxRexmitRetransToSecs(uint8_t maxRexmit, uint32_t timeOut, uint32_t rtoMax)
{
    uint32_t ret = 0;
    if (maxRexmit > 0) {
        uint8_t cnt = maxRexmit - 1;
        uint32_t calcTime = timeOut;
        ret = timeOut;
        while (cnt > 0) {
            calcTime <<= 1;
            if (calcTime > rtoMax) {
                calcTime = rtoMax;
            }
            ret += calcTime;
            cnt--;
        }
    }

    return ret;
}

static int TcpSetDeferAccept(const void *val, TcpSk_t* tcp)
{
    int32_t value = *(int32_t *)val;
    // 只在CLOSED以及LISTEN状态下设置才生效
    if (tcp->state == TCP_CLOSED || tcp->state == TCP_LISTEN) {
        if (value <= 0) {
            tcp->deferAccept = 0;
            tcp->maxRexmit = 0;
        } else {
            tcp->deferAccept = 1;
            tcp->maxRexmit = TcpSecsTransToMaxRexmit((uint64_t)value * 1000,  // 1000: 将 val 转换成秒
                                                     TCP_INITIAL_REXMIT_TICK * TCP_FAST_TIMER_INTERVAL_MS,
                                                     TCP_MAX_REXMIT_TICK * TCP_FAST_TIMER_INTERVAL_MS);
        }
        return 0;
    }

    DP_ADD_ABN_STAT(DP_SETOPT_DFAC_STAT);
    return -EISCONN;
}

int TcpSetSockOpt(Sock_t* sk, int level, int optName, const void* optVal, DP_Socklen_t optLen)
{
    if (level == DP_IPPROTO_IP) {
        return INET_Setsockopt(TcpInetSk(sk), optName, optVal, optLen);
    }

    if (optVal == NULL) {
        DP_ADD_ABN_STAT(DP_SETOPT_PARAM_INVAL);
        return -EFAULT;
    }

    if ((int)optLen < (int)sizeof(DP_Socklen_t) || level != DP_IPPROTO_TCP) {
        DP_ADD_ABN_STAT(DP_SETOPT_PARAM_INVAL);
        return -EINVAL;
    }

    TcpSk_t *tcp = TcpSK(sk);
    uint32_t val = (*(uint32_t *)optVal);
    int ret = 0;
    switch (optName) {
        case DP_TCP_KEEPIDLE:
            ret = TcpSetKeepIdleVal(val, tcp);
            break;
        case DP_TCP_KEEPINTVL:
            ret = TcpSetKeepIntvlVal(val, tcp);
            break;
        case DP_TCP_KEEPCNT:
            ret = TcpSetKeepCntVal(val, tcp);
            break;
        case DP_TCP_NODELAY:
            ret = TcpSetNoDelay(val, tcp);
            break;
        case DP_TCP_CORK:
            ret = TcpSetCork(val, tcp);
            break;
        case DP_TCP_MAXSEG:
            ret = TcpSetMaxSeg(val, tcp);
            break;
        case DP_TCP_DEFER_ACCEPT:
            ret = TcpSetDeferAccept(optVal, tcp);
            break;
        // iperf需要，临时打桩处理
        case DP_TCP_CONGESTION:
            break;
        default:
            DP_ADD_ABN_STAT(DP_SETOPT_NO_SUPPORT);
            return -ENOPROTOOPT;
    }

    return ret;
}

static int GetTcpInfo(TcpSk_t *tcp, TcpInfo_t *info, size_t len)
{
    if (len < sizeof(TcpInfo_t)) {
        DP_ADD_ABN_STAT(DP_GETOPT_INFO_INVAL);
        return -EINVAL;
    }
    (void)memset_s(info, sizeof(TcpInfo_t), 0, sizeof(TcpInfo_t));
    info->tcpSndMSS = tcp->mss;
    info->tcpRtt = tcp->rttval;
    info->tcpSndCwnd = tcp->cwnd;
    return 0;
}

static int CheckOptIsValid(int level, void* optVal, DP_Socklen_t* optLen)
{
    if ((optVal == NULL) || (optLen == NULL)) {
        return -EFAULT;
    }

    if ((int)*optLen < (int)sizeof(int)) {
        return -EINVAL;
    }

    if (level != DP_IPPROTO_TCP) {
        return -EINVAL;
    }
    return 0;
}

int TcpGetSockOpt(Sock_t* sk, int level, int optName, void* optVal, DP_Socklen_t* optLen)
{
    if (level == DP_IPPROTO_IP) {
        return INET_Getsockopt(TcpInetSk(sk), optName, optVal, optLen);
    }

    int ret = CheckOptIsValid(level, optVal, optLen);
    if (ret != 0) {
        DP_ADD_ABN_STAT(DP_GETOPT_PARAM_INVAL);
        return ret;
    }

    TcpSk_t *tcp = TcpSK(sk);
    ret = 0;
    switch (optName) {
        case DP_TCP_KEEPIDLE:
            *(uint32_t *)optVal = (uint32_t)TCP_TICK2_SEC(tcp->keepIdle);
            break;
        case DP_TCP_KEEPINTVL:
            *(uint32_t *)optVal = (uint32_t)TCP_TICK2_SEC(tcp->keepIntvl);
            break;
        case DP_TCP_KEEPCNT:
            *(uint32_t *)optVal = (uint32_t)tcp->keepProbes;
            break;
        case DP_TCP_NODELAY:
            *(uint32_t *)optVal = (uint32_t)tcp->nodelay;
            break;
        case DP_TCP_CORK:
            *(uint32_t *)optVal = (uint32_t)tcp->cork;
            break;
        case DP_TCP_MAXSEG:
            *(uint32_t *)optVal = (uint32_t)tcp->mss;
            break;
        case DP_TCP_DEFER_ACCEPT:
            *(uint32_t*)optVal = TcpMaxRexmitRetransToSecs(tcp->maxRexmit,
                TCP_INITIAL_REXMIT_TICK * TCP_FAST_TIMER_INTERVAL_MS, TCP_MAX_REXMIT_TICK * TCP_FAST_TIMER_INTERVAL_MS);
            break;
        case DP_TCP_CONGESTION:
            // iperf需要，临时打桩处理
            *(uint32_t *)optVal = 0;
            break;
        case DP_TCP_INFO:
            ret = GetTcpInfo(tcp, optVal, *optLen);
            break;
        default:
            DP_ADD_ABN_STAT(DP_GETOPT_NO_SUPPORT);
            ret = -ENOPROTOOPT;
            break;
    }

    if (ret == 0 && optName != DP_TCP_INFO) {
        *optLen = sizeof(int);
    }
    return ret;
}

static ssize_t TcpPushSndBuf(Sock_t* sk, const struct DP_Msghdr* msg, uint16_t mss,
                             size_t space, size_t* index, size_t* offset)
{
    uint16_t           headroom = 128;
    ssize_t            ret      = 0;
    ssize_t            written  = 0;
    size_t             i        = *index;
    size_t             off      = *offset;
    size_t             remain   = space;
    size_t             len;
    struct DP_Iovec*   iov;

    uint32_t oldPktCnt = sk->sndBuf.pktCnt;
    while (i < msg->msg_iovlen) {
        iov = msg->msg_iov + i;
        if (iov->iov_base == NULL || iov->iov_len <= 0) {
            i++;
            continue;
        }

        len = remain < (iov->iov_len - off) ? remain : (iov->iov_len - off);
        written = PBUF_ChainWrite(&sk->sndBuf, (uint8_t*)iov->iov_base + off, len, mss, headroom);
        if (written <= 0) {         // 内存申请失败
            break;
        }
        off += (size_t)written;
        ret += written;
        if (off == iov->iov_len) {
            i++;
            off = 0;
        }
        remain -= (size_t)written;
        if (remain == 0) {
            break;
        }
    }
    *index = i;
    *offset = off;
    TcpSk_t* tcp = TcpSK(sk);
    DP_ADD_PKT_STAT(tcp->wid, DP_PKT_SEND_BUF_IN, sk->sndBuf.pktCnt - oldPktCnt);
    return ret;
}

ssize_t TcpSendmsg(Sock_t* sk, const struct DP_Msghdr* msg, int flags, size_t msgDataLen, size_t* index, size_t* offset)
{
    ssize_t  ret = 0;
    TcpSk_t* tcp = TcpSK(sk);
    size_t  space;

    (void)flags;
    (void)msgDataLen;

    if (!SOCK_IS_CONNECTED(sk)) { // 未建链或者建链阶段发生异常
        DP_ADD_ABN_STAT(DP_TCP_SND_CONN_CLOSED);
        return -ENOTCONN;
    } else if (SOCK_IS_CONN_REFUSED(sk)) {
        DP_ADD_ABN_STAT(DP_TCP_SND_CONN_REFUSED);
        if (sk->error != 0) {
            ret = -sk->error;
            sk->error = 0;
            return ret;
        }
    }

    if (!SOCK_CAN_SEND_MORE(sk)) {
        DP_ADD_ABN_STAT(DP_TCP_SND_CANT_SEND);
        return -EPIPE;
    }

    if (sk->family == DP_AF_INET && TcpInetDevIsUp(sk) != 0) {
        return -ENETDOWN;
    }

    space = TcpGetSndSpace(tcp);
    if (space == 0) {
        SOCK_UNSET_WRITABLE(sk);
        DP_ADD_ABN_STAT(DP_TCP_SND_NO_SPACE);
        return -EAGAIN;
    }

    ret = TcpPushSndBuf(sk, msg, tcp->mss, space, index, offset);
    if (ret <= 0) {
        DP_ADD_ABN_STAT(DP_TCP_SND_BUF_NOMEM);
        return -ENOMEM;
    }

    TcpTsqAddLockQue(tcp, TCP_TSQ_SEND_DATA);
    return ret;
}

static void TcpTrySendWndUp(Sock_t* sk)
{
    uint32_t rcvWnd = TcpGetRcvWnd(TcpSK(sk));
    uint32_t newWnd = TcpSelectRcvWnd(TcpSK(sk));

    if (TcpSK(sk)->delayAckEnable != 0) {
        /* 延迟 ACK 打开，回复窗口更新Ack条件：
           1. 窗口扩大为原来的两倍
         */
        if (newWnd >= 2 * rcvWnd) { // 2: 新窗口大于旧窗口的两倍
            TcpTsqAddLockQue(TcpSK(sk), TCP_TSQ_SEND_WND_UP);
        }
    } else {
        /* 延迟 ACK 关闭，回复窗口更新Ack条件：
           1. 如果数据被收空
           2. 如果新窗口>mss 或者 新窗口>缓冲区/2
         */
        if ((sk->rcvBuf.bufLen == 0 && TcpSK(sk)->rcvQue.bufLen == 0) ||
            (newWnd > TcpSK(sk)->mss || newWnd > (sk->rcvHiwat / 2))) { // 2: 新窗口>缓冲区/2时更新窗口
            TcpTsqAddLockQue(TcpSK(sk), TCP_TSQ_SEND_WND_UP);
        }
    }
}

ssize_t TcpRecvmsg(Sock_t* sk, struct DP_Msghdr* msg, int flags, size_t msgDataLen)
{
    ssize_t ret;

    if (!SOCK_IS_CONNECTED(sk)) { // 未建链或者建链阶段发生异常
        DP_ADD_ABN_STAT(DP_TCP_RCV_CONN_CLOSED);
        return -ENOTCONN;
    } else if (SOCK_IS_CONN_REFUSED(sk)) {
        DP_ADD_ABN_STAT(DP_TCP_RCV_CONN_REFUSED);
        return -ECONNRESET;
    }

    uint32_t oldCnt = sk->rcvBuf.pktCnt;
    ret = SOCK_PopRcvBuf(sk, msg, flags, msgDataLen);
    if (ret > 0) {
        TcpTrySendWndUp(sk);
    }

    if (((uint32_t)flags & DP_MSG_PEEK) == 0) {
        DP_ADD_PKT_STAT(TcpSK(sk)->wid, DP_PKT_RECV_BUF_OUT, oldCnt - sk->rcvBuf.pktCnt);
    }

    return ret;
}

static uint8_t CalcWs(TcpSk_t* tcp)
{
    uint8_t ret = 0;

    while ((TcpSk2Sk(tcp)->rcvHiwat >> ret) > 0xFFFF) {
        ret++;
    }

    return ret > 14 ? 14 : ret;  // 14: tcp 的窗口缩放因子最大为 14
}

void TcpInitTcpSk(TcpSk_t* tcp)
{
    tcp->state  = TCP_CLOSED;
    tcp->caCb   = NULL;
    tcp->caMeth = TcpCaGet(-1);

    tcp->synOpt = TCP_SYN_OPTIONS;
    if (CFG_GET_TCP_VAL(DP_CFG_TCP_SELECT_ACK) == DP_ENABLE) {
        tcp->synOpt |= TCP_SYN_OPT_SACK_PERMITTED;
    }
    tcp->mss    = 0;
    tcp->rcvWs  = CalcWs(tcp);

    tcp->wid        = -1;
    tcp->txQueid    = -1;
    tcp->accDataMax = 2; // 当前默认值为 2 ，后续更改为CFG预配置
    tcp->keepProbes = TCP_DEFAULT_KEEP_CNT;
    tcp->keepIntvl  = TCP_DEFAULT_KEEP_INTVL;
    tcp->delayAckEnable = CFG_GET_TCP_VAL(DP_CFG_TCP_DELAY_ACK);
    tcp->keepIdle  = TCP_DEFAULT_KEEP_IDLE;
    tcp->connType  = TCP_ACTIVE;
    // 当前重复ACK次数默认为3，后续可考虑更改为CFG配置
    tcp->reorderCnt = 3;

    for (int i = 0; i < TCP_TIMERID_BUTT; i++) {
        tcp->expiredTick[i] = TCP_TIMER_TICK_INVALID;
    }

    PBUF_ChainInit(&tcp->rcvQue);
    PBUF_ChainInit(&tcp->rexmitQue);
    PBUF_ChainInit(&tcp->reassQue);
}

void TcpInitChildTcpSk(Sock_t* newsk, Sock_t* parent, Pbuf_t* pbuf, DP_TcpHdr_t* tcpHdr)
{
    newsk->family = DP_AF_INET;
    newsk->ops    = parent->ops;
    SOCK_SET_CONNECTING(newsk);

    TcpInitTcpSk(TcpSK(newsk));

    TcpSK(newsk)->caMeth = TcpSK(parent)->caMeth;
    TcpSK(newsk)->synOpt = TcpSK(parent)->synOpt;
    TcpSK(newsk)->mss    = TcpSK(parent)->mss;
    TcpSK(newsk)->rcvWs  = TcpSK(parent)->rcvWs;

    TcpSK(newsk)->wid     = PBUF_GET_WID(pbuf);
    TcpSK(newsk)->txQueid = NETDEV_GetTxQueid(PBUF_GET_DEV(pbuf), PBUF_GET_QUE_ID(pbuf));
    TcpSK(newsk)->irs     = UTILS_NTOHL(tcpHdr->seq);
    TcpSK(newsk)->sndWnd  = UTILS_NTOHS(tcpHdr->win);
    TcpSK(newsk)->rcvNxt  = TcpSK(newsk)->irs + 1;
    TcpSK(newsk)->rcvWup  = TcpSK(newsk)->rcvNxt;
    TcpSK(newsk)->parent  = TcpSK(parent);
    TcpSK(newsk)->connType     = TCP_PASSIVE;
    TcpSK(newsk)->deferAccept  = TcpSK(parent)->deferAccept;
    TcpSK(newsk)->cork         = TcpSK(parent)->cork;
    TcpSK(newsk)->maxRexmit    = TcpSK(parent)->maxRexmit;
    TcpSK(newsk)->keepIdle     = TcpSK(parent)->keepIdle;
    TcpSK(newsk)->keepIntvl    = TcpSK(parent)->keepIntvl;
    TcpSK(newsk)->keepProbes   = TcpSK(parent)->keepProbes;
}

// 该接口用于清理socket除接收队列外的所有队列，通知用户接受接收队列中的数据
void TcpCleanUp(TcpSk_t* tcp)
{
    if (tcp->state != TCP_ABORT && tcp->state != TCP_CLOSED) {
        TcpRemoveAllTimers(tcp);

        if (TcpSk2Sk(tcp)->family == DP_AF_INET) {
            g_tcpInetOps->connectTblRemove(TcpSk2Sk(tcp));
            g_tcpInetOps->unhash(TcpSk2Sk(tcp));
        } else {
            g_tcpInet6Ops->unhash(TcpSk2Sk(tcp));
        }
    }
}

int TcpSetKeepAlive(Sock_t *sk, int enable)
{
    TcpSk_t *tcp = TcpSK(sk);
    // 建链状态之前不需要考虑保活定时器触发
    if (tcp->state >= TCP_ESTABLISHED) {
        if (sk->keepalive != 0 && enable == 0)  {
            TcpTsqAddLockQue(tcp, TCP_TSQ_KEEPALIVE_OFF);
        } else if (sk->keepalive == 0 && enable != 0) {
            TcpTsqAddLockQue(tcp, TCP_TSQ_KEEPALIVE_ON);
        }
    }
    sk->keepalive = enable;
    return 0;
}

void TcpRemoveFromParentList(TcpSk_t* tcp)
{
    TcpSk_t* parent = tcp->parent;
    SOCK_Lock(TcpSk2Sk(parent));
    if (tcp->state >= TCP_ESTABLISHED) {
        LIST_REMOVE(&parent->complete, tcp, childNode);
    } else {
        LIST_REMOVE(&parent->uncomplete, tcp, childNode);
    }
    TcpSK(parent)->childCnt--;
    if (SOCK_Deref(TcpSk2Sk(parent)) == 0) {
        SOCK_Unlock(TcpSk2Sk(parent));
        TcpFreeSk(TcpSk2Sk(parent));
        return;
    }
    SOCK_Unlock(TcpSk2Sk(parent));
}

int TcpClose(Sock_t* sk)
{
    if (SOCK_IS_LISTENED(sk)) {
        TcpRemoveListener(sk);
        TcpWaitIdle(sk);
        TcpSk_t *tcp = TcpSK(sk);
        TcpSk_t *child = NULL;

        LIST_FOREACH(&tcp->uncomplete, child, childNode)
        {
            SOCK_SET_CLOSED(TcpSk2Sk(child));
            TcpTsqAddLockQue(child, TCP_TSQ_DISCONNECT);
        }
        LIST_FOREACH(&tcp->complete, child, childNode)
        {
            SOCK_SET_CLOSED(TcpSk2Sk(child));
            TcpTsqAddLockQue(child, TCP_TSQ_DISCONNECT);
        }
        TcpSetState(tcp, TCP_CLOSED);
        if (SOCK_Deref(sk) == 0) {
            SOCK_Unlock(sk);
            TcpFreeSk(sk);
            return 0;
        }
        goto out;
    }

    if (SOCK_IS_CONNECTED(sk) || SOCK_IS_CONNECTING(sk)) {
        TcpTsqAddLockQue(TcpSK(sk), TCP_TSQ_DISCONNECT);
        goto out;
    }

    if (SOCK_IS_BINDED(sk)) {
        TcpGlobalRemove(sk);
    }

    SOCK_Unlock(sk);
    TcpFreeSk(sk);

    return 0;
out:
    SOCK_Unlock(sk);
    return 0;
}

void TcpFreeSk(Sock_t* sk)
{
    // 用户主动调用CLOSE,或者子socket触发异常事件场景下释放SK资源
    if (SOCK_IS_CLOSED(sk) || TcpSK(sk)->parent != NULL) {
        // 释放资源前应该将剩余锁队列事件清除
        TcpTsqTryRemoveLockQue(TcpSK(sk));
        TcpTsqTryRemoveNoLockQue(TcpSK(sk));

        if (sk->family == DP_AF_INET) {
            g_tcpInetOps->freeFunc(sk);
        } else {
            g_tcpInet6Ops->freeFunc(sk);
        }
    }
}
