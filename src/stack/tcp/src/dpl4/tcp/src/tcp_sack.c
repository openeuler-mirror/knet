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

#include "utils_base.h"
#include "utils_log.h"
#include "shm.h"
#include "tcp_rate.h"
#include "tcp_cc.h"

#include "tcp_sack.h"

void TcpInitSackInfo(TcpSackInfo_t** sackInfo)
{
    *sackInfo = SHM_MALLOC(sizeof(TcpSackInfo_t), MOD_TCP, DP_MEM_FREE);
    if (*sackInfo == NULL) {
        DP_LOG_ERR("Malloc memory failed for tcp sackInfo.");
        return;     // 内存申请失败情况，在使用前都会判空 TCP_SACK_AVAILABLE
    }
    (void)memset_s(*sackInfo, sizeof(TcpSackInfo_t), 0, sizeof(TcpSackInfo_t));
    LIST_INIT_HEAD(&(*sackInfo)->sackHoleHead);
    (*sackInfo)->sackBlockNum = 0;
    (*sackInfo)->rcvSackEnd = 0;
    (*sackInfo)->sackHoleNum = 0;
}

void TcpDeinitSackInfo(TcpSackInfo_t* sackInfo)
{
    if (sackInfo != NULL) {
        TcpClearSackHole(&sackInfo->sackHoleHead);
        SHM_FREE(sackInfo, DP_MEM_FREE);
    }
}

static int TcpProcSackBlock(TcpSk_t* tcp, TcpSackBlock_t* sackBlock, const uint8_t* opt, uint32_t ack)
{
    uint32_t sackStart = UTILS_BYTE2LONG(opt);
    uint32_t sackEnd = UTILS_BYTE2LONG(opt + 4);       // 偏移sackStart所占 4位
    sackStart = UTILS_NTOHL(sackStart);
    sackEnd = UTILS_NTOHL(sackEnd);
    if (TcpSeqGeq(sackStart, sackEnd) || TcpSeqGt(sackEnd, tcp->sndMax)) {
        return -1;
    }
    // 存在DSACK的情况，可能存在sackBlock和ack重合的情况，不认为是异常
    if (TcpSeqLeq(sackEnd, ack)) {
        return 0;
    }
    if (TcpSeqLeq(sackStart, ack)) {
        sackStart = ack;
    }
    sackBlock->seqStart = sackStart;
    sackBlock->seqEnd = sackEnd;
    return 1;
}

static int TcpParseSackOpt(TcpSk_t* tcp, TcpPktInfo_t* pi, TcpSackBlock_t* sackBlock, uint32_t blockSize)
{
    uint8_t* opt = pi->sackOpt;
    uint16_t sackSize = *opt++;
    uint16_t sackBlockCnt = 0;
    int ret;

    // 收到的报文里的sack信息，写到临时sackBlock[]里
    uint16_t sackCnt = (sackSize - TCP_OPTLEN_BASE_SACK) / TCP_OPTLEN_SACK_PERBLOCK;
    while (sackCnt-- > 0 && sackBlockCnt < blockSize) {
        ret = TcpProcSackBlock(tcp, &sackBlock[sackBlockCnt], opt, pi->ack);
        if (ret < 0) {
            return -1;
        }
        if (ret == 1) {
            sackBlockCnt++;
        }
        opt += TCP_OPTLEN_SACK_PERBLOCK;
    }

    (void)memset_s(&sackBlock[sackBlockCnt], (blockSize - sackBlockCnt) * sizeof(TcpSackBlock_t),
                   0, (blockSize - sackBlockCnt) * sizeof(TcpSackBlock_t));
    return (int)sackBlockCnt;
}

static void TcpSackDoSort(TcpSackBlock_t* sackBlock, uint8_t sackCnt)
{
    TcpSackBlock_t tempBlock;
    // 以seqEnd的顺序重新排序
    for (uint8_t i = 0; i < sackCnt; ++i) {
        for (uint8_t j = i; j < sackCnt; ++j) {
            if (TcpSeqGt(sackBlock[i].seqEnd, sackBlock[j].seqEnd)) {
                tempBlock = sackBlock[i];
                sackBlock[i] = sackBlock[j];
                sackBlock[j] = tempBlock;
            }
        }
    }
}

