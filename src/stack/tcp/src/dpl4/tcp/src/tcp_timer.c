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

#include "utils_cfg.h"
#include "utils_log.h"
#include "worker.h"

#include "tcp_out.h"
#include "tcp_tsq.h"
#include "tcp_sock.h"

#include "tcp_cc.h"
#include "tcp_timer.h"

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

static int TcpSlowTimeout(TW_Wheel_t* wheel, TW_Node_t* tn, uint32_t twTick)
{
    TcpSk_t* tcp = CONTAINER_OF(tn, TcpSk_t, twNode[TCP_TIMERID_SLOW]);
    uint16_t twTick16 = (uint16_t)(twTick & 0x7FFF);

    if (TCP_TIMER_TICK_CMP(tcp->expiredTick[TCP_TIMERID_SLOW], twTick16) > 0) {
        return -1;
    }

    // 触发建链保活定时器，或者是超过了用户设置的最大保活次数，直接触发断链
    if (tcp->state < TCP_ESTABLISHED || tcp->keepProbeCnt >= tcp->keepProbes) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_KEEP_DROPS);
        tcp->expiredTick[TCP_TIMERID_SLOW] = TCP_TIMER_TICK_INVALID;
        TcpTsqAddQue(tcp, TCP_TSQ_ABORT);
        return 0;
    // 要么是FIN_WAIT定时器，要么是2MSL定时器，两种情况下均直接释放资源
    } else if (tcp->state > TCP_FIN_WAIT1) {
        tcp->expiredTick[TCP_TIMERID_SLOW] = TCP_TIMER_TICK_INVALID;
        TcpCleanUp(tcp);
        TcpSetState(tcp, TCP_CLOSED);
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_CLOSED);
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_FIN_WAIT_2_DROPS);
        TcpFreeSk(TcpSk2Sk(tcp));
        return 0;
    }

    // 保活定时器处理
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_KEEP_TIME_OUT);
    TcpSndKeepProbe(tcp);
    tcp->keepProbeCnt++;
    tcp->expiredTick[TCP_TIMERID_SLOW] = TCP_TIMER_TICK_INVALID;
    TcpActiveTimer(tcp, wheel, TCP_TIMERID_SLOW, twTick16, tcp->keepIntvl);
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

static int TcpProcRexmitTimer(TcpSk_t* tcp, TW_Wheel_t* wheel, uint32_t twTick)
{
    uint32_t maxRexmit = TCP_MAX_MAXRXTSHIFT;
    if (TcpJudgeDeferAccept(tcp)) {
        maxRexmit = tcp->maxRexmit;
    }

    if (tcp->backoff >= maxRexmit) {
        tcp->expiredTick[TCP_TIMERID_FAST] = TCP_TIMER_TICK_INVALID;
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_TIMEOUT_DROP);
        TcpTsqAddQue(tcp, TCP_TSQ_ABORT);
        return 0;
    }

    // 重传超时则触发拥塞控制
    if (tcp->backoff == 0 || TCP_IS_IN_RECOVERY(tcp)) {
        TcpCaTimeout(tcp);
    }
    TcpRexmitPkt(tcp);

    uint32_t rto = TcpCalcRto(tcp);
    tcp->backoff++;
    rto <<= tcp->backoff;
    rto = (rto > TCP_MAX_REXMIT_TICK) ? TCP_MAX_REXMIT_TICK : rto;
    tcp->expiredTick[TCP_TIMERID_FAST] = TCP_TIMER_TICK_INVALID;
    TcpActiveTimer(tcp, wheel, TCP_TIMERID_FAST, twTick, rto);

    return 0;
}

static int TcpProcPersistTimer(TcpSk_t* tcp, TW_Wheel_t* wheel, uint32_t twTick)
{
    if (tcp->backoff >= TCP_MAX_MAXRXTSHIFT) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_PERSIST_DROPS);
        tcp->expiredTick[TCP_TIMERID_FAST] = TCP_TIMER_TICK_INVALID;
        TcpTsqAddQue(tcp, TCP_TSQ_ABORT);
        return 0;
    }

    DP_INC_TCP_STAT(tcp->wid, DP_TCP_PERSIST_TIMEOUT);
    TcpXmitZeroWndProbePkt(tcp);

    tcp->expiredTick[TCP_TIMERID_FAST] = TCP_TIMER_TICK_INVALID;
    TcpActiveTimer(tcp, wheel, TCP_TIMERID_FAST, twTick, tcpPersistBackoff[tcp->backoff]);
    tcp->backoff++;
    return 0;
}

static int TcpFastTimeout(TW_Wheel_t* wheel, TW_Node_t* tn, uint32_t twTick)
{
    TcpSk_t* tcp = CONTAINER_OF(tn, TcpSk_t, twNode[TCP_TIMERID_FAST]);

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
    /* 最大时间不超0x7fff */
    if (TCP_TIMER_TICK_CMP(tcp->expiredTick[TCP_TIMERID_DELAYACK], (twTick & 0x7FFF)) > 0) {
        return -1;
    }

    // 触发延时定时器后就直接退出定时器
    tcp->expiredTick[TCP_TIMERID_DELAYACK] = TCP_TIMER_TICK_INVALID;
    TcpXmitAckPkt(tcp);
    return 0;
}

#define TCP_FAST_TIMER_WHEEL_CNT 1
#define TCP_SLOW_TIMER_WHEEL_CNT 1
#define TCP_DELAY_TIMER_WHEEL_CNT 1

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

static void TcpInitTimerMemCfg(TcpTimerMemCfg_t* memCfg, int wcnt)
{
    int twBits[TCP_TIMERID_BUTT] = { 0 };

    memCfg->timerSize = sizeof(TcpTimer_t);

    for (int i = 0; i < TCP_TIMERID_BUTT; i++) {
        memCfg->twCfg[i].twBits = twBits[i];
        memCfg->twCfg[i].offset = memCfg->timerSize;
        memCfg->timerSize += TW_GetWheelSize(twBits[i]);
    }

    memCfg->totSize     = sizeof(TcpTimer_t*) * wcnt;
    memCfg->timerOffset = memCfg->totSize;
    memCfg->totSize += memCfg->timerSize * (size_t)wcnt;
}

static int TcpInitTcpTimerMem(TcpTimer_t* tcpTimer, TcpTimerMemCfg_t* memCfg)
{
    TW_Timer_t* twTimer;
    TW_Cb_t     cb[TCP_TIMERID_BUTT] = {
        [TCP_TIMERID_FAST]  = TcpFastTimeout,
        [TCP_TIMERID_SLOW]    = TcpSlowTimeout,
        [TCP_TIMERID_DELAYACK]    = TcpDelayAckTimeout,
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
    g_tcpTimers[wid]->delayAckTimer.intervalTick = WORKER_TIME2_TICK(TCP_DELAYACK_TIMER_INTERVAL_MS);
    g_tcpTimers[wid]->delayAckTimer.expiredTick = tickNow + g_tcpTimers[wid]->delayAckTimer.intervalTick;
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

    WORKER_AddWork(&g_tcpFastTimerWork);
    WORKER_AddWork(&g_tcpSlowTimerWork);
    WORKER_AddWork(&g_tcpDelayAckTimerWork);

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
