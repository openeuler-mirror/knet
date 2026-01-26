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
#include "tcp_in.h"

#include "dp_tcp.h"
#include "dp_ip.h"
#include "dp_ethernet.h"

#include "tcp_types.h"

#include "tcp_out.h"
#include "tcp_timer.h"
#include "tcp_cc.h"
#include "tcp_bbr.h"
#include "tcp_rate.h"
#include "tcp_tsq.h"
#include "tcp_sock.h"
#include "tcp_sack.h"
#include "tcp_frto.h"

#include "utils_base.h"
#include "utils_cksum.h"
#include "utils_debug.h"

#define DEFAULT_TCP_MSS 536
#define DEFAULT_TCP6_MSS 1220
#define SYS_MIN_TCP_MSS 48
#define TCP_CHALLENG_MIN_INTERVAL_TICK 1

#define DP_TCP_STAT_RCV_DATA(tcp, _dataLen)                         \
    do {                                                            \
        DP_ADD_TCP_STAT((tcp)->wid, DP_TCP_RCV_BYTE, (_dataLen));   \
        DP_INC_TCP_STAT((tcp)->wid, DP_TCP_RCV_PACKET);             \
    } while (0)


static Pbuf_t* TcpProcRst(TcpSk_t* tcp, TcpPktInfo_t* pi);
static int TcpProcUnorderPkt(TcpSk_t* tcp, Pbuf_t* pbuf, TcpPktInfo_t* pi);
static int TcpProcOrderPkt(TcpSk_t* tcp, Pbuf_t* pbuf, TcpPktInfo_t* pi);
static Pbuf_t* TcpProcReplyPolicy(TcpSk_t* tcp, int policy);

static uint16_t TcpCalcCksum(Pbuf_t *pbuf)
{
    uint32_t cksum;

    DP_IpHdr_t* ipHdr = (DP_IpHdr_t*)PBUF_GET_L3_HDR(pbuf);

    INET_PseudoHdr_t phdr;
    phdr.src = ipHdr->dst;
    phdr.dst = ipHdr->src;
    phdr.zero = 0;
    phdr.protocol = DP_IPHDR_TYPE_TCP;
    phdr.len = UTILS_HTONS((uint16_t)PBUF_GET_PKT_LEN(pbuf));

    cksum = UTILS_Cksum(0, (uint8_t*)&phdr, sizeof(phdr));
    cksum += PBUF_CalcCksumAcc(pbuf);

    return UTILS_CksumSwap(cksum);
}

static int TcpVerifyCksum(Pbuf_t* pbuf)
{
    if ((DP_PBUF_GET_OLFLAGS(pbuf) & DP_PBUF_OLFLAGS_RX_L4_CKSUM_GOOD) != 0) {
        return 0;
    }

    if ((DP_PBUF_GET_OLFLAGS(pbuf) & DP_PBUF_OLFLAGS_RX_L4_CKSUM_BAD) != 0) {
        return -1;
    }

    if (PBUF_GET_L3_TYPE(pbuf) == DP_ETH_P_IPV6) {
       return -1;
    }
    return TcpCalcCksum(pbuf);
}

int TcpInitPktInfo(Pbuf_t* pbuf, DP_TcpHdr_t* tcpHdr, TcpPktInfo_t* pi)
{
    uint8_t thflags;

    if (UTILS_UNLIKELY(PBUF_GET_SEG_LEN(pbuf) < sizeof(DP_TcpHdr_t))) {
        DP_INC_TCP_STAT(pbuf->wid, DP_TCP_RCV_SHORT);
        return -1;
    }

    pi->hdrLen = tcpHdr->off << 2; // 2: hdrLen占高位的4bit，需要乘以4
    if (UTILS_UNLIKELY(pi->hdrLen > PBUF_GET_SEG_LEN(pbuf) || pi->hdrLen < sizeof(DP_TcpHdr_t))) {
        DP_INC_TCP_STAT(pbuf->wid, DP_TCP_RCV_BAD_OFF);
        return -1;
    }

    pi->dataLen = (uint16_t)PBUF_GET_PKT_LEN(pbuf);

    DP_IpHdr_t* ipHdr = (DP_IpHdr_t*)PBUF_GET_L3_HDR(pbuf);
    if (UTILS_UNLIKELY((ipHdr->src == ipHdr->dst) && (tcpHdr->sport == tcpHdr->dport))) {
        DP_INC_TCP_STAT(pbuf->wid, DP_TCP_RCV_LOCAL_ADDR);
        return -1;
    }

    thflags = tcpHdr->flags & 0x3F;
    if (UTILS_UNLIKELY(thflags == 0x3F || thflags == 0 ||
        (thflags & (DP_TH_FIN | DP_TH_SYN)) == (DP_TH_FIN | DP_TH_SYN) ||
        (thflags & (DP_TH_FIN | DP_TH_RST)) == (DP_TH_FIN | DP_TH_RST))) {
        DP_INC_TCP_STAT(pbuf->wid, DP_TCP_DROP_CONTROL_PKTS);
        return -1;
    }

    if (UTILS_UNLIKELY(TcpVerifyCksum(pbuf) != 0)) {
        DP_INC_TCP_STAT(pbuf->wid, DP_TCP_RCV_BAD_SUM);
        return -1;
    }

    pi->dataLen -= pi->hdrLen;
    pi->thFlags = thflags;
    pi->seq     = UTILS_NTOHL(tcpHdr->seq);
    pi->ack     = UTILS_NTOHL(tcpHdr->ack);
    pi->endSeq  = pi->seq + pi->dataLen;
    pi->sndWnd  = UTILS_NTOHS(tcpHdr->win);
    pi->sackOpt = NULL;
    pi->pktType = 0;
    pi->errCode = 0;

    if ((thflags & (DP_TH_FIN | DP_TH_SYN | DP_TH_RST)) != 0) {
        pi->endSeq += 1;
    }

    return 0;
}

static void TcpParseTimeStamp(uint8_t** opt, TcpSynOpts_t* synOpts, uint16_t* optSize)
{
    *optSize = *(*opt)++;
    synOpts->rcvSynOpt |= TCP_SYN_OPT_TIMESTAMP;
    synOpts->tsVal  = UTILS_NTOHL(UTILS_BYTE2LONG(*opt));
    synOpts->tsEcho = UTILS_NTOHL(UTILS_BYTE2LONG(*opt + 4)); // 4: 取后4位的值
}

static int TcpParseOptWindow(uint8_t** opt, TcpSynOpts_t* synOpts, uint16_t* optSize)
{
    *optSize = *(*opt)++;
    synOpts->rcvSynOpt |= TCP_SYN_OPT_WINDOW;

    synOpts->ws = (*(*opt) > DP_TCP_MAX_WINSHIFT) ? DP_TCP_MAX_WINSHIFT : *(*opt);
    return 0;
}

static inline int TcpCheckOpt(const uint8_t* opt, const uint8_t* optEnd, int len)
{
    if (opt >= optEnd) {
        return -1;
    }
    int optLen = *opt;
    if (optLen != len || (opt + optLen - 1) > optEnd) {      // 减去len所占1位
        return -1;
    }
    return 0;
}

static inline int TcpCheckOptSack(uint8_t* opt, const uint8_t* optEnd, TcpPktInfo_t* pi)
{
    if (opt >= optEnd) {
        return -1;
    }
    int optLen = *opt;
    if ((optLen < TCP_OPTLEN_BASE_SACK) ||
        (((optLen - TCP_OPTLEN_BASE_SACK) % TCP_OPTLEN_SACK_PERBLOCK) != 0) || ((opt + optLen - 1) > optEnd)) {
        // 减去len所占1位
        return -1;
    }
    pi->sackOpt = opt;
    return 0;
}

