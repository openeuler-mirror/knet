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
#include "tcp_timer.h"

#include <securec.h>

#include "utils_cfg.h"
#include "utils_log.h"
#include "worker.h"

#include "tcp_out.h"
#include "tcp_tsq.h"
#include "tcp_sock.h"

#include "tcp_cc.h"
#include "tcp_frto.h"

#define TCP_ACTIVE_TIMER_MAX_TICK (0x7fff)

typedef struct {
    size_t totSize;
    size_t timerSize;
    size_t timerOffset;

    struct {
        int    twBits;
        size_t offset;
    } twCfg[TCP_TIMERID_BUTT];
} TcpTimerMemCfg_t;

TcpTimer_t** g_tcpTimers;

void TcpActiveTimer(TcpSk_t* tcp, TW_Wheel_t* tw, int timerid, uint32_t twTick, uint32_t intervalTick)
{
    long int curTid = TcpGetTid();
    if (CFG_GET_TCP_VAL(CFG_TCP_TSQ_PASSIVE) == DP_ENABLE && curTid != tcp->tid) {
        DP_LOG_ERR("active timer tid not match, curTid: %ld, tcp->tid: %ld,flags:%u,nest:%u,timerid:%d,sk.flags:%u",
            curTid, tcp->tid, tcp->flags, tcp->tsqNested, timerid, TcpSk2Sk(tcp)->flags);
        DP_ADD_ABN_STAT(DP_TIMER_ACTIVE_EXCEPT);
    }

    if (TcpState(tcp) == TCP_CLOSED || TCP_IS_CLEANUP(tcp) || TCP_IS_FREED(tcp)) {
        DP_ADD_ABN_STAT(DP_TIMER_ACTIVE_EXCEPT);
        DP_LOG_ERR("active timer: tid:%ld, state:%u, flags:%u,nested:%u,timerid:%d,sk.flags:%u",
            tcp->tid, tcp->state, tcp->flags, tcp->tsqNested, timerid, TcpSk2Sk(tcp)->flags);
        return;
    }

    if (tcp->expiredTick[timerid] != TCP_TIMER_TICK_INVALID) {
        TW_DelNode(tw, &tcp->twNode[timerid], tcp->expiredTick[timerid]);
    }

    uint32_t tIntvl = intervalTick > TCP_ACTIVE_TIMER_MAX_TICK ? TCP_ACTIVE_TIMER_MAX_TICK : intervalTick;
    /* 使用0x8000代表无效时间，这里最大的tick不能超过0x7fff */
    tcp->expiredTick[timerid] = (uint16_t)(twTick + tIntvl) & TCP_ACTIVE_TIMER_MAX_TICK;
    if (tcp->expiredTick[timerid] == TCP_TIMER_TICK_INVALID) {
        DP_ADD_ABN_STAT(DP_TIMER_ACTIVE_EXCEPT);
        DP_LOG_ERR("active timer expired is invalid, twtick:%u, interval:%u", twTick, intervalTick);
    }

    TW_AddNode(tw, &tcp->twNode[timerid], tcp->expiredTick[timerid]);
}

static inline void TcpAbort(TcpSk_t* tcp, int reason)
{
    DP_INC_TCP_STAT(tcp->wid, reason);
    TCP_SET_ABORT(tcp);

    // shutdown关闭写端进入TIME_WAIT超时不能发rst ack
    if (tcp->state != TCP_TIME_WAIT) {
        TcpXmitRstAckPkt(tcp);
    }

    TcpCleanUp(tcp);
    // TSQ 处理依赖 tcp 当前状态，因此不将状态设置为 CLOSED ，tcp 五元组已经清除，此连接不会再接收到报文而影响 tcp 状态
    TcpTsqAddQue(tcp, TCP_TSQ_ABORT);
}

static inline void TcpResetTimer(TcpSk_t* tcp, TW_Wheel_t* tw, int timerId, uint32_t twTick, uint32_t intervalTick)
{
    tcp->expiredTick[timerId] = TCP_TIMER_TICK_INVALID;
    TcpActiveTimer(tcp, tw, timerId, twTick, intervalTick);
}

