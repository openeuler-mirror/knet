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

#include "dp_ethernet.h"
#include "utils_base.h"
#include "utils_cksum.h"
#include "utils_debug.h"
#include "utils_log.h"

#include "pbuf.h"

// 向 pbuf 中填充数据，直到 pbuf 空间用完或者 pbuf 的包长达到 maxPktLen
// 返回实际的填充长度
static uint16_t PBUF_Fill(Pbuf_t* pbuf, const uint8_t* data, uint16_t dataLen, uint32_t maxPktLen)
{
    ASSERT(pbuf->totLen < maxPktLen);

    Pbuf_t*  cur = pbuf;
    uint16_t totLen = 0;
    uint16_t cpyLen;
    uint16_t remainLen;
    uint16_t fillLen;

    fillLen = dataLen < (maxPktLen - pbuf->totLen) ? dataLen : (uint16_t)(maxPktLen - pbuf->totLen);

    // 1. 找到填充起始位置
    while (cur != NULL && (cur->segLen + cur->offset) == cur->payloadLen) {
        cur = cur->next;
    }

    // 2. 开始填充直到 pbuf 空间用完，或者数据填充完，或包长达到 maxPktLen
    while (cur != NULL && totLen < fillLen) {
        cpyLen = cur->payloadLen - cur->offset - cur->segLen;
        remainLen = fillLen - totLen;
        if (cpyLen > remainLen) {
            cpyLen = remainLen;
        }

        (void)memcpy_s(PBUF_MTOD(cur, uint8_t*) + cur->segLen, cpyLen, data + totLen, cpyLen);

        cur->segLen += cpyLen;
        totLen += cpyLen;
        cur = cur->next;
    }
    pbuf->totLen += totLen;

    return totLen;
}

Pbuf_t* PBUF_Build(const uint8_t* data, uint16_t dataLen, uint16_t headroom)
{
    Pbuf_t*  pbuf;

    pbuf = PBUF_Alloc(headroom, dataLen);
    if (pbuf == NULL) {
        return NULL;
    }

    if (dataLen != 0) {
        PBUF_Fill(pbuf, data, dataLen, dataLen);
    }

    return pbuf;
}

Pbuf_t* PBUF_BuildFromPbuf(const Pbuf_t* src, uint16_t dataLen, uint16_t headroom)
{
    ASSERT(dataLen <= src->totLen);

    Pbuf_t* ret;
    const Pbuf_t* srcSeg;
    Pbuf_t* retSeg;
    uint16_t remainLen;
    uint16_t srcSegCopied;
    uint16_t copyLen;

    ret = PBUF_Alloc(headroom, dataLen);
    if (ret == NULL) {
        return NULL;
    }

    srcSeg = src;
    retSeg = ret;
    remainLen = dataLen;
    srcSegCopied = 0;

    while (remainLen > 0 && retSeg != NULL && srcSeg != NULL) {
        copyLen = remainLen;
        copyLen = (uint16_t)UTILS_MIN(copyLen, retSeg->payloadLen - retSeg->segLen - retSeg->offset);
        copyLen = (uint16_t)UTILS_MIN(copyLen, srcSeg->segLen - srcSegCopied);

        (void)memcpy_s(PBUF_MTOD(retSeg, uint8_t*) + retSeg->segLen, copyLen,
                       PBUF_MTOD(srcSeg, uint8_t*) + srcSegCopied, copyLen);

        retSeg->segLen += copyLen;
        srcSegCopied += copyLen;

        if (retSeg->segLen + retSeg->offset == retSeg->payloadLen) {
            retSeg = retSeg->next;
        }

        if (srcSegCopied == srcSeg->segLen) {
            srcSeg = srcSeg->next;
            srcSegCopied = 0;
        }
        remainLen -= copyLen;
    }

    if (remainLen > 0) {
        PBUF_Free(ret);
        return NULL;
    }

    ret->totLen += dataLen;

    return ret;
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

    ret = PBUF_BuildFromPbuf(src, (uint16_t)src->totLen, src->offset);
    if (ret == NULL) {
        return NULL;
    }

    ret->paddr.in = src->paddr.in;

    ret->wid     = src->wid;
    ret->queid   = src->queid;
    ret->pentry  = src->pentry;
    ret->l3type  = src->l3type;
    ret->l3Off   = src->l3Off;
    ret->l4type  = src->l4type;
    ret->l4Off   = src->l4Off;
    ret->hash    = src->hash;
    ret->pktType = src->pktType;
    ret->olFlags = src->olFlags;
    ret->tsoFragSize = src->tsoFragSize;

    return ret;
}