int TcpPreProcChild(TcpSk_t* tcp, Pbuf_t* pbuf, TcpPktInfo_t* pi, uint8_t *isNeedRst, uint32_t *reason)
{
    if (TcpSk2Sk(tcp)->wid != -1 && TcpSk2Sk(tcp)->wid != pbuf->wid) {
        // 共线程部署下tcp sk绑定在worker上，非对应worker的报文都丢弃
        *reason = DP_TCP_LISTEN_RCV_INVALID_WID;
        return -1;
    }

    if ((pi->thFlags & DP_TH_RST) != 0) {
        *reason = DP_TCP_RCV_INVALID_RST;
        return -1;
    }

    if ((pi->thFlags & DP_TH_ACK) != 0) {
        *isNeedRst = 1;
        *reason = DP_TCP_RCV_ERR_ACKFLAG;
        return -1;
    }

    if ((pi->thFlags & 0x3F) != DP_TH_SYN) {
        *reason = DP_TCP_LISTEN_RCV_NOTSYN;
        return -1;
    }

    if (PBUF_GET_PKT_LEN(pbuf) != 0) { // 暂不支持带数据的syn报文
        *reason = DP_TCP_RCV_DATA_SYN;
        return -1;
    }

    return 0;
}

int TcpParseSynOpts(uint8_t* opt, int optLen, TcpSynOpts_t* synOpts)
{
    uint8_t* optEnd    = opt + optLen;
    synOpts->rcvSynOpt = 0;

    while (opt < optEnd) {
        int      optCode = *opt++;
        uint16_t optSize;

        switch (optCode) {
            case DP_TCPOPT_EOL:
                return 0;
            case DP_TCPOPT_NOP:
                optSize = 2; // NOP占1位，设置optSize=2为 在后续opt += optSize - 2;保证不偏移
                break;
            case DP_TCPOPT_MAXSEG:
                if (TcpCheckOpt(opt, optEnd, DP_TCPOLEN_MAXSEG) != 0) {
                    return -1;
                }
                optSize = *opt++;
                synOpts->rcvSynOpt |= TCP_SYN_OPT_MSS;
                synOpts->mss = UTILS_NTOHS(*(uint16_t*)opt);
                break;
            case DP_TCPOPT_SACK_PERMITTED:
                if (TcpCheckOpt(opt, optEnd, DP_TCPOLEN_SACK_PERMITTED) != 0) {
                    return -1;
                }
                optSize = *opt++;
                synOpts->rcvSynOpt |= TCP_SYN_OPT_SACK_PERMITTED;
                break;
            case DP_TCPOPT_TIMESTAMP:
                if (TcpCheckOpt(opt, optEnd, DP_TCPOLEN_TIMESTAMP) != 0) {
                    return -1;
                }
                TcpParseTimeStamp(&opt, synOpts, &optSize);
                break;
            case DP_TCPOPT_WINDOW:
                if (TcpCheckOpt(opt, optEnd, DP_TCPOLEN_WINDOW) != 0) {
                    return -1;
                }
                TcpParseOptWindow(&opt, synOpts, &optSize);
                break;
            default:        //  不允许带有其他的选项，如SACK
                return -1;
        }

        opt += optSize - 2; // 2: 去掉前面的类型和长度
    }

    return 0;
}

static void TcpNegotiateOpts(TcpSk_t* tcp, TcpSynOpts_t* opts)
{
    uint16_t family = TcpSk2Sk(tcp)->family;
    uint16_t defMss = (family == DP_AF_INET) ? DEFAULT_TCP_MSS : DEFAULT_TCP6_MSS;
    // mss协商, 如果不协商mss选项，则采用一个安全值，如果对端设置的mss小于系统的最小值，则采用默认mss与本端设置的mss对比结果
    if (TcpHasMss(tcp) && (opts->rcvSynOpt & TCP_SYN_OPT_MSS) != 0) {
        tcp->rcvMss = opts->mss;
        uint16_t optMss = (opts->mss < SYS_MIN_TCP_MSS) ? defMss : opts->mss;
        tcp->mss = (optMss < tcp->mss) ? optMss : tcp->mss;
        tcp->negOpt |= TCP_SYN_OPT_MSS;
    } else {
        if (CFG_GET_TCP_VAL(DP_CFG_TCP_MSS_USE_DEFAULT) == DP_ENABLE) {   // 配置项使能，则使用默认mss，否则 使用原tcp->mss
            tcp->mss = defMss;
        }
    }

    // ws协商，如果对端不支持这个选项，则本端的sndWs设置为0
    if (TcpHasWs(tcp) && (opts->rcvSynOpt & TCP_SYN_OPT_WINDOW) != 0) {
        tcp->sndWs = (uint8_t)opts->ws;
        tcp->negOpt |= TCP_SYN_OPT_WINDOW;
    } else {
        tcp->rcvWs = 0;
        tcp->sndWs = 0;
        tcp->negOpt &= ~TCP_SYN_OPT_WINDOW;
    }

    // sack 选项协商
    if (TcpHasSackPermitted(tcp) && (opts->rcvSynOpt & TCP_SYN_OPT_SACK_PERMITTED) != 0) {
        tcp->negOpt |= TCP_SYN_OPT_SACK_PERMITTED;
        TcpInitSackInfo(&tcp->sackInfo);
    } else {
        tcp->negOpt &= ~TCP_SYN_OPT_SACK_PERMITTED;
    }

    // 时间戳协商
    if (TcpHasTs(tcp) && (opts->rcvSynOpt & TCP_SYN_OPT_TIMESTAMP) > 0) {
        tcp->negOpt |= TCP_SYN_OPT_TIMESTAMP;
        tcp->tsEcho = opts->tsVal;
    } else {
        tcp->negOpt &= ~TCP_SYN_OPT_TIMESTAMP;
    }
}

static int TcpParseTsOptFast(TcpSk_t* tcp, const uint8_t* opt, TcpPktInfo_t* pi)
{
    if (!TcpNegTs(tcp)) { // 如果未协商选项，但是带了选项则忽略
        return 0;
    }
    uint32_t timeVal = UTILS_BYTE2LONG(opt);
    timeVal = UTILS_NTOHL(timeVal);
    if (TcpSeqLt(timeVal, tcp->tsEcho)) {
        pi->errCode = TCP_PI_ERR_TIMESTAMP;
        return -1;
    }
    uint32_t timeEcho = UTILS_BYTE2LONG(opt + 4);
    pi->tsEcho = UTILS_NTOHL(timeEcho);

    pi->tsVal = timeVal;  // 该时间戳值仅在顺序报文中更新tsEcho，乱序不更新
    pi->pktType |= TCP_PI_HAS_TIMESTAMP;
    return 0;
}