static bool TcpAdjustSlowTimer(TcpSk_t* tcp, uint32_t twTick)
{
    if (tcp->userTimeout == 0 || tcp->keepProbeCnt == 0) {
        return tcp->keepProbeCnt >= tcp->keepProbes;
    }

    if (tcp->userTimeout > 0 && tcp->userTimeStartSlow > 0) {
        return TIME_CMP((tcp->userTimeStartSlow + tcp->userTimeout / TCP_SLOW_TIMER_INTERVAL_MS), twTick) <= 0;
    }
    return false;
}

static bool TcpAdjustUserTimeoutFastTimer(TcpSk_t* tcp, uint32_t twTick)
{
    if (tcp->userTimeout > 0 && tcp->userTimeStartFast > 0 && tcp->state != TCP_SYN_RECV) {
        // syn-ack重传不受tcp_usertimeout超时影响
        return TIME_CMP((tcp->userTimeStartFast + tcp->userTimeout / TCP_FAST_TIMER_INTERVAL_MS), twTick) <= 0;
    }
    return false;
}

static bool TcpSynRetriesExceeded(TcpSk_t* tcp)
{
    return tcp->state == TCP_SYN_SENT && tcp->backoff >= CFG_GET_TCP_VAL(DP_CFG_TCP_SYN_RETRIES);
}

static bool TcpSynAckRetriesExceeded(TcpSk_t* tcp)
{
    return tcp->state == TCP_SYN_RECV && tcp->backoff >= CFG_GET_TCP_VAL(DP_CFG_TCP_SYNACK_RETRIES);
}

static inline int TcpProcFinWaitTimer(TcpSk_t* tcp)
{
    tcp->expiredTick[TCP_TIMERID_SLOW] = TCP_TIMER_TICK_INVALID;
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_FIN_WAIT_2_DROPS);

    // FinWait 定时器，用户一定调用了 Close ，直接释放资源
    TcpDone(tcp);
    return 0;
}

static inline int TcpProcTimeWaitTimer(TcpSk_t* tcp)
{
    tcp->expiredTick[TCP_TIMERID_SLOW] = TCP_TIMER_TICK_INVALID;
    // TimeWait 定时器，用户可能调用 shutdown 或者 close 触发
    if (TCP_IS_CLOSED(tcp)) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_TIME_WAIT_DROPS);
        TcpDone(tcp);
    } else {
        TcpAbort(tcp, DP_TCP_TIME_WAIT_DROPS);
    }
    return 0;
}

