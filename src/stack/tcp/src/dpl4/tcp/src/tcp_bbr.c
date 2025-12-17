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

#include "dp_in_api.h"
#include "dp_tcp_cc_api.h"

#include "dp_ip.h"
#include "dp_tcp.h"
#include "shm.h"
#include "utils_base.h"
#include "utils_log.h"
#include "tcp_timer.h"
#include "tcp_rate.h"
#include "tcp_cc.h"

#include "tcp_bbr.h"

#define BBR_INFINITE_SSTHRESH 0x7fffffff

#define BW_SCALE 32 /* 速率的比例因子,以避免带宽估计中的截断 */
#define BW_UNIT (1UL << BW_SCALE) /* rate unit ~= (1500 bytes / 1 usec / 2^32) ~= 715 bps */

#define BBR_SCALE 8 /* BBR中分数的比例因子 */
#define BBR_UNIT (1 << BBR_SCALE)

/*
 * The next routines deal with comparing 32 bit unsigned ints
 * and worry about wraparound (automatic with unsigned arithmetic).
 */
#define BBR_BEFORE_U32(_seq1, _seq2) ((int32_t)((_seq1) - (_seq2)) < 0)

#define BBR_AFTER_U32(seq2, seq1) BBR_BEFORE_U32(seq1, seq2)

#define BBR_BW_FULL_INCR_FACT (BBR_UNIT * 5 / 4)
#define BBR_BW_FULL_CNT 3

#define BBR_MIN_RTT_VALID_TIME_WIN (10 * 1000)
#define BBR_PROBE_RTT_MIN_WIN 200

// 增益比例使用
#define BBR_STARTUP_GAIN (BBR_UNIT * 2885 / 1000 + 1)         /* STARTUP的增益系数为 2885/1000 */
#define BBR_DRAIN_GAIN (BBR_UNIT * 1000 / 2885)               /* DRAIN的增益系数为1000/2885 */
#define BBR_PROBW_CWND_GAIN (BBR_UNIT * 2)

#define BBR_PROBW_PACING_HIGH_GAIN (BBR_UNIT * 5 / 4)
#define BBR_PROBW_PACING_LOW_GAIN (BBR_UNIT * 3 / 4)

#define BBR_PROBE_BW_CYCLE_LEN 8
#define BBR_PROBE_BW_CYCLE_MAX_IDX (BBR_PROBE_BW_CYCLE_LEN - 1)
#define BBR_PROBE_BW_CYCLE_MASK (BBR_PROBE_BW_CYCLE_LEN - 1)
#define BBR_BW_VALID_TIME_WIN (BBR_PROBE_BW_CYCLE_LEN + 2)

#define BBR_MIN_CWND 4
#define BBR_MAX_CWND 65535

/* extra ack feature */
#define BBR_EXTRA_ACKED_UPDATE_RTT_CNT 5
#define BBR_EXTRA_ACKED_MAX_ACKED_CNT (1U << 20)
#define BBR_EXTRA_ACKED_MAX_TIME_WIN 100 /* unit: ms */

const unsigned int g_bbrMinTsoRate = 1200000;

const uint32_t g_bbrPacingGain[] = {
    BBR_PROBW_PACING_HIGH_GAIN, BBR_PROBW_PACING_LOW_GAIN,
    BBR_UNIT, BBR_UNIT, BBR_UNIT,
    BBR_UNIT, BBR_UNIT, BBR_UNIT
};

/* Pace at ~1% below estimated bw, on average, to reduce queue at bottleneck.
 * In order to help drive the network toward lower queues and low latency while
 * maintaining high utilization, the average pacing rate aims to be slightly
 * lower than the estimated bandwidth. This is an important aspect of the
 * design.
 */
#define BBR_PACING_HEADROOM 1

#define BBR_SLOW_INTVL_MIN_RTT_CNT 4
#define BBR_SLOW_INTVL_MAX_RTT_CNT (4 * BBR_SLOW_INTVL_MIN_RTT_CNT)
#define BBR_SLOW_BW_LOSS_FACTOR 5
#define BBR_SLOW_BW_DIFF (4000 / 8)
#define BBR_SLOW_BW_DIFF_RATIO 8
#define BBR_SLOW_BW_EST_MAX_RTT_CNT 48

#define GSO_MAX_SIZE 65536
#define GSO_MAX_SEGS 65535

/* It is not the same with linux kernel, in linux kernel it is 128 + MAX_HEADER,
 * and MAX_HEADER is 128, but I don't know why it is a so big number
 * So I just use a value 128 */
#define MAX_TCP_HEADER 128

#define TCP_DEFAULT_PACING_SHIFT 8

/* CWND in bbr algorith means packets number, not bytes number */
static inline uint32_t BbrCwndPkt(TcpSk_t* tcp)
{
    ASSERT(tcp->mss != 0);
    return (tcp->cwnd + tcp->mss - 1) / tcp->mss;
}

static inline uint32_t TcpStampMsDelta(uint32_t t1, uint32_t t0)
{
    return (uint32_t)UTILS_MAX((int32_t)(t1 - t0), 0);
}

static uint64_t BBRBw(BBR_Cb_t* bbrCb)
{
    if (bbrCb->slowBwValid != 0) {
        return bbrCb->slowBw;
    }

    return MinmaxGetValue(&bbrCb->bw);
}


static uint64_t BbrRateBytesPerSec(TcpSk_t* tcp, uint64_t bw, uint32_t gain)
{
    // 估计mtu值，20为选项预估长度
    uint32_t mtu = tcp->mss + sizeof(DP_TcpHdr_t) + 20 + sizeof(DP_IpHdr_t);        // 不考虑IPV6长度差距的20字节
    uint64_t rate = bw;

    rate *= mtu;
    rate *= gain;
    rate >>= BBR_SCALE;

    /* left some room for pacing rate */
    rate *= MSEC_PER_SEC / 100 * (100 - BBR_PACING_HEADROOM);       // 100 ：按BBR_PACING_HEADROOM百分比处理rate
    return rate >> BW_SCALE;
}