static int TcpParseOptionsSlow(TcpSk_t* tcp, uint8_t* opt, TcpPktInfo_t* pi)
{
    uint8_t optLen  = pi->hdrLen - sizeof(DP_TcpHdr_t);
    uint8_t* optEnd = opt + optLen;
    while (opt < optEnd) {
        int optCode = *opt++;
        uint16_t optSize;

        switch (optCode) {
            case DP_TCPOPT_EOL:
                return 0;
            case DP_TCPOPT_NOP:
                optSize = 2; // NOP占1位，设置optSize=2为 在后续opt += optSize - 2;保证不偏移
                break;
            case DP_TCPOPT_SACK_PERMITTED:
                if (TcpCheckOpt(opt, optEnd, DP_TCPOLEN_SACK_PERMITTED) != 0) {
                    return -1;
                }
                optSize = *opt++;   // 不处理sack_permitted选项
                break;
            case DP_TCPOPT_SACK:
                if (TcpCheckOptSack(opt, optEnd, pi) != 0) {
                    return -1;
                }
                optSize = *opt++;
                break;
            case DP_TCPOPT_TIMESTAMP:
                if (TcpCheckOpt(opt, optEnd, DP_TCPOLEN_TIMESTAMP) != 0) {
                    return -1;
                }
                optSize = *opt++;
                if (TcpParseTsOptFast(tcp, opt, pi) < 0) {
                    return -1;
                }
                break;
            default:        // 其他选项跳过处理
                if (opt >= optEnd) {
                    return -1;
                }
                optSize = *opt++;
                if ((optSize < 2) || ((opt + (optSize - 2)) > optEnd)) { // 2: 去掉前面的类型和长度
                    return -1;
                }
                break;
        }
        opt += optSize - 2;     // 2: 去掉前面的类型和长度
    }
    return 0;
}

static int TcpParseOptions(TcpSk_t* tcp, uint8_t* opt, TcpPktInfo_t* pi)
{
    uint8_t optLen  = pi->hdrLen - sizeof(DP_TcpHdr_t);

    pi->tsEcho = tcp->tsVal;
    if (optLen == 0) {
        /* 存在协商了时间戳但后续报文可能不带时间戳情况，不丢弃此类报文 */
        return 0;
    }

    uint32_t optVal = 0;
    if (optLen >= 4) { // 快处理情形下，时间戳选项字段占4个字节
        optVal = UTILS_BYTE2LONG(opt);
    }

    // 时间戳快速处理
    if (optLen == DP_TCPOLEN_TSTAMP_APPA && optVal == TCP_OPT_TSTAMP_APPA_VAL) {
        return TcpParseTsOptFast(tcp, opt + 4, pi);      // 4: 补齐的DP_TCPOPT_NOP(2) + 时间戳Code、Size(2)
    }

    // 这里不再对SYN报文的其他选项进行有效性检测
    if ((pi->thFlags & DP_TH_SYN) != 0) {
        return 0;
    }

    return TcpParseOptionsSlow(tcp, opt, pi);
}

static Pbuf_t *TcpTryGenChallengeAckPkt(TcpSk_t* tcp)
{
    // 考虑到对潜在攻击的防范，每次发送ACK报文至少间隔10ms
    uint32_t nowTick = TcpGetRttTick(tcp);
    Pbuf_t *ret = NULL;
    uint32_t passedTime = nowTick - tcp->lastChallengeAckTime;
    // 仅在时间差大于UINT32_MAX，且小于(UINT32_MAX + TCP_CHALLENG_MIN_INTERVAL_TICK)时无法判断发送挑战ack
    if (passedTime >= TCP_CHALLENG_MIN_INTERVAL_TICK) {
        ret = TcpGenAckPkt(tcp);
        tcp->lastChallengeAckTime = nowTick;
        if (ret != NULL) {
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_RESPONSE_CHALLENGE_ACKS);
        }
    }
    return ret;
}

static int TcpProcDupData(TcpSk_t* tcp, Pbuf_t* pbuf, TcpPktInfo_t* pi, Pbuf_t **ret)
{
    if ((pi->thFlags & DP_TH_FIN) != 0) {
        if (TCP_HAS_RECV_FIN(tcp) && pi->endSeq == tcp->rcvMax) {
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_RXMT_FIN);
            *ret = TcpTryGenChallengeAckPkt(tcp);
            // TIME_WAIT状态下需要刷新2msl定时器时间
            if (tcp->state == TCP_TIME_WAIT) {
                TcpReactiveMslTimer(tcp);
            }
        }

        // 其余异常FIN报文直接丢弃
        return -1;
    }

    // 完全重复报文回复ACK报文
    if (TcpSeqLeq(pi->endSeq, tcp->rcvNxt)) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_DUP_PACKET);
        DP_ADD_TCP_STAT(tcp->wid, DP_TCP_RCV_DUP_BYTE, pi->dataLen);
        *ret = TcpTryGenChallengeAckPkt(tcp);
        pi->dataLen = 0;
    } else {
        uint16_t dupLen = UTILS_MIN((uint16_t)(tcp->rcvNxt - pi->seq), pi->dataLen);
        if (dupLen != 0) {
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_PART_DUP_PACKET);
            DP_ADD_TCP_STAT(tcp->wid, DP_TCP_RCV_PART_DUP_BYTE, dupLen);
            pi->dataLen -= dupLen;
            PBUF_CUT_DATA(pbuf, dupLen);
        }
    }

    pi->seq = tcp->rcvNxt;
    return 0;
}

static int TcpProcOverWndData(TcpSk_t* tcp, Pbuf_t* pbuf, TcpPktInfo_t* pi, Pbuf_t **ret)
{
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_PACKET_AFTER_WND);
    uint32_t wndEnd = tcp->rcvWup + tcp->rcvWnd;
    // 完全超窗报文，立即回复ACK报文
    if (TcpSeqGt(pi->seq, wndEnd) && ret != NULL) {
        // 窗口探测报文
        if (pi->dataLen == 1) {
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_WND_PROBE);
        }
        DP_ADD_TCP_STAT(tcp->wid, DP_TCP_RCV_BYTE_AFTER_WND, pi->dataLen);
        *ret = TcpTryGenChallengeAckPkt(tcp);
        return -1;
    }

    // 部分超窗数据, 去除超过部分数据
    DP_ADD_TCP_STAT(tcp->wid, DP_TCP_RCV_BYTE_AFTER_WND, pi->endSeq - wndEnd);
    if ((pi->thFlags & DP_TH_FIN) != 0) {
        pi->thFlags &= ~DP_TH_FIN;
        pi->endSeq -= 1;
    }

    uint32_t dupLen = pi->endSeq - wndEnd;
    PBUF_CutTailData(pbuf, (uint16_t)dupLen);
    pi->endSeq -= dupLen;
    pi->dataLen -= (uint16_t)dupLen;

    return 0;
}

static int TcpPreProcPktData(TcpSk_t* tcp, Pbuf_t* pbuf, TcpPktInfo_t* pi, Pbuf_t** ret)
{
    // 超窗数据
    if (TcpSeqGt(pi->endSeq, tcp->rcvWup + tcp->rcvWnd)) {
        if (TcpProcOverWndData(tcp, pbuf, pi, ret) != 0) {
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_DROP_DATA_PKTS);
            return -1;
        }
    }

    // 重复数据
    if (TcpSeqLt(pi->seq, tcp->rcvNxt)) {
        // 重复报文处理
        if (TcpProcDupData(tcp, pbuf, pi, ret) != 0) {
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_DROP_DATA_PKTS);
            return -1;
        }
    }

    if (pi->dataLen == 0) {
        return 0;
    }
    // RFC 9293  3.6.1 主动断链情况下(主动调用close)收到新数据，应该回复RST告知对端数据丢失，不对此数据报文回应ACK
    if (TCP_IS_CLOSED(tcp)) {
        TcpTsqAddQue(tcp, TCP_TSQ_SEND_RST);
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_DROP_DATA_PKTS);
        return -1;
    }
    return 0;
}

