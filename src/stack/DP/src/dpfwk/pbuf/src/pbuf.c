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

#include "utils_cksum.h"
#include "utils_debug.h"
#include "pbuf_local.h"

#include "pbuf.h"

Pbuf_t* PBUF_Build(const uint8_t* data, uint16_t dataLen, uint16_t headroom)
{
    Pbuf_t*  pbuf;
    Pbuf_t*  cur;
    uint16_t cpyLen;
    uint16_t totLen;
    uint16_t remainLen;

    pbuf = PBUF_Alloc(headroom, dataLen);
    if (pbuf == NULL) {
        return NULL;
    }

    cur = pbuf;
    totLen = 0;
    while (cur != NULL && totLen < dataLen) {
        cpyLen = cur->payloadLen - cur->offset;
        remainLen = dataLen - totLen;
        if (cpyLen > remainLen) {
            cpyLen = remainLen;
        }

        (void)memcpy_s(PBUF_MTOD(cur, uint8_t*), cpyLen, data + totLen, cpyLen);

        cur->segLen = cpyLen;
        totLen += cpyLen;
        cur = cur->next;
    }
    pbuf->totLen = totLen;

    return pbuf;
}

uint16_t PBUF_Append(Pbuf_t* pbuf, const uint8_t* data, uint16_t len)
{
    Pbuf_t*  cur    = pbuf;
    Pbuf_t*  end    = pbuf->end;
    uint16_t writedEnd = 0;

    if ((end->segLen + end->offset) != end->payloadLen) {
        writedEnd = end->payloadLen - end->segLen - end->offset;
        writedEnd = (writedEnd > len) ? len : writedEnd;

        (void)memcpy_s(PBUF_MTOD(end, uint8_t*) + end->segLen, writedEnd, data, writedEnd);

        end->segLen += writedEnd;
        pbuf->totLen += writedEnd;
    }

    if (writedEnd == len) {
        return len;
    }

    cur = PBUF_Build(data + writedEnd, len - writedEnd, 0);
    if (cur == NULL) {
        // build 失败，恢复添加到 end 的部分
        pbuf->totLen -= writedEnd;
        end->segLen -= writedEnd;
        return 0;
    }

    pbuf->nsegs += cur->nsegs;
    pbuf->end->next = cur;
    pbuf->end       = cur->end;
    pbuf->totLen += cur->totLen;

    return len;
}

uint16_t PBUF_Read(Pbuf_t* pbuf, uint8_t* data, size_t len, int peek)
{
    ASSERT(pbuf != NULL);
    uint16_t     ret    = 0;
    uint16_t     totLen = (uint16_t)PBUF_GET_PKT_LEN(pbuf);
    Pbuf_t* cur    = pbuf;

    while (cur != NULL && cur->segLen == 0) {
        cur = cur->next;
    }

    while (cur != NULL && ret < len && totLen > 0) {
        uint16_t cpyLen = (len - ret > 0xFFFF) ? 0xFFFF : (uint16_t)(len - ret); // 如果长度超过uint16(即0xFFFF)，取最大值
        cpyLen          = (cpyLen > cur->segLen) ? cur->segLen : cpyLen;
        if (cpyLen == 0) {
            cur = cur->next;
            continue;
        }

        if (data != NULL) {
            (void)memcpy_s(data + ret, cpyLen, PBUF_MTOD(cur, uint8_t*), cpyLen);
        }
        ret += cpyLen;
        totLen -= cpyLen;
        if (peek != 0) { // peek读取数据
            cur = cur->next;
            continue;
        }
        cur->segLen -= cpyLen;
        cur->offset += cpyLen;
        if (cur->segLen == 0) {
            cur = cur->next;
        }
    }

    if (peek == 0) { // 非peek读取
        pbuf->totLen -= ret;
    }

    return ret;
}

Pbuf_t* PBUF_Clone(Pbuf_t* src)
{
    Pbuf_t* ret;
    Pbuf_t* curSrc;

    ret = PBUF_Alloc(src->offset, (uint16_t)src->totLen);
    if (ret == NULL) {
        return NULL;
    }

    curSrc = src;
    while (curSrc != NULL) {
        PBUF_Append(ret, PBUF_MTOD(curSrc, uint8_t*), PBUF_GET_SEG_LEN(curSrc));

        curSrc = curSrc->next;
    }

    ret->paddr.in6 = src->paddr.in6;

    ret->pentry  = src->pentry;
    ret->l3type  = src->l3type;
    ret->l3Off   = src->l3Off;
    ret->ptype   = src->ptype;
    ret->l4type  = src->l4type;
    ret->l4Off   = src->l4Off;
    ret->hash    = src->hash;
    ret->pktType = src->pktType;
    ret->olFlags = src->olFlags;
    ret->tsoFragSize = src->tsoFragSize;

    return ret;
}

