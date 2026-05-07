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

#include "pbuf.h"
#include "pmgr.h"
#include "utils_base.h"
#include "utils_cksum.h"
#include "utils_debug.h"
#include "utils_log.h"

#include "tcp_types.h"
#include "tcp_sock.h"
#include "tcp_timer.h"
#include "tcp_cookie.h"
#include "tcp_sack.h"
#include "tcp_cc.h"
#include "tcp_rate.h"
#include "tcp_out.h"

#define DP_TCP_STAT_SND(tcp, _pktLen)                                           \
    do {                                                                        \
        if ((tcp)->connType == TCP_ACTIVE) {                                    \
            DP_ADD_TCP_STAT((tcp)->wid, DP_TCP_ACTIVE_SND_BYTE, (_pktLen));     \
        } else {                                                                \
            DP_ADD_TCP_STAT((tcp)->wid, DP_TCP_PASSIVE_SND_BYTE, (_pktLen));    \
        }                                                                       \
    } while (0)

#define DP_TCP_STAT_SND_NEW_DATA(tcp, _pktLen)                                  \
    do {                                                                        \
        DP_INC_TCP_STAT((tcp)->wid, DP_TCP_SND_PACKET);                         \
        DP_ADD_TCP_STAT((tcp)->wid, DP_TCP_SND_BYTE, (_pktLen));                \
        DP_INC_PKT_STAT((tcp)->wid, DP_PKT_SEND_BUF_IN);                        \
    } while (0)

#define DP_TCP_STAT_SND_REXMIT_DATA(tcp, _pktLen)                               \
    do {                                                                        \
        DP_INC_TCP_STAT((tcp)->wid, DP_TCP_SND_REXMT_PACKET);                   \
        DP_ADD_TCP_STAT((tcp)->wid, DP_TCP_SND_REXMT_BYTE, (_pktLen));          \
    } while (0)

#define DP_TCP_STAT_SND_DATA(tcp, _dataLen, isRexmit)                           \
    do {                                                                        \
        DP_INC_TCP_STAT((tcp)->wid, DP_TCP_SND_TOTAL);                          \
        if (isRexmit) {                                                         \
            DP_TCP_STAT_SND_REXMIT_DATA(tcp, _dataLen);                         \
        } else {                                                                \
            DP_TCP_STAT_SND_NEW_DATA(tcp, _dataLen);                            \
        }                                                                       \
        DP_TCP_STAT_SND(tcp, _dataLen);                                         \
    } while (0)

static inline uint8_t TcpCalcWs(uint32_t win)
{
    uint8_t i = 0;
    uint32_t temp = win;

    while (temp > DP_TCP_MAXWIN) {
        i++;
        temp >>= 1;
    }

    return (i > DP_TCP_MAX_WINSHIFT) ? DP_TCP_MAX_WINSHIFT : i;
}

static inline uint8_t TcpGetRcvWs(uint32_t win)
{
    uint8_t cfgWs = (uint8_t)CFG_GET_TCP_VAL(CFG_TCP_WIN_SCALE);
    if (cfgWs != 0) {
        return cfgWs;
    }
    return TcpCalcWs(win);
}

static inline uint8_t TcpAddTstampAppa(TcpSk_t* tcp, uint8_t* opts)
{
    *opts++                = DP_TCPOPT_NOP;
    *opts++                = DP_TCPOPT_NOP;
    *opts++                = DP_TCPOPT_TIMESTAMP;
    *opts++                = DP_TCPOLEN_TIMESTAMP;
    *(uint32_t*)opts       = UTILS_HTONL(tcp->tsVal);
    *(uint32_t*)(opts + 4) = UTILS_HTONL(tcp->tsEcho); // 4: 后四位赋值

    return DP_TCPOLEN_TSTAMP_APPA;
}

static uint8_t TcpAddSackOpt(TcpSk_t* tcp, uint8_t* opts, uint8_t optLen)
{
    int sackNum = 0;
    int remainLen = optLen;
    TcpSackBlock_t sackBlock;

    *opts++ = DP_TCPOPT_NOP;
    *opts++ = DP_TCPOPT_NOP;
    *opts++ = DP_TCPOPT_SACK;
    uint8_t* sackOptLen = opts;
    *opts++ = TCP_OPTLEN_BASE_SACK;
    remainLen = remainLen - TCP_OPTLEN_BASE_SACK - 2;    // 2: 在前面补充对齐的2个NOP
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
    return *sackOptLen + 2; // 2: 在前面补充对齐的2个NOP
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
    cksum += PBUF_CalcCksumAcc(pbuf);

    return UTILS_CksumSwap(cksum);
}