static uint32_t BBRBwToPacingRate(TcpSk_t* tcp, uint64_t bw, uint32_t gain)
{
    /* bw unit is packet_cnt * BW_UNIT / ms => bytes / sec */
    uint64_t rate = BbrRateBytesPerSec(tcp, bw, gain);
    /* not exceed the requirement from usr */
    if (TcpSk2Sk(tcp)->bandWidth != 0) {
        rate = UTILS_MIN(rate, TcpSk2Sk(tcp)->bandWidth);
    }

    return (uint32_t)rate;
}

static void BBRInitPacingRate(TcpSk_t* tcp)
{
    uint64_t bw;
    uint32_t rttMs = tcp->srtt >> 3;        // 3：取srtt的1/8
    if (rttMs == 0) {
        rttMs = 1;             // 初始化为1毫秒
    }

    bw = (uint64_t)BbrCwndPkt(tcp) * BW_UNIT;
    bw /= rttMs;        // rttMs能够保证不为0
    tcp->pacingRate = BBRBwToPacingRate(tcp, bw, BBR_STARTUP_GAIN);
}

static void BBRResetSlowBwEstimate(TcpSk_t* tcp)
{
    BBR_Cb_t* bbrCb = tcp->caCb;

    bbrCb->slowBwEstimateStart = 0;

    bbrCb->slowBwValid = 0;
    bbrCb->slowBw = 0;

    bbrCb->slowIntervalRttCnt = 0;
}

static uint32_t BBRCalcBdp(TcpSk_t* tcp, uint64_t bw, int gain)
{
    BBR_Cb_t* bbrCb = tcp->caCb;

    /* If failed to get a min rtt at first, just use the init cwnd instead */
    if (UTILS_UNLIKELY(bbrCb->minRttMs == ~0U)) {
        return tcp->initCwnd;
    }

    uint64_t w = bw * bbrCb->minRttMs * (uint32_t)gain;

    /* Apply a gain to the given value, then remove the BW_SCALE shift. */
    return (uint32_t)(((w >> BBR_SCALE) + BW_UNIT - 1) / BW_UNIT);
}

/* override sysctl_tcp_min_tso_segs */
static uint32_t BbrMinTsoSegs(TcpSk_t* tcp)
{
    return tcp->pacingRate < (g_bbrMinTsoRate >> 3U) ? 1U : 2U;
}

/* pac */
static uint32_t BBRTsoSegsGoal(TcpSk_t* tcp)
{
    uint32_t segs;
    uint32_t bytes;

    if (tcp->mss == 0) {
        return 0;
    }

    /*
     * (pacing_rate >> pacing_shift) stands for how many bytes it queued in
     * 1/(1 << pacing_shift) senconds.These queued bytes can be used to make a TSO SEGMENT
     */
    bytes = UTILS_MIN((uint32_t)tcp->pacingRate >> TCP_DEFAULT_PACING_SHIFT,
        (uint32_t)(GSO_MAX_SIZE - 1 - MAX_TCP_HEADER));
    segs = UTILS_MAX((uint32_t)(bytes / tcp->mss), BbrMinTsoSegs(tcp));

    return UTILS_MIN(segs, 0x7FU);
}

/*
 * Return maximum extra acked in past k-2k round trips,
 * where k = bbr_extra_acked_update_rtt_cnt.
 */
static inline uint32_t BBRExtraAcked(BBR_Cb_t* bbrCb)
{
    if (bbrCb->extraAcked[0] > bbrCb->extraAcked[1]) {
        return bbrCb->extraAcked[0];
    }

    return bbrCb->extraAcked[1];
}


#define MAX_CACHE_PACKETS_IN_HOSTS 3

static inline uint32_t BBRExtraTsoQuota(TcpSk_t* tcp)
{
    return MAX_CACHE_PACKETS_IN_HOSTS * BBRTsoSegsGoal(tcp);
}

#define BBR_EXTRA_CWND_IN_PROBE_BW 2
/* a way to increase the cwnd to full fill the BDP path. */
static uint32_t BBRCalcExtraCwnd(TcpSk_t* tcp, uint32_t cwnd)
{
    BBR_Cb_t* bbrCb = tcp->caCb;
    uint32_t ret = cwnd;

    ret += BBRExtraTsoQuota(tcp);
    /* round up the cwnd value */
    ret = (ret + 1) & ~1U;

    if (bbrCb->mode == BBR_PROBE_BW) {
        if (bbrCb->cycleIdx == 0) {
            return (ret + BBR_EXTRA_CWND_IN_PROBE_BW);
        }
    }
    return ret;
}

static uint32_t BBRTargetInflight(TcpSk_t* tcp, uint64_t bw, int gain)
{
    uint32_t inflight = BBRCalcBdp(tcp, bw, gain);
    return BBRCalcExtraCwnd(tcp, inflight);
}

static bool BBRShouldAdvanceCycle(TcpSk_t* tcp)
{
    BBR_Cb_t* bbrCb = tcp->caCb;
    TcpRateSample_t* rs = &tcp->rs;
    uint64_t maxBw;
    uint32_t priorInflight;
    bool shouldAdvanceCycle = TcpStampMsDelta(tcp->ConnDeliveredMstamp, bbrCb->cycleStartTime) >
        bbrCb->minRttMs;

    /* if the cycle time met, goto next cycle */
    if (bbrCb->pacingGain == BBR_UNIT) {
        return shouldAdvanceCycle;
    }

    priorInflight = rs->priorInFlight;
    maxBw = MinmaxGetValue(&bbrCb->bw);
    /*
     * make sure the bytes in flight reach the target we set though the
     * the cycle time is expired as long as the loss is not observed.
     */
    if (bbrCb->pacingGain > BBR_UNIT && (rs->losses == 0) &&
        (priorInflight < BBRTargetInflight(tcp, maxBw, bbrCb->pacingGain))) {
        return false;
    }

    /* the excess bytes are drained, then it's ok */
    if (bbrCb->pacingGain < BBR_UNIT && (priorInflight <= BBRTargetInflight(tcp, maxBw, BBR_UNIT))) {
        return true;
    }

    return shouldAdvanceCycle;
}