static int TcpPreProcPkt(TcpSk_t* tcp, Pbuf_t* pbuf, TcpPktInfo_t* pi, Pbuf_t **ret)
{
    /*
        RFC 5961 recommends that in these synchronized states, if the SYN bit is set,
        irrespective of the sequence number, TCP endpoints MUST send a "challenge ACK" to the remote peer:
    */
    if ((pi->thFlags & DP_TH_SYN) != 0) {
        if (TcpState(tcp) != TCP_TIME_WAIT) {
            DP_INC_TCP_STAT(pbuf->wid, DP_TCP_RCV_SYN_ESTABLISHED);
            *ret = TcpTryGenChallengeAckPkt(tcp);
        }
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_DROP_CONTROL_PKTS);
        return -1;
    }

    if ((pi->thFlags & DP_TH_ACK) == 0) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_PKT_WITHOUT_ACK);
        return -1;
    }

    /* RFC 5961 recommends that the ACK value is acceptable only if it is in the range of
     * ((SND.UNA - MAX.SND.WND) =< SEG.ACK =< SND.NXT). All incoming segments whose ACK value doesn't satisfy
     * the above condition MUST be discarded and an ACK sent back.
     */
    // ack 超出sndMax
    if (TcpSeqGt(pi->ack, tcp->sndMax)) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_ACK_TOO_MUCH);
        if (pi->dataLen > 0) {
            *ret = TcpTryGenChallengeAckPkt(tcp);
        }
        return -1;
    }

    // ack 小于sndUna
    if (TcpSeqLt(pi->ack, tcp->sndUna)) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_OLD_ACK);
        /* RFC5961:收到报文的SEQ.ACK<SND.UNA-MAX.SND.WND时回复Challenge ACK
           对ack在 (sndUna-wnd, sndUna)范围内报文不做处理 */
        if (TcpSeqLt(pi->ack, tcp->sndUna - tcp->sndWnd)) {
            if (pi->dataLen > 0) {
                *ret = TcpTryGenChallengeAckPkt(tcp);
            }
            return -1;
        }
    }

    if (TcpPreProcPktData(tcp, pbuf, pi, ret) != 0) {
        return -1;
    }

    // 接受对端FIN报文后，不再处理超窗数据报文，仍然接收处理重传的乱序数据报文
    if (TCP_HAS_RECV_FIN(tcp) && TcpSeqGt(pi->seq, tcp->rcvMax)) {
        return -1;
    }

    return 0;
}

static inline uint32_t TcpCalcMrtt(uint32_t mrtt)
{
    return (int)mrtt <= 0 ? 1 : mrtt;
}

static void TcpUpdateRtt(TcpSk_t* tcp, TcpPktInfo_t* pktInfo)
{
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_RTT_UPDATED);
    uint32_t mrtt = TcpCalcMrtt(TcpGetRttTick(tcp) - pktInfo->tsEcho);
    if (tcp->srtt == 0) {
    /* rfc 6298  1. first rtt : srtt =  r rttval = r / 2  rto = srtt + max(g, k * rttval) */
        tcp->srtt = mrtt << 3; // 3: Mrtt * 8
        tcp->rttval = mrtt << 1; // 1: 内部计算使用4 * rttval 即 2 * mrtt
    } else {
        uint32_t srtt = tcp->srtt;
        srtt >>= 3; // 3: srtt / 8
        tcp->rttval -= tcp->rttval >> 2; // 2: 3 * rttval
        tcp->rttval += srtt > mrtt ? srtt - mrtt : mrtt - srtt; // |srtt - r|
        tcp->srtt -= srtt; // 7 * srtt
        tcp->srtt += mrtt;
    }
    tcp->maxRtt = UTILS_MAX(tcp->maxRtt, tcp->rttval);
    tcp->rttFlag = 0;
}

void TcpAdjustCookieRtt(TcpSk_t* tcp, TcpPktInfo_t* pi)
{
    pi->tsEcho = TcpGetRttTick(tcp);
    tcp->srtt = 0;
    TcpUpdateRtt(tcp, pi);
}

static void TcpInsertListenerComplete(TcpSk_t* parent, TcpSk_t* child)
{
    Sock_t* sk = TcpSk2Sk(child);

    // 被动建链这里无需加锁
    SOCK_SET_CONNECTED(sk);
    SOCK_SET_RECV_MORE(sk);
    SOCK_SET_SEND_MORE(sk);
    SOCK_SetState(sk, SOCK_STATE_WRITE | SOCK_STATE_WRITE_ET);
    DP_INC_TCP_STAT(child->wid, DP_TCP_ACCEPTS);

    SOCK_Lock(TcpSk2Sk(parent));

    LIST_REMOVE(&parent->uncomplete, child, childNode);
    LIST_INSERT_TAIL(&parent->complete, child, childNode);

    SOCK_WakeupRdSem(TcpSk2Sk(parent));
    SOCK_SetState(TcpSk2Sk(parent), SOCK_STATE_READ | SOCK_STATE_READ_ET);
    SOCK_Unlock(TcpSk2Sk(parent));

    if (CFG_GET_TCP_VAL(CFG_TCP_TSQ_PASSIVE) == DP_ENABLE) {
        // 限制共线程被动调度模式下才添加此事件，否则和 TcpAccept 之间存在并发问题
        TcpTsqAddQue(child, TCP_TSQ_PCONNECTED);
    }
}

static void TcpSetEstablish(TcpSk_t* tcp, TcpPktInfo_t* pktInfo)
{
    TcpDeactiveConKeepTimer(tcp);
    TcpDeactiveRexmitTimer(tcp);

    TcpSetState(tcp, TCP_ESTABLISHED);
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_CONNECTS);

    tcp->idleStart = TcpGetSlowTick(tcp);
    tcp->lastChallengeAckTime = TcpGetRttTick(tcp);
    tcp->connLatency = UTILS_TimeNow() - tcp->startConn;

    // 开启保活选项后，启动保活定时器;
    if (TcpSk2Sk(tcp)->keepalive != 0) {
        TcpActiveKeepTimer(tcp);
    }

    tcp->sndUna = pktInfo->ack;
    tcp->sndSml = pktInfo->ack;

    TcpCaInit(tcp);

    if (TcpNegTs(tcp)) {
        tcp->mss -= DP_TCPOLEN_TSTAMP_APPA;
    }

    if (TCP_SACK_AVAILABLE(tcp)) {
        tcp->sackInfo->rcvSackEnd = tcp->sndUna;
    }
}

Pbuf_t* TcpProcListen(TcpSk_t* tcp, TcpSynOpts_t* synOpts, TcpPktInfo_t* pi)
{
    Pbuf_t* ret = NULL;
    TcpSk_t* parent = tcp->parent;

    TcpNegotiateOpts(tcp, synOpts);

    TcpNotifyEvent(TcpSk2Sk(parent), SOCK_EVENT_RCVSYN, tcp->tsqNested);
    // 用户会在 RCVSYN 里调用 Close 关闭监听 socket ，此时 tcp 已经被释放
    if (SOCK_IS_CLOSED(TcpSk2Sk(parent))) {
        return NULL;
    }

    tcp->startConn = UTILS_TimeNow();

    tcp->sndWl1 = pi->seq;
    tcp->sndWnd = pi->sndWnd;

    // 无论成功与否都加入重传定时器，统一在最后进行返回
    ret = TcpGenSynAckPkt(tcp);
    tcp->rttFlag = 1;

    TcpActiveConKeepTimer(tcp);
    TcpActiveInitialRexmitTimer(tcp);

    return ret;
}