static void TcpClearSackHoleBeforeAck(TcpSackInfo_t* sackInfo, TcpPktInfo_t* pi)
{
    TcpSackHole_t* hole = LIST_FIRST(&sackInfo->sackHoleHead);
    TcpSackHole_t* nextHole;

    while (hole != NULL) {
        nextHole = LIST_NEXT(hole, node);
        // 清除完全在ack前的空洞
        if (TcpSeqLeq(hole->seqEnd, pi->ack)) {
            LIST_REMOVE(&sackInfo->sackHoleHead, hole, node);
            SHM_FREE(hole, DP_MEM_FREE);
            sackInfo->sackHoleNum--;
            hole = nextHole;
            continue;
        }
        // 部分在ack前的空洞，移动其左边界
        if (TcpSeqLt(hole->seqStart, pi->ack)) {
            hole->seqStart = pi->ack;
            hole->seqRetrans = TCP_SEQ_MAX(hole->seqRetrans, hole->seqStart);
        }
        break;
    }
}

static TcpSackHole_t* TcpCreateSackHole(TcpSk_t* tcp, uint32_t seqStart, uint32_t seqEnd, uint32_t seqRetrans)
{
    if (TCP_SACK_HOLE_IS_FULL(tcp)) {
        return NULL;
    }
    TcpSackHole_t* newHole = SHM_MALLOC(sizeof(TcpSackHole_t), MOD_TCP, DP_MEM_FREE);
    if (newHole == NULL) {
        DP_LOG_ERR("Malloc memory failed for tcp newHole.");
        return NULL;
    }

    newHole->node.prev = NULL;
    newHole->node.next = NULL;
    newHole->seqStart = seqStart;
    newHole->seqEnd = seqEnd;
    newHole->seqRetrans = seqRetrans;
    tcp->sackInfo->sackHoleNum++;
    return newHole;
}

// 根据每一个sackBlock对每一个hole修改
static int TcpUpdateSackHoleBySackBlock(TcpSk_t* tcp, TcpSackHole_t* hole, TcpSackBlock_t sackBlock)
{
    TcpSackHole_t* newHole;
    // 新增block 与 当前hole 无交集
    if (TcpSeqLt(hole->seqEnd, sackBlock.seqStart) ||
        TcpSeqGeq(hole->seqStart, sackBlock.seqEnd)) {
        return 0;
    }

    if (TcpSeqLt(hole->seqStart, sackBlock.seqStart)) {
        // block 覆盖hole右侧
        uint32_t tempSeqRetrans = hole->seqRetrans;
        uint32_t tempSeqEnd = hole->seqEnd;
        hole->seqEnd = sackBlock.seqStart - 1;
        hole->seqRetrans = TCP_SEQ_MIN(hole->seqRetrans, hole->seqEnd + 1);
        if (TcpSeqGt(tempSeqEnd, sackBlock.seqEnd)) {
            // block 完全在 hole中
            newHole = TcpCreateSackHole(tcp, sackBlock.seqEnd, tempSeqEnd,
                                        TCP_SEQ_MAX(tempSeqRetrans, sackBlock.seqEnd));
            if (newHole == NULL) {
                return -1;
            }
            LIST_INSERT_AFTER(&tcp->sackInfo->sackHoleHead, hole, newHole, node);
        }
    } else if (TcpSeqGt(hole->seqEnd, sackBlock.seqEnd)) {
        // block 覆盖了 hole的左侧
        hole->seqStart = sackBlock.seqEnd;
        hole->seqRetrans = TCP_SEQ_MAX(hole->seqRetrans, hole->seqStart);
        return 1;
    } else {
        // block 完全覆盖 hole, 空洞被补齐，删除
        LIST_REMOVE(&tcp->sackInfo->sackHoleHead, hole, node);
        SHM_FREE(hole, DP_MEM_FREE);
    }
    return 0;
}

static int TcpUpdateSackHole(TcpSk_t* tcp, TcpPktInfo_t* pi, TcpSackBlock_t* sackBlock, uint8_t sackCnt)
{
    int ret;
    TcpSackHole_t* hole;
    TcpSackHole_t* nextHole;

    for (uint8_t i = 0; i < sackCnt; ++i) {
        TcpSackHole_t* newHole;
        TcpSackBlock_t block;
        block = sackBlock[i];

        hole = LIST_FIRST(&tcp->sackInfo->sackHoleHead);

        while (hole != NULL) {
            nextHole = LIST_NEXT(hole, node);
            ret = TcpUpdateSackHoleBySackBlock(tcp, hole, block);
            if (ret == -1) {
                return -1;
            }
            if (ret == 1) {
                break;          // 已经判断到block的右侧，无需继续遍历hole
            }
            hole = nextHole;
        }

        uint32_t tempSackEnd = tcp->sackInfo->rcvSackEnd;
        if (TcpSeqLt(tempSackEnd, block.seqEnd)) {
            tcp->sackInfo->rcvSackEnd = block.seqEnd;
            pi->pktType |= TCP_PI_SACKED_DATA;
        }

        if (TcpSeqGeq(tempSackEnd, block.seqStart)) {
            continue;
        }

        // 在尾部新增
        newHole = TcpCreateSackHole(tcp, tempSackEnd, block.seqStart - 1, tempSackEnd);
        if (newHole == NULL) {
            return -1;
        }
        LIST_INSERT_TAIL(&tcp->sackInfo->sackHoleHead, newHole, node);
    }
    return 0;
}