static int TcpSlowTimeout(TW_Wheel_t* wheel, TW_Node_t* tn, uint32_t twTick)
{
    TcpSk_t* tcp = CONTAINER_OF(tn, TcpSk_t, twNode[TCP_TIMERID_SLOW]);

    if (tcp->expiredTick[TCP_TIMERID_SLOW] == TCP_TIMER_TICK_INVALID) {
        DP_LOG_ERR("slow timeout but invalid, tid:%ld, state:%u, flags:%u,sk.flags:%u",
            tcp->tid, tcp->state, tcp->flags, TcpSk2Sk(tcp)->flags);
        DP_ADD_ABN_STAT(DP_TIMER_EXPIRED_INVAL);
        return 0;
    }

    if (TCP_TIMER_TICK_CMP(tcp->expiredTick[TCP_TIMERID_SLOW], (uint16_t)(twTick & 0x7FFF)) > 0) {
        return -1;
    }

    if (tcp->state == TCP_FIN_WAIT2) {
        return TcpProcFinWaitTimer(tcp);
    }

    if (tcp->state == TCP_TIME_WAIT) {
        return TcpProcTimeWaitTimer(tcp);
    }

    if (tcp->state < TCP_ESTABLISHED) {
        // 触发建链保活定时器，直接触发断链
        tcp->expiredTick[TCP_TIMERID_SLOW] = TCP_TIMER_TICK_INVALID;
        TcpAbort(tcp, DP_TCP_CONN_KEEP_DROPS);
        return 0;
    }

    // 保活定时器处理

    // 超过了用户设置的最大保活次数，或者超过TCP_USER_TIMEOUT，直接触发断链
    if (TcpAdjustSlowTimer(tcp, twTick)) {
        tcp->expiredTick[TCP_TIMERID_SLOW] = TCP_TIMER_TICK_INVALID;
        TcpAbort(tcp, DP_TCP_KEEP_DROPS);
        return 0;
    }

    // 没有发送过探测报文，keepIdle超时，在这里进行老化处理
    if (tcp->keepIdleLimit > 0) {
        if (tcp->keepProbeCnt == 0) {
            tcp->keepIdleCnt++;
        }
        if (tcp->keepIdleCnt >= tcp->keepIdleLimit) {
            tcp->expiredTick[TCP_TIMERID_SLOW] = TCP_TIMER_TICK_INVALID;
            TcpAbort(tcp, DP_TCP_AGE_DROPS);
            return 0;
        }
    }

    // 保活定时器处理
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_KEEP_TIME_OUT);

    /* rfc9293 3.8.4
     * A TCP connection is said to be "idle" if
     * for some long amount of time there have been no incoming segments received and
     * there is no new or unacknowledged data to be sent.
     */
    if (TcpSk2Sk(tcp)->sndBuf.pktCnt == 0 && tcp->sndQue.pktCnt == 0 && tcp->sndUna == tcp->sndNxt) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_KEEP_PROBE);
        TcpSndProbePkt(tcp);
        tcp->keepProbeCnt++;
    }
    tcp->trType = TCP_TRTYPE_CONN;
    uint32_t intervalTick = ((tcp->keepIntvl > 0) ? tcp->keepIntvl : 1);
    TcpResetTimer(tcp, wheel, TCP_TIMERID_SLOW, (uint16_t)(twTick & 0x7FFF), intervalTick);
    return 0;
}

static bool TcpJudgeDeferAccept(TcpSk_t* tcp)
{
    if (tcp->deferAccept == 0) {
        return false;
    }

    if (tcp->parent == NULL || tcp->state != TCP_SYN_RECV) {
        return false;
    }

    return true;
}

uint32_t TcpCalcRto(TcpSk_t* tcp, uint32_t twTick)
{
    uint32_t rto = 0;
    if (tcp->state < TCP_ESTABLISHED) {
        rto = TCP_INITIAL_REXMIT_TICK;
    } else {
        rto = TcpCalcRexmit(tcp);
    }

    tcp->backoff++;
    rto <<= tcp->backoff;
    rto = (rto > TCP_MAX_REXMIT_TICK) ? TCP_MAX_REXMIT_TICK : rto;

    if (tcp->userTimeout > 0) {
        if (((tcp->state == TCP_SYN_SENT) && (tcp->synRetries == 0) &&
            (CFG_GET_TCP_VAL(DP_CFG_TCP_SYN_RETRIES) == 0)) || (tcp->state >= TCP_ESTABLISHED)) {
            uint32_t userRto = tcp->userTimeout / TCP_FAST_TIMER_INTERVAL_MS + tcp->userTimeStartFast - twTick;
            rto = (userRto > rto) ? rto : userRto;
        }
    }
    return rto;
}