static Pbuf_t* TcpProcSynSent(TcpSk_t* tcp, DP_TcpHdr_t* tcpHdr, TcpPktInfo_t* pktInfo)
{
    TcpSynOpts_t opts = {0};
    Pbuf_t*   ret = NULL;

    if ((pktInfo->thFlags & DP_TH_ACK) != 0 && pktInfo->ack != tcp->sndNxt) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_SYN_SENT_RCV_ERR_ACK);
        uint32_t realSndNxt = tcp->sndNxt;
        tcp->sndNxt = pktInfo->ack;
        ret = TcpGenRstPkt(tcp);
        tcp->sndNxt = realSndNxt;
        goto out;
    }

    // 没有syn标记，或者携带有数据报文，直接丢弃
    if ((pktInfo->thFlags & DP_TH_SYN) == 0 || pktInfo->dataLen != 0) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_SYN_SENT_RCV_NO_SYN);
        goto out;
    }

    if (TcpParseSynOpts((uint8_t*)(tcpHdr + 1), pktInfo->hdrLen - sizeof(DP_TcpHdr_t), &opts) != 0) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_ERR_SYNACK_OPT);
    }
    TcpNegotiateOpts(tcp, &opts);

    if ((pktInfo->thFlags & DP_TH_ACK) == 0) {
        tcp->sndWl1 = pktInfo->seq;
        tcp->rcvNxt = pktInfo->endSeq;
        tcp->rcvAdvertise = pktInfo->endSeq;
        tcp->sndWnd = pktInfo->sndWnd;
        tcp->sndNxt = tcp->sndUna;
        TcpSetState(tcp, TCP_SYN_RECV); // 同时建链
        ret = TcpGenSynAckPkt(tcp);
        goto out;
    }

    tcp->irs    = pktInfo->seq;
    tcp->rcvNxt = pktInfo->endSeq;
    tcp->rcvAdvertise = pktInfo->endSeq;
    /* SYN ACK报文 发送的窗口为完整窗口，无需扩大 */
    tcp->sndWnd = pktInfo->sndWnd;
    tcp->rcvUp  = 0;
    tcp->sndWl1 = pktInfo->seq;
    pktInfo->tsEcho = tcp->tsVal;
    TcpSetEstablish(tcp, pktInfo);
    if ((pktInfo->pktType & TCP_PI_HAS_TIMESTAMP) != 0 || tcp->rttFlag == 1) {
        TcpUpdateRtt(tcp, pktInfo);
    }
    TcpTsqAddQue(tcp, TCP_TSQ_CONNECTED);

    ret = TcpGenAckPkt(tcp);

out:
    return ret;
}

static bool TcpCheckDeferAccept(TcpSk_t* tcp, TcpPktInfo_t* pktInfo)
{
    // 同时建链场景或未开启deferAccept选项则不进行处理
    if (tcp->parent == NULL || tcp->deferAccept == 0) {
        return false;
    }

    // 携带FIN标记的报文则忽略deferAccept选项
    if ((pktInfo->thFlags & DP_TH_FIN) != 0) {
        return false;
    }

    // 开启deferAccept且未携带数据报文则不进行建链
    if (pktInfo->dataLen == 0) {
        return true;
    }

    return false;
}

static int SynRecvProcSyn(TcpSk_t* tcp, TcpPktInfo_t* pktInfo, Pbuf_t **ret, uint32_t *reason)
{
    if (pktInfo->thFlags == DP_TH_SYN) {
        if (pktInfo->seq == tcp->irs && pktInfo->ack == 0) { // 重复的syn报文，立即触发synack响应
            tcp->sndNxt = tcp->sndUna;
            *ret = TcpGenSynAckPkt(tcp);
            return -1;
        }
        *reason = DP_TCP_RCV_DUP_SYN;
        return 0;
    }
    /* RFC5961: SYN_RECEIVED 状态时, 收到 SYN 报文时回复挑战 ACK */
    *ret = TcpTryGenChallengeAckPkt(tcp);
    *reason = DP_TCP_SYN_RECV_UNEXPECT_SYN;
    return 0;
}

static void SynRecvProcUnexpectAck(TcpSk_t* tcp, TcpPktInfo_t* pktInfo, Pbuf_t **ret, uint32_t *reason)
{
    uint32_t realSndNxt = tcp->sndNxt;
    tcp->sndNxt = pktInfo->ack;
    *ret = TcpGenRstPkt(tcp);
    tcp->sndNxt = realSndNxt;
    *reason = DP_TCP_RCV_INVALID_ACK;
}

static int SynRecvPktCheck(TcpSk_t* tcp, DP_TcpHdr_t* tcpHdr, TcpPktInfo_t* pktInfo, Pbuf_t **ret)
{
    uint32_t reason = 0;
    if ((pktInfo->thFlags & DP_TH_SYN) != 0) {
        if (SynRecvProcSyn(tcp, pktInfo, ret, &reason) != 0) {
            return -1;
        }
        goto drop;
    }

    if ((pktInfo->thFlags & (DP_TH_ACK)) == 0) {
        reason = DP_TCP_SYNRCV_NONACK;
        goto drop;
    }

    // 完全超窗报文直接丢弃
    if (TcpSeqGt(pktInfo->seq, tcp->rcvWup + tcp->rcvWnd)) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_DROP_CONTROL_PKTS);
        reason = DP_TCP_RCV_ERR_SEQ;
        goto drop;
    }

    // 完全重复报文直接丢弃
    if (TcpSeqLt(pktInfo->endSeq, tcp->rcvNxt)) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_DROP_CONTROL_PKTS);
        reason = DP_TCP_SYN_RECV_DUP_PACKET;
        goto drop;
    }

    if (pktInfo->ack != tcp->sndMax) { // 非法的ack或者序号
        SynRecvProcUnexpectAck(tcp, pktInfo, ret, &reason);
        goto drop;
    }

    if (TcpParseOptions(tcp, (uint8_t*)(tcpHdr + 1), pktInfo) != 0) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_ERR_ESTABLISH_ACK_OPT);
        if (pktInfo->errCode == TCP_PI_ERR_TIMESTAMP) {
            goto drop;
        }
    }

    // 处理DeferAccept选项
    if (TcpCheckDeferAccept(tcp, pktInfo)) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_CHECK_DEFFER_ACCEPT_ERR);
        goto drop;
    }

    return 0;
drop:
    if (reason != 0) {
        DP_INC_TCP_STAT(tcp->wid, reason);
    }
    ATOMIC32_Inc(&tcp->rcvDrops);
    return -1;
}

static void TcpProcParDupData(TcpSk_t* tcp, Pbuf_t* pbuf, TcpPktInfo_t* pktInfo)
{
    uint16_t dupLen = (uint16_t)UTILS_MIN((tcp->rcvNxt - pktInfo->seq), pktInfo->dataLen);
    if (dupLen != 0) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_PART_DUP_PACKET);
        DP_ADD_TCP_STAT(tcp->wid, DP_TCP_RCV_PART_DUP_BYTE, dupLen);
        pktInfo->dataLen -= dupLen;
        PBUF_CUT_DATA(pbuf, dupLen);
    }
}

