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

#include "dp_tcp.h"

#include "tcp_types.h"
#include "tcp_sock.h"
#include "tcp_timer.h"
#include "tcp_cookie.h"
#include "tcp_sack.h"
#include "tcp_cc.h"
#include "tcp_out.h"

#include "pbuf.h"
#include "pmgr.h"
#include "utils_base.h"
#include "utils_cksum.h"
#include "utils_debug.h"

static inline uint8_t TcpCalcWs(uint32_t win)
{
    uint8_t i = 0;
    uint32_t temp = win;

    while (temp > (0xFFFF >> 1)) {
        i++;
        temp >>= 1;
    }

    return i > 14 ? 14 : i; // 14: tcp 的窗口缩放因子最大为 14
}

static inline int TcpAddTstampAppa(TcpSk_t* tcp, uint8_t* opts)
{
    *opts++                = DP_TCPOPT_NOP;
    *opts++                = DP_TCPOPT_NOP;
    *opts++                = DP_TCPOPT_TIMESTAMP;
    *opts++                = DP_TCPOLEN_TIMESTAMP;
    tcp->tsVal = TcpGetRttTick(tcp);
    *(uint32_t*)opts       = UTILS_HTONL(tcp->tsVal);
    *(uint32_t*)(opts + 4) = UTILS_HTONL(tcp->tsEcho); // 4: 后四位赋值

    return DP_TCPOLEN_TSTAMP_APPA;
}

static inline uint8_t TcpAddTstamp(TcpSk_t* tcp, uint8_t* opts)
{
    *opts++                = DP_TCPOPT_TIMESTAMP;
    *opts++                = DP_TCPOLEN_TIMESTAMP;
    tcp->tsVal = TcpGetRttTick(tcp);
    *(uint32_t*)opts       = UTILS_HTONL(tcp->tsVal);
    *(uint32_t*)(opts + 4) = UTILS_HTONL(tcp->tsEcho); // 4: 后四位赋值
    return DP_TCPOLEN_TIMESTAMP;
}

static uint8_t TcpAddSackOpt(TcpSk_t* tcp, uint8_t* opts, uint8_t optLen)
{
    int sackNum = 0;
    int remainLen = optLen;
    TcpSackBlock_t sackBlock;

    *opts++ = DP_TCPOPT_SACK;
    uint8_t* sackOptLen = opts;
    *opts++ = TCP_OPTLEN_BASE_SACK;
    remainLen -= TCP_OPTLEN_BASE_SACK;
    // opt长度的判断能够保证sackNum不会超出sackBlock数组大小范围
    while (tcp->sackInfo->sackBlockNum > sackNum) {      // sackBlock中还有内容未填写
        if (remainLen < TCP_OPTLEN_SACK_PERBLOCK) {      // opt中还有位置填
            break;
        }
        sackBlock = tcp->sackInfo->sackBlock[sackNum++];
        *(uint32_t*)opts       = UTILS_HTONL(sackBlock.seqStart);
        *(uint32_t*)(opts + 4) = UTILS_HTONL(sackBlock.seqEnd); // 4: 后四位赋值

        opts += TCP_OPTLEN_SACK_PERBLOCK;
        *sackOptLen += TCP_OPTLEN_SACK_PERBLOCK;
        remainLen -= TCP_OPTLEN_SACK_PERBLOCK;
    }
    return *sackOptLen;
}

uint16_t TcpCalcTxCksum(uint32_t pseudoHdrCksum, Pbuf_t* pbuf)
{
    uint32_t  cksum;
    Netdev_t* dev = PBUF_GET_DEV(pbuf);
    uint16_t  len = (uint16_t)PBUF_GET_PKT_LEN(pbuf);

    ASSERT(dev != NULL);

    if (NETDEV_TX_TCP_CKSUM_ENABLED(dev)) {
        DP_PBUF_SET_OLFLAGS_BIT(pbuf, DP_PBUF_OLFLAGS_TX_TCP_CKSUM);

        if (NETDEV_TX_L4_CKSUM_PARTIAL(dev)) {
            if ((DP_PBUF_GET_OLFLAGS(pbuf) & DP_PBUF_OLFLAGS_TX_TSO) != 0) {
                // 开启 TSO 下，伪校验和不能包括报文长度
                len = 0;
            }
            cksum = pseudoHdrCksum + UTILS_HTONS(len);
            return UTILS_CksumAdd(cksum);
        }

        return 0;
    }

    cksum = pseudoHdrCksum + UTILS_HTONS(len);
    cksum += PBUF_CalcCksum(pbuf);

    return UTILS_CksumSwap(cksum);
}