static void BBRUpdateCyclePhase(TcpSk_t* tcp)
{
    BBR_Cb_t* bbrCb = tcp->caCb;

    if (BBRShouldAdvanceCycle(tcp)) {
        bbrCb->cycleIdx = (bbrCb->cycleIdx + 1) & BBR_PROBE_BW_CYCLE_MASK;
        bbrCb->cycleStartTime = tcp->ConnDeliveredMstamp;

        /* cwnd gain not changed */
        bbrCb->pacingGain = ((bbrCb->slowBwValid == 1) ? BBR_UNIT : g_bbrPacingGain[bbrCb->cycleIdx]);
    }
}

static void BBREnterStartupMode(BBR_Cb_t* bbrCb)
{
    bbrCb->mode = BBR_STARTUP;
    bbrCb->pacingGain = BBR_STARTUP_GAIN;
    bbrCb->cwndGain = BBR_STARTUP_GAIN;
}

static void BBREnterDrainMode(BBR_Cb_t* bbrCb)
{
    bbrCb->mode = BBR_DRAIN;
    bbrCb->pacingGain = BBR_DRAIN_GAIN;
    bbrCb->cwndGain = BBR_STARTUP_GAIN;
}

#define SYSCTL_TCP_MIN_RTT_WLEN 5
void TcpRcvMinRttUpdate(TcpSk_t* tcp, int32_t caRttMs, uint32_t lastAckTime)
{
    uint32_t wlen = SYSCTL_TCP_MIN_RTT_WLEN * MSEC_PER_SEC;
    uint32_t now = 0;
    int32_t val = caRttMs;
    if (val < 0) {
        now = UTILS_TimeNow();
        if (tcp->tsVal != 0 && tcp->tsEcho != 0) {
            val = (int32_t)(TCP_TICK2_SEC(TcpGetRttTick(tcp) - tcp->tsVal) * MSEC_PER_SEC);
        }
    } else {
        now = (uint32_t)val + lastAckTime;
    }
    if (val >= 0) {
        (void)MinmaxUpdateMinValue(&tcp->rttMin, wlen, now, (val != 0) ? (uint32_t)val : 1U);
    }
}

static void BBRSaveCwnd(TcpSk_t* tcp)
{
    BBR_Cb_t* bbrCb = tcp->caCb;
    uint32_t cwndPkt;

    cwndPkt = BbrCwndPkt(tcp);
    if (bbrCb->prevCaState < TCP_CA_RECOVERY && bbrCb->mode != BBR_PROBE_RTT) {
        bbrCb->priorCwnd = cwndPkt;
        return;
    }

    bbrCb->priorCwnd = UTILS_MAX(bbrCb->priorCwnd, cwndPkt);
}

static void BBREnterProbeBwMode(TcpSk_t* tcp)
{
    BBR_Cb_t* bbrCb = tcp->caCb;

    bbrCb->mode = BBR_PROBE_BW;
    /* pick a random cycle index */
    bbrCb->cycleIdx = RAND_GEN() % BBR_PROBE_BW_CYCLE_MAX_IDX;
    bbrCb->cycleStartTime = tcp->ConnDeliveredMstamp;

    bbrCb->cwndGain = BBR_PROBW_CWND_GAIN;
    bbrCb->pacingGain = (bbrCb->slowBwValid == 1) ? BBR_UNIT : g_bbrPacingGain[bbrCb->cycleIdx];
}

static void BBRSlowBwEstimateDone(TcpSk_t* tcp)
{
    BBR_Cb_t* bbrCb = tcp->caCb;

    /* keep rtt rounds running till next estimate */
    if (bbrCb->mode == BBR_PROBE_BW) {
        if (bbrCb->newRttStart == 1) {
            ++bbrCb->slowIntervalRttCnt;
        }

        if (bbrCb->slowIntervalRttCnt >= BBR_SLOW_BW_EST_MAX_RTT_CNT) {
            BBRResetSlowBwEstimate(tcp);
            BBREnterProbeBwMode(tcp);
        }
    }
}

static void BBRSlowIntervalStart(TcpSk_t* tcp)
{
    BBR_Cb_t* bbrCb = tcp->caCb;
    bbrCb->slowIntervalStartTime = tcp->ConnDeliveredMstamp;
    bbrCb->slowIntervalStartCumAckCnt = tcp->connDeliveredCnt;
    bbrCb->slowLastCumLostCnt = tcp->connLostCnt;
    bbrCb->slowIntervalRttCnt = 0;
}

static bool BBRSlowBwIsStable(TcpSk_t* tcp, uint64_t old, uint64_t new)
{
    uint64_t diff = (new > old) ? (new - old) : (old - new);
    uint64_t diffRate;

    /* relative value is small enough */
    if (diff * BBR_SLOW_BW_DIFF_RATIO <= old) {
        return true;
    }

    diffRate = BBRBwToPacingRate(tcp, diff, BBR_UNIT); /* unit: bytes/sec */
    /* absolute value is small enough */
    if (diffRate <= BBR_SLOW_BW_DIFF) {
        return true;
    }

    return false;
}

static bool BBRCheckSlowUpdateBw(TcpSk_t* tcp, TcpRateSample_t* rs)
{
    BBR_Cb_t* bbrCb = tcp->caCb;

    if (rs->isAppLimited) {
        BBRResetSlowBwEstimate(tcp);
        return false;
    }

    if (bbrCb->newRttStart == 1) {
        bbrCb->slowIntervalRttCnt++;
    }

    if (bbrCb->slowIntervalRttCnt < BBR_SLOW_INTVL_MIN_RTT_CNT) {
        return false;
    }

    /* failed to get slow bw after a long time */
    if (bbrCb->slowIntervalRttCnt > BBR_SLOW_INTVL_MAX_RTT_CNT) {
        BBRResetSlowBwEstimate(tcp);
        return false;
    }

    /*
     * estimating slow bandwidth only if packet lost is obeserved continuously
     * for a long time, otherwise the estimating will be reset.
     */
    if (rs->losses == 0) {
        return false;
    }

    return true;
}

static inline bool BBRSlowCheckPacketCnt(uint32_t lostCnt, uint32_t ackCnt)
{
    /* no packet is acked during the period */
    if (ackCnt == 0) {
        return false;
    }

    /* packet loss rate is not big enough min lost rate is 20% */
    if (lostCnt * BBR_SLOW_BW_LOSS_FACTOR < ackCnt) {
        return false;
    }

    return true;
}