Pbuf_t* TcpProcSynRecv(TcpSk_t* tcp, DP_TcpHdr_t* tcpHdr, Pbuf_t* pbuf, TcpPktInfo_t* pktInfo)
{
    Pbuf_t *ret = NULL;
    int policy = 0;

    if (SynRecvPktCheck(tcp, tcpHdr, pktInfo, &ret) != 0) {
        return ret;
    }
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_ACK_PACKET);

    if ((pktInfo->pktType & TCP_PI_HAS_TIMESTAMP) != 0 || tcp->rttFlag == 1) {
        TcpUpdateRtt(tcp, pktInfo);
    }
    /* update min rtt when recved third ack, -1 mean to use time stamp option */
    TcpRcvMinRttUpdate(tcp, -1, 0);

    TcpSetEstablish(tcp, pktInfo);

    if (tcp->parent != NULL) {
        TcpInsertListenerComplete(tcp->parent, tcp);
    } else {
        TcpTsqAddQue(tcp, TCP_TSQ_CONNECTED);
    }

    // 已经在之前判断过完全超窗的情况了，这里只能是部分超窗
    if (TcpSeqGt(pktInfo->endSeq, tcp->rcvWup + tcp->rcvWnd)) {
        (void)TcpProcOverWndData(tcp, pbuf, pktInfo, &ret);
    }

    // 报文序号大于rcvNxt，说明是乱序报文
    if (TcpSeqGt(pktInfo->seq, tcp->rcvNxt)) {
        policy = TcpProcUnorderPkt(tcp, pbuf, pktInfo);
    } else {
        // 此时报文可能部分重复，去除重复部分
        TcpProcParDupData(tcp, pbuf, pktInfo);
        tcp->sndWl1 = pktInfo->seq;
        tcp->sndWnd = pktInfo->sndWnd << tcp->sndWs;
        policy = TcpProcOrderPkt(tcp, pbuf, pktInfo);
    }
    return TcpProcReplyPolicy(tcp, policy);
}

static Pbuf_t* TcpProcRst(TcpSk_t* tcp, TcpPktInfo_t* pi)
{
    uint32_t rcvMax;
    Pbuf_t *ret = NULL;
    if (tcp->state == TCP_SYN_SENT) { // syn报文，rst的序号需要和tcp记录的序号匹配
        // 收到不合法的RST报文直接丢弃
        if (tcp->rcvNxt != pi->seq || (pi->thFlags & DP_TH_ACK) == 0 || pi->ack != tcp->sndMax) {
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_INVALID_RST);
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_DROP_CONTROL_PKTS);
            return NULL;
        }
    } else if (tcp->state == TCP_TIME_WAIT && CFG_GET_TCP_VAL(DP_CFG_TCP_RFC1337) == DP_ENABLE) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_RST_IN_RFC1337);
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_DROP_CONTROL_PKTS);
        return NULL;
    } else {
        if (TcpSeqGt(pi->seq, tcp->rcvWup + tcp->rcvWnd) || TcpSeqLt(pi->seq, tcp->rcvNxt)) { // 超窗的rst
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_EXD_WND_RST);
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_DROP_CONTROL_PKTS);
            return NULL;
        }
        /*
            rfc 9293
            If the RST bit is set and the sequence number does not exactly match the next expected sequence value,
            yet is within the current receive window,
            TCP endpoints MUST send an acknowledgment (challenge ACK)
        */
        rcvMax = TcpGetRcvMax(tcp);
        // 开始sack情况下，允许当前报文的序号与收到的最大的序号相等
        if ((pi->seq != tcp->rcvNxt) && !(TCP_SACK_AVAILABLE(tcp) && pi->seq == rcvMax)) {
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_INVALID_RST);
            ret = TcpTryGenChallengeAckPkt(tcp);
            return ret;
        }
    }

    // 连接建立失败的计数
    if (tcp->state == TCP_SYN_SENT) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_SYNSNT_CONN_DROPS);
    } else if (tcp->state == TCP_SYN_RECV) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_SYNRCV_CONN_DROPS);
    } else {        // >= TCP_ESTABLISHED
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_DROPS);
    }

    TCP_SET_RCVRST(tcp);
    // 在这里直接移除所有定时器和hash表
    TcpCleanUp(tcp);
    // TSQ 处理依赖 tcp 当前状态，因此不将状态设置为 CLOSED ，tcp 五元组已经清除，此连接不会再接收到报文而影响 tcp 状态
    TcpTsqAddQue(tcp, TCP_TSQ_RECV_RST);
    return NULL;
}

static void TcpProcDupAck(TcpSk_t* tcp, TcpPktInfo_t* pi)
{
    if (tcp->rexmitQue.bufLen == 0) { // 异常的重复ack报文
        return;
    }

    tcp->dupAckCnt++;
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_DUP_ACK);
    tcp->rttStartSeq = tcp->sndMax + 1;

    if (tcp->frto == 1) {
        TcpFrtoRcvDupAck(tcp, pi);
    } else {
        TcpCaDupAck(tcp);
    }
    if (TCP_IS_FAST_REXMIT(tcp)) {
        TcpFastRexmitPkt(tcp);
    } else if (TCP_IS_FAST_RECOVERY(tcp)) {
        TcpFastRecoryPkt(tcp);
    }
}

static int TcpUpdateTcpState(TcpSk_t* tcp)
{
    switch (tcp->state) {
        case TCP_FIN_WAIT1:
            TcpDeactiveRexmitTimer(tcp);
            if (TCP_IS_IN_KEEP(tcp)) {
                TcpDeactiveKeepTimer(tcp);
            }
            TcpSetState(tcp, TCP_FIN_WAIT2);
            if (TCP_IS_CLOSED(tcp)) {
                TcpActiveFinWaitTimer(tcp);
            }
            break;
        case TCP_LAST_ACK:
            TcpDone(tcp);
            return -1;
        case TCP_CLOSING:
            TcpDeactiveRexmitTimer(tcp);
            TcpSetState(tcp, TCP_TIME_WAIT);
            TcpActiveMslTimer(tcp);
            break;
        case TCP_FIN_WAIT2:
        case TCP_TIME_WAIT:
        case TCP_CLOSE_WAIT:
            break;
        default:
            ASSERT(0); // 逻辑上不会走到这里
    }
    return 0;
}

static int TcpAckUpdateTimerAndState(TcpSk_t* tcp, uint8_t* isFree)
{
    if (tcp->sndUna != tcp->sndMax) {
        // 坚持定时器只会发送一个字节，这里没有确认完数据，说明数据长度>1
        // 走到这里只可能是重传定时器
        TcpReactiveRexmitTimer(tcp);
        return 0;
    }

    // 发送队列无数据时修改 tcp 状态，否则继续发送数据
    if (tcp->sndQue.pktCnt == 0 && tcp->state != TCP_ESTABLISHED) {
        if (TcpUpdateTcpState(tcp) != 0) {
            *isFree = 1;
            return -1;
        }
    }
    // 重传场景下，确认了全部的数据则删除重传定时器
    if (TCP_IS_IN_REXMIT(tcp)) {
        TcpDeactiveRexmitTimer(tcp);
        // 对端虽然确认了数据但是窗口值未发生改变，如果后续有报文需要发送，在发送报文处重新添加坚持定时器
    } else if (TCP_IS_IN_PERSIST(tcp)) {
        TcpDeactivePersistTimer(tcp);
    }

    return 0;
}

static inline void TcpNotifyWritable(TcpSk_t* tcp)
{
    if (TcpGetSndSpace(tcp) > TcpSk2Sk(tcp)->sndLowat) {
        TcpTsqAddQue(tcp, TCP_TSQ_SET_WRITABLE);
    }
}