static void TcpFillHdr(TcpSk_t* tcp, Pbuf_t* pbuf, uint8_t thFlags, int optLen)
{
    DP_TcpHdr_t* tcpHdr;
    uint32_t     rcvWnd;
    uint8_t      l4Len;

    if ((tcp == NULL) || (pbuf == NULL)) {
        DP_LOG_ERR("TcpFillHdr! tcp is NULL or pbuf is NULL\n");
        return;
    }

    /* SYN、SYN ACK报文首部窗口不计算Ws缩放 */
    if ((thFlags & DP_TH_SYN) != 0) {
        rcvWnd = tcp->rcvWnd;
    } else {
        rcvWnd = tcp->rcvWnd >> tcp->rcvWs;
    }

    rcvWnd = (rcvWnd > DP_TCP_MAXWIN) ? DP_TCP_MAXWIN : rcvWnd;

    l4Len = (uint8_t)((uint32_t)optLen + sizeof(*tcpHdr));

    PBUF_PUT_HEAD(pbuf, sizeof(DP_TcpHdr_t));
    PBUF_SET_L4_OFF(pbuf);
    PBUF_SET_L4_LEN(pbuf, l4Len);

    tcpHdr         = PBUF_MTOD(pbuf, DP_TcpHdr_t*);
    tcpHdr->sport  = TcpGetLport(tcp);
    tcpHdr->dport  = TcpGetPport(tcp);
    tcpHdr->seq    = UTILS_HTONL(tcp->sndNxt);
    if ((thFlags & DP_TH_ACK) == 0) {
        tcpHdr->ack = 0;   // 若报文thFlags不带ack标识，首部ack置0
    } else {
        tcpHdr->ack = UTILS_HTONL(tcp->rcvNxt); // 数据报文或synack/rstack/finack报文均会携带ack标识，正常填充
    }
    tcpHdr->off    = l4Len >> 2; // 2: TCP头部的长度是以32位（4字节）为单位，此处除以4
    tcpHdr->resv   = 0;
    tcpHdr->flags  = thFlags;
    tcpHdr->win    = (uint16_t)UTILS_HTONS(rcvWnd);
    tcpHdr->chksum = 0;
    tcpHdr->urg    = 0;

    if (PBUF_GET_DEV(pbuf) == NULL) {
        DP_LOG_ERR("TcpFillHdr! pbuf->dev = NULL, rcvWnd = %u, rcvWs = %u\n", tcp->rcvWnd, tcp->rcvWs);
    } else {
        tcpHdr->chksum = TcpCalcTxCksum(tcp->pseudoHdrCksum, pbuf);
    }

    tcp->accDataCnt  = 0; // 清理累计数据计数
}

