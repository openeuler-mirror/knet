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

#include "tcp_sack.h"

#define TCP_REASS_MAX_PKT_CNT (0xFFFFU)

static inline bool TcpReassCanInsert(TcpSk_t* tcp)
{
    uint32_t maxPktCnt = (TcpSk2Sk(tcp)->rcvHiwat / tcp->mss) * 2 + 1;
    maxPktCnt = UTILS_MIN(maxPktCnt, TCP_REASS_MAX_PKT_CNT);
    if (tcp->reassQue.pktCnt < maxPktCnt) {
        return true;
    }

    return false;
}

static int TcpReassProcPrevDup(TcpSk_t* tcp, Pbuf_t* pbuf, Pbuf_t* prev, Pbuf_t* nxt)
{
    if (prev == NULL) {
        return -1;
    }
    TcpReassInfo_t* reassInfo = TcpReassGetInfo(pbuf);
    TcpReassInfo_t* prevReassInfo = TcpReassGetInfo(prev);
    /*
        prev
      |------|
               |----
      1. 无重叠
     */
    if (TcpSeqGt(reassInfo->seq, prevReassInfo->endSeq)) {
        // NOTE: 这里不判断 prev 是否为 fin 报文，
        // 因为 prev 为 fin 报文时一定为最后一片，这种场景已经提前处理
        return -1;
    }

    /*
        prev
      |------|
        |---|
      2. 完全重叠
     */
    if (TcpSeqLeq(reassInfo->endSeq, prevReassInfo->endSeq)) {
        // pbuf 不带 fin ，直接丢弃 pubf ，重组完成
        if ((reassInfo->thFlags & DP_TH_FIN) == 0) {
            PBUF_Free(pbuf);
            return 0;
        }
        // pbuf 带 fin ，切除 prev 多余部分并带上 fin 标记
        uint32_t cutLen = prevReassInfo->endSeq - reassInfo->endSeq;

        // prev 不带 fin ，需要多 cut 掉一位并让 prev 携带 fin 标记
        if ((prevReassInfo->thFlags & DP_TH_FIN) == 0) {
            cutLen += 1;
            prevReassInfo->thFlags |= DP_TH_FIN;
        }
        PBUF_CutTailData(prev, (uint16_t)cutLen);
        tcp->reassQue.bufLen -= cutLen;
        prevReassInfo->endSeq = reassInfo->endSeq;

        // nxt 后面的报文都丢弃
        if (nxt != NULL) {
            (void)PBUF_ChainRemoveAfter(&tcp->reassQue, nxt);
        }
        PBUF_Free(pbuf);
        return 0;
    }

    /*
        prev
      |------|
        |------|
      3. 部分重叠
     */

    // prev 带 fin ，直接丢弃 pbuf
    if ((prevReassInfo->thFlags & DP_TH_FIN) != 0) {
        PBUF_Free(pbuf);
        return 0;
    }
    uint32_t cutLen = prevReassInfo->endSeq - reassInfo->seq;
    PBUF_CutTailData(prev, (uint16_t)cutLen);
    tcp->reassQue.bufLen -= cutLen;
    prevReassInfo->endSeq = reassInfo->seq;
    return -1;
}

// 处理后部完全覆盖
static Pbuf_t* TcpReassProcNxtFullCover(TcpSk_t* tcp, Pbuf_t* pbuf, Pbuf_t* cur,
                                        TcpReassInfo_t* reassInfo, TcpReassInfo_t* curReassInfo)
{
    if ((curReassInfo->thFlags & DP_TH_FIN) != 0) {
        uint32_t cutLen = reassInfo->endSeq - curReassInfo->endSeq;
        if ((reassInfo->thFlags & DP_TH_FIN) == 0) {
            cutLen += 1;
            reassInfo->thFlags |= DP_TH_FIN;
        }
        reassInfo->endSeq = curReassInfo->endSeq;
        PBUF_CutTailData(pbuf, (uint16_t)cutLen);
    }

    Pbuf_t* tmp = PBUF_CHAIN_NEXT(cur);
    PBUF_ChainRemove(&tcp->reassQue, cur);
    PBUF_Free(cur);
    cur = tmp;

    return cur;
}