static void TcpFillHdr(TcpSk_t* tcp, Pbuf_t* pbuf, uint8_t thFlags, int optLen)
{
    DP_TcpHdr_t* tcpHdr;
    uint32_t     rcvWnd;
    uint8_t      l4Len;

    rcvWnd = tcp->rcvWnd >> tcp->rcvWs;
    rcvWnd = rcvWnd > 0xFFFF ? 0xFFFF : rcvWnd;

    l4Len = (uint8_t)((uint32_t)optLen + sizeof(*tcpHdr));

    PBUF_PUT_HEAD(pbuf, sizeof(DP_TcpHdr_t));
    PBUF_SET_L4_OFF(pbuf);
    PBUF_SET_L4_LEN(pbuf, l4Len);

    tcpHdr         = PBUF_MTOD(pbuf, DP_TcpHdr_t*);
    tcpHdr->sport  = TcpGetLport(tcp);
    tcpHdr->dport  = TcpGetPport(tcp);
    tcpHdr->seq    = UTILS_HTONL(tcp->sndNxt);
    tcpHdr->ack    = UTILS_HTONL(tcp->rcvNxt);
    tcpHdr->off    = l4Len >> 2; // 2: TCP头部的长度是以32位（4字节）为单位，此处除以4
    tcpHdr->resv   = 0;
    tcpHdr->flags  = thFlags;
    tcpHdr->win    = (uint16_t)UTILS_HTONS(rcvWnd);
    tcpHdr->chksum = 0;
    tcpHdr->urg    = 0;

    tcpHdr->chksum = TcpCalcTxCksum(tcp->pseudoHdrCksum, pbuf);

    tcp->accDataCnt  = 0; // 清理累计数据计数
}

static int TcpAddEstablishOpt(TcpSk_t* tcp, Pbuf_t* pbuf)
{
    uint8_t  establishOpts[DP_TCPOLEN_MAX];
    uint8_t* opts = establishOpts;
    uint8_t  optLen = 0;

    // 当前establish状态下仅支持ts和sack，如果只有ts，则使用时间戳快处理填写
    if (TcpNegTs(tcp) && (!TCP_SACK_AVAILABLE(tcp) || tcp->sackInfo->sackBlockNum == 0)) {
        PBUF_PUT_HEAD(pbuf, DP_TCPOLEN_TSTAMP_APPA);
        opts = PBUF_MTOD(pbuf, uint8_t*);
        return TcpAddTstampAppa(tcp, opts);
    }

    // 写入时间戳信息
    if (TcpNegTs(tcp)) {
        optLen += TcpAddTstamp(tcp, opts);
        opts += DP_TCPOLEN_TIMESTAMP;
    }

    // 写入sack块
    if (TCP_SACK_AVAILABLE(tcp) && tcp->sackInfo->sackBlockNum != 0) {
        uint8_t sackLen = TcpAddSackOpt(tcp, opts, (DP_TCPOLEN_MAX - optLen)); // 此处相减不会导致溢出
        optLen += sackLen;
        opts += sackLen;
    }

    uint8_t padLen = (4 - (optLen & 0x3)) & 0x3;         // 3: 选项4字节对齐需要在后面补充NOP选项，&3 求和4的余数
    while (padLen > 0) {
        *opts++ = DP_TCPOPT_NOP;
        padLen--;
    }

    optLen = opts - establishOpts;
    PBUF_PUT_HEAD(pbuf, (uint16_t)optLen);
    opts = PBUF_MTOD(pbuf, uint8_t*);
    (void)memcpy_s(opts, optLen * sizeof(uint8_t), establishOpts, optLen * sizeof(uint8_t));

    return (int)optLen;
}

