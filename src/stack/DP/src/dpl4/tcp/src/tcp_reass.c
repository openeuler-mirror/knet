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

#include "tcp_reass.h"

#include "pbuf.h"
#include "dp_tcp.h"
#include "tcp_tsq.h"

#include "tcp_sack.h"

#define TCP_REASS_MAX_PKT_CNT (0xFFFFU)

static int TcpReassInsertSlow(TcpSk_t* tcp, Pbuf_t* pbuf)
{
    TcpReassInfo_t* reassInfo = TcpReassGetInfo(pbuf);
    uint32_t        seqEnd    = reassInfo->seq + PBUF_GET_PKT_LEN(pbuf);
    uint32_t        curSeqEnd;
    TcpReassInfo_t* curReassInfo;
    Pbuf_t*         cur = PBUF_CHAIN_FIRST(&tcp->reassQue);

    while (cur != NULL) {
        curReassInfo = TcpReassGetInfo(cur);
        curSeqEnd    = curReassInfo->seq + PBUF_GET_PKT_LEN(cur);
        if (TcpSeqGeq(reassInfo->seq, curSeqEnd)) {
            cur = PBUF_CHAIN_NEXT(cur);
            continue;
        }

        if (TcpSeqLeq(seqEnd, curReassInfo->seq)) { // 完全不重复的数据块
            DP_INC_PKT_STAT(tcp->wid, DP_PKT_TCP_REASS_PKT);
            PBUF_ChainInsertBefore(&tcp->reassQue, cur, pbuf);
            return 0;
        }

        if (TcpSeqLt(reassInfo->seq, curReassInfo->seq)) {
            if (TcpSeqGt(seqEnd, curSeqEnd)) { // pbuf覆盖了cur的数据范围，删除cur数据
                Pbuf_t* next = PBUF_CHAIN_NEXT(cur);
                DP_DEC_PKT_STAT(tcp->wid, DP_PKT_TCP_REASS_PKT);
                PBUF_ChainRemove(&tcp->reassQue, cur);
                PBUF_Free(cur);
                cur = next;
            } else { // 部分重复数据，删除cur前部数据
                PBUF_CUT_DATA(cur, seqEnd - curReassInfo->seq);
                tcp->reassQue.bufLen -= seqEnd - curReassInfo->seq;
                DP_INC_PKT_STAT(tcp->wid, DP_PKT_TCP_REASS_PKT);
                PBUF_ChainInsertBefore(&tcp->reassQue, cur, pbuf);
                curReassInfo->seq += seqEnd - curReassInfo->seq;
                return 0;
            }
        } else {
            if (TcpSeqLeq(seqEnd, curSeqEnd)) { // cur覆盖了pbuf的数据范围，删除pbuf
                DP_ADD_TCP_STAT(pbuf->wid, DP_TCP_RCV_DUP_BYTE, PBUF_GET_PKT_LEN(pbuf));
                DP_INC_TCP_STAT(pbuf->wid, DP_TCP_RCV_DUP_PACKET);
                PBUF_Free(pbuf);
                return -1;
            } else { // 部分重复数据，删除pbuf前部数据
                PBUF_CUT_DATA(pbuf, curSeqEnd - reassInfo->seq);
                reassInfo->seq = curSeqEnd;
            }
        }
    }
    DP_INC_PKT_STAT(tcp->wid, DP_PKT_TCP_REASS_PKT);
    PBUF_ChainPush(&tcp->reassQue, pbuf);

    return 0;
}

static inline bool TcpReassCanInsert(TcpSk_t* tcp)
{
    uint32_t maxPktCnt = (TcpSk2Sk(tcp)->rcvHiwat / tcp->mss) * 2 + 1;
    maxPktCnt = UTILS_MIN(maxPktCnt, TCP_REASS_MAX_PKT_CNT);
    if (tcp->reassQue.pktCnt < maxPktCnt) {
        return true;
    }

    return false;
}

int TcpReassInsert(TcpSk_t* tcp, Pbuf_t* pbuf, TcpPktInfo_t* pi)
{
    TcpReassInfo_t* reassInfo = TcpReassGetInfo(pbuf);
    TcpReassInfo_t* lastSegInfo;
    Pbuf_t*         lastSeg;

    if (!TcpReassCanInsert(tcp)) {
        PBUF_Free(pbuf);
        PBUF_ChainClean(&tcp->reassQue);
        if (TCP_SACK_AVAILABLE(tcp)) {
            TcpClearSackBlock(tcp->sackInfo);
        }
        return -1;
    }

    reassInfo->seq    = pi->seq;
    reassInfo->endSeq = pi->endSeq;
    reassInfo->ack    = pi->ack;
    reassInfo->sndWnd = pi->sndWnd;

    if ((pi->thFlags & DP_TH_FIN) != 0) { // 外部通过tcp->rcvMax 比较判断是否已经收到FIN
        reassInfo->endSeq -= 1;
    }

    lastSeg = PBUF_CHAIN_TAIL(&tcp->reassQue);
    if (lastSeg == NULL) {
        DP_INC_PKT_STAT(tcp->wid, DP_PKT_TCP_REASS_PKT);
        PBUF_ChainPush(&tcp->reassQue, pbuf);
        return 0;
    }

    lastSegInfo = TcpReassGetInfo(lastSeg);
    if (TcpSeqGeq(pi->seq, lastSegInfo->endSeq)) {
        DP_INC_PKT_STAT(tcp->wid, DP_PKT_TCP_REASS_PKT);
        PBUF_ChainPush(&tcp->reassQue, pbuf);
        return 0;
    }

    return TcpReassInsertSlow(tcp, pbuf);
}

uint32_t TcpReass(TcpSk_t* tcp)
{
    TcpReassInfo_t* curReassInfo;
    Pbuf_t*         cur;
    uint32_t        pushData = 0;

    do {
        uint32_t dupLen;
        cur = PBUF_CHAIN_FIRST(&tcp->reassQue);
        if (cur == NULL) {
            break;
        }

        curReassInfo = TcpReassGetInfo(cur);
        if (TcpSeqLt(tcp->rcvNxt, curReassInfo->seq)) {
            break;
        }

        cur = PBUF_CHAIN_POP(&tcp->reassQue);

        dupLen = tcp->rcvNxt - curReassInfo->seq;
        if (dupLen >= PBUF_GET_PKT_LEN(cur)) {
            PBUF_Free(cur);
        } else {
            PBUF_CUT_DATA(cur, dupLen);

            PBUF_ChainPush(&tcp->rcvQue, cur);

            tcp->rcvNxt += PBUF_GET_PKT_LEN(cur);
            tcp->accDataCnt++;
            pushData += PBUF_GET_PKT_LEN(cur);
        }
    } while (true);

    if (TcpState(tcp) == TCP_CLOSE_WAIT || TcpState(tcp) == TCP_CLOSING || TcpState(tcp) == TCP_TIME_WAIT) {
        if (tcp->rcvNxt + 1 == tcp->rcvMax) {
            TcpTsqAddQue(tcp, TCP_TSQ_RECV_FIN);
        }
    }

    return pushData;
}
