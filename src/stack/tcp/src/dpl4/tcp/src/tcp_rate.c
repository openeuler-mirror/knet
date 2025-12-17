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
#include "utils_base.h"
#include "tcp_timer.h"
#include "tcp_types.h"

#include "tcp_rate.h"

/* *Bandwidth will be taking by a delivery rate sample from each ACK.
 *
 * Rate sample records the time interval between data transmissions and it's ack packet.
 * but due the common ACK losts,the method using ack rate will cause packets can temporarily
 * appear to be delivered much quicker than the bottleneck rate.
 *  Since it is impossible to do that in a sustained fashion, if the condition that ack rate is faster than
 * transmit rate detected, the following algorithms will be used

 *    "send_rate = #delivered_pkts/(last_snd_time - first_snd_time)"
 *    "ack_rate  = #delivered_pkts/(last_ack_time - first_ack_time)"
 *    "band_width = min(send_rate, ack_rate)"
 * Please note that the estimator mainly estimates the goodput, not always
 * network bottleneck link rate when sending or receiving is limited
 * by other factors, such as application or receiver window limits.
 * Estimator avoid using the inter-packet spacing method because it
 * requires a lot of samples and complicate filtering.
 *
 * TCP streams are typically limited by the application in the request/response workload.
 * The estimator marks the bandwidth sample as an application limit
 * if some moment there was no data in send queue during the sampled window.
 */
/* update delivery information to generate a rate sample later */
void TcpBwOnSent(TcpSk_t* tcp, TcpScoreBoard_t *scb, uint32_t dataLen)
{
    struct tcpSendTimeState* tx = &scb->rtxTimeState.tx;
    uint32_t packetsOut = tcp->sndNxt - tcp->sndUna;
    tx->packetTxStamp = UTILS_TimeNow();
    if (packetsOut == 0) {         // 没有未ack的数据
        tcp->ConnDeliveredMstamp = tx->packetTxStamp;
        tcp->ConnFirstTxMstamp  = tx->packetTxStamp;
    }
    tx->shotConnDeliveredMstamp = tcp->ConnDeliveredMstamp;
    tx->shotConnFirstTxMstamp = tcp->ConnFirstTxMstamp;
    tx->shotConnDeliveredCnt = tcp->connDeliveredCnt;
    tx->isAppLimited = tcp->appLimited > 0 ? true : false;

    scb->startSeq = tcp->sndNxt;
    scb->endSeq   = scb->startSeq + dataLen;
    scb->state    = TF_SEG_NONE;
}

/* when packet is sacked or acked, we fill in the rate sample with the
 * delivery information when the pakcet was last transmitted.
 *
 * the function will be called more than once if 1 ACK (s)acks more than 1 packets.
 */
void TcpBwOnAcked(TcpSk_t* tcp, Pbuf_t* pbuf)
{
    TcpRateSample_t* rs = &tcp->rs;
    TcpScoreBoard_t* scb = PBUF_GET_SCORE_BOARD(pbuf);
    struct tcpSendTimeState* tx = &scb->rtxTimeState.tx;

    // 已采样过
    if (tx->shotConnDeliveredMstamp == 0) {
        return;
    }
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_BBR_SAMPLE_CNT);
    if ((rs->priorAckCnt == 0) || TcpSeqGt(tx->shotConnDeliveredCnt, rs->priorAckCnt)) {
        rs->priorAckCnt  = tx->shotConnDeliveredCnt;
        rs->priorAckTime = tx->shotConnDeliveredMstamp;
        rs->isAppLimited = tx->isAppLimited;

        /* get interval of the "send phase" of this window: */
        rs->intervalMs = (int32_t)(tx->packetTxStamp - tx->shotConnFirstTxMstamp);

        /* get send time of most recently ACKed packet: */
        tcp->ConnFirstTxMstamp = tx->packetTxStamp;
    }

    /* reset the sent packet once it's sacked to avoid being
     * used again when it's cumulatively acked.
     */
    if ((scb->state & TF_SEG_SACKED) != 0) {
        tx->shotConnDeliveredMstamp = 0;
    }
}

/* get a rate sample. */
void TcpBwSample(TcpSk_t* tcp, uint32_t lost, TcpRateSample_t *rs)
{
    int32_t sndMs, ackMs;
    uint32_t now = UTILS_TimeNow();

    /* check bubble for app limit */
    if ((tcp->appLimited != 0) && TcpSeqGt(tcp->connDeliveredCnt, tcp->appLimited)) {
        tcp->appLimited = 0;
    }

    if (rs->ackedSacked != 0) {
        tcp->ConnDeliveredMstamp = now;
    }

    /* new marked lost */
    rs->losses = lost;
    /* return wrong sample if timing information is unavailable. */
    if (rs->priorAckTime == 0) {
        rs->incrAckCnt = -1;
        rs->intervalMs = -1;
        return;
    }
    rs->incrAckCnt = (int32_t)(tcp->connDeliveredCnt - rs->priorAckCnt);

    /* the ACK phase is longer, but with ACK compression the send phase can be longer.
     * we use the longer phase for safe.
     */
    /* for send */
    sndMs = rs->intervalMs;
    ackMs = (int32_t)(now - rs->priorAckTime);
    rs->intervalMs = UTILS_MAX(sndMs, ackMs);

    /* interval_ms >= min-rtt is expected.
     * But rate may be over-estimated when a fake
     * retransmistted packet was first (s)acked because interval_ms
     * is under-estimated (up to an RTT). However repeated
     * measuring the delivery rate during loss recovery is important
     * for unstable connections.
     */
    if (UTILS_UNLIKELY(rs->intervalMs < (int32_t)TcpGetMinRtt(tcp))) {
        rs->intervalMs = -1;
        return;
    }
}