static int TcpProcRexmitTimer(TcpSk_t* tcp, TW_Wheel_t* wheel, uint32_t twTick)
{
    bool isAbort = false;
    int reason = DP_TCP_TIMEOUT_DROP;

    /*
        不允许重传的场景，按优先级判断如下：
        1. tcp 未建链完成
            1.1 用户设置 defer accept，且报文重传次数超过配置的最大重传次数
            1.2 用户通过 TCP_SYNCNT 设置了重传次数，此时 SYN/SYNACK 报文重传超过用户设置的重传次数
            1.3 用户设置了 USER_TIMEOUT , 超过 USER_TIMEOUT
            1.4 SYN 报文重传次数超过配置项 DP_CFG_TCP_SYN_RETRIES 设置的值(媒体中间会在适配层设置为0)
            1.5 SYN/ACK 报文重传次数超过配置项 DP_CFG_TCP_SYNACK_RETRIES 设置的值
            1.6 报文超过最大重传次数(默认12，与 TCP_MAX_MAXRXTSHIFT 相等，OS栈需要在适配层调整为最大值 256，保证配置项正常使用)
        2. tcp 建链完成
            2.1 tcp 报文超过最大重传次数 TCP_MAX_MAXRXTSHIFT
            2.2 用户设置了 USER_TIMEOUT , 超过 USER_TIMEOUT
    */
    if ((tcp->state == TCP_SYN_SENT) || (tcp->state == TCP_SYN_RECV)) {
        if (TcpJudgeDeferAccept(tcp) && tcp->backoff >= tcp->maxRexmit) {
            isAbort = true;
            reason = DP_TCP_DEFER_ACCEPT_DROP;
        } else if (tcp->synRetries > 0) {      // SYN包/SYNACK包重传，设置DP_TCP_SYNCNT后，userTimeout不生效
            isAbort = (tcp->backoff >= tcp->synRetries);
            reason = DP_TCP_SYN_RETRIES_DROPS;
        } else if (TcpAdjustUserTimeoutFastTimer(tcp, twTick)) {
            isAbort = true;
            reason = DP_TCP_USER_TIMEOUT_DROPS;
        } else if ((CFG_GET_TCP_VAL(DP_CFG_TCP_SYN_RETRIES) > 0) && TcpSynRetriesExceeded(tcp)) {
            isAbort = true;
            reason = DP_TCP_SYN_RETRIES_DROPS;
        } else if (TcpSynAckRetriesExceeded(tcp)) {
            isAbort = true;
            reason = DP_TCP_SYNACK_RETRIES_DROPS;
        } else if (tcp->backoff >= (uint32_t)TCP_MAX_REXMIT_CNT_NO_EST) {
            isAbort = true;
        }
    } else if (tcp->backoff >= TCP_MAX_MAXRXTSHIFT) {
        isAbort = true;
    } else if (TcpAdjustUserTimeoutFastTimer(tcp, twTick)) {
        isAbort = true;
        reason = DP_TCP_USER_TIMEOUT_DROPS;
    }

    if (isAbort) {
        tcp->expiredTick[TCP_TIMERID_FAST] = TCP_TIMER_TICK_INVALID;
        TcpAbort(tcp, reason);
        return 0;
    }

    if (TcpFrtoIsAvailable(tcp)) {
        TcpFrtoEnterLoss(tcp);
    // 只有建链后需要判断拥塞控制
    } else if (tcp->state >= TCP_ESTABLISHED) {
        // 重传超时则触发拥塞控制
        if (tcp->backoff == 0 || TCP_IS_IN_RECOVERY(tcp)) {
            TcpCaTimeout(tcp);
        }
    }
    TcpRexmitPkt(tcp);

    uint32_t rto = TcpCalcRto(tcp, twTick);
    tcp->trType = TCP_TRTYPE_REXMIT;
    TcpResetTimer(tcp, wheel, TCP_TIMERID_FAST, twTick, rto);

    return 0;
}

static int TcpProcPersistTimer(TcpSk_t* tcp, TW_Wheel_t* wheel, uint32_t twTick)
{
    if (tcp->backoff >= TCP_MAX_MAXRXTSHIFT) {
        tcp->expiredTick[TCP_TIMERID_FAST] = TCP_TIMER_TICK_INVALID;
        TcpAbort(tcp, DP_TCP_PERSIST_DROPS);
        return 0;
    }

    if (TcpAdjustUserTimeoutFastTimer(tcp, twTick)) {
        tcp->expiredTick[TCP_TIMERID_FAST] = TCP_TIMER_TICK_INVALID;
        TcpAbort(tcp, DP_TCP_PERSIST_USER_TIMEOUT_DROPS);
        return 0;
    }

    DP_INC_TCP_STAT(tcp->wid, DP_TCP_PERSIST_TIMEOUT);
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_PROBE);
    TcpSndProbePkt(tcp);

    /* 坚持定时器计算rto时间及退避算法 */
    uint32_t rto = TcpCalcPersist(tcp);
    rto *= tcpPersistBackoff[tcp->backoff];
    rto = (rto > TCP_MAX_REXMIT_TICK) ? TCP_MAX_REXMIT_TICK : rto;

    if (tcp->userTimeout > 0) {
        uint32_t userRto = tcp->userTimeout / TCP_FAST_TIMER_INTERVAL_MS + tcp->userTimeStartFast - twTick;
        rto = (userRto > rto) ? rto : userRto;
    }

    tcp->backoff++;
    tcp->trType = TCP_TRTYPE_PERSIST;
    TcpResetTimer(tcp, wheel, TCP_TIMERID_FAST, twTick, rto);
    return 0;
}