static int TcpAddSynOpt(TcpSk_t* tcp, uint8_t sndOptions, Pbuf_t* pbuf)
{
    uint8_t  tcpOpts[DP_TCPOLEN_MAX];
    uint8_t* opts = tcpOpts;
    uint8_t  optLen;

    if ((sndOptions & TCP_SYN_OPT_MSS) != 0) {
        *opts++          = DP_TCPOPT_MAXSEG;
        *opts++          = DP_TCPOLEN_MAXSEG;
        *(uint16_t*)opts = UTILS_HTONS(tcp->mss);
        opts += 2; // 2: 移动到下一个TCP选项位置
    }

    if ((sndOptions & TCP_SYN_OPT_WINDOW) != 0) {
        *opts++ = DP_TCPOPT_WINDOW;
        *opts++ = DP_TCPOLEN_WINDOW;
        *opts++ = tcp->rcvWs;
        *opts++ = DP_TCPOPT_NOP; // 补齐一个字节
    }

    if ((sndOptions & TCP_SYN_OPT_SACK_PERMITTED) != 0) {
        *opts++ = DP_TCPOPT_SACK_PERMITTED;
        *opts++ = DP_TCPOLEN_SACK_PERMITTED;
    }

    if ((sndOptions & TCP_SYN_OPT_TIMESTAMP) != 0) {
        tcp->tsVal = TcpGetRttTick(tcp);
        uint32_t tsVal = UTILS_HTONL(tcp->tsVal);
        uint32_t echo = UTILS_HTONL(tcp->tsEcho);
        *opts++        = DP_TCPOPT_TIMESTAMP;
        *opts++        = DP_TCPOLEN_TIMESTAMP;

        UTILS_LONG2BYTE(opts, tsVal);
        UTILS_LONG2BYTE(opts + 4, echo); // 4: 为后四个字节赋值
        opts += 8; // 8: 移动到下一个TCP选项位置
    }

    optLen = (uint8_t)(opts - tcpOpts);

    uint8_t padLen = (4 - (optLen & 0x3)) & 0x3;         // 3: 选项4字节对齐需要在后面补充NOP选项，&3 求和4的余数
    while (padLen > 0) {
        *opts++ = DP_TCPOPT_NOP;
        padLen--;
    }

    optLen = (uint8_t)(opts - tcpOpts);

    PBUF_PUT_HEAD(pbuf, optLen);
    opts = PBUF_MTOD(pbuf, uint8_t*);

    (void)memcpy_s(opts, optLen * sizeof(uint8_t), tcpOpts, optLen * sizeof(uint8_t));

    return (int)optLen;
}

static int TcpAddSynCookieOpt(Pbuf_t* pbuf, TcpSynOpts_t* synOpts, uint16_t ws, uint8_t hasOpt)
{
    uint8_t  tcpOpts[DP_TCPOLEN_MAX] = {0};
    uint8_t* opts = tcpOpts;
    uint8_t  optLen;

    if ((hasOpt & synOpts->rcvSynOpt & TCP_SYN_OPT_MSS) != 0) {
        *opts++          = DP_TCPOPT_MAXSEG;
        *opts++          = DP_TCPOLEN_MAXSEG;
        *(uint16_t*)opts = UTILS_HTONS(synOpts->mss);
        opts += 2; // 2: 移动到下一个TCP选项位置
    }

    if ((hasOpt & synOpts->rcvSynOpt & TCP_SYN_OPT_WINDOW) != 0) {
        *opts++ = DP_TCPOPT_WINDOW;
        *opts++ = DP_TCPOLEN_WINDOW;
        *opts++ = (uint8_t)ws;
        *opts++ = DP_TCPOPT_NOP; // 补齐一个字节
    }

    if ((hasOpt & synOpts->rcvSynOpt & TCP_SYN_OPT_SACK_PERMITTED) != 0) {
        *opts++ = DP_TCPOPT_SACK_PERMITTED;
        *opts++ = DP_TCPOLEN_SACK_PERMITTED;
    }

    if ((hasOpt & synOpts->rcvSynOpt & TCP_SYN_OPT_TIMESTAMP) != 0) {
        *opts++ = DP_TCPOPT_TIMESTAMP;
        *opts++ = DP_TCPOLEN_TIMESTAMP;

        uint32_t tsVal = UTILS_HTONL(TcpCookieGenTsval(synOpts, hasOpt));
        uint32_t echo = UTILS_HTONL(synOpts->tsVal);
        UTILS_LONG2BYTE(opts, tsVal);
        UTILS_LONG2BYTE(opts + 4, echo); // 4: 为后四个字节赋值
        opts += 8; // 8: 移动到下一个TCP选项位置

        *opts++ = DP_TCPOPT_NOP;
        *opts++ = DP_TCPOPT_NOP; // 补齐两个字节
    }
    optLen = (uint32_t)(opts - tcpOpts + 3) & 0xFC; // 3 : 4字节对齐
    PBUF_PUT_HEAD(pbuf, optLen);
    opts = PBUF_MTOD(pbuf, uint8_t*);

    (void)memcpy_s(opts, optLen * sizeof(uint8_t), tcpOpts, optLen * sizeof(uint8_t));

    return (int)optLen;
}

static int TcpGetXmitInfo(TcpSk_t* tcp, TcpXmitInfo_t* info)
{
    if (TcpSk2Sk(tcp)->family == DP_AF_INET) {
        return g_tcpInetOps->getXmitInfo(TcpSk2Sk(tcp), info);
    }
    return g_tcpInet6Ops->getXmitInfo(TcpSk2Sk(tcp), info);
}

static inline bool TcpIsTsoSend(TcpSk_t* tcp, uint32_t dataLen, uint16_t mss)
{
    (void)tcp;
    return dataLen > mss;
}