static int TcpAddEstablishOpt(TcpSk_t* tcp, Pbuf_t* pbuf)
{
    uint8_t  establishOpts[DP_TCPOLEN_MAX];
    uint8_t* opts = establishOpts;
    uint8_t  optLen = 0;

    // 写入时间戳信息
    if (TcpNegTs(tcp)) {
        tcp->tsVal = TcpGetRttTick(tcp);
        optLen += TcpAddTstampAppa(tcp, opts);
        opts += optLen;
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

static void TcpAddTimeStampOpt(TcpSk_t* tcp, uint8_t* opts)
{
    uint32_t tsVal = UTILS_HTONL(tcp->tsVal);
    uint32_t echo = UTILS_HTONL(tcp->tsEcho);
    *opts++ = DP_TCPOPT_TIMESTAMP;
    *opts++ = DP_TCPOLEN_TIMESTAMP;
    UTILS_LONG2BYTE(opts, tsVal);
    UTILS_LONG2BYTE(opts + 4, echo); // 4: 为后四个字节赋值
}

static int TcpAddSynOpt(TcpSk_t* tcp, uint8_t sndOptions, Pbuf_t* pbuf)
{
    uint8_t  tcpOpts[DP_TCPOLEN_MAX];
    uint8_t* opts = tcpOpts;
    uint8_t  optLen;
    // 可能通过setsockopt修改缓冲区大小，需要更新tcp->rcvWs

    if ((sndOptions & TCP_SYN_OPT_MSS) != 0) {
        *opts++          = DP_TCPOPT_MAXSEG;
        *opts++          = DP_TCPOLEN_MAXSEG;
        *(uint16_t*)opts = UTILS_HTONS(tcp->mss);
        opts += 2; // 2: 移动到下一个TCP选项位置
    }

    if ((sndOptions & TCP_SYN_OPT_WINDOW) != 0) {
        tcp->rcvWs = TcpGetRcvWs(TcpSk2Sk(tcp)->rcvHiwat);
        *opts++ = DP_TCPOPT_WINDOW;
        *opts++ = DP_TCPOLEN_WINDOW;
        *opts++ = tcp->rcvWs;
        *opts++ = DP_TCPOPT_NOP; // 补齐一个字节
    } else {
        tcp->rcvWs = 0;
    }

    tcp->tsVal = TcpGetRttTick(tcp);
    if ((sndOptions & TCP_SYN_OPT_TIMESTAMP) != 0) {
        if ((sndOptions & TCP_SYN_OPT_SACK_PERMITTED) != 0) {
            *opts++ = DP_TCPOPT_SACK_PERMITTED;
            *opts++ = DP_TCPOLEN_SACK_PERMITTED;
        } else {
            *opts++ = DP_TCPOPT_NOP;
            *opts++ = DP_TCPOPT_NOP;
        }
        TcpAddTimeStampOpt(tcp, opts);
        opts += DP_TCPOLEN_TIMESTAMP;
    } else {
        if ((sndOptions & TCP_SYN_OPT_SACK_PERMITTED) != 0) {
            *opts++ = DP_TCPOPT_NOP;
            *opts++ = DP_TCPOPT_NOP;
            *opts++ = DP_TCPOPT_SACK_PERMITTED;
            *opts++ = DP_TCPOLEN_SACK_PERMITTED;
        }
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
    return -1;
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
    DP_PBUF_SET_WID(pbuf, (uint8_t)tcp->wid);
    PBUF_SET_PKT_TYPE(pbuf, PBUF_PKTTYPE_HOST);
    PBUF_SET_DEV(pbuf, xmitInfo->dev);
    PBUF_SET_FLOW(pbuf, xmitInfo->flow);
    DP_PBUF_SET_VPNID(pbuf, TcpSk2Sk(tcp)->vpnid);
    PBUF_SET_USER_DATA(pbuf, TcpSk2Sk(tcp)->userData);

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

static inline void TcpUpdateRcvAdertise(TcpSk_t *tcp)
{
    if (tcp->rcvWnd > 0 && TcpSeqGt(tcp->rcvNxt + tcp->rcvWnd, tcp->rcvAdvertise)) {
        tcp->rcvAdvertise = tcp->rcvNxt + tcp->rcvWnd;
    }
    return;
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

    TcpUpdateRcvAdertise(tcp);
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
    DP_TCP_STAT_SND(tcp, PBUF_GET_PKT_LEN(ret) - (sizeof(DP_TcpHdr_t) + optLen));

    if ((thflags & (DP_TH_ACK)) != 0 && TCP_IS_IN_DELAY(tcp)) {
        TcpDeactiveDelayAckTimer(tcp);
    }

    return ret;
}

Pbuf_t* TcpGenCookieSynAckPktByPkt(Pbuf_t* pbuf, TcpSk_t* parent, TcpPktInfo_t* pi, TcpSynOpts_t* opts, uint32_t iss)
{
    DP_TcpHdr_t* origTcpHdr = (DP_TcpHdr_t *)PBUF_GET_L4_HDR(pbuf);

    Pbuf_t* ret = PBUF_Alloc(IP_INET_MAX_HDR_LEN, 0);
    if (ret == NULL) {
        return NULL;
    }
    uint32_t rcvWnd = TcpSk2Sk(parent)->rcvHiwat;
    uint16_t rcvWs = TcpGetRcvWs(rcvWnd);

    /* SYN ACK报文中携带窗口为未缩放值 */
    rcvWnd = (rcvWnd > DP_TCP_MAXWIN) ? DP_TCP_MAXWIN : rcvWnd;

    int optLen = TcpAddSynCookieOpt(ret, opts, rcvWs, parent->synOpt);

    PBUF_PUT_HEAD(ret, sizeof(DP_TcpHdr_t));
    PBUF_SET_L4_OFF(pbuf);
    PBUF_SET_L4_LEN(pbuf, sizeof(DP_TcpHdr_t) + optLen);

    DP_TcpHdr_t* tcpHdr = PBUF_MTOD(ret, DP_TcpHdr_t*);
    tcpHdr->sport  = origTcpHdr->dport;
    tcpHdr->dport  = origTcpHdr->sport;
    tcpHdr->seq    = UTILS_HTONL(iss);
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
    PBUF_SET_L4_OFF(ret);
    PBUF_SET_L4_LEN(ret, sizeof(DP_TcpHdr_t));

    DP_TcpHdr_t* tcpHdr = PBUF_MTOD(ret, DP_TcpHdr_t*);
    tcpHdr->sport  = origTcpHdr->dport;
    tcpHdr->dport  = origTcpHdr->sport;
    if ((origTcpHdr->flags & DP_TH_ACK) != 0) {
        tcpHdr->ack = 0;  // 回复rst不带ack标识时，ack置0
        tcpHdr->seq = origTcpHdr->ack;
        tcpHdr->flags  = DP_TH_RST;
    } else {
        tcpHdr->ack = UTILS_HTONL(pi->endSeq);
        tcpHdr->seq = 0;
        tcpHdr->flags  = DP_TH_RST | DP_TH_ACK;
    }
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
    if (cwnd == 0) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_CWND_LIMIT);
    } else if (swnd == 0) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_SWND_LIMIT);
    }

    return wndSize;
}

static bool TcpCanMergePbuf(Pbuf_t* pbuf, Pbuf_t* mergedPbuf, uint32_t pktLen, uint16_t maxSegNum)
{
    uint32_t mergedLen = (mergedPbuf == NULL ? 0 : PBUF_GET_PKT_LEN(mergedPbuf));
    uint16_t segNum = (mergedPbuf == NULL ? 0 : PBUF_GET_SEGS(mergedPbuf));
    if (mergedLen == 0) {
        return true;
    }

    if (segNum >= maxSegNum) {    // pbuf 的 seg num 达到上限时，不在进行聚合
        return false;
    }

    if ((pbuf->flags & DP_PBUF_FLAGS_REFERENCED) == DP_PBUF_FLAGS_REFERENCED) {
        if (mergedLen < pktLen) {
            return true;
        } else {
            return false;
        }
    }

    if (mergedLen + PBUF_GET_PKT_LEN(pbuf) > pktLen) {
        return false;
    }

    return true;
}

