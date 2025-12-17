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
#include "tcp_sock.h"
#include "utils_statistic.h"

#ifdef __cplusplus
extern "C" {
#endif

/* tcp坚持定时器使用的退避算法重传时间数组，当计算的RTO小于1s时,确保最大超时重传时间可以退避至64s */
static const uint16_t tcpPersistBackoff[12] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 512, 512 };

// 因为tcp定时器超时时间不超过0x7fff 这里要左移一位来判断超时器时间的大小
#define TCP_TIMER_TICK_CMP(a, b) (int16_t)(((a) - (b)) << 1)

#define TCP_TIMER_TICK_INVALID (0x8000)

#define TCP_REXMIT_MODE 1
#define TCP_PERSIST_MODE 0

#define TCP_FAST_TIMER_INTERVAL_MS (10) // 快超时定时器间隔10ms
#define TCP_SLOW_TIMER_INTERVAL_MS (500) // 慢超时定时器间隔500ms
#define TCP_DELAYACK_TIMER_INTERVAL_MS (100) // 延迟回复ACK定时器间隔100ms
#define TCP_PACING_TIMER_INTERVAL_MS (10) // 限速超时定时器间隔10ms

#define TCP_SLOW_TIMER_HZ (1000 / TCP_SLOW_TIMER_INTERVAL_MS)
#define TCP_FAST_TIMER_HZ (1000 / TCP_FAST_TIMER_INTERVAL_MS)
#define TCP_DELAY_TIMER_HZ (1000 / TCP_DELAYACK_TIMER_INTERVAL_MS)
#define TCP_PACING_TIMER_HZ (1000 / TCP_PACING_TIMER_INTERVAL_MS)

#define TCP_MSL_TIME_SEC     (CFG_GET_TCP_VAL(DP_CFG_TCP_MSL_TIME)) // msl时间
#define TCP_FINWAIT_TIME_SEC (CFG_GET_TCP_VAL(DP_CFG_TCP_FIN_TIMEOUT)) // finwait时间

#define TCP_2MSL_TIME_MS (2 * 1000 * TCP_MSL_TIME_SEC)

#define TCP_CONKEEP_SEC (75)

#define TCP_MAX_MAXRXTSHIFT 12
#define TCP_MAX_REXMIT_CNT_NO_EST  (CFG_GET_TCP_VAL(CFG_TCP_MAX_REXMIT_CNT_NO_EST))

#define TCP_MSEC2_FASTTICK(val)     ((val) / 10) // 毫秒转换tick
#define TCP_SEC2_TICK(val)      ((1000 * (val)) / 500)  // 时钟转换成tick
#define TCP_TICK2_SEC(val)      ((500 * (val)) / 1000)  // tick转换成时钟
#define TCP_CONKEEP_TICK        (TCP_CONKEEP_SEC * TCP_SLOW_TIMER_HZ) // 建链保活时间定义为75s
#define TCP_DELAYACK_TICK       (TCP_DELAY_TIMER_HZ / 5) // 延迟回复ACK时间最小值200ms
#define TCP_MIN_REXMIT_TICK     (TCP_FAST_TIMER_HZ / 5) // 重传时间最小值200ms
#define TCP_MAX_REXMIT_TICK     (64 * TCP_FAST_TIMER_HZ) // 重传时间最大值64s
#define TCP_INITIAL_REXMIT_TICK (1 * TCP_FAST_TIMER_HZ) // 初始重传时间
#define TCP_FINWAIT_TICK        (TCP_FINWAIT_TIME_SEC * TCP_SLOW_TIMER_HZ)
#define TCP_MSL_TICK            (TCP_MSL_TIME_SEC * TCP_SLOW_TIMER_HZ)
#define TCP_2MSL_TICK           (2 * TCP_MSL_TIME_SEC * TCP_SLOW_TIMER_HZ)
#define TCP_PACING_TICK_SEC     (TCP_PACING_TIMER_INTERVAL_MS) // PACING定时器间隔时间
#define TCP_PACING_TICK         (TCP_PACING_TIMER_HZ / 100)    // PACING定时器间隔tick

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

#define TCP_SHOULD_DEACTIVE_KEEP(tcp) ((tcp)->state == TCP_ESTABLISHED || (tcp)->state == TCP_CLOSE_WAIT)

#define TCP_SHOULD_ACTIVE_KEEP(tcp) ((tcp)->state == TCP_ESTABLISHED || (tcp)->state == TCP_CLOSE_WAIT)