static inline void TcpNomAckUpdateRtt(TcpSk_t* tcp, TcpPktInfo_t* pi)
{
    // 仅新ACK报文才更新RTT
    if (TcpSeqLeq(pi->ack, tcp->sndUna)) {
        return;
    }

    /* 更新rtt场景
     * 1. 收到的报文携带时间戳信息
     * 2. 不携带时间戳时，只需要在当次发包的第一个ack时更新（tcp->tsVal记录的是第一个发包的时间，解析报文时被存到pi->tsEcho中）
     */
    if ((pi->pktType & TCP_PI_HAS_TIMESTAMP) != 0 ||
        (tcp->rttFlag != 0 && TcpSeqGt(pi->ack, tcp->rttStartSeq))) {
        TcpUpdateRtt(tcp, pi);
    }

    return;
}

static int TcpProcNomAck(TcpSk_t* tcp, TcpPktInfo_t* pi, uint8_t* isFree)
{
    if (TCP_IS_IN_PERSIST(tcp) && tcp->sndWnd > 0) {
        TcpDeactivePersistTimer(tcp);
    }

    TcpNomAckUpdateRtt(tcp, pi);

    uint32_t acked = pi->ack - tcp->sndUna;
    tcp->sndUna = pi->ack;
    if (tcp->frto == 1) {
        TcpFrtoRcvAck(tcp, pi, acked);
    } else {
        TcpCaAcked(tcp, acked, 0);
    }
    uint32_t oldPktCnt = tcp->rexmitQue.pktCnt;
    PBUF_CHAIN_CUT(&tcp->rexmitQue, acked);
    if (TcpSeqGt(tcp->sndUna, tcp->sndNxt)) {
        tcp->sndNxt = tcp->sndUna;
        tcp->rtxHead = PBUF_CHAIN_FIRST(&tcp->rexmitQue);
    }
    tcp->dupAckCnt = 0;

    DP_ADD_PKT_STAT(tcp->wid, DP_PKT_SEND_BUF_OUT, oldPktCnt - tcp->rexmitQue.pktCnt);
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_ACK_PACKET);
    DP_ADD_TCP_STAT(tcp->wid, DP_TCP_RCV_ACK_BYTE, acked);

    /* 这里说明此时重传了报文，但是对端仍未完全确认，需要接着在上次的基础上重传报文 */
    if (TCP_IS_IN_RECOVERY(tcp)) {
        TcpFastRecoryPkt(tcp);
    } else if (tcp->sndQue.bufLen > 0 || TcpSk2Sk(tcp)->sndBuf.bufLen > 0 || tcp->rexmitQue.bufLen > 0) {
        TcpTsqAddQue(tcp, TCP_TSQ_SEND_DATA);
    }

    TcpNotifyWritable(tcp);

    if (TcpAckUpdateTimerAndState(tcp, isFree) != 0) {
        return -1;
    }

    return pi->endSeq == pi->seq ? -1 : 0;
}

/*
1. pi->ack >= tcp->sndUna，老旧ack在preproc时丢弃
2. pi->seq >= tcp->rcvNxt, 可能为乱序报文，重复数据在preproc时丢弃
3. keepalive探测，在preproc时，标记acknow
4. 在这里状态只可能是>= TCP_ESTABLISH，SYN_SEND和SYN_RECV状态已经在这里之前处理
return: 0: 继续处理
-1: 纯ACK, 丢弃处理
*/
static int TcpProcAck(TcpSk_t* tcp, TcpPktInfo_t* pi, uint8_t* isFree)
{
    ASSERT(TcpSeqGeq(pi->ack, tcp->sndUna - tcp->sndWnd));
    ASSERT(TcpSeqGeq(pi->seq, tcp->rcvNxt));
    ASSERT(tcp->state != TCP_SYN_SENT);
    ASSERT(tcp->state != TCP_SYN_RECV);

    // 不处理老旧ack
    if (TcpSeqLt(pi->ack, tcp->sndUna)) {
        return 0;
    }

    if (tcp->caMeth->algId == TCP_CAMETH_BBR) {     // 更新最近发送/接收报文时的时间戳
        TcpUpdateMstamp(tcp);
    }
    tcp->rs.ackedSacked = 0;

    // 从pi中拿到sack信息的指针，读取并转换成sackHole，填写/更新到tcp中
    if (TCP_SACK_AVAILABLE(tcp)) {
        if (TcpProcSackAck(tcp, pi) != 0) {
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_ERR_SACKOPT);
        }
    }

    // 重复ACK、新ACK都需要更新对端时间戳
    if ((pi->seq == tcp->rcvNxt) && (pi->dataLen == 0)) {
        tcp->tsEcho = pi->tsVal;
    }

    if (pi->ack == tcp->sndUna) {
        if (TcpSeqGeq(pi->seq, tcp->sndWl1) && pi->sndWnd != tcp->sndWnd) { // 窗口更新报文
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_WND_UPDATE);
            tcp->sndWl1 = pi->seq;
            tcp->sndWnd = pi->sndWnd;
        } else if (pi->endSeq == pi->seq) {
            uint32_t lost = tcp->connLostCnt;
            TcpProcDupAck(tcp, pi);
            if (tcp->caMeth->algId == TCP_CAMETH_BBR) {
                TcpBwSample(tcp, tcp->connLostCnt - lost, &tcp->rs);
            }
            TcpCaCongCtrl(tcp);
            return -1;
        }
    } else {
        tcp->sndWl1 = pi->seq;
        tcp->sndWnd = pi->sndWnd;
    }

    return TcpProcNomAck(tcp, pi, isFree);
}

static int TcpAckDataPolicy(TcpSk_t* tcp, TcpPktInfo_t* pi)
{
    // 用户设置了逐包回复ACK则立即回复
    if (tcp->quickAckNum > 0) {
        tcp->quickAckNum--;
        return TCP_REPLY_POLICY_ACK;
    }

    // 延迟 ack 关闭，收到数据包数量超过限制发送 ack; 延迟 ack 打开，收到 9 个报文时立即回复 ack
    if ((tcp->delayAckEnable == 0 && tcp->accDataCnt >= tcp->accDataMax) ||
        (tcp->delayAckEnable != 0 && (int)(tcp->rcvNxt - tcp->rcvWup) >= 9 * tcp->mss)) { // 收到 9 个报文时立即回复 ack
        return TCP_REPLY_POLICY_ACK;
    }

    // 如果在延迟ack定时器中，则代表有收到的数据报文没有响应，不更新定时器超时时间
    if (!TCP_IS_IN_DELAY(tcp)) {
        tcp->tsEcho = pi->tsVal;    // 延迟ack仅更新第一个收到的报文时间戳
        TcpActiveDelayAckTimer(tcp);
    }

    DP_INC_TCP_STAT(tcp->wid, DP_TCP_DELAYED_ACK);      // 依靠delayAckEnable配置项区分

    return TCP_REPLY_POLICY_DATA;
}

static int TcpProcData(TcpSk_t* tcp, Pbuf_t* pbuf, TcpPktInfo_t* pi)
{
    TcpTsqAddQue(tcp, TCP_TSQ_RECV_DATA);

    if (!PBUF_CHAIN_IS_EMPTY(&tcp->reassQue)) {
        PBUF_REF(pbuf);
        TcpReassInsert(tcp, pbuf, pi);
        uint32_t reassLen = TcpReass(tcp, pi);
        DP_ADD_TCP_STAT(tcp->wid, DP_TCP_REASS_SUCC_BYTE, reassLen);
        // 无论是否重组成功，都应该回复ACK
        return TCP_REPLY_POLICY_ACK;
    }

    DP_TCP_STAT_RCV_DATA(tcp, pi->dataLen);
    PBUF_REF(pbuf);
    PBUF_ChainPush(&tcp->rcvQue, pbuf);
    tcp->rcvNxt += pi->dataLen;
    tcp->accDataCnt++;

    return TcpAckDataPolicy(tcp, pi);
}