static int TcpReassProcNxtDup(TcpSk_t* tcp, Pbuf_t* pbuf, Pbuf_t* nxt)
{
    TcpReassInfo_t* reassInfo = TcpReassGetInfo(pbuf);
    Pbuf_t* cur = nxt;
    TcpReassInfo_t* curReassInfo;

    // 后部可能有多个报文段重复
    while (cur != NULL) {
        curReassInfo = TcpReassGetInfo(cur);
        /*
                   cur
                |-------|
          |---|
          1. 不重复，插入当前位置，如果带 fin ，移除 cur 之后的报文
         */
        if (TcpSeqLeq(reassInfo->endSeq, curReassInfo->seq)) {
            if ((reassInfo->thFlags & DP_TH_FIN) != 0) {
                (void)PBUF_ChainRemoveAfter(&tcp->reassQue, cur);
                cur = NULL;
            }
            break;
        }
        /*
                   cur
                |-------|
          |--------------|
          2. 完全覆盖，移除 cur, 如果 cur 带 fin, cut pbuf 多余部分
         */
        if (TcpSeqGeq(reassInfo->endSeq, curReassInfo->endSeq)) {
            cur = TcpReassProcNxtFullCover(tcp, pbuf, cur, reassInfo, curReassInfo);
        } else {
            /*
                   cur
                |-------|
          |-----------|
          3. 部分覆盖，cut 重复部分
         */
            // 带 fin ，删除 cur 及之后的部分
            if ((reassInfo->thFlags & DP_TH_FIN) != 0) {
                (void)PBUF_ChainRemoveAfter(&tcp->reassQue, cur);
                cur = NULL;
            } else {
                uint32_t cutLen = reassInfo->endSeq - curReassInfo->seq;
                PBUF_CUT_DATA(cur, cutLen);
                tcp->reassQue.bufLen -= cutLen;
                curReassInfo->seq = reassInfo->endSeq;
            }
            break;
        }
    }

    DP_INC_PKT_STAT(tcp->wid, DP_PKT_TCP_REASS_PKT);
    PBUF_ChainInsertBefore(&tcp->reassQue, cur, pbuf);
    return 0;
}

int TcpReassInsertSlow(TcpSk_t* tcp, Pbuf_t* pbuf)
{
    TcpReassInfo_t* reassInfo = TcpReassGetInfo(pbuf);

    Pbuf_t* cur = NULL;
    Pbuf_t* prev = NULL;
    TcpReassInfo_t* curReassInfo;
    int ret;
    /*
         prev           cur
      |------|       |------|
            |-----...
      1. 查找重组队列中数据起始序号大于等于当前报文序号的第一个报文，当前报文应该插入在该报文之前
     */
    cur = PBUF_CHAIN_FIRST(&tcp->reassQue);
    while (cur != NULL) {
        curReassInfo = TcpReassGetInfo(cur);
        if (TcpSeqGeq(curReassInfo->seq, reassInfo->seq)) {
            break;
        }
        prev = cur;
        cur = PBUF_CHAIN_NEXT(cur);
    }

    ret = TcpReassProcPrevDup(tcp, pbuf, prev, cur);
    if (ret == 0) {
        return 0;
    }

    return TcpReassProcNxtDup(tcp, pbuf, cur);
}

int TcpReassInsert(TcpSk_t* tcp, Pbuf_t* pbuf, TcpPktInfo_t* pi)
{
    TcpReassInfo_t* curSegInfo = TcpReassGetInfo(pbuf);
    TcpReassInfo_t* lastSegInfo;

    if (!TcpReassCanInsert(tcp)) {
        PBUF_Free(pbuf);
        PBUF_ChainClean(&tcp->reassQue);
        if (TCP_SACK_AVAILABLE(tcp)) {
            TcpClearSackInfo(tcp->sackInfo, tcp->sndUna);
        }
        return -1;
    }

    curSegInfo->seq = pi->seq;
    curSegInfo->endSeq = pi->endSeq;
    curSegInfo->thFlags = (pi->thFlags & DP_TH_FIN);

    if (PBUF_CHAIN_IS_EMPTY(&tcp->reassQue)) {
        DP_INC_PKT_STAT(tcp->wid, DP_PKT_TCP_REASS_PKT);
        PBUF_ChainPush(&tcp->reassQue, pbuf);
        return 0;
    }

    lastSegInfo = TcpReassGetInfo(PBUF_CHAIN_TAIL(&tcp->reassQue));
    if (TcpSeqGeq(curSegInfo->seq, lastSegInfo->endSeq)) {
        if ((lastSegInfo->thFlags & DP_TH_FIN) != 0) {
            PBUF_Free(pbuf);
            return 0;
        }
        DP_INC_PKT_STAT(tcp->wid, DP_PKT_TCP_REASS_PKT);
        PBUF_ChainPush(&tcp->reassQue, pbuf);
        return 0;
    }

    return TcpReassInsertSlow(tcp, pbuf);
}

uint32_t TcpReass(TcpSk_t* tcp, TcpPktInfo_t* pi)
{
    TcpReassInfo_t* curReassInfo;
    Pbuf_t*         cur;
    uint32_t        reassedLen = 0;

    while (true) {
        cur = PBUF_CHAIN_FIRST(&tcp->reassQue);
        if (cur == NULL) {
            break;
        }
        curReassInfo = TcpReassGetInfo(cur);
        if (TcpSeqLt(tcp->rcvNxt, curReassInfo->seq)) {
            break;
        }
        // 如果有 fin 标识，将 fin 标识带出，后续处理
        if ((curReassInfo->thFlags & DP_TH_FIN) != 0) {
            ASSERT(PBUF_CHAIN_NEXT(cur) == NULL);

            pi->thFlags |= curReassInfo->thFlags;
        }

        cur = PBUF_CHAIN_POP(&tcp->reassQue);
        if (PBUF_GET_PKT_LEN(cur) != 0) {
            PBUF_ChainPush(&tcp->rcvQue, cur);
            tcp->rcvNxt += PBUF_GET_PKT_LEN(cur);
            tcp->accDataCnt++;
            reassedLen += PBUF_GET_PKT_LEN(cur);
        } else {
            // 无数据的纯 fin 报文，需要释放
            PBUF_Free(cur);
        }
    }

    return reassedLen;
}
