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
#include "tcp_frto.h"

#include "tcp_cc.h"
#include "tcp_tsq.h"

#include "utils_cfg.h"

bool TcpFrtoIsAvailable(TcpSk_t* tcp)
{
    if (CFG_GET_TCP_VAL(DP_CFG_TCP_FRTO) == DP_DISABLE) {
        return false;
    }

    ASSERT(tcp->caMeth != NULL);
    if (tcp->caMeth->algId != TCP_CAMETH_NEWRENO) {
        return false;
    }

    // 非数据报文超时，不启动 FRTO
    if (PBUF_CHAIN_IS_EMPTY(&tcp->rexmitQue)) {
        return false;
    }

    // 不能从 Recovery 和 Loss状态启用FRTO ，此时网络上有重传包，会导致 FRTO 误判，认为超时是 spurious
    if (tcp->caState < TCP_CA_RECOVERY) {
        return true;
    }

    // RFC5681 sec 3.2a: If the retransmission timeout expires again, go to step 1 of the algorithm
    // 如果是 Loss 状态，不能是首次进入，第二次RTO就可以。
    if (tcp->frto > 0 || tcp->backoff > 0) {
        return true;
    }

    return false;
}

void TcpFrtoEnterLoss(TcpSk_t* tcp)
{
    TcpCaTimeout(tcp); // 计算 ssthresh
    tcp->frto       = 1;
    tcp->caState    = TCP_CA_LOSS;

    // RFC5681 sec 3.1 step1: When the retransmission timer expires, retransmit the first unacknowledged segment
    tcp->cwnd = PBUF_GET_PKT_LEN(PBUF_CHAIN_FIRST(&tcp->rexmitQue));
}

void TcpFrtoRcvDupAck(TcpSk_t* tcp, TcpPktInfo_t* pi)
{
    if (tcp->seqRecover == tcp->sndMax) {
        // RFC5681 sec 3.1 step2: If duplicate ACKs arrive, Stay in step 2
        if (TCP_SACK_AVAILABLE(tcp)) {
            return;
        }

        // RFC5681 sec 2.1 step2.a: revert to the conventional RTO
        TcpFrtoRecovery(tcp, false);
    } else {
        // RFC 5681 sec 3.1 step 3.a
        if (TCP_SACK_AVAILABLE(tcp)) {
            if ((pi->pktType & TCP_PI_SACKED_DATA) != 0 && TcpSeqLeq(tcp->sackInfo->rcvSackEnd, tcp->seqRecover)) {
                TcpFrtoRecovery(tcp, true);
            } else {
                TcpFrtoRecovery(tcp, false);
            }
        } else {
            // RFC 5681 sec 2.1 step 3.a
            TcpFrtoRecovery(tcp, false);
        }
    }
}

void TcpFrtoRcvAck(TcpSk_t* tcp, TcpPktInfo_t* pi, uint32_t acked)
{
    if (acked == 0) {
        return;
    }

    if (tcp->seqRecover == tcp->sndMax) {
        // RFC 5681 sec 2.1 step 2.a, sec 3.1 step 3.a
        if (TcpSeqLt(tcp->sndUna, tcp->sndNxt) || TcpSeqGeq(tcp->sndUna, tcp->seqRecover)) {
            TcpFrtoRecovery(tcp, false);
            return;
        }

        uint32_t inflight = tcp->sndMax - tcp->sndUna;
        // 如果可以发送新数据，发送新数据
        if ((tcp->sndQue.pktCnt > 0 || TcpSk2Sk(tcp)->sndBuf.pktCnt > 0) && tcp->sndWnd - inflight > 0) {
            tcp->force  = 1;
            tcp->sndNxt = tcp->sndMax;
            tcp->cwnd   = inflight + tcp->mss * 2; // 2: 最多发送 2 个报文
        } else {
            TcpFrtoRecovery(tcp, false);
        }
    } else {
        // 确认或 sacked 了之前发送的数据，假超时
        if (TcpSeqLeq(tcp->sndUna, tcp->seqRecover) ||
            ((pi->pktType & TCP_PI_SACKED_DATA) != 0 && TcpSeqLeq(tcp->sackInfo->rcvSackEnd, tcp->seqRecover))) {
            TcpFrtoRecovery(tcp, true);
        } else {
            TcpFrtoRecovery(tcp, false);
        }
    }
}

void TcpFrtoRecovery(TcpSk_t* tcp, bool spurios)
{
    tcp->frto = 0;
    if (spurios) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_FRTO_SPURIOS_CNT);
        tcp->cwnd    = tcp->ssthresh;
        tcp->caState = TCP_CA_OPEN;
    } else {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_FRTO_REAL_CNT);
        // 继续重传
        tcp->sndNxt = tcp->sndUna;
        TcpCaTimeout(tcp);
    }
    TcpTsqAddQue(tcp, TCP_TSQ_SEND_DATA);
}