#define BBR_SLOW_MIN_INTERVAL 1U

static bool BBRSlowCheckIntervalLen(uint32_t intervalMs)
{
    /* too small */
    if (intervalMs < BBR_SLOW_MIN_INTERVAL) {
        return false;
    }

    return true;
}

static uint64_t BBRSlowUpdateBw(TcpSk_t* tcp)
{
    BBR_Cb_t* bbrCb = tcp->caCb;
    uint32_t incrLostCnt;
    uint32_t incrAckCnt;
    uint32_t intervalMs;

    incrLostCnt = tcp->connLostCnt - bbrCb->slowLastCumLostCnt;
    incrAckCnt = tcp->connDeliveredCnt - bbrCb->slowIntervalStartCumAckCnt;
    if (!BBRSlowCheckPacketCnt(incrLostCnt, incrAckCnt)) {
        return 0;
    }

    intervalMs = (tcp->ConnDeliveredMstamp - bbrCb->slowIntervalStartTime);
    if (!BBRSlowCheckIntervalLen(intervalMs)) {
        return 0;
    }

    return ((uint64_t)incrAckCnt * BW_UNIT) / intervalMs;
}

static void BBRUpdateSlowBw(BBR_Cb_t* bbrCb, uint64_t bw)
{
    bbrCb->slowBw = (bw + bbrCb->slowBw) >> 1;
    bbrCb->slowIntervalRttCnt = 0;

    bbrCb->slowBwValid = 1;
}

static void BBRSlowFilterBw(TcpSk_t* tcp, uint64_t bw)
{
    BBR_Cb_t* bbrCb = tcp->caCb;

    if (bbrCb->slowBw != 0) {
        /*
         * if the bw change not large enough by policer, it means
         * the bw is stable.
         */
        if (BBRSlowBwIsStable(tcp, bbrCb->slowBw, bw)) {
            BBRUpdateSlowBw(bbrCb, bw);
            bbrCb->pacingGain = BBR_UNIT;
            return;
        }
    }

    /* not stable yet and continue to update bw */
    bbrCb->slowBw = bw;
    BBRSlowIntervalStart(tcp);
}

static void BBRTrySlowUpdateBw(TcpSk_t* tcp, TcpRateSample_t* rs)
{
    BBR_Cb_t* bbrCb = tcp->caCb;
    uint64_t bw;

    /* slow bandwidth is obtained */
    if (bbrCb->slowBwValid == 1) {
        /* this round slow bandwidth estimate is done */
        BBRSlowBwEstimateDone(tcp);
        return;
    }

    if (bbrCb->slowBwEstimateStart == 0) {
        if (rs->losses == 0) {
            return;
        }

        bbrCb->slowBwEstimateStart = 1;
        BBRSlowIntervalStart(tcp);
    }

    /* estimating the slow bandwidth during slow interval */
    if (!BBRCheckSlowUpdateBw(tcp, rs)) {
        return;
    }

    /*
     * if failed to get slow bw for a long time, the estimate process will reset,
     * then we have to wait for next packet loss.
     */
    bw = BBRSlowUpdateBw(tcp);
    if (bw == 0) {
        return;
    }
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_BBR_SLOWBW_CNT);
    BBRSlowFilterBw(tcp, bw);
}

static void BBRUpdateIdle(BBR_Cb_t* bbrCb, uint32_t time)
{
    bbrCb->idleRestart = 1;
    bbrCb->aggrEpochStartTime = time;
    bbrCb->aggrEpochAcked = 0;
}

static void BBRCalcPacingRate(TcpSk_t* tcp, uint64_t bw, uint32_t gain)
{
    BBR_Cb_t* bbrCb = tcp->caCb;
    if (bw == 0) {
        return;
    }

    uint32_t targetRate = BBRBwToPacingRate(tcp, bw, gain);
    if (bbrCb->fullBwReached != 0 || targetRate > tcp->pacingRate) {
        tcp->pacingRate = targetRate;
    }
}

static void BBREnterProbeRttMode(BBR_Cb_t* bbrCb)
{
    bbrCb->mode = BBR_PROBE_RTT;
    bbrCb->probeRttExitTime = 0;
    bbrCb->pacingGain = BBR_UNIT;
    bbrCb->cwndGain = BBR_UNIT;
}

static void BBRExitProbeRttMode(TcpSk_t* tcp)
{
    BBR_Cb_t* bbrCb = tcp->caCb;

    if (bbrCb->fullBwReached == 1) {
        BBREnterProbeBwMode(tcp);
    } else {
        BBREnterStartupMode(bbrCb);
    }
}

static void BBRUpdateRttRound(TcpSk_t* tcp, TcpRateSample_t *rs)
{
    BBR_Cb_t* bbrCb = tcp->caCb;

    /* new rtt round start and be care about the number wrapping */
    if (!BBR_BEFORE_U32(rs->priorAckCnt, bbrCb->nextRttDelivered)) {
        bbrCb->newRttStart = 1;
        bbrCb->rttCnt++;

        /* next rtt record point */
        bbrCb->nextRttDelivered = tcp->connDeliveredCnt;
        bbrCb->inRecovery = 0;
    }
}

void BBRUpdateBw(TcpSk_t* tcp)
{
    uint64_t bw;
    BBR_Cb_t* bbrCb = tcp->caCb;
    TcpRateSample_t* rs = &tcp->rs;

    BBRTrySlowUpdateBw(tcp, rs);

    // 进入之前会判断incrAckCnt、intervalMs大于0，强转无问题
    bw = (uint32_t)rs->incrAckCnt * BW_UNIT;
    /* the interval ms is valid by caller */
    bw = bw / (uint32_t)rs->intervalMs;

    if (!rs->isAppLimited || bw >= MinmaxGetValue(&bbrCb->bw)) {
        (void)MinmaxUpdateMaxValue(&bbrCb->bw, BBR_BW_VALID_TIME_WIN, bbrCb->rttCnt, bw);
    }
}