static void TcpSetXmitPbuf(TcpSk_t* tcp, Pbuf_t* pbuf, TcpXmitInfo_t* xmitInfo)
{
    DP_PBUF_SET_OLFLAGS(pbuf, 0);
    PBUF_SET_L4_TYPE(pbuf, DP_IPPROTO_TCP);
    PBUF_SET_ENTRY(pbuf, xmitInfo->pentry);
    PBUF_SET_QUE_ID(pbuf, (uint8_t)tcp->txQueid);
    PBUF_SET_WID(pbuf, (uint8_t)tcp->wid);
    PBUF_SET_PKT_TYPE(pbuf, PBUF_PKTTYPE_HOST);
    PBUF_SET_DEV(pbuf, xmitInfo->dev);
    PBUF_SET_FLOW(pbuf, xmitInfo->flow);

    if (TcpIsTsoSend(tcp, PBUF_GET_PKT_LEN(pbuf), xmitInfo->mss)) {
        DP_PBUF_SET_OLFLAGS_BIT(pbuf, DP_PBUF_OLFLAGS_TX_TSO);
        DP_PBUF_SET_TSO_FRAG_SIZE(pbuf, xmitInfo->mss);
    }
}

static inline void TcpUpdateRcvWnd(TcpSk_t* tcp)
{
    tcp->rcvWnd = TcpSelectRcvWnd(tcp);
    tcp->rcvWup = tcp->rcvNxt;
}

Pbuf_t* TcpGenCtrlPkt(TcpSk_t* tcp, uint8_t thflags, int upWnd)
{
    Pbuf_t* ret;
    int     optLen;
    TcpXmitInfo_t xmitInfo;

    if (TcpGetXmitInfo(tcp, &xmitInfo) != 0) {
        return NULL;
    }

    ret = PBUF_Alloc(IP_INET_MAX_HDR_LEN, 0);
    if (ret == NULL) {
        return NULL;
    }

    TcpSetXmitPbuf(tcp, ret, &xmitInfo);

    if ((thflags & DP_TH_SYN) != 0) {
        optLen = TcpAddSynOpt(tcp, tcp->negOpt, ret);
    } else {
        optLen = TcpAddEstablishOpt(tcp, ret);
    }

    if (upWnd != 0) {
        TcpUpdateRcvWnd(tcp);
    }

    TcpFillHdr(tcp, ret, thflags, optLen);

    if ((thflags & (DP_TH_FIN | DP_TH_SYN)) != 0) {
        if (tcp->sndNxt == tcp->sndMax) { // 顺序报文才需要+1
            tcp->sndMax += 1;
        }
        tcp->sndNxt += 1;
    }

    if ((thflags & (DP_TH_ACK)) != 0) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_ACKS);
    }

    if ((thflags & (DP_TH_RST)) != 0) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_RST);
    }

    DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_CONTROL);
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_TOTAL);

    if ((thflags & (DP_TH_ACK)) != 0 && TCP_IS_IN_DELAY(tcp)) {
        TcpDeactiveDelayAckTimer(tcp);
    }

    return ret;
}

Pbuf_t* TcpGenCookieSynAckPktByPkt(Pbuf_t* pbuf, TcpSk_t* parent, TcpPktInfo_t* pi, TcpSynOpts_t* opts,
                                   TcpCookieInetHashInfo_t* info)
{
    DP_TcpHdr_t* origTcpHdr = (DP_TcpHdr_t *)PBUF_GET_L4_HDR(pbuf);

    Pbuf_t* ret = PBUF_Alloc(IP_INET_MAX_HDR_LEN, 0);
    if (ret == NULL) {
        return NULL;
    }
    uint32_t rcvWnd = TcpSk2Sk(parent)->rcvHiwat;
    uint16_t rcvWs = TcpCalcWs(rcvWnd);
    rcvWnd >>= rcvWs;
    rcvWnd = rcvWnd > 0xFFFF ? 0xFFFF : rcvWnd;

    int optLen = TcpAddSynCookieOpt(ret, opts, rcvWs, parent->synOpt);

    PBUF_PUT_HEAD(ret, sizeof(DP_TcpHdr_t));

    DP_TcpHdr_t* tcpHdr = PBUF_MTOD(ret, DP_TcpHdr_t*);
    tcpHdr->sport  = origTcpHdr->dport;
    tcpHdr->dport  = origTcpHdr->sport;
    tcpHdr->seq    = UTILS_HTONL(info->iss);
    tcpHdr->ack    = UTILS_HTONL(pi->endSeq);
    tcpHdr->off    = (uint8_t)(((uint32_t)optLen + sizeof(*tcpHdr)) >> 2); // 2: TCP头部的长度是以32位（4字节）为单位，此处除以4
    tcpHdr->resv   = 0;
    tcpHdr->flags  = DP_TH_SYN | DP_TH_ACK;
    tcpHdr->win    = UTILS_HTONS((uint16_t)rcvWnd);
    tcpHdr->chksum = 0;
    tcpHdr->urg    = 0;

    return ret;
}

