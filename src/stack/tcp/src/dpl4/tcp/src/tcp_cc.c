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

#include "tcp_cc.h"

#include "utils_cfg.h"

static inline uint32_t TcpNewRenoSsthresh(TcpSk_t* tcp)
{
    uint32_t ssthresh;
    uint32_t initCwnd;

    // sstrensh = MIN(inflight / 2, INIT_CWND * MSS)
    initCwnd = TcpGetInitCwnd(tcp);
    // 2: 计算当前已发送但未确认的数据量（inflight）的一半，作为慢启动阈值（ssthresh）
    ssthresh = (tcp->sndMax - tcp->sndUna) / 2; // inflight:在途数据

    return ssthresh > initCwnd ? ssthresh : initCwnd;
}

static int TcpNewRenoInit(TcpSk_t* tcp)
{
    tcp->cwnd     = TcpGetInitCwnd(tcp);
    tcp->ssthresh = INT_MAX;
    tcp->caState  = TCP_CA_OPEN;

    return 0;
}

static void TcpNewRenoAcked(TcpSk_t* tcp, uint32_t acked, uint32_t rtt)
{
    (void)rtt;

    if (tcp->caState == TCP_CA_RECOVERY) { // 在recovery阶段
        // recovery -> open
        if (TcpSeqGeq(tcp->sndUna, tcp->seqRecover)) {
            tcp->cwnd    = tcp->ssthresh;
            tcp->caState = TCP_CA_OPEN;
        } else {
            // 按照mss增长，期望能够发送新的报文
            tcp->cwnd += tcp->mss;
        }
        return;
    }

    // 慢启动或者拥塞避免
    if (tcp->cwnd > tcp->ssthresh) { // 拥塞避免
        uint32_t incWnd = tcp->mss * tcp->mss / tcp->cwnd;
        tcp->cwnd += incWnd == 0 ? 1 : incWnd;
    } else { // 慢启动，一次窗口增长的大小不超过2个mss
        tcp->cwnd += UTILS_MIN(acked, (uint32_t)tcp->mss << 1);
    }
}

static void TcpNewRenoDupAck(TcpSk_t* tcp)
{
    if (tcp->dupAckCnt < tcp->reorderCnt) { // 乱序状态不增加cwnd
        return;
    }

    if (tcp->caState == TCP_CA_OPEN) {
        tcp->seqRecover = tcp->sndMax;
        tcp->caState  = TCP_CA_RECOVERY;
        tcp->ssthresh = TcpNewRenoSsthresh(tcp);
        tcp->cwnd     = tcp->ssthresh + tcp->mss * tcp->dupAckCnt; // 3: NewReno算法
    }
}

static void TcpNewRenoLoss(TcpSk_t* tcp)
{
    tcp->caState    = TCP_CA_OPEN; // new reno在这里直接使用open状态，恢复到慢启动状态
    tcp->seqRecover = tcp->sndMax;
    tcp->ssthresh   = TcpNewRenoSsthresh(tcp);
    tcp->cwnd       = TcpGetInitCwnd(tcp);
}

static void TcpNewRenoRestart(TcpSk_t* tcp)
{
    tcp->cwnd    = TcpGetInitCwnd(tcp);
    tcp->caState = TCP_CA_OPEN;
}

static const TcpCaMeth_t* g_caList = NULL;

const TcpCaMeth_t* TcpCaGet(int algId)
{
    static TcpCaMeth_t newreno = {
        .algId     = 0,
        .caInit    = TcpNewRenoInit,
        .caDeinit  = NULL,
        .caAcked   = TcpNewRenoAcked,
        .caDupAck  = TcpNewRenoDupAck,
        .caTimeout = TcpNewRenoLoss,
        .caRestart = TcpNewRenoRestart,
    };
    const TcpCaMeth_t* caMeth;
    int                defaultAlgId = CFG_GET_TCP_VAL(CFG_TCP_CA_ALG);
    if (algId < 0 || algId == defaultAlgId) {
        return &newreno;
    }

    caMeth = g_caList;

    while (caMeth != NULL) {
        if (caMeth->algId == algId) {
            return caMeth;
        }
        caMeth = caMeth->next;
    }

    return &newreno;
}

void TcpCaRegist(TcpCaMeth_t* meth)
{
    if (g_caList != NULL) {
        meth->next = g_caList;
    }

    g_caList = meth;
}