#define TCP_IS_IN_FIN_WAIT(tcp) ((tcp)->expiredTick[TCP_TIMERID_SLOW] != TCP_TIMER_TICK_INVALID && \
                                    (tcp)->state == TCP_FIN_WAIT2)

#define TCP_IS_IN_2MSL(tcp) ((tcp)->expiredTick[TCP_TIMERID_SLOW] != TCP_TIMER_TICK_INVALID && \
                                    (tcp)->state == TCP_TIME_WAIT)

#define TCP_IS_IN_PACING(tcp) ((tcp)->expiredTick[TCP_TIMERID_PACING] != TCP_TIMER_TICK_INVALID)

#define TcpReactiveMslTimer TcpActiveMslTimer
#define TcpReactiveRexmitTimer TcpActiveRexmitTimer

#define TcpUpdateKeepTimer(tcp) TcpActiveKeepTimer((tcp))

#define TcpDeactiveConKeepTimer TcpDeactiveSlowTimer
#define TcpDeactiveKeepTimer TcpDeactiveSlowTimer
#define TcpDeactive2MslTimer TcpDeactiveSlowTimer
#define TcpDeactiveFinWaitTimer TcpDeactiveSlowTimer

#define FAST_TIMER_TWSLOT_DEFAULT      13  // 快定时器默认时间轮槽个数，掩码 0x1FFF
#define SLOW_TIMER_TWSLOT_DEFAULT      14  // 慢定时器默认时间轮槽个数，掩码 0x3FFF
#define DELAYACK_TIMER_TWSLOT_DEFAULT  2   // delayack定时器默认时间轮槽个数，掩码 0x3
#define PACING_TIMER_TWSLOT_DEFAULT    0   // pacing定时器超时时间与精度相同，不使用时间轮

/** TCP定时器类型，用于统计网络状态
 *  0: 表示未启动定时器
 *  1: 表示重传定时器
 *  2: 表示建链保活/保活/FIN_WAIT_2定时器，具体与当前连接状态相关
 *  3: 表示TIME_WAIT定时器
 *  4: 表示坚持定时器
 */
typedef enum {
    TCP_TRTYPE_NONE = 0,
    TCP_TRTYPE_REXMIT,
    TCP_TRTYPE_CONN,
    TCP_TRTYPE_TIMEWAIT,
    TCP_TRTYPE_PERSIST,
} TCP_TimetType_t;

/**
 * @ingroup cfg
 * tcp时间轮规格
 */
typedef struct TW_MaskBits {
    uint8_t bits[TCP_TIMERID_BUTT];
} TW_MaskBits_t;

/**
 * @ingroup cfg
 * @brief 时间轮槽规格配置接口，暂不对外开放
 *
 * @par 描述: 时间轮槽规格配置接口，用于配置tcp定时器时间轮规格。
 * @attention
 * 约束必须在DP协议栈初始化前进行注册
 *
 * @param bitsReg [IN]  时间轮槽配置个数数组<非空指针>
 *
 * @retval 0  成功
 * @retval -1 失败

 * @see TW_MaskBits_t
 */
int TcpTimerSetMaskBits(TW_MaskBits_t* bitsReg);

typedef struct {
    int init;

    TW_Timer_t fastTimer;
    TW_Timer_t slowTimer;
    TW_Timer_t delayAckTimer;
    TW_Timer_t pacingTimer;

    TW_Wheel_t* tws[TCP_TIMERID_BUTT];
} TcpTimer_t;

extern TcpTimer_t** g_tcpTimers;

// RFC6298 第2节
// RTO: srtt + max(G, k * rttval)
// G取值TCP_MIN_REXMIT_TICK
static inline uint32_t TcpCalcRexmit(TcpSk_t* tcp)
{
    uint32_t rto;

    rto = tcp->srtt >> 3; // 3: srtt / 8
    // 根据用户设置最小值来计算
    rto += tcp->rttval < tcp->rxtMin ? tcp->rxtMin : tcp->rttval;

    return rto > TCP_MAX_REXMIT_TICK ? TCP_MAX_REXMIT_TICK : rto;
}