Pbuf_t* TcpGenRstPktByPkt(DP_TcpHdr_t* origTcpHdr, TcpPktInfo_t* pi)
{
    Pbuf_t* ret = PBUF_Alloc(IP_INET_MAX_HDR_LEN, 0);
    if (ret == NULL) {
        return NULL;
    }

    PBUF_PUT_HEAD(ret, sizeof(DP_TcpHdr_t));

    DP_TcpHdr_t* tcpHdr = PBUF_MTOD(ret, DP_TcpHdr_t*);
    tcpHdr->sport  = origTcpHdr->dport;
    tcpHdr->dport  = origTcpHdr->sport;
    if ((origTcpHdr->flags & DP_TH_ACK) != 0) {
        tcpHdr->seq = origTcpHdr->ack;
        tcpHdr->flags  = DP_TH_RST;
    } else {
        tcpHdr->seq = 0;
        tcpHdr->flags  = DP_TH_RST | DP_TH_ACK;
    }
    tcpHdr->ack    = UTILS_HTONL(pi->endSeq);
    tcpHdr->off    = sizeof(DP_TcpHdr_t) >> 2; // 2: TCP头部的长度是以32位（4字节）为单位，此处除以4
    tcpHdr->resv   = 0;
    tcpHdr->win    = 0;
    tcpHdr->chksum = 0;
    tcpHdr->urg    = 0;

    return ret;
}

uint32_t TcpCalcFreeWndSize(TcpSk_t* tcp)
{
    uint32_t inflight;
    uint32_t swnd;
    uint32_t cwnd;
    uint32_t wndSize;

    ASSERT(TcpSeqGeq(tcp->sndMax, tcp->sndUna));
    inflight = tcp->sndMax - tcp->sndUna; // 在途数据

    swnd = (tcp->sndWnd >= inflight) ? (tcp->sndWnd - inflight) : 0; // 通告窗口的剩余空间
    cwnd = ((tcp->cwnd >= inflight)) ? (tcp->cwnd - inflight) : 0; // 拥塞窗口的剩余空间
    // 窗口大小取通告窗口和拥塞窗口的最小值
    wndSize = UTILS_MIN(swnd, cwnd);

    return wndSize;
}

static Pbuf_t* TcpTrySplicePbuf(TcpSk_t* tcp, Pbuf_t* pbuf, uint32_t maxSndLen, uint16_t mss)
{
    Pbuf_t* ret = pbuf;
    uint32_t pktLen = PBUF_GET_PKT_LEN(pbuf);
    uint32_t spliceLen;

    spliceLen = UTILS_MIN(pktLen, maxSndLen);
    if (spliceLen > mss) {
        // TSO 发送时，dataLen 必须为 mss 整数倍
        spliceLen = spliceLen - (spliceLen % mss);
    }

    if (spliceLen == pktLen) {
        return ret;
    }

    Pbuf_t* newBuf = PBUF_Splice(ret, (uint16_t)spliceLen, IP_INET_MAX_HDR_LEN);
    if (newBuf == NULL) {
        PBUF_ChainPushHead(&tcp->sndQue, ret);
        return NULL;
    }
    PBUF_ChainPushHead(&tcp->sndQue, newBuf);

    return ret;
}

static inline bool TcpCanMergePbuf(Pbuf_t* pbuf, uint32_t pktLen, uint32_t mergedLen, uint16_t mss)
{
    if (mergedLen < mss) {
        return true;
    }

    if (mergedLen + PBUF_GET_PKT_LEN(pbuf) > pktLen) {
        return false;
    }

    return true;
}

static Pbuf_t* TcpTryMergePbuf(TcpSk_t* tcp, uint32_t pktLen, uint16_t mss)
{
    Pbuf_t* ret = PBUF_CHAIN_POP(&tcp->sndQue);
    ASSERT(ret != NULL);
    uint32_t dataLen = PBUF_GET_PKT_LEN(ret);

    while (dataLen < pktLen && tcp->sndQue.pktCnt > 0) {
        if (!TcpCanMergePbuf(PBUF_CHAIN_FIRST(&tcp->sndQue), pktLen, dataLen, mss)) {
            break;
        }

        Pbuf_t* nxt = PBUF_CHAIN_POP(&tcp->sndQue);
        PBUF_Merge(ret, nxt, false);
        dataLen = PBUF_GET_PKT_LEN(ret);
    }
    return TcpTrySplicePbuf(tcp, ret, pktLen, mss);
}