static Pbuf_t* TcpTryMergePbuf(TcpSk_t* tcp, uint32_t pktLen)
{
    Pbuf_t* ret = NULL;
    uint32_t dataLen = 0;
    uint16_t maxSegNum = tcp->maxSegNum - 1;    // 零拷贝场景下，考虑额外添加的一个作为头部的pbuf
    Pbuf_t* nxt = tcp->sndQue.head;
    Pbuf_t* pbuf = NULL;

    /* 先获取一个pbuf出来，然后判断长度是否超了，不够就继续获取，够了就返回，下面判断整体长度是否超过可发送空间，超了就后面部分切片，并重新放回sndQue */
    while (nxt != NULL && dataLen < pktLen && tcp->sndQue.pktCnt > 0) {
        if (!TcpCanMergePbuf(PBUF_CHAIN_FIRST(&tcp->sndQue), ret, pktLen, maxSegNum)) {
            break;
        }

        pbuf = PBUF_CHAIN_FIRST(&tcp->sndQue);
        if (dataLen + PBUF_GET_PKT_LEN(pbuf) > pktLen) {
            break;
        }
        PBUF_CHAIN_POP(&tcp->sndQue);

        if (ret == NULL) {
            ret = pbuf;
        } else {
            PBUF_Concat(ret, pbuf);
        }

        dataLen = PBUF_GET_PKT_LEN(ret);
        nxt = tcp->sndQue.head;
    }

    if (ret == NULL) {
        return NULL;
    }

    return ret;
}

static inline uint32_t TcpCalcTcTimeInc(uint32_t timeNow, SOCK_Pacing_t* pacing)
{
    if (pacing->timeStamp == 0) {
        return 0;
    }

    if (timeNow < pacing->timeStamp) {
        return TCP_PACING_TICK_SEC * pacing->cir;
    }

    if (((timeNow - pacing->timeStamp) / TCP_PACING_TICK_SEC) < ((uint32_t)PACING_RATE_NOLIMIT / pacing->cir)) {
        return (timeNow - pacing->timeStamp) / TCP_PACING_TICK_SEC * pacing->cir;
    }

    return pacing->cbs; // 时间间隔过长，直接返回最大值
}

static bool TcpCalcPacingTc(TcpSk_t* tcp, uint32_t pktLen)
{
    SOCK_Lock(TcpSk2Sk(tcp));
    SOCK_Pacing_t* pacing = TcpSk2Sk(tcp)->pacingCb;
    uint32_t timeNow = UTILS_TimeNow();

    pacing->tc += TcpCalcTcTimeInc(timeNow, pacing);
    pacing->tc = pacing->tc < pacing->cbs ? pacing->tc : pacing->cbs;

    pacing->timeStamp = timeNow; // 记录本次发送的时间点

    if (pacing->tc < pktLen) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_PACING_LIMIT);
        TcpActivePacingTimer(tcp); // 当前限速不发送则启动pacing定时器
        SOCK_Unlock(TcpSk2Sk(tcp));
        return false;
    }

    pacing->tc -= pktLen;
    SOCK_Unlock(TcpSk2Sk(tcp));
    return true;
}

static bool TcpCalcPacingByBbr(TcpSk_t* tcp, uint32_t pktLen)
{
    SOCK_Lock(TcpSk2Sk(tcp));
    if (TcpSk2Sk(tcp)->pacingCb == NULL) {
        TcpSk2Sk(tcp)->pacingCb = MEM_MALLOC(sizeof(SOCK_Pacing_t), MOD_SOCKET, DP_MEM_FREE);
        if (TcpSk2Sk(tcp)->pacingCb == NULL) {
            SOCK_Unlock(TcpSk2Sk(tcp));
            return false;
        }
        TcpSk2Sk(tcp)->pacingCb->timeStamp = 0;
    }

    SOCK_Pacing_t* pacing = TcpSk2Sk(tcp)->pacingCb;
    // 依照BBR更新令牌桶
    uint32_t rate = tcp->pacingRate / PACING_BYTES_10MS_PER_SEC; // 令牌承诺速率，bytes/s转换为bytes/10ms
    pacing->cir = rate > PACING_RATE_DEFAULT ? rate : PACING_RATE_DEFAULT;
    pacing->cbs = pacing->cir;
    pacing->tc = pacing->cbs;

    uint32_t timeNow = UTILS_TimeNow();

    pacing->tc += TcpCalcTcTimeInc(timeNow, pacing);
    pacing->timeStamp = timeNow; // 记录本次发送的时间点

    if (pacing->tc < pktLen) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_BBR_PACING_LIMIT);
        TcpActivePacingTimer(tcp); // 当前限速不发送则启动pacing定时器
        SOCK_Unlock(TcpSk2Sk(tcp));
        return false;
    }

    pacing->tc -= pktLen;
    SOCK_Unlock(TcpSk2Sk(tcp));
    return true;
}

static bool TcpPacingProc(TcpSk_t* tcp, uint32_t pktLen)
{
    if (tcp->pacingRate != PACING_RATE_NOLIMIT) { // bbr算法场景更新令牌桶
        return TcpCalcPacingByBbr(tcp, pktLen);
    }

    // 非bbr场景
    if (TcpSk2Sk(tcp)->bandWidth == 0) {
        return true;    // 不限速场景，直接发送
    }

    return TcpCalcPacingTc(tcp, pktLen);
}