static void TcpUpdateScoreBoardBySack(TcpSk_t* tcp, TcpSackBlock_t* sackBlock, uint8_t sackCnt)
{
    Pbuf_t* cur = tcp->rexmitQue.head;
    uint32_t sackStart = 0;
    uint32_t sackEnd = 0;
    // sack块是排好序的，不需要重新遍历pbuf
    for (int i = 0; i < sackCnt; ++i) {
        sackStart = sackBlock[i].seqStart;
        sackEnd = sackBlock[i].seqEnd;
        while (cur != NULL) {
            TcpScoreBoard_t *scb = PBUF_GET_SCORE_BOARD(cur);
            // 报文尾序号未到达sack的起始、报文的头序号超过sack的结尾
            if (TcpSeqLt(scb->endSeq, sackStart) || TcpSeqGeq(scb->startSeq, sackEnd)) {
                cur = cur->chainNext;
                continue;
            }
            if ((scb->state & TF_SEG_SACKED) == 0 && (tcp->inflight >= PBUF_GET_PKT_LEN(cur))) {
                tcp->inflight -= PBUF_GET_PKT_LEN(cur);     // 若报文被sack确认过，无论是否完整，认为这个报文已经到达，不在途
                tcp->rs.ackedSacked++;                      // 统一在nomalAck/ dupAck时处理
                tcp->connDeliveredCnt++;
            }
            scb->state |= TF_SEG_SACKED;
            TcpBwOnAcked(tcp, cur);
            cur = cur->chainNext;
            if (TcpSeqGeq(scb->endSeq, sackEnd)) {          // 当前sack块已经处理完，处理下一个sack块
                break;
            }
        }
    }
}

int TcpProcSackAck(TcpSk_t* tcp, TcpPktInfo_t* pi)
{
    TcpSackBlock_t sackBlock[TCP_SACK_BLOCK_SIZE];

    // 遍历空洞，删除ack前的空洞，确保空洞链表的头的seqStart，在ack后（空洞始终有序）
    TcpClearSackHoleBeforeAck(tcp->sackInfo, pi);

    // rcvSackEnd更新，用于序列号最大的洞的填写
    if (TcpSeqLt(tcp->sackInfo->rcvSackEnd, pi->ack)) {
        tcp->sackInfo->rcvSackEnd = pi->ack;
    }

    // 报文中没有sack块
    if (pi->sackOpt == NULL) {
        return 0;
    }
    int sackCnt = TcpParseSackOpt(tcp, pi, sackBlock, TCP_SACK_BLOCK_SIZE);
    if (sackCnt < 0) {
        return -1;
    }

    TcpSackDoSort(sackBlock, (uint8_t)sackCnt);     // sackCnt不会超过6

    // sackHole填写
    if (TcpUpdateSackHole(tcp, pi, sackBlock, (uint8_t)sackCnt) != 0) {     // sackCnt不会超过6
        return -1;
    }

    if (tcp->caMeth->algId == TCP_CAMETH_BBR) {
        TcpUpdateScoreBoardBySack(tcp, sackBlock, (uint8_t)sackCnt);
    }
    return 0;
}

static uint8_t TcpMergeSack(TcpSk_t* tcp, TcpSackBlock_t* curBlock, TcpSackBlock_t* tempBlock, uint32_t blockSize)
{
    uint8_t savedNum = 0;
    uint32_t seqStart;
    uint32_t seqEnd;

    for (int i = 0; i < tcp->sackInfo->sackBlockNum && savedNum < blockSize; ++i) {
        seqStart = tcp->sackInfo->sackBlock[i].seqStart;
        seqEnd = tcp->sackInfo->sackBlock[i].seqEnd;

        // 如果start比rcvNxt小，则此block块整体已经被ack
        if (TcpSeqLeq(seqStart, tcp->rcvNxt)) {
            continue;
        }

        // curBlock和sackBlock[i]存在交集，整合后修改cur
        if (TcpSeqLeq(curBlock->seqStart, seqEnd) &&        // 排除完全在右侧情况
            TcpSeqGeq(curBlock->seqEnd, seqStart)) {        // 排除完全在左侧情况
            if (TcpSeqGt(curBlock->seqStart, seqStart)) {
                curBlock->seqStart = seqStart;
            }
            if (TcpSeqLt(curBlock->seqEnd, seqEnd)) {
                curBlock->seqEnd = seqEnd;
            }
        } else {
            // 没有交集，按原有顺序写入tempBlock
            tempBlock[savedNum].seqStart = seqStart;
            tempBlock[savedNum].seqEnd = seqEnd;
            savedNum++;
        }
    }
    return savedNum;
}