bool TcpCanSendPbuf(TcpSk_t* tcp, uint32_t pktLen, uint16_t mss, int force)
{
    // 报文长度比 mss 大，立即发送
    if (pktLen >= mss) {
        return true;
    }

    // 断链情况下需要将报文尽可能发送出去
    if (tcp->state == TCP_FIN_WAIT1 || tcp->state == TCP_LAST_ACK) {
        return true;
    }

    // 开启了CORK选项且不强制发送小报文则一定不能发送小报文
    if ((tcp->cork) != 0 && (force == 0)) {
        return false;
    }

    // 关闭CORK选项且关闭Nagle算法一定能发送小报文
    if (tcp->nodelay != 0) {
        return true;
    }

    // 关闭CORK选项且开启Nagle算法，没有数据报文在途则可以发送
    if (tcp->sndUna == tcp->sndMax) {
        return true;
    }

    return false;
}

static void TcpTryActiveTimer(TcpSk_t* tcp)
{
    // 未发送数据场景
    if (tcp->sndUna == tcp->sndMax) {
        if (tcp->sndWnd == 0 && tcp->sndQue.bufLen > 0 && TCP_IS_FASTTIMER_IDLE(tcp)) {
            TcpActivePersistTimer(tcp);
        }
    } else if (TCP_IS_FASTTIMER_IDLE(tcp)) {
        TcpActiveRexmitTimer(tcp);
    }
}

static void TcpXmitPbuf(TcpSk_t* tcp, Pbuf_t* pbuf, uint8_t thflags, TcpXmitInfo_t* xmitInfo)
{
    int      optLen;
    uint16_t dataLen = (uint16_t)PBUF_GET_PKT_LEN(pbuf);
    uint16_t dataOff = PBUF_GET_HEADROOM(pbuf);

    PBUF_ChainPush(&tcp->rexmitQue, pbuf);

    TcpSetXmitPbuf(tcp, pbuf, xmitInfo);

    optLen = TcpAddEstablishOpt(tcp, pbuf);

    TcpFillHdr(tcp, pbuf, thflags, optLen);

    if ((thflags & DP_TH_FIN) != 0) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_FIN);
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_CONTROL);
        dataLen += 1;
    }

    tcp->sndMax += dataLen;
    tcp->sndNxt += dataLen;

    if (TCP_IS_IN_DELAY(tcp)) {
        TcpDeactiveDelayAckTimer(tcp);
    }

    PBUF_REF(pbuf);
    PMGR_Dispatch(pbuf);

    PBUF_SET_HEAD(pbuf, dataOff);
    DP_PBUF_SET_OLFLAGS(pbuf, 0);
}

void TcpXmitData(TcpSk_t *tcp, int force)
{
    /* 这个时候代表上次重传数据未重传全部数据，需要重传剩下的数据 */
    if (tcp->sndNxt != tcp->sndMax) {
        TcpRexmitQue(tcp);
        return;
    }

    uint32_t totalSndLen = UTILS_MIN((uint32_t)tcp->sndQue.bufLen, TcpCalcFreeWndSize(tcp));
    uint32_t snded = 0;
    uint8_t  thflags = DP_TH_ACK;
    Pbuf_t*  pbuf = NULL;
    TcpXmitInfo_t xmitInfo;

    if (TcpGetXmitInfo(tcp, &xmitInfo) != 0) {
        return;
    }

    TcpUpdateRcvWnd(tcp);

    // 有数据才能发送
    while (totalSndLen > 0 && tcp->sndQue.pktCnt > 0) {
        uint32_t pktLen = UTILS_MIN(xmitInfo.tsoSize, totalSndLen);
        // 判断能否发送，不能发送就没必要聚合。tcp发送缓冲区高水位类型为uint32_t，这里转换后不会截断
        if (!TcpCanSendPbuf(tcp, pktLen, xmitInfo.mss, force)) {
            break;
        }

        // 从发送队列中尝试聚合一个 pbuf
        pbuf = TcpTryMergePbuf(tcp, pktLen, xmitInfo.mss);
        if (pbuf == NULL) {
            return;
        }

        // 已发送完全部数据，这个报文是缓冲区的最后一个报文
        if (tcp->sndQue.pktCnt == 0) {
            thflags |= DP_TH_PUSH;
            if (tcp->state == TCP_FIN_WAIT1 || tcp->state == TCP_LAST_ACK) {
                thflags |= DP_TH_FIN;
            }
        }

        // 发送报文
        totalSndLen -= PBUF_GET_PKT_LEN(pbuf);
        snded += PBUF_GET_PKT_LEN(pbuf);
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_PACKET);
        DP_ADD_TCP_STAT(tcp->wid, DP_TCP_SND_BYTE, PBUF_GET_PKT_LEN(pbuf));
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_TOTAL);
        DP_INC_PKT_STAT(tcp->wid, DP_PKT_SEND_BUF_IN);
        TcpXmitPbuf(tcp, pbuf, thflags, &xmitInfo);
    }

    TcpTryActiveTimer(tcp);

    // 当没有发送数据报文且需要强制发送报文时，需要发送一个 Ack 报文，窗口更新场景需要
    if (snded == 0 && force != 0) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_WND_UPDATE);
        TcpXmitAckPkt(tcp);
    }
}