bool TcpCanSendPbuf(TcpSk_t* tcp, uint32_t pktLen, uint16_t mss, int force)
{
    // 报文长度比 mss 大，立即发送
    if (pktLen >= mss) {
        return true;
    }

    if (tcp->force == 1) {
        return true;
    }

    // 断链情况下需要将报文尽可能发送出去
    if (tcp->state == TCP_FIN_WAIT1 || tcp->state == TCP_LAST_ACK) {
        return true;
    }

    // 开启了CORK选项且不强制发送小报文则一定不能发送小报文
    if ((tcp->cork) != 0 && (force == 0)) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_CORK_LIMIT);
        return false;
    }

    // 开启了MSG_MORE选项且不强制发送小报文则一定不能发送小报文。
    if ((TcpSk2Sk(tcp)->flags & SOCK_FLAGS_MSG_MORE) != 0 && (force == 0)) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_MSGMORE_LIMIT);
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
    // 有小包在途则不允许发送。若sndSml > sndNxt说明序号已回绕；
    // 存在发送窗口足够大，sndSml > sndUna误判的可能，但需要一直不发小包，后果只是多发一个小包，忽略该风险。
    if (TcpSeqGt(tcp->sndSml, tcp->sndUna) && !TcpSeqGt(tcp->sndSml, tcp->sndNxt)) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_NAGLE_LIMIT);
        return false;
    }

    return true;
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

static void TcpXmitPbufProcCC(TcpSk_t* tcp, Pbuf_t* pbuf, uint16_t dataLen)
{
    if (tcp->caMeth->algId == TCP_CAMETH_BBR) {
        TcpBwOnSent(tcp, PBUF_GET_SCORE_BOARD(pbuf), dataLen);
        tcp->inflight += dataLen;
        TcpUpdateMstamp(tcp);
    }
}

static void TcpXmitPbuf(TcpSk_t* tcp, Pbuf_t* pbuf, uint8_t thflags, TcpXmitInfo_t* xmitInfo)
{
    int      optLen;
    uint16_t dataLen = (uint16_t)PBUF_GET_PKT_LEN(pbuf);
    uint16_t dataOff = PBUF_GET_HEADROOM(pbuf);

    PBUF_ChainPush(&tcp->rexmitQue, pbuf);

    if (tcp->rttFlag == 0) {        // 收到ack后/重传后，第一个正常发送的报文
        tcp->tsVal = TcpGetRttTick(tcp);
        tcp->rttStartSeq = tcp->sndNxt;
        tcp->rttFlag = 1;
    }

    TcpSetXmitPbuf(tcp, pbuf, xmitInfo);

    optLen = TcpAddEstablishOpt(tcp, pbuf);

    TcpFillHdr(tcp, pbuf, thflags, optLen);

    if ((thflags & DP_TH_FIN) != 0) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_FIN);
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_CONTROL);
        dataLen += 1;
    }

    if (TcpPacketInPipe(tcp) == 0) {
        TcpCaCwndEvent(tcp, DP_TCP_CA_EVENT_TX_START);
    }

    TcpXmitPbufProcCC(tcp, pbuf, dataLen);

    tcp->sndMax += dataLen;
    tcp->sndNxt += dataLen;
    // 报文长度小于mss时记录小包的结束序号。开启TSO时可能出现报文由网卡分片后存在小包的情况，此处简单实现，忽略该场景。
    if (dataLen < xmitInfo->mss) {
        tcp->sndSml = tcp->sndNxt;
    }

    if (TCP_IS_IN_DELAY(tcp)) {
        TcpDeactiveDelayAckTimer(tcp);
    }

    DP_TCP_STAT_SND_DATA(tcp, PBUF_GET_PKT_LEN(pbuf) - DP_PBUF_GET_L4_LEN(pbuf), false);
    // 增加计数，避免网卡发送后被释放，ack后再释放
    PBUF_REF(pbuf);
    PMGR_Dispatch(pbuf);

    PBUF_SET_HEAD(pbuf, dataOff);
    DP_PBUF_SET_OLFLAGS(pbuf, 0);
}

static int TcpXmitCtrlPktAfterData(TcpSk_t *tcp, int force, int isNeedRst, uint32_t snded)
{
    /* 在主动调用close情况下，收到对端的数据，可以继续发送缓冲区的报文
     * 数据发送完成后发送RST报文，通知对端之前的数据报文数据丢失，并释放socket
     */
    if ((tcp->sndQue.pktCnt == 0) && (isNeedRst != 0)) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCVDATA_AFTER_CLOSE);
        TcpXmitRstPkt(tcp);
        TcpDone(tcp);
        return -1;
    }

    // 当没有发送数据报文且需要强制发送报文时，需要发送一个 Ack 报文，窗口更新场景需要
    if (snded == 0 && force != 0) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_WND_UPDATE);
        TcpXmitAckPkt(tcp);
    }
    return 0;
}

static inline void TcpUpdateFlags(TcpSk_t *tcp, uint8_t *flags, int isNeedRst)
{
    uint8_t tempFlags = *flags;
    // 已发送完全部数据，这个报文是缓冲区的最后一个报文
    if (tcp->sndQue.pktCnt == 0) {
        tempFlags |= DP_TH_PUSH;
        if ((tcp->state == TCP_FIN_WAIT1 || tcp->state == TCP_LAST_ACK) && (isNeedRst == 0)) {
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_FIN);
            tempFlags |= DP_TH_FIN;
        }
    }
    *flags = tempFlags;
}