// 规避超大函数检查
#define ADDCARRY(x) ((x) > 65535 ? (x) -= 65535 : (x))
#define REDUCE                                         \
    {                                                  \
        u32Util.u32 = checkSum;                        \
        checkSum    = u32Util.u16[0] + u32Util.u16[1]; \
        ADDCARRY(checkSum);                            \
    }

#define ODD_BYTE(u16Util, r16, checkSum, mlen, cur, len) \
    do {                                                 \
        (u16Util).u8[1] = *(char*)(r16);                 \
        (checkSum) += (u16Util).u16;                     \
        (r16)  = (uint16_t*)((char*)(r16) + 1);          \
        (mlen) = PBUF_GET_SEG_LEN((cur)) - 1;            \
        (len)--;                                         \
    } while (0)

#define EVEN_BOUNDARY(checkSum, u16Util, r16, mlen, byteSwapped) \
    do {                                                         \
        REDUCE;                                                  \
        (checkSum) <<= 8;                                        \
        (u16Util).u8[0] = *(char*)(r16);                         \
        (r16)           = (uint16_t*)((char*)(r16) + 1);         \
        (mlen)--;                                                \
        (byteSwapped) = 1;                                       \
    } while (0)

#define UNROLL_LOOP32(checkSum, mlen, r16) \
    do {                                   \
        while (((mlen) -= 32) >= 0) {      \
            (checkSum) += (r16)[0];        \
            (checkSum) += (r16)[1];        \
            (checkSum) += (r16)[2];        \
            (checkSum) += (r16)[3];        \
            (checkSum) += (r16)[4];        \
            (checkSum) += (r16)[5];        \
            (checkSum) += (r16)[6];        \
            (checkSum) += (r16)[7];        \
            (checkSum) += (r16)[8];        \
            (checkSum) += (r16)[9];        \
            (checkSum) += (r16)[10];       \
            (checkSum) += (r16)[11];       \
            (checkSum) += (r16)[12];       \
            (checkSum) += (r16)[13];       \
            (checkSum) += (r16)[14];       \
            (checkSum) += (r16)[15];       \
            (r16) += 16;                   \
        }                                  \
        (mlen) += 32;                      \
    } while (0)

#define UNROLL_LOOP8(checkSum, mlen, r16) \
    do {                                  \
        while (((mlen) -= 8) >= 0) {      \
            (checkSum) += (r16)[0];       \
            (checkSum) += (r16)[1];       \
            (checkSum) += (r16)[2];       \
            (checkSum) += (r16)[3];       \
            (r16) += 4;                   \
        }                                 \
        (mlen) += 8;                      \
    } while (0)

#define BYTE_SWAPPED(checkSum, byteSwapped, mlen, u16Util, r16) \
    do {                                                        \
        REDUCE;                                                 \
        (checkSum) <<= 8;                                       \
        (byteSwapped) = 0;                                      \
        if ((mlen) == -1) {                                     \
            (u16Util).u8[1] = *(char*)(r16);                    \
            (checkSum) += (u16Util).u16;                        \
            (mlen) = 0;                                         \
        } else {                                                \
            (mlen) = -1;                                        \
        }                                                       \
    } while (0)

#define LAST_HANDLE(checkSum, byteSwapped, mlen, u16Util, r16)                 \
    do {                                                                       \
        while (((mlen) -= 2) >= 0) {                                           \
            (checkSum) += *(r16)++;                                            \
        }                                                                      \
        if ((byteSwapped) == 1) {                                                   \
            BYTE_SWAPPED((checkSum), (byteSwapped), (mlen), (u16Util), (r16)); \
        } else if ((mlen) == -1) {                                             \
            (u16Util).u8[0] = *(char*)(r16);                                   \
        }                                                                      \
    } while (0)