uint32_t PBUF_CalcCksum(Pbuf_t* pbuf)
{
    ASSERT(pbuf != NULL);
    uint32_t     cksum;
    Pbuf_t* cur;
    uint8_t      tail[2];
    int          tailCnt;

    if (PBUF_GET_SEGS(pbuf) == 1) {
        return UTILS_Cksum(0, PBUF_MTOD(pbuf, uint8_t*), PBUF_GET_PKT_LEN(pbuf));
    }

    cksum   = 0;
    cur     = pbuf;
    tailCnt = 0;

    do {
        uint8_t* data    = PBUF_MTOD(cur, uint8_t*);
        uint16_t dataLen = PBUF_GET_SEG_LEN(cur);

        if (tailCnt > 0) {
            tail[1] = *data++;
            cksum += *(uint16_t*)tail;
            tailCnt = 0;
            dataLen -= 1;
        }

        cksum = UTILS_CkMultiSum(cksum, data, dataLen & (~0x1));

        if ((dataLen & 0x1) != 0) {
            tail[0] = data[dataLen - 1];
            tailCnt = 1;
        }

        cur = PBUF_NEXT_SEG(cur);
    } while (cur != NULL);

    if (tailCnt != 0) {
        tail[1] = 0;
        cksum += *(uint16_t*)tail;
    }

    return cksum;
}

uint16_t DP_PbufCopy(DP_Pbuf_t* pbuf, uint8_t* data, size_t len)
{
    if (pbuf == NULL || data == NULL || len == 0u) {
        return 0;
    }
    return PBUF_Read(pbuf, data, len, 1);
}

DP_Pbuf_t* DP_PbufBuild(const uint8_t* data, uint16_t dataLen, uint16_t headroom)
{
    if (data == NULL || dataLen == 0u || (uint32_t)dataLen + headroom > PBUF_MAX_PAYLOAD_LEN) {
        return NULL;
    }

    return PBUF_Build(data, dataLen, headroom);
}

Pbuf_t* PBUF_Splice(Pbuf_t* pbuf, uint16_t spliceLen, uint16_t headroom)
{
    uint16_t     curLen = spliceLen;
    Pbuf_t* cur    = pbuf;
    Pbuf_t* newBuf;
    uint8_t      nsegs = 1; // 记录分片数量

    ASSERT(pbuf->totLen > spliceLen);

    while (curLen > cur->segLen) {
        curLen -= cur->segLen;
        cur = cur->next;
        nsegs++;
    }

    // 申请新的分片
    if (curLen < cur->segLen) {
        newBuf = PBUF_Build(PBUF_MTOD(cur, uint8_t*) + curLen, cur->segLen - curLen, headroom);
        if (newBuf == NULL) {
            return NULL;
        }

        ASSERT(newBuf->nsegs == 1);

        cur->segLen  = curLen;
        newBuf->next = cur->next;
        cur->next    = newBuf;
        pbuf->end    = (cur == pbuf->end) ? newBuf : pbuf->end;
        pbuf->nsegs += 1;
    }

    newBuf         = cur->next;
    cur->next      = NULL;
    newBuf->end    = (pbuf->end == pbuf) ? newBuf : pbuf->end;
    newBuf->nsegs  = pbuf->nsegs - nsegs;
    newBuf->totLen = pbuf->totLen - spliceLen;

    pbuf->end    = cur;
    pbuf->nsegs  = nsegs;
    pbuf->totLen = spliceLen;

    return newBuf;
}

static void PbufCleanUnusedSegs(Pbuf_t* pbuf)
{
    Pbuf_t* cur  = pbuf->next; // 首片不能释放
    Pbuf_t* prev = pbuf;

    while (cur != NULL) {
        if (cur->segLen == 0) {
            prev->next = cur->next;
            pbuf->nsegs--;
            PBUF_Free(cur);
            cur = prev;
        } else {
            prev = cur;
        }

        cur = cur->next;
    }
}

uint16_t PBUF_CutTailData(Pbuf_t* pbuf, uint16_t len)
{
    Pbuf_t* cur;
    uint16_t     totLen;
    uint16_t     newTotLen;

    ASSERT(pbuf != NULL);
    ASSERT(pbuf->totLen >= len);

    if (pbuf->nsegs == 1) {
        pbuf->totLen -= len;
        pbuf->segLen -= len;

        return len;
    }

    totLen    = 0;
    newTotLen = (uint16_t)pbuf->totLen - len;
    cur       = pbuf;

    while (cur != NULL) {
        if (totLen < newTotLen) {
            totLen += cur->segLen;
            if (totLen > newTotLen) {
                uint16_t dupLen = totLen - newTotLen;

                cur->segLen -= dupLen;
                totLen = newTotLen;
            }
        } else {
            cur->segLen = 0;
        }
        cur = cur->next;
    }

    PbufCleanUnusedSegs(pbuf);

    pbuf->totLen = newTotLen;

    return len;
}