static int TcpFastTimeout(TW_Wheel_t* wheel, TW_Node_t* tn, uint32_t twTick)
{
    TcpSk_t* tcp = CONTAINER_OF(tn, TcpSk_t, twNode[TCP_TIMERID_FAST]);

    if (tcp->expiredTick[TCP_TIMERID_FAST] == TCP_TIMER_TICK_INVALID) {
        DP_LOG_ERR("fast timeout but invalid, tid:%ld, state:%u, flags:%u,sk.flags:%u",
            tcp->tid, tcp->state, tcp->flags, TcpSk2Sk(tcp)->flags);
        DP_ADD_ABN_STAT(DP_TIMER_EXPIRED_INVAL);
        return 0;
    }

    /* 最大时间不超0x7fff */
    if (TCP_TIMER_TICK_CMP(tcp->expiredTick[TCP_TIMERID_FAST], (twTick & 0x7FFF)) > 0) {
        return -1;
    }

    if (tcp->fastMode == TCP_REXMIT_MODE) {
        return TcpProcRexmitTimer(tcp, wheel, twTick);
    } else {
        return TcpProcPersistTimer(tcp, wheel, twTick);
    }
}

static int TcpDelayAckTimeout(TW_Wheel_t* wheel, TW_Node_t* tn, uint32_t twTick)
{
    (void)wheel;
    TcpSk_t* tcp = CONTAINER_OF(tn, TcpSk_t, twNode[TCP_TIMERID_DELAYACK]);

    if (tcp->expiredTick[TCP_TIMERID_DELAYACK] == TCP_TIMER_TICK_INVALID) {
        DP_LOG_ERR("delayack timeout but invalid, tid:%ld, state:%u, flags:%u,sk.flags:%u",
            tcp->tid, tcp->state, tcp->flags, TcpSk2Sk(tcp)->flags);
        DP_ADD_ABN_STAT(DP_TIMER_EXPIRED_INVAL);
        return 0;
    }

    /* 最大时间不超0x7fff */
    if (TCP_TIMER_TICK_CMP(tcp->expiredTick[TCP_TIMERID_DELAYACK], (twTick & 0x7FFF)) > 0) {
        return -1;
    }

    // 触发延时定时器后就直接退出定时器
    tcp->expiredTick[TCP_TIMERID_DELAYACK] = TCP_TIMER_TICK_INVALID;
    TcpXmitAckPkt(tcp);
    return 0;
}

static int TcpPacingTimeout(TW_Wheel_t* wheel, TW_Node_t* tn, uint32_t twTick)
{
    (void)wheel;

    TcpSk_t* tcp = CONTAINER_OF(tn, TcpSk_t, twNode[TCP_TIMERID_PACING]);

    if (tcp->expiredTick[TCP_TIMERID_PACING] == TCP_TIMER_TICK_INVALID) {
        DP_LOG_ERR("pacing timeout but invalid, tid:%ld,state:%u, flags:%u,sk.flags:%u",
            tcp->tid, tcp->state, tcp->flags, TcpSk2Sk(tcp)->flags);
        DP_ADD_ABN_STAT(DP_TIMER_EXPIRED_INVAL);
        return 0;
    }

    /* 最大时间不超0x7fff */
    if (TCP_TIMER_TICK_CMP(tcp->expiredTick[TCP_TIMERID_PACING], (twTick & 0x7FFF)) > 0) {
        return -1;
    }

    // 未设置限速且未使用BBR算法时，不应触发pacing定时器
    if (TcpSk2Sk(tcp)->bandWidth == 0 && tcp->pacingRate == PACING_RATE_NOLIMIT) {
        tcp->expiredTick[TCP_TIMERID_PACING] = TCP_TIMER_TICK_INVALID;
        return 0;
    }

    tcp->expiredTick[TCP_TIMERID_PACING] = TCP_TIMER_TICK_INVALID;

    if (tcp->state >= TCP_ESTABLISHED && tcp->state <= TCP_LAST_ACK) {
        TcpTsqAddQue(tcp, TCP_TSQ_SEND_DATA);
    }
    return 0;
}

