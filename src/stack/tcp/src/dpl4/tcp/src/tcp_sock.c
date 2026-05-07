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

#include "dp_socket_api.h"
#include "dp_socket_types_api.h"

#include "tcp_tsq.h"
#include "tcp_cc.h"
#include "tcp_timer.h"
#include "tcp_out.h"
#include "tcp_inet.h"
#include "tcp_reass.h"
#include "tcp_in.h"
#include "tcp_bbr.h"

#include "dp_tcp.h"

#include "sock.h"

#include "utils_statistic.h"
#include "utils_base.h"
#include "utils_debug.h"
#include "utils_cfg.h"
#include "tcp_sock.h"

// TCP MSS配置最小值
#define TCP_MSS_SPEC_MIN 88
// TCP MSS配置最大值
#define TCP_MSS_SPEC_MAX 32767
// 最大重试次数
#define TCP_MAX_REXMIT_TIMES 255
// 最大SYN重传次数
#define TCP_MAX_SYN_RETIRES 6
// 默认保活探测次数
#define TCP_DEFAULT_KEEP_CNT 9
// 默认保活时间间隔2h
#define TCP_DEFAULT_KEEP_IDLE TCP_SEC2_TICK(120 * 60)
// 默认保活探测时间间隔 75s
#define TCP_DEFAULT_KEEP_INTVL TCP_SEC2_TICK(75)
// 用户设置的重传间隔不得高于20 tick 即 200ms
#define TCP_MAX_USER_SETTING_RXMT_INTER 200
// 最大逐包ACK数量
#define TCP_MAX_QUICKACK_NUM 0xffff

TcpFamilyOps_t* g_tcpInetOps  = NULL;
TcpFamilyOps_t* g_tcpInet6Ops = NULL;

__thread long int g_tcpCacheTid = 0;

typedef struct {
    int optname;
    int (*get)(TcpSk_t* tcp, void* optVal, DP_Socklen_t* optLen);
    int (*set)(TcpSk_t* tcp, const void* optVal, DP_Socklen_t optLen);
} TcpSockOptOps_t;

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

static int ProcChildorLingerClose(Sock_t* sk)
{
    TcpSk_t* tcp = TcpSK(sk);
    bool needClose = false;
    // 调用close且开启linger则直接断链, 如果用户调用shutdown则进行正常断链
    if (tcp->parent != NULL) {
        needClose = true;
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_CHILD_CLOSE);
    } else if ((SOCK_IS_CLOSED(sk) && (sk->lingerOnoff != 0))) {
        needClose = true;
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_LINGER_CLOSE);
    }

    if (needClose) {
        TcpXmitRstAckPkt(tcp);
        TcpDone(tcp);
        return 1;
    }

    return 0;
}

static int TcpProcDisconnRcvData(Sock_t* sk)
{
    TcpSk_t* tcp = TcpSK(sk);

    if (!SOCK_IS_CLOSED(sk) || (sk->rcvBuf.bufLen == 0 && tcp->rcvQue.bufLen == 0)) {
        return 0;
    }

    DP_ADD_TCP_STAT(tcp->wid, DP_TCP_CLOSE_NORCV_DATALEN, tcp->rcvQue.bufLen);
    DP_ADD_TCP_STAT(tcp->wid, DP_TCP_CLOSE_NORCV_DATALEN, sk->rcvBuf.bufLen);

    if (CFG_GET_TCP_VAL(CFG_TCP_CLOSE_NO_RST) == DP_ENABLE) {
        PBUF_ChainClean(&tcp->rcvQue);
        PBUF_ChainClean(&sk->rcvBuf);
        return 0;
    }

    DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCVBUF_NOT_CLEAN);
    TcpXmitRstAckPkt(tcp);
    TcpDone(tcp);
    return -1;
}

int TcpDisconnect(Sock_t* sk)
{
    TcpSk_t* tcp = TcpSK(sk);

    if (SOCK_IS_CLOSED(sk)) {
        TCP_SET_CLOSED(tcp);
    }

    // 尚未建链直接关闭
    if (tcp->state == TCP_SYN_SENT) {
        TcpDone(tcp);
        return -1;
    }

    if (ProcChildorLingerClose(sk) != 0) {
        return -1;
    }

    if (TcpState(tcp) == TCP_SYN_RECV || TcpState(tcp) == TCP_ESTABLISHED) {
        TcpSetState(tcp, TCP_FIN_WAIT1);
    } else if (TcpState(tcp) == TCP_CLOSE_WAIT) { // TCP_CLOSE_WAIT
        TcpSetState(tcp, TCP_LAST_ACK);
    } else {
        if (TcpState(tcp) == TCP_FIN_WAIT2) {   // 由于调用shutdown写进入fin_wait2状态，主动调用close后启动fin_wait定时器
            TcpActiveFinWaitTimer(tcp);
        }
        return 0;
    }

    /* 在主动调用close情况下
     * 1. 如果收缓冲区有数据，无论发缓冲区是否有数据，发送RST/ACK报文，释放socket
     * 2. 如果收缓冲区无数据，发缓冲区有数据，继续发送报文
     * 3. 收、发缓冲区均无数据，发送FIN/ACK报文
     */
    if (TcpProcDisconnRcvData(sk) != 0) {
        return -1;
    }

    if (tcp->sndQue.bufLen == 0 && sk->sndBuf.bufLen == 0) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_FIN);
        TcpXmitFinAckPkt(tcp);
    } else {
        // 共线程调度时，直接发送缓冲区数据
        if (CFG_GET_TCP_VAL(CFG_TCP_TSQ_PASSIVE) == DP_ENABLE) {
            PBUF_ChainConcat(&TcpSK(sk)->sndQue, &sk->sndBuf);
            TcpXmitData(tcp, 0, 0);
        } else {
            TcpTsqAddQue(tcp, TCP_TSQ_SEND_DATA);
        }
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
    if (sk->error != 0) {
        SOCK_WakeupWrSem(sk);
        SOCK_Unlock(sk);
        return;
    }

    SOCK_Unlock(sk);

    tcp->sndUp  = 0;
    tcp->sndWl1 = 0;

    tcp->negOpt = tcp->synOpt;

    TcpSetState(tcp, TCP_SYN_SENT);

    DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_SYN);
    TcpActiveConKeepTimer(tcp);
    TcpActiveInitialRexmitTimer(tcp);
    tcp->startConn = UTILS_TimeNow();
    TcpXmitSynPkt(tcp);
    tcp->rttFlag = 1;
}