static bool BBRUpdateMinRtt(BBR_Cb_t* bbrCb, TcpRateSample_t *rs, uint32_t nowMs)
{
    bool minRttExpired;

    minRttExpired = BBR_AFTER_U32(nowMs, bbrCb->minRttTime + BBR_MIN_RTT_VALID_TIME_WIN);
    if (rs->rttMs >= 0 && ((uint32_t)(rs->rttMs) < bbrCb->minRttMs || (minRttExpired))) {
        /* && !rs->is_ack_delayed Stack does not have ack delay */
        bbrCb->minRttMs = (uint32_t)rs->rttMs;
        bbrCb->minRttTime = nowMs;
    }

    return minRttExpired;
}

static uint8_t BBRUpdateExtraAckSlot(BBR_Cb_t* bbrCb)
{
    if (bbrCb->newRttStart == 0) {
        return bbrCb->extraAckedSlot;
    }

    if (++bbrCb->extraAckedRttCnt >= (uint32_t)BBR_EXTRA_ACKED_UPDATE_RTT_CNT) {
        bbrCb->extraAckedRttCnt = 0;
        bbrCb->extraAckedSlot = (bbrCb->extraAckedSlot == 1) ? 0 : 1;
        bbrCb->extraAcked[bbrCb->extraAckedSlot] = 0;
    }

    return bbrCb->extraAckedSlot;
}

/* Estimates the windowed max degree of ack aggregation.
 * This is used to provision extra in-flight data to keep sending during
 * inter-ACK silences.
 *
 * Degree of ack aggregation is estimated as extra data acked beyond expected.
 *
 * max_extra_acked = "maximum recent excess data ACKed beyond maxBw * interval"
 * "cwnd += max_extra_acked"
 *
 * Max extraAcked is clamped by cwnd and bw * bbr_extra_acked_max_ms (100 ms).
 * Max filter is an approximate sliding window of 5-10 (packet timed) round
 * trips.
 */
static void BBRUpdateAckAggregation(TcpSk_t* tcp, TcpRateSample_t *rs)
{
    uint32_t epochMs;
    uint32_t expectedAcked;
    uint32_t extraAcked;
    uint8_t slot;
    BBR_Cb_t* bbrCb = tcp->caCb;

    if (rs->ackedSacked <= 0) {
        return;
    }

    slot = BBRUpdateExtraAckSlot(bbrCb);

    /* Compute how many packets we expected to be delivered over epoch. */
    epochMs = TcpStampMsDelta(tcp->ConnDeliveredMstamp, bbrCb->aggrEpochStartTime);
    expectedAcked = (uint32_t)((BBRBw(bbrCb) * epochMs) / BW_UNIT);  // bw存储时为uint_32数 * BW_UNIT，此时 / BW_UNIT，不会超过uint32
    /* Reset the aggregation epoch if ACK rate is below expected rate or
     * significantly large no. of ack received since epoch (potentially
     * quite old epoch).
     */
    if (bbrCb->aggrEpochAcked <= expectedAcked ||
        (bbrCb->aggrEpochAcked + rs->ackedSacked >= BBR_EXTRA_ACKED_MAX_ACKED_CNT)) {
        bbrCb->aggrEpochAcked = rs->ackedSacked;
        bbrCb->aggrEpochStartTime = tcp->ConnDeliveredMstamp;
        return;
    }

    bbrCb->aggrEpochAcked += rs->ackedSacked;

    /* Compute excess data delivered, beyond what was expected. */
    extraAcked = UTILS_MIN(bbrCb->aggrEpochAcked - expectedAcked, tcp->cwnd);
    if (extraAcked > bbrCb->extraAcked[slot]) {
        bbrCb->extraAcked[slot] = extraAcked;
    }
}

/* Find the cwnd increment based on estimate of ack aggregation */
static uint32_t BBRCalcAggregationCwnd(TcpSk_t* tcp)
{
    uint64_t maxAggrCwnd;
    uint32_t aggrCwnd;
    BBR_Cb_t* bbrCb = tcp->caCb;

    if (bbrCb->fullBwReached == 0) {
        return 0;
    }

    maxAggrCwnd = BBRBw(bbrCb) * BBR_EXTRA_ACKED_MAX_TIME_WIN;
    aggrCwnd = BBRExtraAcked(bbrCb);

    return UTILS_MIN(aggrCwnd, (uint32_t)(maxAggrCwnd >> BW_SCALE));         // (uint32 * 100000) >> 24 不会超过uint32
}

#define BBR_ENTER_RECOVER 0
#define BBR_EXIT_RECOVER 1
#define BBR_STABLE_RECOVER 2

static int BBRUpdateRecoveryState(TcpSk_t* tcp)
{
    BBR_Cb_t* bbrCb = tcp->caCb;
    uint8_t currCaState = tcp->caState;
    uint8_t prevCaState = bbrCb->prevCaState;

    if (UTILS_LIKELY(currCaState == prevCaState)) {
        return BBR_STABLE_RECOVER;
    }

    bbrCb->prevCaState = currCaState;

    if (prevCaState != TCP_CA_RECOVERY && currCaState == TCP_CA_RECOVERY) {
        /* extent the current round by reset the next_rtt_cum_ack_cnt */
        bbrCb->nextRttDelivered = tcp->connDeliveredCnt;
        bbrCb->inRecovery = 1;
        return BBR_ENTER_RECOVER;
    }

    if (currCaState < TCP_CA_RECOVERY && prevCaState >= TCP_CA_RECOVERY) {
        bbrCb->inRecovery = 0;
        return BBR_EXIT_RECOVER;
    }

    return BBR_STABLE_RECOVER;
}

static void BBRCheckFullBwReached(TcpSk_t* tcp)
{
    BBR_Cb_t* bbrCb = tcp->caCb;
    uint64_t bwThresh;
    uint64_t bwMinmax;

    if (tcp->rs.isAppLimited) {
        return;
    }
    bwMinmax = MinmaxGetValue(&bbrCb->bw);
    bwThresh = (((uint64_t)bbrCb->fullBw * BBR_BW_FULL_INCR_FACT) >> BBR_SCALE);
    if (bwMinmax >= bwThresh) {
        bbrCb->fullBw = bwMinmax;
        bbrCb->fullBwCnt = 0;
        return;
    }

    if (++bbrCb->fullBwCnt >= (uint32_t)BBR_BW_FULL_CNT) {
        bbrCb->fullBwReached = 1;
    } else {
        bbrCb->fullBwReached = 0;
    }
}