static inline bool IsAllowFinRexmit(TcpSk_t* tcp)
{
    /* 如果已经发送了FIN，重传完所有数据之后再重传FIN */
    if ((TcpState(tcp) == TCP_FIN_WAIT1) || (TcpState(tcp) == TCP_CLOSING) || (TcpState(tcp) == TCP_LAST_ACK)) {
        if ((tcp->rtxHead == NULL) && (tcp->sndNxt == tcp->sndMax - 1)) {
            /* 如果rtxHead为空，表示重传队列数据已经全部发送，如果之前发送过FIN，sndMax必然等于sndNxt+1 */
            return true;
        }
    }
    return false;
}

int TcpXmitData(TcpSk_t *tcp, uint8_t force, int isNeedRst)
{
    /* 这个时候代表上次重传数据未重传全部数据，需要重传剩下的数据 */
    if (UTILS_UNLIKELY(tcp->sndNxt != tcp->sndMax)) {
        TcpRexmitQue(tcp, isNeedRst);
    }

    /* 数据还没有完全重传完，但已经没有窗口了 */
    if (UTILS_UNLIKELY(TcpSeqLt(tcp->sndNxt, tcp->sndMax))) {
        if ((isNeedRst == 0) || !IsAllowFinRexmit(tcp)) {
            /* 没有RST事件，直接返回，必须重传完unack的数据再发新数据
               有RST事件
                1、没有发送过FIN的状态，直接返回，必须重传完unack的数据再发RST
                2、发送过FIN的状态，如果没有重传完unack的数据，直接返回 */
            return 0;
        }
    }

    // 仍有窗口可以继续发送新的数据
    uint32_t totalSndLen = UTILS_MIN((uint32_t)tcp->sndQue.bufLen, TcpCalcFreeWndSize(tcp));
    uint32_t snded = 0;
    uint8_t  thflags = DP_TH_ACK;
    Pbuf_t*  pbuf = NULL;
    TcpXmitInfo_t xmitInfo;

    if (UTILS_UNLIKELY(TcpGetXmitInfo(tcp, &xmitInfo) != 0)) {
        return 0;
    }

    TcpUpdateRcvWnd(tcp);

    // 有数据才能发送
    while (totalSndLen > 0 && tcp->sndQue.pktCnt > 0) {
        // 能发送的数据大小：tsosize, 可发送窗口大小
        uint32_t pktLen = UTILS_MIN(xmitInfo.tsoSize, totalSndLen);
        // 判断能否发送，不能发送就没必要聚合。tcp发送缓冲区高水位类型为uint32_t，这里转换后不会截断
        if (UTILS_UNLIKELY(!TcpCanSendPbuf(tcp, pktLen, xmitInfo.mss, force) || !TcpPacingProc(tcp, pktLen))) {
            break;
        }

        // 从发送队列中尝试聚合一个 pbuf
        pbuf = TcpTryMergePbuf(tcp, pktLen);
        if (UTILS_UNLIKELY(pbuf == NULL)) {
            return 0;
        }

        TcpUpdateFlags(tcp, &thflags, isNeedRst);

        if ((pbuf->flags & DP_PBUF_FLAGS_EXTERNAL) == DP_PBUF_FLAGS_EXTERNAL) {
            if (UTILS_UNLIKELY(Pbuf_Zcopy_Alloc(&pbuf) != 0)) {
                return 0;
            }
            pbuf->flags |= DP_PBUF_FLAGS_EXT_HEAD;
        }

        // 发送报文
        totalSndLen -= PBUF_GET_PKT_LEN(pbuf);
        snded += PBUF_GET_PKT_LEN(pbuf);
        TcpXmitPbuf(tcp, pbuf, thflags, &xmitInfo);
    }

    TcpTryActiveTimer(tcp);

    // 不管有没有发送数据，强制发送置为 0
    tcp->force = 0;

    return TcpXmitCtrlPktAfterData(tcp, force, isNeedRst, snded);
}

void TcpXmitCtrlPkt(TcpSk_t* tcp, uint8_t thflags)
{
    Pbuf_t* pbuf;

    // 启动坚持定时器代表对端窗口为0，此时不能发送FIN报文
    if (UTILS_UNLIKELY(TCP_IS_IN_PERSIST(tcp) && (thflags & DP_TH_FIN) != 0)) {
        return;
    }

    pbuf = TcpGenCtrlPkt(tcp, thflags, 1);
    if (UTILS_UNLIKELY(pbuf == NULL)) {
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
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_REXMIT_SYN);
            thFlags = DP_TH_SYN;
            break;
        case TCP_SYN_RECV:
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_REXMIT_SYNACK);
            thFlags = DP_TH_SYN | DP_TH_ACK;
            break;
        case TCP_CLOSING:
        case TCP_FIN_WAIT1:
        case TCP_LAST_ACK:
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_REXMIT_FIN);
            thFlags = DP_TH_FIN | DP_TH_ACK;
            break;
        case TCP_CLOSED:
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_REXMIT_RST);
            thFlags = DP_TH_RST | DP_TH_ACK;
            break;
        default:
            thFlags = DP_TH_ACK;
    }
    if (tcp->rttFlag == 1 && (thFlags & DP_TH_SYN) != 0) {
        tcp->rttFlag = 0;       // 重传syn、syn/ack报文后，不更新rtt
    }
    TcpXmitCtrlPkt(tcp, (uint8_t)thFlags);
}