#define TCP_FAST_TIMER_WHEEL_CNT   1
#define TCP_SLOW_TIMER_WHEEL_CNT   1
#define TCP_DELAY_TIMER_WHEEL_CNT  1
#define TCP_PACING_TIMER_WHEEL_CNT 1

static void TcpPacingTimer(int wid, uint32_t tickNow)
{
    TcpTimer_t* timer = g_tcpTimers[wid];

    TW_Timeout(&timer->pacingTimer, tickNow, &timer->tws[TCP_TIMERID_PACING], TCP_PACING_TIMER_WHEEL_CNT);
}

static void TcpFastTimer(int wid, uint32_t tickNow)
{
    TcpTimer_t* timer = g_tcpTimers[wid];

    TW_Timeout(&timer->fastTimer, tickNow, &timer->tws[TCP_TIMERID_FAST], TCP_FAST_TIMER_WHEEL_CNT);
}

static void TcpSlowTimer(int wid, uint32_t tickNow)
{
    TcpTimer_t* timer = g_tcpTimers[wid];

    TW_Timeout(&timer->slowTimer, tickNow, &timer->tws[TCP_TIMERID_SLOW], TCP_SLOW_TIMER_WHEEL_CNT);
}

static void TcpDelayAckTimer(int wid, uint32_t tickNow)
{
    TcpTimer_t* timer = g_tcpTimers[wid];

    TW_Timeout(&timer->delayAckTimer, tickNow, &timer->tws[TCP_TIMERID_DELAYACK], TCP_DELAY_TIMER_WHEEL_CNT);
}

void TcpRemoveAllTimers(TcpSk_t* tcp)
{
    for (int i = 0; i < TCP_TIMERID_BUTT; i++) {
        if (tcp->expiredTick[i] != TCP_TIMER_TICK_INVALID) {
            TcpDeactiveTimer(tcp, g_tcpTimers[tcp->wid]->tws[i], i);
        }
    }
}

/*
 * 时间轮槽默认个数参考各定时器最大超时时间 * 定时器频率(1s / 定时器精度)
 * 例如：fastTimer: TCP_MAX_REXMIT_TICK(64s) * TCP_FAST_TIMER_HZ(1000 / TCP_FAST_TIMER_INTERVAL_MS(10ms)) = 6400
 * (1 << 13) = 8192 > 6400 > 4096 = (1 << 12)
 * 所以fastTimer默认bits值取13
 */
static TW_MaskBits_t g_twBits = {
    .bits = {
        FAST_TIMER_TWSLOT_DEFAULT,
        SLOW_TIMER_TWSLOT_DEFAULT,
        DELAYACK_TIMER_TWSLOT_DEFAULT,
        PACING_TIMER_TWSLOT_DEFAULT
    }
};

int TcpTimerSetMaskBits(TW_MaskBits_t* bitsReg)
{
    if (bitsReg == NULL) {
        DP_LOG_ERR("Set twBits failed with NULL.");
        return -1;
    }

    TW_MaskBits_t tmpBits = {0};
    for (uint32_t i = 0; i < TCP_TIMERID_BUTT; i++) {
        if (bitsReg->bits[i] > 15) {  // 掩码长度不能超过15，避免超出有效范围0x7FFF
            DP_LOG_ERR("Set twBits failed with invalid val, bits must less then 15.");
            return -1;
        }
        tmpBits.bits[i] = bitsReg->bits[i];
    }

    (void)memcpy_s(&g_twBits, sizeof(TW_MaskBits_t), &tmpBits, sizeof(TW_MaskBits_t));

    return 0;
}