uint32_t PBUF_CalcCksumAcc(Pbuf_t* pbuf)
{
    Pbuf_t*            cur         = NULL;
    register uint16_t* r16         = NULL;
    register uint32_t  checkSum    = 0;
    register int32_t   len         = (int32_t)PBUF_GET_PKT_LEN(pbuf);
    register int32_t   mlen        = 0;
    int                byteSwapped = 0;

    union {
        char     u8[2];
        uint16_t u16;
    } u16Util;

    union {
        uint16_t u16[2];
        uint32_t u32;
    } u32Util;

    cur = pbuf;
    for (; cur != NULL && len > 0; cur = cur->next) {
        if (cur->segLen == 0) {
            continue;
        }
        r16 = PBUF_MTOD(cur, uint16_t*);
        if (mlen == -1) {
            /*
             * The first byte of this mbuf is the continuation
             * of a word spanning between this mbuf and the
             * last mbuf.
             *
             * u16Util.u8[0] is already saved when scanning previous
             * mbuf.
             */
            ODD_BYTE(u16Util, r16, checkSum, mlen, cur, len);
        } else {
            mlen = PBUF_GET_SEG_LEN(cur);
        }
        if (len < mlen) {
            mlen = len;
        }
        len -= mlen;
        /*
         * Force to even boundary.
         */
        if ((1 & (unsigned long)(uintptr_t)r16) != 0 && (mlen > 0)) {
            EVEN_BOUNDARY(checkSum, u16Util, r16, mlen, byteSwapped);
        }
        /*
         * Unroll the loop to make overhead from
         * branches &u8 small.
         */
        UNROLL_LOOP32(checkSum, mlen, r16);

        UNROLL_LOOP8(checkSum, mlen, r16);

        if (mlen == 0 && byteSwapped == 0) {
            continue;
        }
        REDUCE;

        LAST_HANDLE(checkSum, byteSwapped, mlen, u16Util, r16);
    }

    if (mlen == -1) {
        u16Util.u8[1] = 0;
        checkSum += u16Util.u16;
    }

    REDUCE;
    return checkSum;
}

uint16_t DP_PbufCopy(DP_Pbuf_t* pbuf, uint8_t* data, size_t len)
{
    if (pbuf == NULL || data == NULL || len == 0u) {
        DP_ADD_ABN_STAT(DP_PBUF_COPY_PARAM_ERR);
        return 0;
    }
    return PBUF_Read(pbuf, data, len, 1);
}

DP_Pbuf_t* DP_PbufBuild(const uint8_t* data, uint16_t dataLen, uint16_t headroom)
{
    if (data == NULL || dataLen == 0u || (uint32_t)dataLen + headroom > PBUF_MAX_PAYLOAD_LEN) {
        DP_ADD_ABN_STAT(DP_PBUF_BUILD_PARAM_ERR);
        return NULL;
    }

    return PBUF_Build(data, dataLen, headroom);
}