static void BBRProbeRtt(TcpSk_t* tcp, uint32_t nowMs, bool minRttExpired)
{
    BBR_Cb_t* bbrCb = tcp->caCb;
    TcpRateSample_t* rs = &tcp->rs;

    if (minRttExpired && (bbrCb->idleRestart == 0) && bbrCb->mode != BBR_PROBE_RTT) {
        BBREnterProbeRttMode(bbrCb);
        BBRSaveCwnd(tcp);
    }

    if (bbrCb->mode == BBR_PROBE_RTT) {
        uint32_t limited = tcp->connDeliveredCnt + TcpPacketInPipe(tcp);
        if (limited > 0) {
            tcp->appLimited = limited;
        } else {
            tcp->appLimited = 1;
        }

        if ((bbrCb->probeRttExitTime == 0) && TcpPacketInPipe(tcp) <= BBR_MIN_CWND) {
            /* schedule a minimum timepoint for exiting the PROBE_RTT mode */
            bbrCb->probeRttExitTime = nowMs + BBR_PROBE_RTT_MIN_WIN;
            bbrCb->probeRttExit = 0;
            bbrCb->nextRttDelivered = tcp->connDeliveredCnt;
        } else if (bbrCb->probeRttExitTime != 0) {
            if (bbrCb->newRttStart == 1) {
                bbrCb->probeRttExit = 1;
            }

            if ((bbrCb->probeRttExit == 1) && BBR_AFTER_U32(nowMs, bbrCb->probeRttExitTime)) {
                /* wait a while until PROBE_RTT */
                bbrCb->minRttTime = nowMs;
                tcp->cwnd = UTILS_MAX(tcp->cwnd, bbrCb->priorCwnd * tcp->mss);
                BBRExitProbeRttMode(tcp);
            }
        }
    }
    /* Restart after idle ends only once we process a new S/ACK for data */
    if (rs->incrAckCnt > 0) {
        bbrCb->idleRestart = 0;
    }
}

bool BBRUpdateCwndInRecovery(TcpSk_t* tcp, uint32_t acked, uint32_t *newCwnd, int recoveryState)
{
    BBR_Cb_t* bbrCb = tcp->caCb;
    TcpRateSample_t* rs = &tcp->rs;
    uint32_t cwnd = BbrCwndPkt(tcp);
    uint32_t packetsInRecovery;

    cwnd = (cwnd > rs->losses) ? (cwnd - rs->losses) : 1;

    /* if the cwnd is limited by loss, no need to calc cwnd */
    if (bbrCb->inRecovery == 1) {
        packetsInRecovery = TcpPacketInPipe(tcp) + acked;
        if (recoveryState == BBR_ENTER_RECOVER) {
            *newCwnd = packetsInRecovery;
        } else {
            *newCwnd = UTILS_MAX(cwnd, packetsInRecovery);
        }
        return true;
    }

    if (recoveryState == BBR_EXIT_RECOVER) {
        *newCwnd = UTILS_MAX(cwnd, bbrCb->priorCwnd);
        return false;
    }

    /* if the cwnd is not limited, keep on get the cwnd */
    *newCwnd = cwnd;
    return false;
}

static void BBRCalcCwnd(TcpSk_t* tcp, uint32_t acked, uint64_t bw, int gain, int recoveryState)
{
    BBR_Cb_t* bbrCb = tcp->caCb;
    uint32_t cwnd = BbrCwndPkt(tcp);
    uint32_t targetCwnd;

    if (acked == 0) {
        goto done;
    }

    if (BBRUpdateCwndInRecovery(tcp, acked, &cwnd, recoveryState)) {
        goto done;
    }

    /*
     * extra cwnd value is suggested by bbrv2
     */
    targetCwnd = BBRCalcBdp(tcp, bw, gain) + BBRCalcAggregationCwnd(tcp);
    targetCwnd = BBRCalcExtraCwnd(tcp, targetCwnd);

    if (bbrCb->fullBwReached == 1) {
        cwnd = UTILS_MIN((uint64_t)(cwnd + acked), targetCwnd);
    } else if (cwnd < targetCwnd || tcp->connDeliveredCnt < tcp->initCwnd) {
        cwnd = cwnd + acked;
    }
    cwnd = UTILS_MAX(cwnd, (uint32_t)BBR_MIN_CWND);

done:
    cwnd = UTILS_MIN(cwnd, (uint32_t)BBR_MAX_CWND);
    if (bbrCb->mode == BBR_PROBE_RTT) {
        cwnd = UTILS_MIN(cwnd, (uint32_t)BBR_MIN_CWND);
    }

    cwnd = UTILS_MAX(cwnd, (uint32_t)BBR_MIN_CWND);
    tcp->cwnd = cwnd * tcp->mss;
}

void BBRStartUp(TcpSk_t* tcp)
{
    BBR_Cb_t* bbrCb = tcp->caCb;

    if ((bbrCb->newRttStart == 1) && (bbrCb->fullBwReached == 0)) {
        BBRCheckFullBwReached(tcp);
    }

    if (bbrCb->fullBwReached == 1) {
        BBREnterDrainMode(bbrCb);
        tcp->ssthresh = BBRTargetInflight(tcp, MinmaxGetValue(&bbrCb->bw), BBR_UNIT) * tcp->mss;
    }
}

void BBRDrain(TcpSk_t* tcp)
{
    BBR_Cb_t* bbrCb = tcp->caCb;

    if ((bbrCb->newRttStart == 1) && (bbrCb->fullBwReached == 0)) {
        BBRCheckFullBwReached(tcp);
    }

    if (TcpPacketInPipe(tcp) <= BBRTargetInflight(tcp, MinmaxGetValue(&bbrCb->bw), BBR_UNIT)) {
        BBREnterProbeBwMode(tcp);
    }
}