// 此函数只处理顺序 fin 报文
static int TcpProcFin(TcpSk_t* tcp, int policy)
{
    switch (tcp->state) {
        case TCP_ESTABLISHED:
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_FIN);
            TcpSetState(tcp, TCP_CLOSE_WAIT);
            break;
        case TCP_FIN_WAIT1:
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_FIN);
            if (TCP_IS_IN_KEEP(tcp)) {
                TcpDeactiveKeepTimer(tcp);
            }
            TcpSetState(tcp, TCP_CLOSING);
            break;
        case TCP_FIN_WAIT2:
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_FIN);
            TcpDeactiveFinWaitTimer(tcp);
            TcpSetState(tcp, TCP_TIME_WAIT);
            TcpActiveMslTimer(tcp);
            break;
        case TCP_TIME_WAIT:
            // TIME_WAIT状态下接受到重复FIN报文应该重启2MSL定时器
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_RXMT_FIN);
            TcpReactiveMslTimer(tcp);
            break;
        case TCP_CLOSE_WAIT:
        case TCP_CLOSING:
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_RXMT_FIN);
            // 该两种状态下接收到重复FIN报文，返回重复ACK
            return TCP_REPLY_POLICY_DUPACK;
        case TCP_LAST_ACK:
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_RXMT_FIN);
            return policy;
        default:
            ASSERT(0); // 逻辑上保证不能走到这里
            break;
    }

    TcpTsqAddQue(tcp, TCP_TSQ_RECV_FIN);

    tcp->rcvNxt += 1;

    tcp->rcvMax = tcp->rcvNxt;

    return policy == TCP_REPLY_POLICY_DUPACK ? policy : TCP_REPLY_POLICY_ACK;
}

static Pbuf_t* TcpProcReplyPolicy(TcpSk_t* tcp, int policy)
{
    if (policy == TCP_REPLY_POLICY_DUPACK) { // 触发重复ACK
        return TcpGenDupAckPkt(tcp);
    }

    // 有数据，通过数据发送ACK
    if (tcp->sndQue.bufLen > 0) {
        TcpTsqAddQue(tcp, TCP_TSQ_SEND_DATA);
        return NULL;
    }

    if (policy == TCP_REPLY_POLICY_ACK) {
        return TcpGenAckPkt(tcp);
    }

    return NULL;
}

static int TcpProcUnorderPkt(TcpSk_t* tcp, Pbuf_t* pbuf, TcpPktInfo_t* pi)
{
    if (pi->dataLen > 0 || (pi->thFlags & DP_TH_FIN) != 0) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_OUT_ORDER_PACKET);
        DP_ADD_TCP_STAT(tcp->wid, DP_TCP_RCV_OUT_ORDER_BYTE, pi->dataLen);

        PBUF_REF(pbuf);
        TcpReassInsert(tcp, pbuf, pi);

        return TCP_REPLY_POLICY_DUPACK;
    }

    return 0;
}

static int TcpProcOrderPkt(TcpSk_t* tcp, Pbuf_t* pbuf, TcpPktInfo_t* pi)
{
    int policy = 0;

    if (pi->dataLen > 0) {
        policy = TcpProcData(tcp, pbuf, pi);
    }

    if ((pi->thFlags & DP_TH_FIN) != 0) {
        policy = TcpProcFin(tcp, policy);
    }

    // 顺序纯控制报文更新时间戳
    // 在policy为ACK及DUPACK时均更新当前报文时间戳，policy为DATA时为延迟ACK场景，仅更新第一个报文的时间戳
    if (pi->dataLen == 0 || policy != TCP_REPLY_POLICY_DATA) {
        tcp->tsEcho = pi->tsVal;
    }

    return policy;
}

static int TcpProcThFlags(TcpSk_t* tcp, Pbuf_t* pbuf, DP_TcpHdr_t* tcpHdr, TcpPktInfo_t* pi, Pbuf_t **ret)
{
    if ((pi->thFlags & DP_TH_RST) != 0) {
        *ret = TcpProcRst(tcp, pi);
        goto stat;
    }

    if (tcp->state == TCP_SYN_SENT) {
        *ret = TcpProcSynSent(tcp, tcpHdr, pi);
        goto stat;
    }

    if (tcp->state == TCP_SYN_RECV) {
        *ret = TcpProcSynRecv(tcp, tcpHdr, pbuf, pi);
        goto drop;      // 已在SynRecvPktCheck中统计rcvDrops
    }
    return 0;

stat:
    ATOMIC32_Inc(&tcp->rcvDrops);
drop:
    PBUF_Free(pbuf);
    return -1;
}

static void TcpProcSegment(TcpSk_t* tcp, Pbuf_t* pbuf, TcpPktInfo_t* pi, Pbuf_t **ret)
{
    int policy = 0;

    /*
    RFC 9293 3.10.7 This should not occur since a FIN has been received from the remote side.
    Ignore the segment text.
    */
    if (pi->dataLen > 0 && TCP_HAS_RECV_FIN(tcp)) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_DATA_AFTER_FIN);
        pi->dataLen = 0;
    }

    if (pi->seq != tcp->rcvNxt) {
        policy = TcpProcUnorderPkt(tcp, pbuf, pi);
    } else {
        policy = TcpProcOrderPkt(tcp, pbuf, pi);
    }

    if (pi->dataLen > 0) {
        tcp->keepIdleCnt = 0;
        if (TCP_SACK_AVAILABLE(tcp)) {
            TcpUpdateSackList(tcp, pi);
        }
    }

    if (*ret == NULL) {
        *ret = TcpProcReplyPolicy(tcp, policy);
    }
}

Pbuf_t* TcpInput(TcpSk_t* tcp, Pbuf_t* pbuf, DP_TcpHdr_t* tcpHdr, TcpPktInfo_t* pi)
{
    Pbuf_t* ret    = NULL;
    uint8_t isFree = 0;

    PBUF_SET_L4_OFF(pbuf);
    PBUF_CUT_HEAD(pbuf, pi->hdrLen);

    if (UTILS_UNLIKELY(TcpProcThFlags(tcp, pbuf, tcpHdr, pi, &ret) != 0)) {
        return ret;
    }

    if (TcpParseOptions(tcp, (uint8_t*)(tcpHdr + 1), pi) != 0) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_ERR_OPT);
        if (pi->errCode == TCP_PI_ERR_TIMESTAMP) {
            goto drop;
        }
    }

    pi->sndWnd <<= tcp->sndWs;

    if (UTILS_UNLIKELY(TcpPreProcPkt(tcp, pbuf, pi, &ret) != 0)) {
        goto drop;
    }

    // 建链状态下且开启保活定时器，此时更新保活定时器
    if (tcp->state >= TCP_ESTABLISHED && TcpSk2Sk(tcp)->keepalive != 0) {
        tcp->idleStart = TcpGetSlowTick(tcp);
        TcpUpdateKeepTimer(tcp);
    }

    if (UTILS_UNLIKELY(TcpProcAck(tcp, pi, &isFree) < 0)) {
        goto drop;
    }

    TcpProcSegment(tcp, pbuf, pi, &ret);
    PBUF_Free(pbuf);
    return ret;
drop:
    if (isFree == 0) {
        ATOMIC32_Inc(&tcp->rcvDrops);
    } else if (ret != NULL) {
        PBUF_Free(ret);
        ret = NULL;
    }
    PBUF_Free(pbuf);
    return ret;
}