// 无论是否乱序（产生新的sack），都需要更新
void TcpUpdateSackList(TcpSk_t* tcp, TcpPktInfo_t* pi)
{
    TcpSackBlock_t curBlock;
    TcpSackBlock_t tempBlock[TCP_SACK_BLOCK_SIZE];
    uint8_t headNum = 0;
    uint8_t savedNum;

    curBlock.seqStart = pi->seq;
    curBlock.seqEnd = pi->seq + pi->dataLen;

    // 根据原有sackInfo中sackBlock，与curBlock连续则合并，否则顺序写入tempBlock
    savedNum = TcpMergeSack(tcp, &curBlock, tempBlock, TCP_SACK_BLOCK_SIZE);

    // tcp->rcvNxt已经在TcpProcData中修改
    if (TcpSeqGt(curBlock.seqStart, tcp->rcvNxt)) {
        tcp->sackInfo->sackBlock[0] = curBlock;
        headNum = 1;
        // 如果sackBlock[0]无需放置curBlock，则tempBlock对应的savedNum无需判断
        if (savedNum >= TCP_SACK_BLOCK_SIZE) {
            savedNum = TCP_SACK_BLOCK_SIZE - headNum;
        }
    }

    if (savedNum > 0) {
        (void)memcpy_s(&tcp->sackInfo->sackBlock[headNum], sizeof(TcpSackBlock_t) * savedNum,
                       tempBlock, sizeof(TcpSackBlock_t) * savedNum);
    }
    tcp->sackInfo->sackBlockNum = savedNum + headNum;
}

void TcpClearSackHole(TcpSackHoleHead* sackHoleHead)
{
    TcpSackHole_t* item;
    while (!LIST_IS_EMPTY(sackHoleHead)) {
        item = LIST_FIRST(sackHoleHead);
        LIST_REMOVE(sackHoleHead, item, node);
        SHM_FREE(item, DP_MEM_FREE);
    }
}

void TcpClearSackInfo(TcpSackInfo_t *sackInfo, uint32_t sndUna)
{
    TcpClearSackHole(&sackInfo->sackHoleHead);
    sackInfo->sackBlockNum = 0;
    sackInfo->rcvSackEnd = sndUna;
    sackInfo->sackHoleNum = 0;
}

static int TcpGetRexmitSackSeq(TcpSackHoleHead sackHoleHead, uint32_t* rexmitSackSeq, TcpSackHole_t** sackHole)
{
    *sackHole = sackHoleHead.first;
    while (*sackHole != NULL) {
        if (TcpSeqGt((*sackHole)->seqRetrans, (*sackHole)->seqEnd)) {
            *sackHole = (*sackHole)->node.next;
            continue;
        }
        *rexmitSackSeq = (*sackHole)->seqRetrans;
        return 0;
    }
    return -1;
}

Pbuf_t* TcpGetRexmitSack(TcpSk_t* tcp, uint32_t* sndSeq)
{
    // 发送sackHole开始位置所在的Pbuf
    Pbuf_t* pbuf = NULL;
    uint16_t pktLen;
    TcpSackHole_t* sackHole = NULL;

    if (TcpGetRexmitSackSeq(tcp->sackInfo->sackHoleHead, sndSeq, &sackHole) != 0) {
        return NULL;
    }

    // tcp->rexmitQue的第一个序号为tcp->sndUna
    pbuf = PBUF_CHAIN_FIRST(&tcp->rexmitQue);
    uint32_t pbufStartSeq = tcp->sndUna;
    while (pbuf != NULL) {
        pktLen = (uint16_t)PBUF_GET_PKT_LEN(pbuf);
        if (TcpSeqGt(pbufStartSeq + pktLen, *sndSeq)) {     // 空洞的首位在此Pbuf中
            *sndSeq = pbufStartSeq;
            sackHole->seqRetrans = TCP_SEQ_MIN(sackHole->seqEnd + 1, pbufStartSeq + pktLen);
            return pbuf;
        }
        pbufStartSeq += pktLen;
        pbuf = PBUF_CHAIN_NEXT(pbuf);
    }
    // 存在空洞，但是空洞首位序号不在rexmitQue里
    return NULL;
}
