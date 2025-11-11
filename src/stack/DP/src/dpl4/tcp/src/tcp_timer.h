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

#ifndef TCP_TIMER_H
#define TCP_TIMER_H

#include "tcp_types.h"
#include "utils_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

static const uint8_t tcpPersistBackoff[12] = { 3, 6, 12, 24, 48, 96, 120, 120, 120, 120, 120, 120 };

// 因为tcp定时器超时时间不超过0x7fff 这里要左移一位来判断超时器时间的大小
#define TCP_TIMER_TICK_CMP(a, b) (int16_t)(((a) - (b)) << 1)

#define TCP_TIMER_TICK_INVALID (0x8000)

#define TCP_REXMIT_MODE 1
#define TCP_PERSIST_MODE 0

#define TCP_FAST_TIMER_INTERVAL_MS (10) // 快超时定时器间隔10ms
#define TCP_SLOW_TIMER_INTERVAL_MS (500) // 慢超时定时器间隔500ms
#define TCP_DELAYACK_TIMER_INTERVAL_MS (100) // 延迟回复ACK定时器间隔100ms

#define TCP_SLOW_TIMER_HZ (1000 / TCP_SLOW_TIMER_INTERVAL_MS)
#define TCP_FAST_TIMER_HZ (1000 / TCP_FAST_TIMER_INTERVAL_MS)
#define TCP_DELAY_TIMER_HZ (1000 / TCP_DELAYACK_TIMER_INTERVAL_MS)

#define TCP_MSL_TIME_MS     (CFG_GET_TCP_VAL(DP_CFG_TCP_MSL_TIME)) // msl时间
#define TCP_FINWAIT_TIME_MS (CFG_GET_TCP_VAL(DP_CFG_TCP_FIN_TIMEOUT)) // finwait时间

#define TCP_MAX_MAXRXTSHIFT 12

#define TCP_SEC2_TICK(val)      ((1000 * (val)) / 500)  // 时钟转换成tick
#define TCP_TICK2_SEC(val)      ((500 * (val)) / 1000)  // tick转换成时钟
#define TCP_CONKEEP_TICK        (75 * TCP_SLOW_TIMER_HZ) // 建链保活时间定义为75s
#define TCP_DELAYACK_TICK       (TCP_DELAY_TIMER_HZ / 5) // 延迟回复ACK时间最小值200ms
#define TCP_MIN_REXMIT_TICK     (TCP_FAST_TIMER_HZ / 5) // 重传时间最小值200ms
#define TCP_MAX_REXMIT_TICK     (64 * TCP_FAST_TIMER_HZ) // 重传时间最大值64s
#define TCP_INITIAL_REXMIT_TICK (1 * TCP_FAST_TIMER_HZ) // 初始重传时间
#define TCP_FINWAIT_TICK        (TCP_FINWAIT_TIME_MS * TCP_SLOW_TIMER_HZ)
#define TCP_MSL_TICK            (TCP_MSL_TIME_MS * TCP_SLOW_TIMER_HZ)
#define TCP_2MSL_TICK           (2 * TCP_MSL_TIME_MS * TCP_SLOW_TIMER_HZ)

#define TCP_IS_IN_REXMIT(tcp) ((tcp)->expiredTick[TCP_TIMERID_FAST] != TCP_TIMER_TICK_INVALID && \
                                    (tcp)->fastMode == TCP_REXMIT_MODE)
#define TCP_IS_IN_PERSIST(tcp) ((tcp)->expiredTick[TCP_TIMERID_FAST] != TCP_TIMER_TICK_INVALID && \
                                    (tcp)->fastMode == TCP_PERSIST_MODE)

#define TCP_IS_FASTTIMER_IDLE(tcp) ((tcp)->expiredTick[TCP_TIMERID_FAST] == TCP_TIMER_TICK_INVALID)

#define TCP_IS_IN_DELAY(tcp) ((tcp)->expiredTick[TCP_TIMERID_DELAYACK] != TCP_TIMER_TICK_INVALID)

#define TCP_IS_IN_CON_KEEP(tcp) ((tcp)->expiredTick[TCP_TIMERID_SLOW] != TCP_TIMER_TICK_INVALID && \
                                    (tcp)->state < TCP_ESTABLISHED)