static void TcpInitTimerMemCfg(TcpTimerMemCfg_t* memCfg, int wcnt)
{
    memCfg->timerSize = sizeof(TcpTimer_t);

    for (int i = 0; i < TCP_TIMERID_BUTT; i++) {
        memCfg->twCfg[i].twBits = g_twBits.bits[i];
        memCfg->twCfg[i].offset = memCfg->timerSize;
        // 各时间轮下按配置的规格数量申请nodehead，每个nodehead用于存放待触发定时tick的node节点，与设计一致
        memCfg->timerSize += TW_GetWheelSize(g_twBits.bits[i]);
    }

    memCfg->totSize     = sizeof(TcpTimer_t*) * wcnt;
    memCfg->timerOffset = memCfg->totSize;
    memCfg->totSize += memCfg->timerSize * (size_t)wcnt;
}

static int TcpInitTcpTimerMem(TcpTimer_t* tcpTimer, TcpTimerMemCfg_t* memCfg)
{
    TW_Timer_t* twTimer;
    TW_Cb_t     cb[TCP_TIMERID_BUTT] = {
        [TCP_TIMERID_FAST]     = TcpFastTimeout,
        [TCP_TIMERID_SLOW]     = TcpSlowTimeout,
        [TCP_TIMERID_DELAYACK] = TcpDelayAckTimeout,
        [TCP_TIMERID_PACING]   = TcpPacingTimeout,
    };

    for (int i = 0; i < TCP_TIMERID_BUTT; i++) {
        tcpTimer->tws[i] = (TW_Wheel_t*)PTR_NEXT(tcpTimer, memCfg->twCfg[i].offset);

        TW_InitWheel(tcpTimer->tws[i], memCfg->twCfg[i].twBits, cb[i]);
    }

    twTimer               = &tcpTimer->fastTimer;
    twTimer->twTick       = RAND_GEN();
    twTimer->intervalTick = 0;

    twTimer               = &tcpTimer->slowTimer;
    twTimer->twTick       = RAND_GEN();
    twTimer->intervalTick = 0;

    twTimer               = &tcpTimer->delayAckTimer;
    twTimer->twTick       = RAND_GEN();
    twTimer->intervalTick = 0;

    twTimer               = &tcpTimer->pacingTimer;
    twTimer->twTick       = RAND_GEN();
    twTimer->intervalTick = 0;

    return 0;
}

static void TcpFastTimerInit(int wid, uint32_t tickNow)
{
    g_tcpTimers[wid]->fastTimer.intervalTick = WORKER_TIME2_TICK(TCP_FAST_TIMER_INTERVAL_MS);
    g_tcpTimers[wid]->fastTimer.expiredTick = tickNow + g_tcpTimers[wid]->fastTimer.intervalTick;
}

static void TcpSlowTimerInit(int wid, uint32_t tickNow)
{
    g_tcpTimers[wid]->slowTimer.intervalTick = WORKER_TIME2_TICK(TCP_SLOW_TIMER_INTERVAL_MS);
    g_tcpTimers[wid]->slowTimer.expiredTick = tickNow + g_tcpTimers[wid]->slowTimer.intervalTick;
}

static void TcpDelayTimerInit(int wid, uint32_t tickNow)
{
    if (CFG_GET_TCP_VAL(DP_CFG_TCP_ADJUST_DELAY_ACK) == DP_ENABLE) { // 用户开启了更新延迟ACK间隔，直接设置定时器间隔为10ms触发
        g_tcpTimers[wid]->delayAckTimer.intervalTick = WORKER_TIME2_TICK(TCP_FAST_TIMER_INTERVAL_MS);
    } else {
        g_tcpTimers[wid]->delayAckTimer.intervalTick = WORKER_TIME2_TICK(TCP_DELAYACK_TIMER_INTERVAL_MS);
    }
    g_tcpTimers[wid]->delayAckTimer.expiredTick = tickNow + g_tcpTimers[wid]->delayAckTimer.intervalTick;
}