void BBRProbeBw(TcpSk_t* tcp)
{
    BBR_Cb_t* bbrCb = tcp->caCb;

    BBRUpdateCyclePhase(tcp);

    if ((bbrCb->newRttStart == 1) && (bbrCb->fullBwReached == 0)) {
        BBRCheckFullBwReached(tcp);
    }
}

static void TcpBBRInit(void* tcpSk)
{
    TcpSk_t* tcp = (TcpSk_t*)tcpSk;
    BBR_Cb_t* bbrCb = SHM_MALLOC(sizeof(BBR_Cb_t), MOD_TCP, DP_MEM_FREE);
    if (bbrCb == NULL) {
        DP_LOG_ERR("Malloc memory failed for tcp bbrCb.");
        return;     // 内存申请失败情况，在使用前都会判空 TCP_SACK_AVAILABLE
    }
    (void)memset_s(bbrCb, sizeof(BBR_Cb_t), 0, sizeof(BBR_Cb_t));
    tcp->caCb = bbrCb;

    tcp->ssthresh = BBR_INFINITE_SSTHRESH;
    tcp->caState = TCP_CA_OPEN;
    tcp->tcpMstamp = UTILS_TimeNow();
    tcp->cwnd = TcpGetInitCwnd(tcp);
    (void)MinmaxResetValue(&tcp->rttMin, UTILS_TimeNow(), ~0U);
    tcp->appLimited = ~0U;

    bbrCb->prevCaState = TCP_CA_OPEN;
    bbrCb->inRecovery = 0;

    bbrCb->priorCwnd = 0;
    bbrCb->rttCnt = 0;
    bbrCb->nextRttDelivered = 0;

    bbrCb->probeRttExitTime = 0;
    bbrCb->probeRttExit = 0;
    bbrCb->newRttStart = 0;
    bbrCb->idleRestart = 0;

    bbrCb->minRttMs = TcpGetMinRtt(tcp);
    bbrCb->minRttTime = UTILS_TimeNow();

    (void)MinmaxResetValue(&bbrCb->bw, 0, 0);
    BBRInitPacingRate(tcp);

    bbrCb->fullBwReached = 0;
    bbrCb->fullBw = 0;
    bbrCb->fullBwCnt = 0;

    bbrCb->cycleStartTime = 0;
    bbrCb->cycleIdx = 0;

    bbrCb->aggrEpochStartTime = tcp->tcpMstamp;
    bbrCb->aggrEpochAcked = 0;
    bbrCb->extraAckedRttCnt = 0;
    bbrCb->extraAckedSlot = 0;
    bbrCb->extraAcked[0] = 0;
    bbrCb->extraAcked[1] = 0;
    bbrCb->slowLastCumLostCnt = 0;

    BBRResetSlowBwEstimate(tcp);
    BBREnterStartupMode(bbrCb);
}

static void TcpBBRDeinit(void* tcpSk)
{
    TcpSk_t* tcp = (TcpSk_t*)tcpSk;
    if (tcp->caCb != NULL) {
        SHM_FREE(tcp->caCb, DP_MEM_FREE);
        tcp->caCb = NULL;
    }
    tcp->pacingRate = PACING_RATE_NOLIMIT;
}

static void TcpBBRAcked(void* tcpSk, uint32_t acked, uint32_t rtt)
{
    (void)rtt;

    TcpSk_t* tcp = (TcpSk_t*)tcpSk;
    size_t remainLen = acked;
    Pbuf_t* cur = tcp->rexmitQue.head;
    uint32_t lastAckTime = 0;
    int32_t caRttMs = -1;
    uint32_t lost = tcp->connLostCnt;

    tcp->rs.rttMs = -1L;
    tcp->rs.priorInFlight = TcpPacketInPipe(tcp);

    if (TcpSeqGeq(tcp->sndUna, tcp->seqRecover)) {
        tcp->caState = TCP_CA_OPEN;
    }

    while (cur != NULL && remainLen != 0) {
        uint32_t pbufState = PBUF_GET_SCORE_BOARD(cur)->state;
        if ((pbufState & TF_SEG_SACKED) == 0 && (tcp->inflight >= PBUF_GET_PKT_LEN(cur))) {
            tcp->inflight -= PBUF_GET_PKT_LEN(cur);
            tcp->rs.ackedSacked++;
            tcp->connDeliveredCnt++;
        }
        if (remainLen < cur->totLen) {      // 如果ack到发送报文的中间，需要标记为sacked，防止再次ack此报文时多次采样
            PBUF_GET_SCORE_BOARD(cur)->state |= TF_SEG_SACKED;
        }
        TcpBwOnAcked(tcp, cur);
        if ((pbufState & TF_SEG_SACKED) == 0 && (pbufState & TF_SEG_RETRANSMITTED) == 0) {
            lastAckTime = PBUF_GET_SCORE_BOARD(cur)->rtxTimeState.tx.packetTxStamp;
        }
        remainLen = (remainLen > cur->totLen) ? remainLen - cur->totLen : 0;
        cur = cur->chainNext;
    }
    if (UTILS_LIKELY(lastAckTime > 0)) {
        caRttMs = (int32_t)(UTILS_TimeNow() - lastAckTime);
        TcpRcvMinRttUpdate(tcp, caRttMs, lastAckTime);
        tcp->rs.rttMs = caRttMs;
    }
    if (tcp->rs.ackedSacked > 0) {
        // 当前不支持RACK，无法预测丢包情况(lost == tcp->connLostCnt)
        TcpBwSample(tcp, tcp->connLostCnt - lost, &tcp->rs);
        TcpCaCongCtrl(tcp);
    }
}

static void TcpBBRDupAck(void* tcpSk)
{
    TcpSk_t* tcp = (TcpSk_t*)tcpSk;
    if (tcp->dupAckCnt < tcp->reorderCnt) {
        return;
    }

    if (TCP_IS_IN_OPEN(tcp)) {
        tcp->seqRecover = tcp->sndMax;
        TcpCaSetState(tcp, TCP_CA_RECOVERY);
    }
}