Pbuf_t* PBUF_Splice(Pbuf_t* pbuf, uint16_t spliceLen, uint16_t headroom)
{
    uint16_t curLen = spliceLen;
    Pbuf_t*  cur    = pbuf;
    Pbuf_t*  newBuf;
    uint8_t  nsegs = 1; // 记录分片数量

    ASSERT(pbuf->totLen > spliceLen);

    while (curLen > cur->segLen) {
        curLen -= cur->segLen;
        cur = cur->next;
        nsegs++;
    }

    // 申请新的分片
    if (curLen < cur->segLen && curLen > 0) {
        newBuf = PBUF_Build(PBUF_MTOD(cur, uint8_t*) + curLen, cur->segLen - curLen, headroom);
        if (newBuf == NULL) {
            return NULL;
        }

        cur->segLen  = curLen;
        newBuf->end->next = cur->next;
        cur->next    = newBuf;
        pbuf->end    = (cur == pbuf->end) ? newBuf->end : pbuf->end;
        pbuf->nsegs += newBuf->nsegs;
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
    if (pbuf == NULL) {
        return;
    }
    Pbuf_t* cur  = pbuf->next; // 从clean下一报文开始释放，调用点保证后续报文seglen均为0
    pbuf->next = NULL;

    if (cur != NULL) {
        PBUF_Free(cur);
    }
}

uint16_t PBUF_CutTailData(Pbuf_t* pbuf, uint16_t len)
{
    Pbuf_t*  cur;
    Pbuf_t*  clean;
    uint32_t totLen;
    uint32_t newTotLen;

    ASSERT(pbuf != NULL);
    ASSERT(pbuf->totLen >= len);

    if (pbuf->nsegs == 1) {
        pbuf->totLen -= len;
        pbuf->segLen -= len;

        return len;
    }

    totLen    = 0;
    newTotLen = pbuf->totLen - len;
    cur       = pbuf;
    clean     = NULL;

    while (cur != NULL) {
        if (totLen < newTotLen) {
            totLen += cur->segLen;
            if (totLen > newTotLen) {
                uint16_t dupLen = (uint16_t)(totLen - newTotLen); // 此处差值为当前cur报文多出的长度，不会超过segLen

                cur->segLen -= dupLen;
                totLen = newTotLen;
            }
            clean = cur;     // 记录totLen刚刚满足newTotLen的cur位置，用于后续释放cur后的节点
        } else {
            cur->segLen = 0; // 满足totLen == newTotLen后，clean后续链表节点的segLen都置0
            pbuf->nsegs--;
        }
        cur = cur->next;
    }
    pbuf->end = clean;
    PbufCleanUnusedSegs(clean);

    pbuf->totLen = newTotLen;

    return len;
}

void PbufReset(Pbuf_t* pbuf)
{
    ASSERT(pbuf != NULL);

    Pbuf_t* cur;

    pbuf->totLen  = 0;
    pbuf->segLen  = 0;
    pbuf->flags   = 0;
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

    size_t  ret     = 0;
    Pbuf_t* end     = chain->tail;
    size_t  towrite = len;

    if (end != NULL && (uint16_t)PBUF_GET_PKT_LEN(end) < fragSize) {
        // end 实际数据段长度可能小于 fragSize, 或者大于等于 fragSize
        ret = PBUF_Fill(end, data, (uint16_t)len, fragSize);
        towrite -= ret;
        chain->bufLen += ret;
    }

    while (towrite > 0) {
        uint16_t appendLen;

        end = PBUF_Alloc(headroom, fragSize);
        if (end == NULL) {
            break;
        }

        appendLen = (fragSize > towrite) ? (uint16_t)towrite : fragSize;
        PBUF_Fill(end, data + ret, appendLen, appendLen);

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
        if (written <= 0) { // 内存申请失败
            return ret;
        }

        ret += written;
        cur = PBUF_NEXT_SEG(cur);
    }

    return ret;
}

size_t PBUF_ChainRead(PBUF_Chain_t* chain, uint8_t* buf, size_t len, int peek, int resetData)
{
    size_t  ret = 0;
    Pbuf_t* cur = chain->head;

    (void)resetData;
    while (cur != NULL && ret < len) {
        Pbuf_t*  next;
        uint16_t bufLen =
            (len - ret > 0xFFFF) ? 0xFFFF : (uint16_t)(len - ret); // 如果长度超过uint16(即0xFFFF)，则取最大值
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
        cur          = cur->chainNext;
        PBUF_Free(prev);
    }

    chain->head   = NULL;
    chain->tail   = NULL;
    chain->pktCnt = 0;
    chain->bufLen = 0;
}

static void PbufFreeCb(void* addr, void* pbuf)
{
    (void)addr;
    DP_ZcopyMemCntSub(DP_PBUF_GET_WID((Pbuf_t*)pbuf), ((Pbuf_t*)pbuf)->payloadLen, DP_MEM_ZCOPY_RECV);
    PBUF_Free(((Pbuf_t*)pbuf)->head);
}

size_t PBUF_ChainReadZcopy(PBUF_Chain_t* chain, struct DP_ZIovec* iov)
{
    size_t  ret  = 0;
    Pbuf_t* pbuf = chain->head;
    Pbuf_t* cur  = pbuf;

    if (pbuf == NULL) {
        ASSERT(chain->bufLen == 0);
        ASSERT(chain->pktCnt == 0);
        chain->head = NULL;
        chain->tail = NULL;
        return ret;
    }

    while (cur != NULL) {
        if (cur->segLen == 0) {
            cur = cur->next;
            continue;
        }

        iov->iov_base = PBUF_MTOD(cur, void*);
        iov->iov_len  = cur->segLen;
        iov->cb       = (void*)cur;
        iov->freeCb   = PbufFreeCb;
        cur->head     = pbuf; // 在 pbuf 首部中记录 head 以便释放
        pbuf->ref++;

        DP_ZcopyMemCntAdd(DP_PBUF_GET_WID(cur), cur->payloadLen, DP_MEM_ZCOPY_RECV);

        ret = iov->iov_len;
        cur->offset += cur->segLen;
        cur->segLen = 0;
        break;
    }

    pbuf->totLen -= (uint32_t)ret; // ret小于pbuf->totLen，此处转换无风险
    if (pbuf->totLen == 0) {
        chain->head = pbuf->chainNext;
        chain->pktCnt--;
        if (chain->head != NULL) {
            chain->head->chainPrev = NULL;
        } else {
            chain->tail = NULL;
        }
        PBUF_Free(pbuf);
    }

    chain->bufLen -= ret;

    return ret;
}

Pbuf_t* PBUF_BuildZcopy(Pbuf_t* pbuf, uint32_t dataLen, uint32_t segNum, uint16_t fragSize)
{
    ASSERT(pbuf != NULL);
    ASSERT(dataLen > 0);
    ASSERT(segNum > 0);

    void*        ebuf   = NULL;
    Pbuf_t*      ret    = NULL;
    Pbuf_t*      temp   = NULL;
    ssize_t      remain = (dataLen > PBUF_GET_PKT_LEN(pbuf) ? PBUF_GET_PKT_LEN(pbuf) : dataLen);
    ssize_t      totLen = 0;
    uint64_t     offset = DP_PBUF_GET_OFFSET(pbuf);
    uint16_t     segLen;
    RefPbufCb_t* refPbufCb = NULL;

    // 从 pbuf 中获取 extern buffer 的指针
    refPbufCb = PBUF_GetRefPbufCb(pbuf);
    ebuf      = refPbufCb->ebuf;

    while (remain > 0) {
        if (ret != NULL && PBUF_GET_SEGS(ret) >= segNum) {
            break;
        }

        segLen = (fragSize > remain) ? (uint16_t)remain : fragSize;

        temp = PBUF_Construct(ebuf, offset + totLen + (pbuf->payload - (uint8_t*)ebuf), segLen);
        if (temp == NULL) {
            break;
        }

        if (ret == NULL) {
            ret = temp;
        } else {
            PBUF_Concat(ret, temp);
        }

        remain -= segLen;
        totLen = PBUF_GET_PKT_LEN(ret);
    }

    if (ret == NULL) {
        return NULL;
    }

    pbuf->segLen -= (uint16_t)totLen; // totLen小于pbuf->segLen，这里三处转换均无风险
    pbuf->totLen -= (uint32_t)totLen;
    pbuf->offset += (uint16_t)totLen;

    return ret;
}

int Pbuf_Zcopy_Alloc(Pbuf_t** pbuf)
{
    Pbuf_t*  head = NULL;
    uint16_t headroom = 128;    // pbuf的headroom默认为128
    // 申请一个 pbuf 作为头部，zero copy 场景下发送队列中的 pbuf 都是间接的，不带有 headroom
    head = PBUF_Alloc(headroom, 0);
    if (head == NULL) {
        return -1;
    }

    PBUF_Concat(head, *pbuf);
    *pbuf = head;
    return 0;
}