void TcpXmitCtrlPkt(TcpSk_t* tcp, uint8_t thflags)
{
    Pbuf_t* pbuf;

    // 启动坚持定时器代表对端窗口为0，此时不能发送FIN报文
    if (TCP_IS_IN_PERSIST(tcp) && (thflags & DP_TH_FIN) != 0) {
        return;
    }

    pbuf = TcpGenCtrlPkt(tcp, thflags, 1);
    if (pbuf == NULL) {
        return;
    }

    PMGR_Dispatch(pbuf);

    if ((thflags & (DP_TH_FIN | DP_TH_SYN)) != 0 && TCP_IS_FASTTIMER_IDLE(tcp)) {
        TcpActiveRexmitTimer(tcp);
    }
}

static void TcpRexmitCtrlPkt(TcpSk_t* tcp)
{
    uint32_t thFlags = 0;
    switch (TcpState(tcp)) {
        case TCP_SYN_SENT:
            thFlags = DP_TH_SYN;
            break;
        case TCP_SYN_RECV:
            thFlags = DP_TH_SYN | DP_TH_ACK;
            break;
        case TCP_CLOSING:
        case TCP_FIN_WAIT1:
        case TCP_LAST_ACK:
            thFlags = DP_TH_FIN | DP_TH_ACK;
            break;
        case TCP_CLOSED:
            thFlags = DP_TH_RST | DP_TH_ACK;
            break;
        default:
            thFlags = DP_TH_ACK;
    }
    TcpXmitCtrlPkt(tcp, (uint8_t)thFlags);
}

static void TcpRexmitPbuf(TcpSk_t* tcp, Pbuf_t* pbuf, TcpXmitInfo_t* xmitInfo)
{
    DP_TcpHdr_t *tcpHdr = (DP_TcpHdr_t *)PBUF_GET_L4_HDR(pbuf);
    uint8_t thFlags = tcpHdr->flags;
    uint16_t dataLen = (uint16_t)PBUF_GET_PKT_LEN(pbuf);
    uint16_t dataOff = PBUF_GET_HEADROOM(pbuf);

    TcpSetXmitPbuf(tcp, pbuf, xmitInfo);

    int optLen = TcpAddEstablishOpt(tcp, pbuf);

    TcpFillHdr(tcp, pbuf, thFlags, optLen);

    if ((thFlags & DP_TH_FIN) != 0) {
        dataLen += 1;
    }

    tcp->sndNxt += dataLen;

    DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_REXMT_PACKET);
    DP_ADD_TCP_STAT(tcp->wid, DP_TCP_SND_REXMT_BYTE, dataLen);
    PBUF_REF(pbuf);
    PMGR_Dispatch(pbuf);

    PBUF_SET_HEAD(pbuf, dataOff);
    DP_PBUF_SET_OLFLAGS(pbuf, 0);
}

static Pbuf_t* TcpRexmitQueSplit(TcpSk_t* tcp, Pbuf_t* pbuf, uint16_t len)
{
    Pbuf_t* ret;
    Pbuf_t* curSeg;
    uint16_t remain = len;
    uint16_t appendLen;

    ASSERT(len < pbuf->totLen);

    ret = PBUF_Alloc(pbuf->l4Off + sizeof(DP_TcpHdr_t), len);
    if (ret == NULL) {
        return NULL;
    }

    curSeg = pbuf;
    while (curSeg != NULL && remain > 0) {
        appendLen = PBUF_GET_SEG_LEN(curSeg) > remain ? remain : PBUF_GET_SEG_LEN(curSeg);
        PBUF_Append(ret, PBUF_MTOD(curSeg, uint8_t*), appendLen);
        remain -= appendLen;
        curSeg = curSeg->next;
    }

    PBUF_CUT_DATA(pbuf, len);

    tcp->rexmitQue.bufLen -= PBUF_GET_PKT_LEN(ret);
    PBUF_ChainInsertBefore(&tcp->rexmitQue, pbuf, ret);

    ret->l4Off = pbuf->l4Off;
    ((DP_TcpHdr_t *)PBUF_GET_L4_HDR(ret))->flags = ((DP_TcpHdr_t *)PBUF_GET_L4_HDR(pbuf))->flags;

    return ret;
}