static void TcpPacingTimerInit(int wid, uint32_t tickNow)
{
    g_tcpTimers[wid]->pacingTimer.intervalTick = WORKER_TIME2_TICK(TCP_PACING_TIMER_INTERVAL_MS);
    g_tcpTimers[wid]->pacingTimer.expiredTick = tickNow + g_tcpTimers[wid]->pacingTimer.intervalTick;
}

static WORKER_Work_t g_tcpFastTimerWork = {
    .type = WORKER_WORK_TYPE_TIMER,
    .task = {
        .timerWork = {
            .internalTick = WORKER_TIME2_TICK(TCP_FAST_TIMER_INTERVAL_MS),
            .timerCb = TcpFastTimer,
            .initCb = TcpFastTimerInit,
        },
    },
    .map = WORKER_BITMAP_ALL,
    .next = NULL,
};

static WORKER_Work_t g_tcpSlowTimerWork = {
    .type = WORKER_WORK_TYPE_TIMER,
    .task = {
        .timerWork = {
            .internalTick = WORKER_TIME2_TICK(TCP_SLOW_TIMER_INTERVAL_MS),
            .timerCb = TcpSlowTimer,
            .initCb = TcpSlowTimerInit,
        },
    },
    .map = WORKER_BITMAP_ALL,
    .next = NULL,
};

static WORKER_Work_t g_tcpDelayAckTimerWork = {
    .type = WORKER_WORK_TYPE_TIMER,
    .task = {
        .timerWork = {
            .internalTick = WORKER_TIME2_TICK(TCP_DELAYACK_TIMER_INTERVAL_MS),
            .timerCb = TcpDelayAckTimer,
            .initCb = TcpDelayTimerInit,
        },
    },
    .map = WORKER_BITMAP_ALL,
    .next = NULL,
};

static WORKER_Work_t g_tcpPacingTimerWork = {
    .type = WORKER_WORK_TYPE_TIMER,
    .task = {
        .timerWork = {
            .internalTick = WORKER_TIME2_TICK(TCP_PACING_TIMER_INTERVAL_MS),
            .timerCb = TcpPacingTimer,
            .initCb = TcpPacingTimerInit,
        },
    },
    .map = WORKER_BITMAP_ALL,
    .next = NULL,
};

int TcpInitTimer(void)
{
    TcpTimerMemCfg_t memCfg;
    int              wcnt;

    if (g_tcpTimers != NULL) {
        return -1;
    }

    wcnt = CFG_GET_VAL(DP_CFG_WORKER_MAX);

    TcpInitTimerMemCfg(&memCfg, wcnt);

    g_tcpTimers = MEM_MALLOC(memCfg.totSize, MOD_TCP, DP_MEM_FIX);
    if (g_tcpTimers == NULL) {
        DP_LOG_ERR("Malloc memory failed for tcp timers.");
        return -1;
    }

    g_tcpTimers[0] = (TcpTimer_t*)PTR_NEXT(g_tcpTimers, memCfg.timerOffset);
    for (int i = 1; i < wcnt; i++) {
        g_tcpTimers[i] = (TcpTimer_t*)PTR_NEXT(g_tcpTimers[i - 1], memCfg.timerSize);
    }

    for (int i = 0; i < wcnt; i++) {
        TcpInitTcpTimerMem(g_tcpTimers[i], &memCfg);
    }

    if (CFG_GET_TCP_VAL(DP_CFG_TCP_ADJUST_DELAY_ACK) == DP_ENABLE) { // 用户开启了更新延迟ACK间隔，直接设置定时器间隔为10ms触发
        g_tcpDelayAckTimerWork.task.timerWork.internalTick = WORKER_TIME2_TICK(TCP_FAST_TIMER_INTERVAL_MS);
    }

    WORKER_AddWork(&g_tcpFastTimerWork);
    WORKER_AddWork(&g_tcpSlowTimerWork);
    WORKER_AddWork(&g_tcpDelayAckTimerWork);
    WORKER_AddWork(&g_tcpPacingTimerWork);

    return 0;
}

void TcpDeinitTimer(void)
{
    if (g_tcpTimers == NULL) {
        return;
    }
    MEM_FREE(g_tcpTimers, DP_MEM_FIX);
    g_tcpTimers = NULL;
}