int TcpAccept(Sock_t* sk, struct DP_Sockaddr* addr, DP_Socklen_t* addrlen, Sock_t** newSk)
{
    TcpSk_t* tcp = TcpSK(sk);
    TcpSk_t* childTcp;

    if (SOCK_IS_CLOSED(sk)) {
        DP_LOG_DBG("TcpAccept failed, socket is closed.");
        DP_ADD_ABN_STAT(DP_ACCEPT_CLOSED);
        return -ECONNABORTED;
    }

    if (!SOCK_IS_LISTENED(sk)) {
        DP_LOG_DBG("TcpAccept failed, socket is not listen.");
        DP_ADD_ABN_STAT(DP_ACCEPT_NOT_LISTENED);
        return -EINVAL;
    }

    childTcp = LIST_FIRST(&tcp->complete);
    if (childTcp == NULL) {
        DP_ADD_ABN_STAT(DP_ACCEPT_NO_CHILD);
        DP_LOG_DBG("TcpAccept failed, complete list has no child.");
        return -EAGAIN;
    }

    SOCK_Lock(TcpSk2Sk(childTcp));
    if (addr != NULL) {
        int ret = sk->ops->getDstAddr(TcpSk2Sk(childTcp), NULL, addr, addrlen);
        if (ret != 0) {
            SOCK_Unlock(TcpSk2Sk(childTcp));
            DP_LOG_DBG("TcpAccept failed, get dstAddr failed.");
            DP_ADD_ABN_STAT(DP_ACCEPT_GET_ADDR_FAILED);
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
    SOCK_Unlock(TcpSk2Sk(childTcp));
    *newSk = (Sock_t*)(childTcp);
    DP_INC_TCP_STAT(childTcp->wid, DP_TCP_USER_ACCEPT);
    return 0;
}

static int TcpSetKeepIdleVal(TcpSk_t* tcp, const void* optVal, DP_Socklen_t optLen)
{
    if ((int)optLen < (int)sizeof(DP_Socklen_t)) {
        DP_ADD_ABN_STAT(DP_SETOPT_PARAM_INVAL);
        return -EINVAL;
    }

    uint32_t val = (*(uint32_t *)optVal);
    if (val < 1 || val > MAX_TCP_KEEPIDLE) {
        DP_ADD_ABN_STAT(DP_SETOPT_KPID_INVAL);
        return -EINVAL;
    }

    tcp->keepIdle = (uint32_t)TCP_SEC2_TICK(val);

    if (SOCK_IS_CONN_REFUSED(TcpSk2Sk(tcp))) {
        return 0;
    }

    // 大于listen的状态才存在tsq队列，保活状态为开启时，则需要重启保活定时器，刷新时间
    if (tcp->state > TCP_LISTEN && TcpSk2Sk(tcp)->keepalive == 1) {
        TcpTsqAddLockQue(tcp, TCP_TSQ_KEEPALIVE_ON, true);
    }
    return 0;
}

static int TcpSetKeepIntvlVal(TcpSk_t* tcp, const void* optVal, DP_Socklen_t optLen)
{
    if ((int)optLen < (int)sizeof(DP_Socklen_t)) {
        DP_ADD_ABN_STAT(DP_SETOPT_PARAM_INVAL);
        return -EINVAL;
    }

    uint32_t val = (*(uint32_t *)optVal);
    if (val < 1 || val > MAX_TCP_KEEPINTVL) {
        DP_ADD_ABN_STAT(DP_SETOPT_KPIN_INVAL);
        return -EINVAL;
    }
    tcp->keepIntvl = (uint32_t)TCP_SEC2_TICK(val);
    return 0;
}

static int TcpSetKeepCntVal(TcpSk_t* tcp, const void* optVal, DP_Socklen_t optLen)
{
    if ((int)optLen < (int)sizeof(DP_Socklen_t)) {
        DP_ADD_ABN_STAT(DP_SETOPT_PARAM_INVAL);
        return -EINVAL;
    }

    uint32_t val = (*(uint32_t *)optVal);
    if (val < 1 || val > MAX_TCP_KEEPCNT) {
        DP_ADD_ABN_STAT(DP_SETOPT_KPCN_INVAL);
        return -EINVAL;
    }
    tcp->keepProbes = (uint8_t)val;
    return 0;
}

static int TcpSetNoDelay(TcpSk_t* tcp, const void* optVal, DP_Socklen_t optLen)
{
    if ((int)optLen < (int)sizeof(DP_Socklen_t)) {
        DP_ADD_ABN_STAT(DP_SETOPT_PARAM_INVAL);
        return -EINVAL;
    }

    uint32_t val = (*(uint32_t *)optVal);
    tcp->nodelay = val == 0 ? 0 : 1;
    return 0;
}

static int TcpSetCork(TcpSk_t* tcp, const void* optVal, DP_Socklen_t optLen)
{
    if ((int)optLen < (int)sizeof(DP_Socklen_t)) {
        DP_ADD_ABN_STAT(DP_SETOPT_PARAM_INVAL);
        return -EINVAL;
    }

    uint32_t val = (*(uint32_t *)optVal);
    // 开启CORK后又关闭场景需要发送数据报文避免小数据报文长期阻塞
    if (tcp->cork != 0 && val == 0) {
        tcp->cork = 0;
        Sock_t *sk = TcpSk2Sk(tcp);
        // 正在建链或者已经建链或者关闭状态下需要添加发送时间
        if (SOCK_IS_CONNECTING(sk) || SOCK_IS_CONNECTED(sk) || SOCK_IS_CLOSED(sk)) {
            TcpTsqAddLockQue(tcp, TCP_TSQ_SEND_DATA, true);
        }
    } else {
        tcp->cork = val == 0 ? 0 : 1;
    }
    return 0;
}

static int TcpSetMaxSeg(TcpSk_t* tcp, const void* optVal, DP_Socklen_t optLen)
{
    if ((int)optLen < (int)sizeof(DP_Socklen_t)) {
        DP_ADD_ABN_STAT(DP_SETOPT_PARAM_INVAL);
        return -EINVAL;
    }

    uint32_t val = (*(uint32_t *)optVal);
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

static int TcpSetDeferAccept(TcpSk_t* tcp, const void* optVal, DP_Socklen_t optLen)
{
    if ((int)optLen < (int)sizeof(DP_Socklen_t)) {
        DP_ADD_ABN_STAT(DP_SETOPT_PARAM_INVAL);
        return -EINVAL;
    }

    int32_t value = *(int32_t *)optVal;
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

static int IpSetSockOpt(Sock_t* sk, int level, int optName, const void* optVal, DP_Socklen_t optLen)
{
    if (level == DP_IPPROTO_IP) {
        if (sk->family != DP_AF_INET) {
            DP_ADD_ABN_STAT(DP_SETOPT_PARAM_INVAL);
            DP_LOG_DBG("IpSetSockOpt failed, level is DP_IPPROTO_IP, sk->family = %d.", sk->family);
            return -EINVAL;
        }
        return INET_Setsockopt(TcpInetSk(sk), optName, optVal, optLen);
    }
    
    return -EINVAL;
}

static int TcpSetUserTimeout(TcpSk_t* tcp, const void* optVal, DP_Socklen_t optLen)
{
    if ((int)optLen < (int)sizeof(DP_Socklen_t)) {
        DP_ADD_ABN_STAT(DP_SETOPT_PARAM_INVAL);
        return -EINVAL;
    }

    uint32_t val = (*(uint32_t *)optVal);
    tcp->userTimeout = val;
    return 0;
}

static int TcpSetSynRetries(TcpSk_t* tcp, const void* optVal, DP_Socklen_t optLen)
{
    if ((int)optLen < (int)sizeof(DP_Socklen_t)) {
        DP_ADD_ABN_STAT(DP_SETOPT_PARAM_INVAL);
        return -EINVAL;
    }

    uint32_t val = (*(uint32_t *)optVal);

    if (val > TCP_MAX_SYN_RETIRES) {
        return -EINVAL;
    }

    tcp->synRetries = val;
    return 0;
}

static int TcpSetKeepIdleLimit(TcpSk_t* tcp, const void* optVal, DP_Socklen_t optLen)
{
    if ((int)optLen < (int)sizeof(DP_Socklen_t)) {
        DP_ADD_ABN_STAT(DP_SETOPT_PARAM_INVAL);
        return -EINVAL;
    }

    uint32_t val = (*(uint32_t *)optVal);

    if (val > INT_MAX) {
        return -EINVAL;
    }

    tcp->keepIdleLimit = val;
    return 0;
}

static int TcpSetQuickAckNum(TcpSk_t* tcp, const void* optVal, DP_Socklen_t optLen)
{
    if ((int)optLen < (int)sizeof(DP_Socklen_t)) {
        DP_ADD_ABN_STAT(DP_SETOPT_PARAM_INVAL);
        return -EINVAL;
    }

    uint32_t val = (*(uint32_t *)optVal);
    if (val > TCP_MAX_QUICKACK_NUM) {
        return -EINVAL;
    }

    // val不超过0xffff直接强转无风险
    tcp->quickAckNum = (uint16_t)val;
    return 0;
}

static int TcpSetCCAlgorithm(TcpSk_t* tcp, const void* optVal, DP_Socklen_t optLen)
{
    char caMethName[DP_TCP_CA_NAME_MAX_LEN] = {0};
    if (optLen < 1) {
        return -EINVAL;
    }

    if (strncpy_s(caMethName, DP_TCP_CA_NAME_MAX_LEN, (const char *)optVal, optLen) != 0) {
        return -EFAULT;
    }

    if (UTILS_CheckCaAlgValid(caMethName) != 0) {
        return -EPERM;
    }
    /* 与原算法一致，不执行修改，直接返回 */
    if (tcp->caMeth != NULL && strncmp(caMethName, tcp->caMeth->algName, optLen) == 0) {
        return 0;
    }

    const DP_TcpCaMeth_t *tempCaMeth = TcpCaGetByName(caMethName);

    /* 不存在此名字的拥塞控制算法 */
    if (tempCaMeth == NULL) {
        DP_ADD_ABN_STAT(DP_SETOPT_CA_INVAL);
        return -EINVAL;
    }

    // 只有未开始建链时可以直接修改拥塞算法
    if (!SOCK_IS_CONNECTING(TcpSk2Sk(tcp)) && !SOCK_IS_CONNECTED(TcpSk2Sk(tcp))) {
        tcp->caMeth = tempCaMeth;
        return 0;
    }

    tcp->nextCaMethId = tempCaMeth->algId;

    // 正在建链或者已经建链状态下需要添加TSQ事件
    TcpTsqAddLockQue(tcp, TCP_TSQ_CC_MOD, true);

    return 0;
}

static int GetTcpInfo(TcpSk_t* tcp, void* optVal, DP_Socklen_t* optLen)
{
    if (*optLen < sizeof(DP_TcpInfo_t)) {
        DP_ADD_ABN_STAT(DP_GETOPT_INFO_INVAL);
        return -EINVAL;
    }
    *optLen = sizeof(DP_TcpInfo_t);
    DP_TcpInfo_t *info = (DP_TcpInfo_t *)optVal;
    (void)memset_s(info, sizeof(DP_TcpInfo_t), 0, sizeof(DP_TcpInfo_t));

    info->tcpState = tcp->state;
    info->tcpCaState = tcp->caState;

    info->tcpSndWScale = tcp->sndWs;
    info->tcpRcvWScale = tcp->rcvWs;

    info->tcpRtt = tcp->rttval;
    info->tcpRttVar = tcp->srtt;
    info->tcpSndMSS = tcp->mss;
    info->tcpRcvMSS = tcp->rcvMss;

    info->tcpTotalRetrans = tcp->rexmitCnt;

    info->tcpSndCwnd = tcp->cwnd;
    info->tcpSndWnd = tcp->sndWnd;
    info->tcpRcvWnd = tcp->rcvWnd;

    info->tcpRcvDrops = tcp->rcvDrops;
    info->tcpMaxRtt = tcp->maxRtt;
    info->tcpConnLatency = tcp->connLatency;
    info->tcpMss = tcp->mss + (TcpNegTs(tcp) ? DP_TCPOLEN_TSTAMP_APPA : 0);

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

static int IpGetSockOpt(Sock_t* sk, int level, int optName, void* optVal, DP_Socklen_t* optLen)
{
    if (level == DP_IPPROTO_IP) {
        if (sk->family != DP_AF_INET) {
            DP_ADD_ABN_STAT(DP_GETOPT_PARAM_INVAL);
            DP_LOG_DBG("IpGetSockOpt failed, level is DP_IPPROTO_IP, sk->family = %d.", sk->family);
            return -EINVAL;
        }
        return INET_Getsockopt(TcpInetSk(sk), optName, optVal, optLen);
    }

    return -EINVAL;
}

static int TcpGetKeepIdleVal(TcpSk_t* tcp, void* optVal, DP_Socklen_t* optLen)
{
    *(uint32_t *)optVal = (uint32_t)TCP_TICK2_SEC(tcp->keepIdle);
    *optLen = sizeof(int);
    return 0;
}

static int TcpGetKeepIntvlVal(TcpSk_t* tcp, void* optVal, DP_Socklen_t* optLen)
{
    *(uint32_t *)optVal = (uint32_t)TCP_TICK2_SEC(tcp->keepIntvl);
    *optLen = sizeof(int);
    return 0;
}

static int TcpGetKeepCntVal(TcpSk_t* tcp, void* optVal, DP_Socklen_t* optLen)
{
    *(uint32_t *)optVal = (uint32_t)tcp->keepProbes;
    *optLen = sizeof(int);
    return 0;
}

static int TcpGetNoDelay(TcpSk_t* tcp, void* optVal, DP_Socklen_t* optLen)
{
    *(uint32_t *)optVal = (uint32_t)tcp->nodelay;
    *optLen = sizeof(int);
    return 0;
}

static int TcpGetCork(TcpSk_t* tcp, void* optVal, DP_Socklen_t* optLen)
{
    *(uint32_t *)optVal = (uint32_t)tcp->cork;
    *optLen = sizeof(int);
    return 0;
}

static int TcpGetMaxSeg(TcpSk_t* tcp, void* optVal, DP_Socklen_t* optLen)
{
    *(uint32_t *)optVal = (uint32_t)tcp->mss;
    *optLen = sizeof(int);
    return 0;
}

static int TcpGetDeferAccept(TcpSk_t* tcp, void* optVal, DP_Socklen_t* optLen)
{
    *(uint32_t*)optVal = TcpMaxRexmitRetransToSecs(tcp->maxRexmit,
        TCP_INITIAL_REXMIT_TICK * TCP_FAST_TIMER_INTERVAL_MS, TCP_MAX_REXMIT_TICK * TCP_FAST_TIMER_INTERVAL_MS);
    *optLen = sizeof(int);
    return 0;
}

static int TcpGetUserTimeout(TcpSk_t* tcp, void* optVal, DP_Socklen_t* optLen)
{
    *(uint32_t *)optVal = tcp->userTimeout;
    *optLen = sizeof(int);
    return 0;
}

static int TcpGetCongestion(TcpSk_t* tcp, void* optVal, DP_Socklen_t* optLen)
{
    (void)tcp;
    if (*optLen < DP_TCP_CA_NAME_MAX_LEN) {
        return -EINVAL;
    }
    const DP_TcpCaMeth_t* caMeth = (tcp->nextCaMethId == -1) ? tcp->caMeth : TcpCaGet(tcp->nextCaMethId);
    const char *algName = (caMeth != NULL) ? caMeth->algName : "";
    if (strncpy_s((char *)optVal, *optLen, algName, DP_TCP_CA_NAME_MAX_LEN) != 0) {
        return -EINVAL;
    }
    *optLen = DP_TCP_CA_NAME_MAX_LEN;
    return 0;
}

static int TcpGetSynRetries(TcpSk_t* tcp, void* optVal, DP_Socklen_t* optLen)
{
    *(uint32_t *)optVal = ((tcp->synRetries > 0) ? tcp->synRetries : (uint32_t)CFG_GET_TCP_VAL(DP_CFG_TCP_SYN_RETRIES));
    *optLen = sizeof(int);
    return 0;
}

static int TcpGetKeepIdleLimit(TcpSk_t* tcp, void* optVal, DP_Socklen_t* optLen)
{
    *(uint32_t *)optVal = tcp->keepIdleLimit;
    *optLen = sizeof(int);
    return 0;
}

static TcpSockOptOps_t g_tcpSockOptOps[] = {
    {DP_TCP_KEEPIDLE, TcpGetKeepIdleVal, TcpSetKeepIdleVal},
    {DP_TCP_KEEPINTVL, TcpGetKeepIntvlVal, TcpSetKeepIntvlVal},
    {DP_TCP_KEEPCNT, TcpGetKeepCntVal, TcpSetKeepCntVal},
    {DP_TCP_NODELAY, TcpGetNoDelay, TcpSetNoDelay},
    {DP_TCP_CORK, TcpGetCork, TcpSetCork},
    {DP_TCP_INFO, GetTcpInfo, NULL},
    {DP_TCP_MAXSEG, TcpGetMaxSeg, TcpSetMaxSeg},
    {DP_TCP_DEFER_ACCEPT, TcpGetDeferAccept, TcpSetDeferAccept},
    {DP_TCP_CONGESTION, TcpGetCongestion, TcpSetCCAlgorithm},
    {DP_TCP_USER_TIMEOUT, TcpGetUserTimeout, TcpSetUserTimeout},
    {DP_TCP_SYNCNT, TcpGetSynRetries, TcpSetSynRetries},
    {DP_TCP_KEEPIDLE_LIMIT, TcpGetKeepIdleLimit, TcpSetKeepIdleLimit},
    {DP_TCP_QUICKACKNUM, NULL, TcpSetQuickAckNum},
};

int TcpGetSockOpt(Sock_t* sk, int level, int optName, void* optVal, DP_Socklen_t* optLen)
{
    if ((level == DP_IPPROTO_IP) || (level == DP_IPPROTO_IPV6)) {
        return IpGetSockOpt(sk, level, optName, optVal, optLen);
    }

    int ret = CheckOptIsValid(level, optVal, optLen);
    if (ret != 0) {
        DP_ADD_ABN_STAT(DP_GETOPT_PARAM_INVAL);
        DP_LOG_DBG("TcpGetSockOpt failed, CheckOptIsValid failed.");
        return ret;
    }

    TcpSk_t *tcp = TcpSK(sk);
    for (size_t i = 0; i < DP_ARRAY_SIZE(g_tcpSockOptOps); i++) {
        if (g_tcpSockOptOps[i].optname != optName) {
            continue;
        }

        if (g_tcpSockOptOps[i].get == NULL) {
            break;
        }
        return g_tcpSockOptOps[i].get(tcp, optVal, optLen);
    }
    DP_ADD_ABN_STAT(DP_GETOPT_NO_SUPPORT);
    DP_LOG_DBG("TcpGetSockOpt failed, optName not support, optName = %d.", optName);
    return -ENOPROTOOPT;
}

int TcpSetSockOpt(Sock_t* sk, int level, int optName, const void* optVal, DP_Socklen_t optLen)
{
    if ((level == DP_IPPROTO_IP) || (level == DP_IPPROTO_IPV6)) {
        return IpSetSockOpt(sk, level, optName, optVal, optLen);
    }

    if (optVal == NULL) {
        DP_ADD_ABN_STAT(DP_SETOPT_PARAM_INVAL);
        DP_LOG_DBG("TcpSetSockOpt failed, optVal is NULL.");
        return -EFAULT;
    }

    if (level != DP_IPPROTO_TCP) {
        DP_ADD_ABN_STAT(DP_SETOPT_PARAM_INVAL);
        DP_LOG_DBG("TcpSetSockOpt failed, socket is tcp, level = %d.", level);
        return -EINVAL;
    }

    TcpSk_t *tcp = TcpSK(sk);
    for (size_t i = 0; i < DP_ARRAY_SIZE(g_tcpSockOptOps); i++) {
        if (g_tcpSockOptOps[i].optname != optName) {
            continue;
        }

        if (g_tcpSockOptOps[i].set == NULL) {
            break;
        }
        return g_tcpSockOptOps[i].set(tcp, optVal, optLen);
    }
    DP_ADD_ABN_STAT(DP_SETOPT_NO_SUPPORT);
    DP_LOG_ERR("TcpSetSockOpt failed, optName not support, optName = %d.", optName);
    return -ENOPROTOOPT;
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
    uint16_t           fragSize = PBUF_GetSegLen() - headroom;
    fragSize = fragSize > mss ? mss : fragSize;

    uint32_t oldPktCnt = sk->sndBuf.pktCnt;
    while (i < msg->msg_iovlen) {
        iov = msg->msg_iov + i;
        if (iov->iov_base == NULL || iov->iov_len <= 0) {
            i++;
            continue;
        }

        len = remain < (iov->iov_len - off) ? remain : (iov->iov_len - off);
        // 当超长时，每次处理 UINT16_MAX 长度的数据
        if (len > UINT16_MAX) {
            len = UINT16_MAX;
        }
        written = PBUF_ChainWrite(&sk->sndBuf, (uint8_t*)iov->iov_base + off, len, fragSize, headroom);
        if (written <= 0) {         // 内存申请失败
            DP_ADD_ABN_STAT(DP_TCP_PUSH_SND_PBUF_FAILED);
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

static void TcpPushSendBufRevoke(PBUF_Chain_t* sndBuf, size_t pushCnt)
{
    Pbuf_t*  pbuf;
    size_t                i;

    for (i = 0; i < pushCnt; ++i) {
        pbuf = PBUF_CHAIN_POP_TAIL(sndBuf);
        if (pbuf == NULL) {
            break;
        }

        PBUF_Free(pbuf);
    }
}

static ssize_t TcpWriteIovWithZcopy(Sock_t* sk, struct DP_ZIovec* iov)
{
    ssize_t  ret = 0;
    size_t   remain = iov->iov_len;
    Pbuf_t*  pbuf = NULL;
    uint16_t left;
    uint16_t index = 0;

    while (remain > 0) {
        pbuf = EBUF_GETNEXTPBUF(iov->iov_base, iov->iov_len, index++);
        if (pbuf == NULL) {
            break;
        }
        left = remain - pbuf->payloadLen;
        if (left >= 0) {
            pbuf->totLen = pbuf->payloadLen;
            pbuf->segLen = pbuf->payloadLen;
            remain = left;
        } else {
            pbuf->totLen = remain;
            pbuf->segLen = remain;
            remain = 0;
        }
        ret += pbuf->segLen;
        pbuf->flags |= DP_PBUF_FLAGS_EXTERNAL;
        PBUF_ChainPush(&sk->sndBuf, pbuf);
    }
    EBUF_SETREFCNT(iov->iov_base, index + 1);
    return ret;
}

static ssize_t TcpPushSndBufZcopy(Sock_t* sk, const struct DP_ZMsghdr* msg, uint16_t mss, size_t* index)
{
    ssize_t            ret      = 0;
    size_t             i        = *index;
    ssize_t            written;
    struct DP_ZIovec*  iov;
    bool               isAllSent = true;

    uint32_t oldPktCnt = sk->sndBuf.pktCnt;
    while (i < msg->msg_iovlen) {
        iov = msg->msg_iov + i;
        if (UTILS_UNLIKELY(iov->iov_base == NULL || iov->iov_len <= 0)) {
            i++;
            continue;
        }
        written = TcpWriteIovWithZcopy(sk, iov);
        if (written < (ssize_t)iov->iov_len) {
            isAllSent = false;
            break;
        }

        ret += written;
        ++i;
    }

    if (!isAllSent) {      // 如果没有全部发送成功，则回撤
        DP_ADD_ABN_STAT(DP_TCP_SND_BUF_ZCOPY_NOMEM);
        TcpPushSendBufRevoke(&sk->sndBuf, sk->sndBuf.pktCnt - oldPktCnt);
        ret = -1;
        i = 0;
    }

    *index = i;
    TcpSk_t* tcp = TcpSK(sk);
    DP_ADD_PKT_STAT(tcp->wid, DP_PKT_SEND_BUF_IN, sk->sndBuf.pktCnt - oldPktCnt);
    return ret;
}

static inline bool TcpSndSpaceEnough(size_t space, size_t msgDataLen, int flags)
{
    if (((uint32_t)flags & DP_MSG_ZEROCOPY) != 0) {
        return space >= msgDataLen;
    }

    return space > 0;
}

ssize_t TcpSendmsg(Sock_t* sk, const struct DP_Msghdr* msg, int flags, size_t msgDataLen, size_t* index, size_t* offset)
{
    ssize_t  ret = 0;
    TcpSk_t* tcp = TcpSK(sk);

    if (UTILS_UNLIKELY(!SOCK_IS_CONNECTED(sk))) { // 未建链或者建链阶段发生异常
        DP_ADD_ABN_STAT(DP_TCP_SND_CONN_CLOSED);
        return -ENOTCONN;
    } else if (UTILS_UNLIKELY(SOCK_IS_CONN_REFUSED(sk))) {
        DP_ADD_ABN_STAT(DP_TCP_SND_CONN_REFUSED);
        if (sk->error != 0) {
            ret = -sk->error;
            sk->error = 0;
            return ret;
        }
    }

    if (UTILS_UNLIKELY(!SOCK_CAN_SEND_MORE(sk))) {
        DP_ADD_ABN_STAT(DP_TCP_SND_CANT_SEND);
        return -EPIPE;
    }

    if (UTILS_UNLIKELY(sk->family == DP_AF_INET && TcpInetDevIsUp(sk) != 0)) {
        DP_ADD_ABN_STAT(DP_TCP_SND_DEV_DOWN);
        return -ENETDOWN;
    }

    size_t space = TcpGetSndSpace(tcp);
    if (!TcpSndSpaceEnough(space, msgDataLen, flags)) {
        SOCK_UNSET_WRITABLE(sk);
        DP_ADD_ABN_STAT(DP_TCP_SND_NO_SPACE);
        return -EAGAIN;
    }

    if (((uint32_t)flags & DP_MSG_ZEROCOPY) != 0) {
        ret = TcpPushSndBufZcopy(sk, (const struct DP_ZMsghdr*)msg, tcp->mss, index);
    } else {
        ret = TcpPushSndBuf(sk, msg, tcp->mss, space, index, offset);
    }
    if (UTILS_UNLIKELY(ret <= 0)) {
        DP_ADD_ABN_STAT(DP_TCP_SND_BUF_NOMEM);
        return -ENOMEM;
    }

    if (CFG_GET_VAL(DP_CFG_DEPLOYMENT) == DP_DEPLOYMENT_DEFAULT) {
        TcpTsqAddLockQue(tcp, TCP_TSQ_SEND_DATA, true);
    } else {
        TcpTsqAddQue(tcp, TCP_TSQ_SEND_DATA);
    }

    return ret;
}

static void TcpTrySendWndUp(Sock_t* sk)
{
    uint32_t rcvWnd = TcpGetRcvWnd(TcpSK(sk));
    uint32_t localWnd = TcpSelectRcvWnd(TcpSK(sk));
    uint32_t incWnd = 0;

    if (localWnd > 0) {
        /*
            localWnd是当前可以向另一端发送的最大窗口值
            rcvAdvertise - rcvNxt是TCP上一次发送报文告诉对端的窗口值
        */
        incWnd = localWnd - (TcpSK(sk)->rcvAdvertise - TcpSK(sk)->rcvNxt); // 该值一定大于等于0
    }

    if (TcpSK(sk)->delayAckEnable != 0) {
        /* 延迟 ACK 打开，回复窗口更新Ack条件：
           1. 窗口扩大为原来的两倍
         */
        if (incWnd >= 2 * rcvWnd) { // 2: 新窗口大于旧窗口的两倍
            TcpTsqAddLockQue(TcpSK(sk), TCP_TSQ_SEND_WND_UP, true);
        }
    } else {
        /* 延迟 ACK 关闭，回复窗口更新Ack条件：
           1. 如果数据被收空
           2. 如果新窗口>mss 或者 新窗口>缓冲区/2
         */
        if ((sk->rcvBuf.bufLen == 0 && TcpSK(sk)->rcvQue.bufLen == 0) ||
            (incWnd > TcpSK(sk)->mss || incWnd > (sk->rcvHiwat / 2))) { // 2: 新窗口>缓冲区/2时更新窗口
            TcpTsqAddLockQue(TcpSK(sk), TCP_TSQ_SEND_WND_UP, true);
        }
    }
}

static void TcpCMsgRecvProc(Sock_t* sk, struct DP_Msghdr* msg)
{
    uint32_t offset = 0;
    InetSk_t *inetSk = TcpInetSk(sk);
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

ssize_t TcpRecvmsg(Sock_t* sk, struct DP_Msghdr* msg, int flags, size_t msgDataLen)
{
    ssize_t ret;

    if (UTILS_UNLIKELY(!SOCK_IS_CONNECTED(sk))) { // 未建链或者建链阶段发生异常
        DP_ADD_ABN_STAT(DP_TCP_RCV_CONN_CLOSED);
        return -ENOTCONN;
    }
    uint32_t oldCnt = sk->rcvBuf.pktCnt;
    // 没有数据并且接收到RST报文，则返回对应错误码
    if (UTILS_UNLIKELY(oldCnt == 0 && SOCK_IS_CONN_REFUSED(sk))) {
        DP_ADD_ABN_STAT(DP_TCP_RCV_CONN_REFUSED);
        return -ECONNRESET;
    }

    ret = SOCK_PopRcvBuf(sk, msg, flags, msgDataLen);
    // 未接收到RST报文的场景下才发送窗口更新报文
    if (UTILS_LIKELY(ret > 0 && !SOCK_IS_CONN_REFUSED(sk))) {
        TcpTrySendWndUp(sk);
    }

    if (((uint32_t)flags & DP_MSG_PEEK) == 0) {
        DP_ADD_PKT_STAT(TcpSK(sk)->wid, DP_PKT_RECV_BUF_OUT, oldCnt - sk->rcvBuf.pktCnt);
    }

    if (UTILS_LIKELY(ret > 0)) {
        DP_ADD_TCP_STAT(TcpSK(sk)->wid, DP_TCP_RCV_OUT_BYTE, ret);
    }

    TcpCMsgRecvProc(sk, msg);

    return ret;
}

void TcpInitTcpSk(TcpSk_t* tcp)
{
    tcp->tid    = TcpGetTid();
    tcp->state  = TCP_CLOSED;
    tcp->caCb   = NULL;
    tcp->caMeth = TcpCaGet(-1);
    tcp->nextCaMethId = -1;
    tcp->initCwnd = (uint32_t)CFG_GET_TCP_VAL(DP_CFG_TCP_INIT_CWND);

    tcp->synOpt = TCP_SYN_OPTIONS;
    if (CFG_GET_TCP_VAL(DP_CFG_TCP_SELECT_ACK) == DP_ENABLE) {
        tcp->synOpt |= TCP_SYN_OPT_SACK_PERMITTED;
    }
    if (CFG_GET_TCP_VAL(DP_CFG_TCP_TIMESTAMP) == DP_DISABLE) {
        tcp->synOpt &= ~TCP_SYN_OPT_TIMESTAMP;
    }
    if (CFG_GET_TCP_VAL(DP_CFG_TCP_WINDOW_SCALING) == DP_DISABLE) {
        tcp->synOpt &= ~TCP_SYN_OPT_WINDOW;
    }
    tcp->mss            = 0;
    tcp->rcvWs          = 0;
    tcp->synRetries     = 0;
    tcp->keepIdleLimit  = 0;
    tcp->wid            = -1;
    tcp->txQueid        = -1;
    tcp->accDataMax     = 2; // 当前默认值为 2 ，后续更改为CFG预配置
    tcp->keepProbes     = (uint8_t)CFG_GET_TCP_VAL(DP_CFG_TCP_KEEPALIVE_PROBES);
    uint32_t keeIntvl   = (uint32_t)CFG_GET_TCP_VAL(DP_CFG_TCP_KEEPALIVE_INTVL);
    tcp->keepIntvl      = TCP_SEC2_TICK(keeIntvl);
    tcp->delayAckEnable = CFG_GET_TCP_VAL(DP_CFG_TCP_DELAY_ACK);
    uint32_t keeIdle    = (uint32_t)CFG_GET_TCP_VAL(DP_CFG_TCP_KEEPALIVE_TIME);
    tcp->keepIdle       = TCP_SEC2_TICK(keeIdle);
    tcp->connType       = TCP_ACTIVE;
    tcp->reorderCnt     = 3; // 当前重复ACK次数默认为3，后续可考虑更改为CFG配置
    tcp->userTimeout    = (uint32_t)CFG_GET_TCP_VAL(DP_CFG_TCP_USR_TIMEOUT);
    tcp->pacingRate     = PACING_RATE_NOLIMIT;
    tcp->quickAckNum    = 0;
    tcp->rxtMin         = TCP_MIN_REXMIT_TICK;
    tcp->cookie         = ((CFG_GET_TCP_VAL(DP_CFG_TCP_COOKIE) == DP_ENABLE) ? 1 : 0);

    for (int i = 0; i < TCP_TIMERID_BUTT; i++) {
        tcp->expiredTick[i] = TCP_TIMER_TICK_INVALID;
    }

    PBUF_ChainInit(&tcp->rcvQue);
    PBUF_ChainInit(&tcp->rexmitQue);
    PBUF_ChainInit(&tcp->reassQue);
}

void TcpInitChildTcpSk(Sock_t* newsk, Sock_t* parent, Pbuf_t* pbuf, DP_TcpHdr_t* tcpHdr)
{
    newsk->ops    = parent->ops;
    SOCK_SET_CONNECTING(newsk);

    if (parent->bandWidth != 0) {
        newsk->bandWidth = parent->bandWidth;
        newsk->pacingCb  = MEM_MALLOC(sizeof(SOCK_Pacing_t), MOD_SOCKET, DP_MEM_FREE);
        if (newsk->pacingCb != NULL) {
            (void)memcpy_s(newsk->pacingCb, sizeof(SOCK_Pacing_t), parent->pacingCb, sizeof(SOCK_Pacing_t));
        }
    }

    TcpInitTcpSk(TcpSK(newsk));

    TcpSK(newsk)->caMeth = TcpSK(parent)->caMeth;
    TcpSK(newsk)->nextCaMethId = TcpSK(parent)->nextCaMethId;
    TcpSK(newsk)->synOpt = TcpSK(parent)->synOpt;
    TcpSK(newsk)->mss    = TcpSK(parent)->mss;
    TcpSK(newsk)->rcvWs  = TcpSK(parent)->rcvWs;

    TcpSK(newsk)->wid     = DP_PBUF_GET_WID(pbuf);
    TcpSK(newsk)->txQueid = NETDEV_GetTxQueid(PBUF_GET_DEV(pbuf), PBUF_GET_QUE_ID(pbuf));
    TcpSK(newsk)->irs     = UTILS_NTOHL(tcpHdr->seq);
    TcpSK(newsk)->sndWnd  = UTILS_NTOHS(tcpHdr->win);
    TcpSK(newsk)->rcvNxt  = TcpSK(newsk)->irs + 1;
    TcpSK(newsk)->rcvAdvertise  = TcpSK(newsk)->irs + 1;
    TcpSK(newsk)->rcvWup  = TcpSK(newsk)->rcvNxt;
    TcpSK(newsk)->parent  = TcpSK(parent);
    TcpSK(newsk)->connType     = TCP_PASSIVE;
    TcpSK(newsk)->deferAccept  = TcpSK(parent)->deferAccept;
    TcpSK(newsk)->cork         = TcpSK(parent)->cork;
    TcpSK(newsk)->maxRexmit    = TcpSK(parent)->maxRexmit;
    TcpSK(newsk)->keepIdle     = TcpSK(parent)->keepIdle;
    TcpSK(newsk)->keepIntvl    = TcpSK(parent)->keepIntvl;
    TcpSK(newsk)->keepProbes   = TcpSK(parent)->keepProbes;
    TcpSK(newsk)->keepIdleLimit = TcpSK(parent)->keepIdleLimit;
    TcpSK(newsk)->synRetries   = TcpSK(parent)->synRetries;
    TcpSK(newsk)->userTimeout  = TcpSK(parent)->userTimeout;
    TcpSK(newsk)->cookie  = TcpSK(parent)->cookie;
}

// 该接口用于清理socket除接收队列外的所有队列，通知用户接受接收队列中的数据
void TcpCleanUp(TcpSk_t* tcp)
{
    if (tcp->state != TCP_CLOSED) {
        TCP_SET_CLEANUP(tcp);
        TcpRemoveAllTimers(tcp);

        if (TcpSk2Sk(tcp)->family == DP_AF_INET) {
            g_tcpInetOps->connectTblRemove(TcpSk2Sk(tcp));
            g_tcpInetOps->unhash(TcpSk2Sk(tcp));
        } else {
            g_tcpInet6Ops->connectTblRemove(TcpSk2Sk(tcp));
            g_tcpInet6Ops->unhash(TcpSk2Sk(tcp));
        }
    }
}

int TcpSetKeepAlive(Sock_t *sk, int enable)
{
    TcpSk_t *tcp = TcpSK(sk);
    // 大于listen的状态才存在tsq队列，在修改keepalive状态时，需要添加对应事件
    if (tcp->state > TCP_LISTEN) {
        if (sk->keepalive != 0 && enable == 0) {
            TcpTsqAddLockQue(tcp, TCP_TSQ_KEEPALIVE_OFF, true);
        } else if (sk->keepalive == 0 && enable != 0) {
            TcpTsqAddLockQue(tcp, TCP_TSQ_KEEPALIVE_ON, true);
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

int TcpShutdown(Sock_t* sk, int how)
{
    int ret;

    if (!SOCK_IS_CONNECTED(sk)) {
        return -ENOTCONN;
    }

    if (SOCK_IS_CONN_REFUSED(sk)) {
        ret = -sk->error;
        sk->error = 0;
        return ret;
    }

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
        TcpTsqAddLockQue(TcpSK(sk), TCP_TSQ_DISCONNECT, true);
    }
    return 0;
}

static inline int TcpAddDisconnectToTsq(TcpSk_t* tcp, bool inSkLock)
{
    // 共线程调度模式下，且不处于tsq处理过程中，不立即驱动addQue中的tsq事件，先解锁后驱动tsq事件，防止事件处理中释放socket导致引用空指针
    bool driveTsq = (CFG_GET_TCP_VAL(CFG_TCP_TSQ_PASSIVE) == DP_ENABLE) && (tcp->tsqNested == 0);

    TcpTsqAddLockQue(tcp, TCP_TSQ_DISCONNECT, false);

    if (inSkLock) {
        SOCK_Unlock(TcpSk2Sk(tcp));
    }

    if (driveTsq) {
        TcpProcTsq(tcp->wid);
    }
    return 0;
}

static void TcpCloseChildren(TcpSk_t* tcp)
{
    TcpSk_t* child = LIST_FIRST(&tcp->uncomplete);
    TcpSk_t* next = NULL;

    while (child != NULL) {
        next = LIST_NEXT(child, childNode);
        SOCK_SET_CLOSED(TcpSk2Sk(child));
        TcpAddDisconnectToTsq(child, false);
        child = next;
    }

    child = LIST_FIRST(&tcp->complete);
    while (child != NULL) {
        next = LIST_NEXT(child, childNode);
        SOCK_SET_CLOSED(TcpSk2Sk(child));
        TcpAddDisconnectToTsq(child, false);
        child = next;
    }
}

// 该socket释放接口仅用于 用户线程调用API接口关闭TCP
// 与TcpFreeSk相比，用户线程中不会产生tsq事件，因此移除tsq队列处理，否则与协议栈线程存在并发风险
void TcpFreeUnconnectedSk(Sock_t* sk)
{
    // 用户主动调用CLOSE场景下释放SK资源
    if (SOCK_IS_CLOSED(sk) || TcpSK(sk)->parent != NULL) {
        if (TcpSK(sk)->parent == NULL) {
            SOCK_NotifyEvent(sk, SOCK_EVENT_FREE_SOCKCB);
        }

        if (sk->family == DP_AF_INET) {
            g_tcpInetOps->mFree(sk);
        } else {
            g_tcpInet6Ops->mFree(sk);
        }
    }
}

int TcpClose(Sock_t* sk)
{
    TcpSk_t *tcp = TcpSK(sk);
    long int curTid = TcpGetTid();
    if (CFG_GET_TCP_VAL(CFG_TCP_TSQ_PASSIVE) == DP_ENABLE && curTid != tcp->tid) {
        DP_ADD_ABN_STAT(DP_TIMER_ACTIVE_EXCEPT);
        DP_LOG_ERR("tcp close: tid:%ld, state:%u, flags:%u,nested:%u,sk.flags:%u",
            tcp->tid, tcp->state, tcp->flags, tcp->tsqNested, TcpSk2Sk(tcp)->flags);
    }

    if (SOCK_IS_LISTENED(sk)) {
        TcpRemoveListener(sk);
        TcpWaitIdle(sk);
        TcpCloseChildren(tcp);
        TcpSetState(tcp, TCP_CLOSED);
        if (SOCK_Deref(sk) == 0) {
            SOCK_Unlock(sk);
            TcpFreeUnconnectedSk(sk);
            DP_LOG_INFO("Tcp listen socket closed.");
            return 0;
        }
        goto out;
    }

    if (SOCK_IS_CONNECTED(sk) || SOCK_IS_CONNECTING(sk)) {
        // 内部解锁 sock
        return TcpAddDisconnectToTsq(tcp, true);
    }

    if (SOCK_IS_BINDED(sk)) {
        TcpGlobalRemove(sk);
    }

    SOCK_Unlock(sk);
    TcpFreeUnconnectedSk(sk);
    DP_LOG_INFO("Tcp socket closed.");

    return 0;
out:
    SOCK_Unlock(sk);
    return 0;
}

void TcpFreeSk(Sock_t* sk)
{
    // 子socket触发异常事件场景下释放SK资源
    if (SOCK_IS_CLOSED(sk) || TcpSK(sk)->parent != NULL) {
        // 释放资源前应该将剩余锁队列事件清除
        TcpTsqTryRemoveLockQue(TcpSK(sk));
        TcpTsqTryRemoveNoLockQue(TcpSK(sk));

        if (TcpSK(sk)->parent == NULL) {
            SOCK_NotifyEvent(sk, SOCK_EVENT_FREE_SOCKCB);
        }

        TcpCaDeinit(TcpSK(sk));

        TCP_SET_FREED(TcpSK(sk));
        if (sk->family == DP_AF_INET) {
            g_tcpInetOps->mFree(sk);
        } else {
            g_tcpInet6Ops->mFree(sk);
        }
    }
}

TcpSk_t* TcpReuse(TcpSk_t* tcp, DP_TcpHdr_t* tcpHdr, TcpPktInfo_t* pi)
{
    TcpSk_t *ret = tcp;

    if (pi->thFlags == DP_TH_SYN) {
        TcpSynOpts_t synOpts = {0};
        // 如果选项失败则不使用，且不影响已查找到的socket
        (void)TcpParseSynOpts((uint8_t*)(tcpHdr + 1), pi->hdrLen - sizeof(DP_TcpHdr_t), &synOpts);

        // 复用timewait状态的socket需要满足其中一个条件：序号大于rcvnxt 或 时间戳大于之前时间戳
        if (TcpSeqGt(pi->seq, tcp->rcvNxt) ||
            (((synOpts.rcvSynOpt & TCP_SYN_OPT_TIMESTAMP) != 0) && TcpSeqGt(synOpts.tsVal, tcp->tsEcho))) {
            // 先释放timewait状态的socket在匹配listen socket，重新建链
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_TIMEWAIT_REUSE);
            TcpDone(tcp);
            ret = NULL;
        }
    }

    return ret;
}