static inline uint32_t TcpCalcPersist(TcpSk_t* tcp)
{
    uint32_t rto;

    rto = (tcp->srtt >> 2) + (tcp->rttval >> 1); // 2 : srtt / 4

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

static inline uint32_t TcpGetPacingTick(TcpSk_t* tcp)
{
    return g_tcpTimers[tcp->wid]->pacingTimer.twTick;
}

void TcpActiveTimer(TcpSk_t* tcp, TW_Wheel_t* tw, int timerid, uint32_t twTick, uint32_t intervalTick);

static inline void TcpDeactiveTimer(TcpSk_t* tcp, TW_Wheel_t* tw, int timerid)
{
    long int curTid = TcpGetTid();
    if (CFG_GET_TCP_VAL(CFG_TCP_TSQ_PASSIVE) == DP_ENABLE && curTid != tcp->tid) {
        DP_LOG_ERR("deactive timer tid not match, curTid: %ld, tcp->tid: %ld,flags:%u,nest:%u,timerid:%d,sk.flags:%u",
            curTid, tcp->tid, tcp->flags, tcp->tsqNested, timerid, TcpSk2Sk(tcp)->flags);
        DP_ADD_ABN_STAT(DP_TIMER_ACTIVE_EXCEPT);
    }

    if (tcp->expiredTick[timerid] == TCP_TIMER_TICK_INVALID) {
        return;
    }
    TW_DelNode(tw, &tcp->twNode[timerid], tcp->expiredTick[timerid]);
    tcp->expiredTick[timerid] = TCP_TIMER_TICK_INVALID;
}

static inline void TcpActiveConKeepTimer(TcpSk_t* tcp)
{
    TcpTimer_t* timer = g_tcpTimers[tcp->wid];

    tcp->trType = TCP_TRTYPE_CONN;
    TcpActiveTimer(tcp, timer->tws[TCP_TIMERID_SLOW], TCP_TIMERID_SLOW, TcpGetSlowTick(tcp), TCP_CONKEEP_TICK);
}

static inline void TcpAdjustKeepTimer(TcpSk_t* tcp)
{
    TcpTimer_t* timer = g_tcpTimers[tcp->wid];
    tcp->keepProbeCnt = 0;
    tcp->userTimeStartSlow = TcpGetSlowTick(tcp);
    tcp->trType = TCP_TRTYPE_CONN;

    // 若新增时间轮槽位置当前已触发过，则更新到下一次触发的槽位
    if (TIME_CMP((tcp->idleStart + tcp->keepIdle), TcpGetSlowTick(tcp)) <= 0) {
        TcpActiveTimer(tcp, timer->tws[TCP_TIMERID_SLOW], TCP_TIMERID_SLOW, TcpGetSlowTick(tcp), 1);
        return;
    }

    uint32_t intervalTick = ((tcp->keepIdle > 0) ? tcp->keepIdle : 1);
    TcpActiveTimer(tcp, timer->tws[TCP_TIMERID_SLOW], TCP_TIMERID_SLOW, tcp->idleStart, intervalTick);
}

static inline void TcpActiveKeepTimer(TcpSk_t* tcp)
{
    TcpTimer_t* timer = g_tcpTimers[tcp->wid];
    tcp->keepProbeCnt = 0;
    tcp->userTimeStartSlow = TcpGetSlowTick(tcp);
    tcp->trType = TCP_TRTYPE_CONN;
    uint32_t intervalTick = ((tcp->keepIdle > 0) ? tcp->keepIdle : 1);
    TcpActiveTimer(tcp, timer->tws[TCP_TIMERID_SLOW], TCP_TIMERID_SLOW, tcp->idleStart, intervalTick);
}

static inline void TcpActiveMslTimer(TcpSk_t* tcp)
{
    TcpTimer_t* timer = g_tcpTimers[tcp->wid];
    tcp->trType = TCP_TRTYPE_TIMEWAIT;
    TcpActiveTimer(tcp, timer->tws[TCP_TIMERID_SLOW], TCP_TIMERID_SLOW,
                   TcpGetSlowTick(tcp), (uint32_t)TCP_2MSL_TICK);
}

static inline void TcpActiveFinWaitTimer(TcpSk_t* tcp)
{
    TcpTimer_t* timer = g_tcpTimers[tcp->wid];
    tcp->trType = TCP_TRTYPE_CONN;
    uint32_t intervalTick = (uint32_t)UTILS_MAX(TCP_FINWAIT_TICK, 1);
    TcpActiveTimer(tcp, timer->tws[TCP_TIMERID_SLOW], TCP_TIMERID_SLOW,
                   TcpGetSlowTick(tcp), intervalTick);
}

static inline void TcpActiveDelayAckTimer(TcpSk_t* tcp)
{
    TcpTimer_t* timer = g_tcpTimers[tcp->wid];

    uint32_t tickInter = TCP_DELAYACK_TICK;
    if (CFG_GET_TCP_VAL(DP_CFG_TCP_ADJUST_DELAY_ACK) == DP_ENABLE) {
        tickInter = TCP_MSEC2_FASTTICK((uint32_t)CFG_GET_TCP_VAL(DP_CFG_TCP_DELAY_ACK_INTER));
    }

    TcpActiveTimer(tcp, timer->tws[TCP_TIMERID_DELAYACK], TCP_TIMERID_DELAYACK, TcpGetDelayTick(tcp), tickInter);
}

static inline void TcpDeactiveDelayAckTimer(TcpSk_t* tcp)
{
    TcpDeactiveTimer(tcp, g_tcpTimers[tcp->wid]->tws[TCP_TIMERID_DELAYACK], TCP_TIMERID_DELAYACK);
}

static inline void TcpDeactiveSlowTimer(TcpSk_t* tcp)
{
    tcp->keepIdleCnt = 0;
    TcpDeactiveTimer(tcp, g_tcpTimers[tcp->wid]->tws[TCP_TIMERID_SLOW], TCP_TIMERID_SLOW);
}

static inline void TcpActiveInitialRexmitTimer(TcpSk_t* tcp)
{
    TcpTimer_t* timer = g_tcpTimers[tcp->wid];

    tcp->fastMode = TCP_REXMIT_MODE;
    tcp->backoff = 0;
    tcp->userTimeStartFast = TcpGetRttTick(tcp);
    tcp->trType = TCP_TRTYPE_REXMIT;
    TcpActiveTimer(
        tcp, timer->tws[TCP_TIMERID_FAST], TCP_TIMERID_FAST, TcpGetRttTick(tcp), TCP_INITIAL_REXMIT_TICK);
}

static inline void TcpActiveRexmitTimer(TcpSk_t* tcp)
{
    TcpTimer_t* timer = g_tcpTimers[tcp->wid];
    tcp->fastMode = TCP_REXMIT_MODE;
    tcp->backoff = 0;
    tcp->keepIdleCnt = 0;
    tcp->userTimeStartFast = TcpGetRttTick(tcp);
    tcp->trType = TCP_TRTYPE_REXMIT;
    TcpActiveTimer(tcp, timer->tws[TCP_TIMERID_FAST], TCP_TIMERID_FAST, TcpGetRttTick(tcp), TcpCalcRexmit(tcp));
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
    tcp->userTimeStartFast = TcpGetRttTick(tcp);
    tcp->trType = TCP_TRTYPE_PERSIST;
    TcpActiveTimer(tcp, timer->tws[TCP_TIMERID_FAST], TCP_TIMERID_FAST, TcpGetRttTick(tcp), TcpCalcPersist(tcp));
}

static inline void TcpDeactivePersistTimer(TcpSk_t* tcp)
{
    tcp->backoff = 0;
    TcpDeactiveTimer(tcp, g_tcpTimers[tcp->wid]->tws[TCP_TIMERID_FAST], TCP_TIMERID_FAST);
}

static inline void TcpActivePacingTimer(TcpSk_t* tcp)
{
    TcpTimer_t* timer = g_tcpTimers[tcp->wid];

    TcpActiveTimer(tcp, timer->tws[TCP_TIMERID_PACING], TCP_TIMERID_PACING, TcpGetPacingTick(tcp), TCP_PACING_TICK);
}

static inline void TcpDeactivePacingTimer(TcpSk_t* tcp)
{
    TcpDeactiveTimer(tcp, g_tcpTimers[tcp->wid]->tws[TCP_TIMERID_PACING], TCP_TIMERID_PACING);
}

// 这个接口需要操作实例上的定时器，只有在实例上才能调用
void TcpRemoveAllTimers(TcpSk_t* tcp);

int TcpInitTimer(void);

void TcpDeinitTimer(void);

/* Minimum RTT in msec. ~0 means not available. */
static inline uint32_t TcpGetMinRtt(TcpSk_t* tcp)
{
    return (uint32_t)MinmaxGetValue(&tcp->rttMin);      // rttMin存储时间，传入时为uint32
}

static inline void TcpUpdateMstamp(TcpSk_t* tcp)
{
    uint32_t val = UTILS_TimeNow();
    if (TIME_CMP(val, tcp->tcpMstamp) > 0) {
        tcp->tcpMstamp = val;
    }
}

#ifdef __cplusplus
}
#endif
#endif