static void TcpRexmitPbufProcCC(TcpSk_t* tcp, Pbuf_t* pbuf, uint16_t dataLen)
{
    if (tcp->caMeth->algId == TCP_CAMETH_BBR) {
        TcpBwOnSent(tcp, PBUF_GET_SCORE_BOARD(pbuf), dataLen);
        TcpUpdateMstamp(tcp);
        PBUF_GET_SCORE_BOARD(pbuf)->state |= TF_SEG_RETRANSMITTED;
        tcp->connLostCnt++;
    }
}

static void TcpRexmitPbuf(TcpSk_t* tcp, Pbuf_t* pbuf, TcpXmitInfo_t* xmitInfo, bool reservePbuf)
{
    DP_TcpHdr_t *tcpHdr = (DP_TcpHdr_t *)PBUF_GET_L4_HDR(pbuf);
    uint8_t thFlags = tcpHdr->flags;
    uint16_t dataLen = (uint16_t)PBUF_GET_PKT_LEN(pbuf);
    uint16_t dataOff = PBUF_GET_HEADROOM(pbuf);

    TcpSetXmitPbuf(tcp, pbuf, xmitInfo);

    int optLen = TcpAddEstablishOpt(tcp, pbuf);

    TcpFillHdr(tcp, pbuf, thFlags, optLen);

    TcpRexmitPbufProcCC(tcp, pbuf, dataLen);
    tcp->sndNxt += dataLen;
    if ((thFlags & DP_TH_FIN) != 0) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_REXMIT_FIN);
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_CONTROL);
        tcp->sndNxt++;
    }
    tcp->rexmitCnt++;
    DP_TCP_STAT_SND_DATA(tcp, PBUF_GET_PKT_LEN(pbuf) - DP_PBUF_GET_L4_LEN(pbuf), true);
    if (reservePbuf) {
        PBUF_REF(pbuf);
    }

    PMGR_Dispatch(pbuf);

    if (reservePbuf) {
        PBUF_SET_HEAD(pbuf, dataOff);
        DP_PBUF_SET_OLFLAGS(pbuf, 0);
    }
}

static Pbuf_t* TcpRexmitQueSplit(TcpSk_t* tcp, Pbuf_t* pbuf, uint16_t len)
{
    ASSERT(len <= pbuf->totLen);

    Pbuf_t* ret;

    ret = PBUF_BuildFromPbuf(pbuf, len, pbuf->l4Off + sizeof(DP_TcpHdr_t));
    if (ret == NULL) {
        return NULL;
    }

    PBUF_CUT_DATA(pbuf, len);

    tcp->rexmitQue.bufLen -= PBUF_GET_PKT_LEN(ret);
    PBUF_ChainInsertBefore(&tcp->rexmitQue, pbuf, ret);

    ret->l4Off = pbuf->l4Off;
    ret->vpnid = pbuf->vpnid;
    ((DP_TcpHdr_t *)PBUF_GET_L4_HDR(ret))->flags = ((DP_TcpHdr_t *)PBUF_GET_L4_HDR(pbuf))->flags;

    if (DP_PBUF_GET_TOTAL_LEN(pbuf) == 0) {
        PBUF_ChainRemove(&tcp->rexmitQue, pbuf);
        PBUF_Free(pbuf);
    }

    return ret;
}

void TcpRexmitQue(TcpSk_t* tcp, int isNeedRst)
{
    uint16_t     pktLen;
    uint32_t     sndWin;
    Pbuf_t *pbuf = tcp->rtxHead;
    Pbuf_t *tempPbuf = NULL;
    TcpXmitInfo_t xmitInfo;
    uint8_t      isFirstPkt = 1;

    if (UTILS_UNLIKELY(TcpGetXmitInfo(tcp, &xmitInfo) != 0)) {
        return;
    }

    sndWin = (tcp->cwnd > tcp->sndWnd) ? tcp->sndWnd : tcp->cwnd;
    // 减去在途数据后为真实窗口值
    sndWin -= (tcp->sndNxt - tcp->sndUna);

    TcpUpdateRcvWnd(tcp);
    tcp->rttFlag = 0;       // 重传过报文，无法确认ack对应发送时间，不更新rtt
    while (pbuf != NULL) {
        pktLen = (uint16_t)PBUF_GET_PKT_LEN(pbuf);
        if (pktLen > xmitInfo.mss) {
            // TSO 使能时，重传队列中 pbuf 长度可能会大于 mss ，此时从 pbuf 中分割一个 mss 大小报文
            tempPbuf = TcpRexmitQueSplit(tcp, pbuf, xmitInfo.mss);
        } else if (PBUF_GET_SEGS(pbuf) != 1) {
            // 1、如果重传发送原pbuf链，则会在dpdk释放缓冲区中存放两次相同的mbuf链。dpdk释放mbuf链时会拆链，导致只能释放一次链，mbuf泄漏
            // 2、发现pbuf链中存在seglen = 0的分片时，dpdk发送失败。通过拷贝来避免发送这样的报文
            tempPbuf = TcpRexmitQueSplit(tcp, pbuf, pktLen);
        } else {
            tempPbuf = pbuf;
        }

        if (tempPbuf == NULL) {
            DP_LOG_ERR("TcpRexmitQueSplit malloc PBUF fail\n");
            break;
        }

        pbuf = tempPbuf;
        pktLen = (uint16_t)PBUF_GET_PKT_LEN(pbuf);
        if (pktLen > sndWin) {
            break;
        }

        TcpRexmitPbuf(tcp, pbuf, &xmitInfo, true);
        if (isFirstPkt > 0) {
            TcpUpdateRcvAdertise(tcp);
            isFirstPkt = 0;
        }

        sndWin -= pktLen;

        pbuf = PBUF_CHAIN_NEXT(pbuf);
    }

    tcp->rtxHead = pbuf;
    if ((IsAllowFinRexmit(tcp) == true) && (isNeedRst == 0)) {
        TcpXmitCtrlPkt(tcp, DP_TH_FIN | DP_TH_ACK);
    }
}