#define TCP_IS_IN_KEEP(tcp) ((tcp)->expiredTick[TCP_TIMERID_SLOW] != TCP_TIMER_TICK_INVALID && \
                                    (tcp)->state >= TCP_ESTABLISHED && (tcp)->state <= TCP_FIN_WAIT1)

#define TCP_SHOULD_DEACTIVE_KEEP(tcp) ((tcp)->state == TCP_ESTABLISHED && \
                    (tcp)->expiredTick[TCP_TIMERID_SLOW] != TCP_TIMER_TICK_INVALID)

#define TCP_SHOULD_ACTIVE_KEEP(tcp) ((tcp)->state == TCP_ESTABLISHED && \
                    (tcp)->expiredTick[TCP_TIMERID_SLOW] != TCP_TIMER_TICK_INVALID)

#define TCP_IS_IN_FIN_WAIT(tcp) ((tcp)->expiredTick[TCP_TIMERID_SLOW] != TCP_TIMER_TICK_INVALID && \
                                    (tcp)->state == TCP_FIN_WAIT2)

#define TCP_IS_IN_2MSL(tcp) ((tcp)->expiredTick[TCP_TIMERID_SLOW] != TCP_TIMER_TICK_INVALID && \
                                    (tcp)->state == TCP_TIME_WAIT)

#define TcpActiveFinWaitTimer TcpActiveMslTimer
#define TcpReactiveMslTimer TcpActiveMslTimer
#define TcpReactiveRexmitTimer TcpActiveRexmitTimer

#define TcpUpdateKeepTimer(tcp) TcpActiveKeepTimer((tcp))

#define TcpDeactiveConKeepTimer TcpDeactiveSlowTimer
#define TcpDeactiveKeepTimer TcpDeactiveSlowTimer
#define TcpDeactive2MslTimer TcpDeactiveSlowTimer
#define TcpDeactiveFinWaitTimer TcpDeactiveSlowTimer

typedef struct {
    int init;

    TW_Timer_t fastTimer;
    TW_Timer_t slowTimer;
    TW_Timer_t delayAckTimer;

    TW_Wheel_t* tws[TCP_TIMERID_BUTT];
} TcpTimer_t;

extern TcpTimer_t** g_tcpTimers;

// RFC6298 第2节
// RTO: srtt + max(G, k * rttval)
// G取值TCP_MIN_REXMIT_TICK
static inline uint32_t TcpCalcRto(TcpSk_t* tcp)
{
    uint32_t rto;

    rto = tcp->srtt >> 3; // 3: srtt / 8
    rto += tcp->rttval < TCP_MIN_REXMIT_TICK ? TCP_MIN_REXMIT_TICK : tcp->rttval;

    return rto > TCP_MAX_REXMIT_TICK ? TCP_MAX_REXMIT_TICK : rto;
}

static inline uint32_t TcpGetRttTick(TcpSk_t* tcp)
{
    return g_tcpTimers[tcp->wid]->fastTimer.twTick;
}

/**
 * @brief 获取慢超时定时器的tick数，用于计算保活、坚持等定时器的时间
 *
 * @param wid
 * @return
 */
static inline uint32_t TcpGetSlowTick(TcpSk_t* tcp)
{
    return g_tcpTimers[tcp->wid]->slowTimer.twTick;
}

static inline uint32_t TcpGetDelayTick(TcpSk_t* tcp)
{
    return g_tcpTimers[tcp->wid]->delayAckTimer.twTick;
}

static inline void TcpActiveTimer(TcpSk_t* tcp, TW_Wheel_t* tw, int timerid, uint32_t twTick, uint32_t intervalTick)
{
    if (tcp->expiredTick[timerid] != TCP_TIMER_TICK_INVALID) {
        TW_DelNode(tw, &tcp->twNode[timerid], tcp->expiredTick[timerid]);
    }

    /* 使用0x8000代表无效时间，这里最大的tick不能超过0x7fff */
    tcp->expiredTick[timerid] = (uint16_t)(twTick + intervalTick) & 0x7fff;
    TW_AddNode(tw, &tcp->twNode[timerid], tcp->expiredTick[timerid]);
}

static inline void TcpDeactiveTimer(TcpSk_t* tcp, TW_Wheel_t* tw, int timerid)
{
    if (tcp->expiredTick[timerid] == TCP_TIMER_TICK_INVALID) {
        return;
    }
    TW_DelNode(tw, &tcp->twNode[timerid], tcp->expiredTick[timerid]);
    tcp->expiredTick[timerid] = TCP_TIMER_TICK_INVALID;
}