void TcpRexmitQue(TcpSk_t* tcp)
{
    uint16_t     pktLen;
    uint32_t     sndWin;
    Pbuf_t *pbuf = tcp->rtxHead;
    TcpXmitInfo_t xmitInfo;

    if (TcpGetXmitInfo(tcp, &xmitInfo) != 0) {
        return;
    }

    sndWin = tcp->sndWnd - (tcp->sndNxt - tcp->sndUna);
    sndWin = (tcp->cwnd > sndWin) ? sndWin : tcp->cwnd;

    TcpUpdateRcvWnd(tcp);

    while (pbuf != NULL) {
        pktLen = (uint16_t)PBUF_GET_PKT_LEN(pbuf);
        if (pktLen > xmitInfo.mss) {
            // TSO 使能时，重传队列中 pbuf 长度可能会大于 mss ，此时从 pbuf 中分割一个 mss 大小报文
            pbuf = TcpRexmitQueSplit(tcp, pbuf, xmitInfo.mss);
            pktLen = (uint16_t)PBUF_GET_PKT_LEN(pbuf);
        }

        if (pktLen > sndWin) {
            break;
        }

        TcpRexmitPbuf(tcp, pbuf, &xmitInfo);

        sndWin -= pktLen;

        pbuf = PBUF_CHAIN_NEXT(pbuf);
    }

    tcp->rtxHead = pbuf;
}

void TcpRexmitPkt(TcpSk_t* tcp)
{
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_REXMT_TIMEOUT);
    tcp->rtxHead = PBUF_CHAIN_FIRST(&tcp->rexmitQue);
    tcp->sndNxt = tcp->sndUna;
    if (tcp->rexmitQue.pktCnt > 0) {
        if (TCP_SACK_AVAILABLE(tcp)) {
            TcpClearSackHole(&tcp->sackInfo->sackHoleHead);
        }
        TcpRexmitQue(tcp);
    } else {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_REXMT_PACKET);
        TcpRexmitCtrlPkt(tcp);
    }
}

void TcpFastRexmitPkt(TcpSk_t* tcp)
{
    if (TCP_SACK_AVAILABLE(tcp)) {
        (void)TcpFastRexmitSack(tcp);
        return;
    }
    DP_Pbuf_t* oldHead = tcp->rtxHead;
    tcp->rtxHead = PBUF_CHAIN_FIRST(&tcp->rexmitQue);
    uint32_t temp = tcp->sndNxt;
    uint32_t oldCwnd = tcp->cwnd;
    tcp->sndNxt = tcp->sndUna;
    /* 快恢复阶段一次最多只能重传一个MSS的报文 */
    tcp->cwnd = tcp->mss;
    TcpRexmitQue(tcp);
    tcp->cwnd = oldCwnd;
    tcp->sndNxt = temp;
    tcp->rtxHead = oldHead;
}

void TcpFastRecoryPkt(TcpSk_t* tcp)
{
    if (TCP_SACK_AVAILABLE(tcp)) {
        if (TcpFastRexmitSack(tcp) == 0) {
            return;
        }
    }
    // 不支持SACK 或 无SACK空洞可以重传
    if (tcp->sndQue.bufLen > 0) {
        TcpTsqAddQue(tcp, TCP_TSQ_SEND_DATA);
    }
}

int TcpFastRexmitSack(TcpSk_t* tcp)     // 返回是否有sack空洞需要重传
{
    TcpXmitInfo_t xmitInfo;
    uint32_t sndSeq;

    if (TcpGetXmitInfo(tcp, &xmitInfo) != 0) {
        return -1;
    }

    Pbuf_t* pbuf = TcpGetRexmitSack(tcp, &sndSeq);
    if (pbuf == NULL) {
        return -1;
    }

    uint32_t temp = tcp->sndNxt;
    tcp->sndNxt = sndSeq;
    TcpRexmitPbuf(tcp, pbuf, &xmitInfo);
    tcp->sndNxt = temp;
    return 0;
}

void TcpXmitZeroWndProbePkt(TcpSk_t* tcp)
{
    if (tcp->backoff == 1) {
        // 首次发送需要发送窗口大小为1的报文
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_PROBE);
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_TOTAL);
        tcp->sndWnd = 1;
        TcpXmitData(tcp, 1);
        tcp->sndWnd = 0;
    } else {
        // 其余场景下触发重传即可
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_PROBE);
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_TOTAL);
        tcp->rtxHead = PBUF_CHAIN_FIRST(&tcp->rexmitQue);
        tcp->sndNxt  = tcp->sndUna;
        tcp->sndWnd = 1;
        TcpRexmitQue(tcp);
        tcp->sndWnd = 0;
    }
}

void TcpSndKeepProbe(TcpSk_t* tcp)
{
    // 没有未发送数据才能发送保活报文
    if (TcpSk2Sk(tcp)->sndBuf.pktCnt != 0 || tcp->sndQue.pktCnt != 0) {
        return;
    }
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_KEEP_PROBE);
    tcp->sndNxt -= 1;
    TcpXmitAckPkt(tcp);
    tcp->sndNxt += 1;
}