void PbufReset(Pbuf_t* pbuf)
{
    ASSERT(pbuf != NULL);

    Pbuf_t* cur;

    pbuf->totLen = 0;
    pbuf->segLen = 0;
    pbuf->flags  = 0;
    pbuf->olFlags = 0;

    cur = pbuf->next;
    while (cur != NULL) {
        cur->segLen = 0;
        cur->offset = 0;
        cur         = cur->next;
    }
}

ssize_t PBUF_ChainWrite(PBUF_Chain_t* chain, uint8_t* data, size_t len, uint16_t fragSize, uint16_t headroom)
{
    ASSERT(chain != NULL);
    ASSERT(data != NULL);
    ASSERT(len > 0);

    ssize_t ret = 0;
    Pbuf_t* end = chain->tail;
    ssize_t towrite = (ssize_t)len;

    if (end != NULL) {
        uint16_t appendLen = end->payloadLen - end->segLen - end->offset;
        bool isAppend = false;
        /* 这里要分两种情况 （payloadLen - offset是实际数据段可用长度）
        1、payloadLen - offset <= fragSize：这种时候没有顾虑，往空余空间添加数据即可
        2、payloadLen - offset > fragSize：这种情况要考虑旧数据是否超出分片大小，超出则不处理，不超出则补充数据长度直到等于fragSize
        */
        if ((end->payloadLen - end->offset) <= fragSize) {
            appendLen          = (towrite > appendLen) ? appendLen : (uint16_t)towrite;
            isAppend = true;
        } else {
            if (end->segLen < fragSize) {
                appendLen          = fragSize - end->segLen;
                appendLen          = (towrite > appendLen) ? appendLen : (uint16_t)towrite;
                isAppend = true;
            }
        }
        if (isAppend && appendLen != 0) {
            (void)PBUF_Append(end, data, appendLen);
            ret += appendLen;
            towrite -= appendLen;
            chain->bufLen += appendLen;
        }
    }

    while (towrite > 0) {
        uint16_t appendLen;

        appendLen = (fragSize > towrite) ? (uint16_t)towrite : fragSize;
        end       = PBUF_Build(data + ret, appendLen, headroom);
        if (end == NULL) {
            break;
        }
        PBUF_ChainPush(chain, end);
        ret += appendLen;
        towrite -= appendLen;
    }

    return (ssize_t)ret;
}

ssize_t PBUF_ChainWriteFromPbuf(PBUF_Chain_t* chain, Pbuf_t* src, uint16_t fragSize, uint16_t headroom)
{
    ASSERT(chain != NULL);

    ssize_t ret = 0;
    Pbuf_t* cur = src;

    while (cur != NULL) {
        ssize_t written;

        written = PBUF_ChainWrite(chain, PBUF_MTOD(cur, uint8_t*), PBUF_GET_SEG_LEN(cur), fragSize, headroom);
        if (written <= 0) {         // 内存申请失败
            return ret;
        }

        ret += written;
        cur = PBUF_NEXT_SEG(cur);
    }

    return ret;
}

size_t PBUF_ChainRead(PBUF_Chain_t* chain, uint8_t* buf, size_t len, int peek, int resetData)
{
    size_t ret = 0;
    Pbuf_t* cur = chain->head;

    (void)resetData;
    while (cur != NULL && ret < len) {
        Pbuf_t* next;
        uint16_t bufLen = (len - ret > 0xFFFF) ? 0xFFFF : (uint16_t)(len - ret); // 如果长度超过uint16(即0xFFFF)，则取最大值
        ret += PBUF_Read(cur, buf == NULL ? NULL : buf + ret, bufLen, peek);
        if (cur->totLen == 0) {
            next = cur->chainNext;
            chain->pktCnt--;
            PBUF_Free(cur);
            cur = next;
        } else if (peek != 0) {
            cur = cur->chainNext;
        }
    }

    if (peek != 0) {
        return ret;
    }
    chain->bufLen = chain->bufLen - ret;

    if (cur == NULL) {
        ASSERT(chain->bufLen == 0);
        ASSERT(chain->pktCnt == 0);
        chain->head = NULL;
        chain->tail = NULL;
    } else {
        chain->head    = cur;
        cur->chainPrev = NULL;
    }

    return ret;
}

void PBUF_ChainClean(PBUF_Chain_t* chain)
{
    Pbuf_t* cur = chain->head;

    while (cur != NULL) {
        Pbuf_t* prev = cur;
        cur = cur->chainNext;
        PBUF_Free(prev);
    }

    chain->head   = NULL;
    chain->tail   = NULL;
    chain->pktCnt = 0;
    chain->bufLen = 0;
}