static inline void TcpActiveConKeepTimer(TcpSk_t* tcp)
{
    TcpTimer_t* timer = g_tcpTimers[tcp->wid];

    TcpActiveTimer(tcp, timer->tws[TCP_TIMERID_SLOW], TCP_TIMERID_SLOW, TcpGetSlowTick(tcp), TCP_CONKEEP_TICK);
}

static inline void TcpActiveKeepTimer(TcpSk_t* tcp)
{
    TcpTimer_t* timer = g_tcpTimers[tcp->wid];
    tcp->keepProbeCnt = 0;
    TcpActiveTimer(tcp, timer->tws[TCP_TIMERID_SLOW], TCP_TIMERID_SLOW, tcp->idleStart, tcp->keepIdle);
}

static inline void TcpActiveMslTimer(TcpSk_t* tcp)
{
    TcpTimer_t* timer = g_tcpTimers[tcp->wid];

    TcpActiveTimer(tcp, timer->tws[TCP_TIMERID_SLOW], TCP_TIMERID_SLOW, TcpGetSlowTick(tcp), TCP_2MSL_TICK);
}

static inline void TcpActiveDelayAckTimer(TcpSk_t* tcp)
{
    TcpTimer_t* timer = g_tcpTimers[tcp->wid];

    TcpActiveTimer(tcp, timer->tws[TCP_TIMERID_DELAYACK], TCP_TIMERID_DELAYACK,
        TcpGetDelayTick(tcp), TCP_DELAYACK_TICK);
}

static inline void TcpDeactiveDelayAckTimer(TcpSk_t* tcp)
{
    TcpDeactiveTimer(tcp, g_tcpTimers[tcp->wid]->tws[TCP_TIMERID_DELAYACK], TCP_TIMERID_DELAYACK);
}

static inline void TcpDeactiveSlowTimer(TcpSk_t* tcp)
{
    TcpDeactiveTimer(tcp, g_tcpTimers[tcp->wid]->tws[TCP_TIMERID_SLOW], TCP_TIMERID_SLOW);
}

static inline void TcpActiveInitialRexmitTimer(TcpSk_t* tcp)
{
    TcpTimer_t* timer = g_tcpTimers[tcp->wid];

    tcp->fastMode = TCP_REXMIT_MODE;
    tcp->backoff = 0;

    TcpActiveTimer(
        tcp, timer->tws[TCP_TIMERID_FAST], TCP_TIMERID_FAST, TcpGetRttTick(tcp), TCP_INITIAL_REXMIT_TICK);
}

static inline void TcpActiveRexmitTimer(TcpSk_t* tcp)
{
    TcpTimer_t* timer = g_tcpTimers[tcp->wid];
    tcp->fastMode = TCP_REXMIT_MODE;
    tcp->backoff = 0;

    TcpActiveTimer(tcp, timer->tws[TCP_TIMERID_FAST], TCP_TIMERID_FAST, TcpGetRttTick(tcp), TcpCalcRto(tcp));
}

static inline void TcpDeactiveRexmitTimer(TcpSk_t* tcp)
{
    tcp->backoff = 0;
    TcpDeactiveTimer(tcp, g_tcpTimers[tcp->wid]->tws[TCP_TIMERID_FAST], TCP_TIMERID_FAST);
}

static inline void TcpActivePersistTimer(TcpSk_t* tcp)
{
    TcpTimer_t* timer = g_tcpTimers[tcp->wid];

    tcp->backoff = 0;
    tcp->fastMode = TCP_PERSIST_MODE;

    TcpActiveTimer(tcp, timer->tws[TCP_TIMERID_FAST], TCP_TIMERID_FAST, TcpGetRttTick(tcp), tcpPersistBackoff[0]);
}

static inline void TcpDeactivePersistTimer(TcpSk_t* tcp)
{
    tcp->backoff = 0;
    TcpDeactiveTimer(tcp, g_tcpTimers[tcp->wid]->tws[TCP_TIMERID_FAST], TCP_TIMERID_FAST);
}

// 这个接口需要操作实例上的定时器，只有在实例上才能调用
void TcpRemoveAllTimers(TcpSk_t* tcp);

int TcpInitTimer(void);

void TcpDeinitTimer(void);

#ifdef __cplusplus
}
#endif
#endif