void TcpRexmitAll(TcpSk_t* tcp)
{
    if (tcp->rexmitQue.pktCnt == 0) {
        return;
    }
    tcp->rtxHead = PBUF_CHAIN_FIRST(&tcp->rexmitQue);
    tcp->sndNxt = tcp->sndUna;
    if (TCP_SACK_AVAILABLE(tcp)) {
        TcpClearSackInfo(tcp->sackInfo, tcp->sndUna);
    }
    TcpActiveRexmitTimer(tcp);
    Pbuf_t* tempPbuf = tcp->rtxHead;
    while (tcp->rtxHead != NULL) {
        TcpRexmitQue(tcp, 0);
        if (tempPbuf == tcp->rtxHead) {
            break;          // 避免TcpRexmitQue内部发送失败，导致死循环
        }
        tempPbuf = tcp->rtxHead;
    }
}

void TcpRexmitPkt(TcpSk_t* tcp)
{
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_REXMT_TIMEOUT);
    tcp->rtxHead = PBUF_CHAIN_FIRST(&tcp->rexmitQue);
    tcp->sndNxt = tcp->sndUna;
    if (tcp->caMeth->algId == TCP_CAMETH_BBR && (tcp->state >= TCP_ESTABLISHED)) {
        TcpCaSetState(tcp, TCP_CA_LOSS);
    }
    if (tcp->rexmitQue.pktCnt > 0) {
        if (TCP_SACK_AVAILABLE(tcp)) {
            TcpClearSackInfo(tcp->sackInfo, tcp->sndUna);
        }
        TcpRexmitQue(tcp, 0);
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
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_FAST_REXMT_PACKET);
    DP_Pbuf_t* oldHead = tcp->rtxHead;
    tcp->rtxHead = PBUF_CHAIN_FIRST(&tcp->rexmitQue);
    uint32_t temp = tcp->sndNxt;
    uint32_t oldCwnd = tcp->cwnd;
    tcp->sndNxt = tcp->sndUna;
    /* 快恢复阶段一次最多只能重传一个MSS的报文 */
    tcp->cwnd = tcp->mss;
    TcpRexmitQue(tcp, 0);
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
    if (tcp->sndQue.bufLen > 0 || tcp->rexmitQue.bufLen > 0) {
        TcpTsqAddQue(tcp, TCP_TSQ_SEND_DATA);
    }
    TcpCaSetState(tcp, TCP_CA_RECOVERY);
}

int TcpFastRexmitSack(TcpSk_t* tcp)     // 返回是否有sack空洞需要重传
{
    TcpXmitInfo_t xmitInfo;
    uint32_t sndSeq;
    bool reservePbuf = true; // 是否需要保留原始pbuf。默认为true，pbuf链分片情况下需要克隆pbuf，发送后释放克隆的pbuf

    if (TcpGetXmitInfo(tcp, &xmitInfo) != 0) {
        return -1;
    }

    Pbuf_t* pbuf = TcpGetRexmitSack(tcp, &sndSeq);
    if (pbuf == NULL) {
        return -1;
    }
    if (PBUF_GET_SEGS(pbuf) != 1) {
        // 1、如果重传发送原pbuf链，则会在dpdk释放缓冲区中存放两次相同的mbuf链。dpdk释放mbuf链时会拆链，导致只能释放一次链，mbuf泄漏
        // 2、发现pbuf链中存在seglen = 0的分片时，dpdk发送失败。通过拷贝来避免发送这样的报文
        Pbuf_t* tempPbuf = PBUF_Clone(pbuf);
        if (tempPbuf == NULL) {
            return -1;
        }
        ((DP_TcpHdr_t *)PBUF_GET_L4_HDR(tempPbuf))->flags = ((DP_TcpHdr_t *)PBUF_GET_L4_HDR(pbuf))->flags;
        reservePbuf = false;
        pbuf = tempPbuf;
    }

    DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_SACK_REXMT_PACKET);
    uint32_t temp = tcp->sndNxt;
    tcp->sndNxt = sndSeq;
    TcpRexmitPbuf(tcp, pbuf, &xmitInfo, reservePbuf);
    tcp->sndNxt = temp;
    return 0;
}

void TcpSndProbePkt(TcpSk_t* tcp)
{
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_SND_TOTAL);
    tcp->sndNxt -= 1;
    TcpXmitAckPkt(tcp);
    tcp->sndNxt += 1;
}