static void TcpBBRSetState(void* tcpSk, uint8_t newState)
{
    TcpSk_t* tcp = (TcpSk_t*)tcpSk;
    BBR_Cb_t* bbrCb = tcp->caCb;
    TcpRateSample_t rs = {0};

    switch (newState) {
        case TCP_CA_LOSS:
            if (tcp->caState < TCP_CA_RECOVERY) {
                BBRSaveCwnd(tcp);
            }
            rs.losses = 1;
            bbrCb->prevCaState = TCP_CA_LOSS;
            bbrCb->fullBw = 0;
            bbrCb->newRttStart = 1;
            BBRTrySlowUpdateBw(tcp, &rs);
            break;
        case TCP_CA_RECOVERY:
            if (tcp->caState < TCP_CA_RECOVERY) {
                BBRSaveCwnd(tcp);
            }
            break;
        default:
            break;
    }
}

static void TcpBBRCwndEvent(void* tcpSk, DP_TcpCaEvent_t event)
{
    TcpSk_t* tcp = (TcpSk_t*)tcpSk;
    BBR_Cb_t* bbrCb = tcp->caCb;

    if (event != DP_TCP_CA_EVENT_TX_START || tcp->appLimited == 0) {
        return;
    }
    BBRUpdateIdle(bbrCb, tcp->tcpMstamp);

    /* Avoid pointless buffer overflows: pace at est. bw if we don't
     * need more speed (we're restarting from idle and app-limited).
     */
    if (bbrCb->mode == BBR_PROBE_BW) {
        BBRCalcPacingRate(tcp, BBRBw(bbrCb), BBR_UNIT);
        return;
    }
    if (bbrCb->mode == BBR_PROBE_RTT) {
        uint32_t nowMs = UTILS_TimeNow();
        if ((bbrCb->probeRttExitTime != 0) && BBR_AFTER_U32(nowMs, bbrCb->probeRttExitTime)) {
            /* wait a while until PROBE_RTT */
            bbrCb->minRttTime = nowMs;
            tcp->cwnd = UTILS_MAX(tcp->cwnd, bbrCb->priorCwnd * tcp->mss);
            tcp->cwnd = UTILS_MAX((uint32_t)BBR_MIN_CWND * tcp->mss, tcp->cwnd);
            BBRExitProbeRttMode(tcp);
        }
    }
}

static void TcpBBRCongCtrl(void* tcpSk)
{
    TcpSk_t* tcp = (TcpSk_t*)tcpSk;
    uint64_t bw;
    bool minRttExpired = false;
    int recoveryState;
    uint32_t nowMs = UTILS_TimeNow();

    BBR_Cb_t* bbrCb = tcp->caCb;
    TcpRateSample_t* rs = &tcp->rs;

    /* marked only the first time into a new rtt round */
    bbrCb->newRttStart = 0;

    if (rs->incrAckCnt > 0 && rs->intervalMs > 0) {
        BBRUpdateRttRound(tcp, rs);
        BBRUpdateBw(tcp);
        minRttExpired = BBRUpdateMinRtt(bbrCb, rs, nowMs);
        BBRUpdateAckAggregation(tcp, rs);
    }

    recoveryState = BBRUpdateRecoveryState(tcp);
    switch (bbrCb->mode) {
        case BBR_STARTUP:
            BBRStartUp(tcp);
            break;
        case BBR_DRAIN:
            BBRDrain(tcp);
            break;
        case BBR_PROBE_BW:
            BBRProbeBw(tcp);
            break;
        case BBR_PROBE_RTT:
            /* probe rtt required to be processed in all states */
            break;
        default:
            break;
    }

    BBRProbeRtt(tcp, nowMs, minRttExpired);

    bw = BBRBw(bbrCb);
    BBRCalcPacingRate(tcp, bw, bbrCb->pacingGain);
    BBRCalcCwnd(tcp, rs->ackedSacked, bw, bbrCb->cwndGain, recoveryState);
}

static DP_TcpCaMeth_t g_bbr = {
    .algId     = TCP_CAMETH_BBR,
    .algName   = "bbr",
    .caInit    = TcpBBRInit,
    .caDeinit  = TcpBBRDeinit,
    .caRestart = NULL,
    .caAcked   = TcpBBRAcked,
    .caDupAck  = TcpBBRDupAck,
    .caTimeout = NULL,
    .caSetState = TcpBBRSetState,
    .caCwndEvent = TcpBBRCwndEvent,
    .caCongCtrl = TcpBBRCongCtrl,
};

void TcpBBRRegist(void)
{
    (void)TcpCaRegist(&g_bbr);
}

void TcpShowBBRInfo(TcpSk_t* tcp)
{
    BBR_Cb_t* bbrCb = tcp->caCb;
    if (bbrCb == NULL) {
        return;
    }
    uint32_t offset = 0;
    char output[LEN_INFO] = {0};

    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO, "\r\n-------- Tcp BBR Info --------\n");
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "maxBw = %u\n", MinmaxGetValue(&bbrCb->bw));
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "rttCnt = %u\n", bbrCb->rttCnt);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "mode = %u\n", bbrCb->mode);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "inRecovery = %u\n", bbrCb->inRecovery);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "idleRestart = %u\n", bbrCb->idleRestart);

    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "slowIntervalRttCnt = %u\n", bbrCb->slowIntervalRttCnt);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "slowBwValid = %u\n", bbrCb->slowBwValid);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "slowBw = %u\n", bbrCb->slowBw);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "slowIntervalStartCumAckCnt = %u\n", bbrCb->slowIntervalStartCumAckCnt);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "slowLastCumLostCnt = %u\n", bbrCb->slowLastCumLostCnt);

    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "fullBwReached = %u\n", bbrCb->fullBwReached);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "cycleIdx = %u\n", bbrCb->cycleIdx);

    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset, "priorCwnd = %u\n", bbrCb->priorCwnd);

    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "aggrEpochAcked = %u\n", bbrCb->aggrEpochAcked);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "extraAckedRttCnt = %u\n", bbrCb->extraAckedRttCnt);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "extraAckedSlot = %u\n", bbrCb->extraAckedSlot);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "extraAcked[0] = %u\n", bbrCb->extraAcked[0]);
    offset += (uint32_t)snprintf_truncated_s(output + offset, LEN_INFO - offset,
        "extraAcked[1] = %u\n", bbrCb->extraAcked[1]);

    DEBUG_SHOW(0, output, offset);
}