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

#include "tcp_types.h"

#include "tcp_out.h"
#include "tcp_timer.h"
#include "tcp_cc.h"
#include "tcp_tsq.h"
#include "tcp_sock.h"
#include "tcp_sack.h"

#include "utils_base.h"
#include "utils_cksum.h"
#include "utils_debug.h"

#define DEFAULT_TCP_MSS 576
#define TCP_CHALLENG_MIN_INTERVAL_TICK 1

static Pbuf_t* TcpProcRst(TcpSk_t* tcp, TcpPktInfo_t* pi);

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
    cksum += PBUF_CalcCksum(pbuf);

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

    return TcpCalcCksum(pbuf);
}

int TcpInitPktInfo(Pbuf_t* pbuf, DP_TcpHdr_t* tcpHdr, TcpPktInfo_t* pi)
{
    uint8_t thflags;
    DP_IpHdr_t* ipHdr = (DP_IpHdr_t*)PBUF_GET_L3_HDR(pbuf);

    if (PBUF_GET_SEG_LEN(pbuf) < sizeof(DP_TcpHdr_t)) {
        DP_INC_TCP_STAT(pbuf->wid, DP_TCP_RCV_SHORT);
        return -1;
    }

    pi->hdrLen = tcpHdr->off << 2; // 2: hdrLen占高位的4bit，需要乘以4
    if (pi->hdrLen > PBUF_GET_SEG_LEN(pbuf) || pi->hdrLen < sizeof(DP_TcpHdr_t)) {
        DP_INC_TCP_STAT(pbuf->wid, DP_TCP_RCV_BAD_OFF);
        return -1;
    }

    pi->dataLen = (uint16_t)PBUF_GET_PKT_LEN(pbuf);
    // 不允许向自己发送 TCP 报文
    if ((ipHdr->src == ipHdr->dst) && (tcpHdr->sport == tcpHdr->dport)) {
        return -1;
    }

    thflags = tcpHdr->flags & 0x3F;
    if (thflags == 0 || (thflags & (DP_TH_FIN | DP_TH_SYN)) == (DP_TH_FIN | DP_TH_SYN) ||
        (thflags & (DP_TH_FIN | DP_TH_RST)) == (DP_TH_FIN | DP_TH_RST)) {
        DP_INC_TCP_STAT(pbuf->wid, DP_TCP_DROP_CONTROL_PKTS);
        return -1;
    }

    if (TcpVerifyCksum(pbuf) != 0) {
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

    synOpts->ws = *(*opt) > 14 ? 14 : *(*opt);  // 14: tcp 的窗口缩放因子最大为 14
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
    // mss协商, 如果不协商mss选项，则采用一个安全值
    if (TcpHasMss(tcp) && (opts->rcvSynOpt & TCP_SYN_OPT_MSS) != 0) {
        tcp->mss    = (opts->mss < tcp->mss) ? opts->mss : tcp->mss;
        tcp->negOpt |= TCP_SYN_OPT_MSS;
    } else {
        tcp->mss = DEFAULT_TCP_MSS;
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
        pi->tsEcho = tcp->tsVal;
        return 0;
    }
    uint32_t timeVal = UTILS_BYTE2LONG(opt);
    timeVal = UTILS_NTOHL(timeVal);
    if (TcpSeqLt(timeVal, tcp->tsEcho)) {
        return -1;
    }
    uint32_t timeEcho = UTILS_BYTE2LONG(opt + 4);
    pi->tsEcho = UTILS_NTOHL(timeEcho);
    tcp->tsEcho = timeVal;
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
                if (opt >= optEnd) {
                    return -1;
                }
                optSize = *opt++;
                if (((optSize - TCP_OPTLEN_BASE_SACK) % TCP_OPTLEN_SACK_PERBLOCK) != 0 &&
                    (optSize - 2) <= (optEnd - opt)) {              // 2: 去掉前面的类型和长度，已在opt偏移
                    return -1;
                }
                pi->sackOpt = opt - 1;  // 取出指针从size开始，需要退回1位
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
            default:        // 其他选项不做处理
                return -1;
        }
        opt += optSize - 2;     // 2: 去掉前面的类型和长度
    }
    return 0;
}

static int TcpParseOptions(TcpSk_t* tcp, uint8_t* opt, TcpPktInfo_t* pi)
{
    uint8_t optLen  = pi->hdrLen - sizeof(DP_TcpHdr_t);

    if (optLen == 0) {
        if (TcpNegTs(tcp)) {
            return -1;
        }

        pi->tsEcho = tcp->tsVal;
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
    if (TIME_CMP(tcp->lastChallengeAckTime + TCP_CHALLENG_MIN_INTERVAL_TICK, nowTick) <= 0) {
        ret = TcpGenAckPkt(tcp);
        tcp->lastChallengeAckTime = nowTick;
    }
    return ret;
}

static int TcpProcDupData(TcpSk_t* tcp, Pbuf_t* pbuf, TcpPktInfo_t* pi, Pbuf_t **ret)
{
    if ((pi->thFlags & DP_TH_FIN) != 0) {
        if (TCP_HAS_RECV_FIN(tcp) && pi->endSeq == tcp->rcvMax) {
            *ret = TcpTryGenChallengeAckPkt(tcp);
            if (*ret != NULL) {
                DP_INC_TCP_STAT(tcp->wid, DP_TCP_RESPONSE_CHALLENGE_ACKS);
            }

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
        if (*ret != NULL) {
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_RESPONSE_CHALLENGE_ACKS);
        }
        return -1;
    }

    uint32_t dupLen = tcp->rcvNxt - pi->seq;
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_PART_DUP_PACKET);
    DP_ADD_TCP_STAT(tcp->wid, DP_TCP_RCV_PART_DUP_BYTE, dupLen);
    pi->seq += dupLen;
    pi->dataLen -= (uint16_t)dupLen;
    PBUF_CUT_DATA(pbuf, dupLen);
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
        if (*ret != NULL) {
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_RESPONSE_CHALLENGE_ACKS);
        }
        return -1;
    }

    // 部分超窗数据, 去除超过部分数据
    if (TcpSeqGt(pi->endSeq, wndEnd)) {
        DP_ADD_TCP_STAT(tcp->wid, DP_TCP_RCV_BYTE_AFTER_WND, pi->endSeq - wndEnd);
        if ((pi->thFlags & DP_TH_FIN) != 0) {
            pi->thFlags &= ~DP_TH_FIN;
            pi->endSeq -= 1;
        }

        uint32_t dupLen = pi->endSeq - wndEnd;
        PBUF_CutTailData(pbuf, (uint16_t)dupLen);
        pi->endSeq -= dupLen;
        pi->dataLen -= (uint16_t)dupLen;
    }

    return 0;
}

static int TcpPreProcPktData(TcpSk_t* tcp, Pbuf_t* pbuf, TcpPktInfo_t* pi, Pbuf_t **ret)
{
    // 超窗数据
    if (TcpSeqGt(pi->endSeq, tcp->rcvWup + tcp->rcvWnd)) {
        if (TcpProcOverWndData(tcp, pbuf, pi, ret) != 0) {
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_DROP_DATA_PKTS);
            return -1;
        }
    }

    // RFC 9293  3.6.1 主动断链情况下收到数据，应该回复RST告知对端数据丢失
    if (tcp->state == TCP_FIN_WAIT1 && pi->dataLen > 0) {
        tcp->sndNxt = pi->ack;
        *ret = TcpGenRstPkt(tcp);
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_DROP_DATA_PKTS);
        return -1;
    }

    // 重复数据
    if (TcpSeqLt(pi->seq, tcp->rcvNxt)) {
        // 重复报文处理
        if (TcpProcDupData(tcp, pbuf, pi, ret) != 0) {
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_DROP_DATA_PKTS);
            return -1;
        }
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
            if (*ret != NULL) {
                DP_INC_TCP_STAT(tcp->wid, DP_TCP_RESPONSE_CHALLENGE_ACKS);
            }
        }
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_DROP_CONTROL_PKTS);
        return -1;
    }

    if ((pi->thFlags & DP_TH_ACK) == 0) {
        return -1;
    }

    // ack 超出sndMax，此时判断为异常报文，直接丢弃处理
    if (TcpSeqGt(pi->ack, tcp->sndMax)) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_ACK_TOO_MUCH);
        return -1;
    }

    // ack 小于sndUna
    if (TcpSeqLt(pi->ack, tcp->sndUna)) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_DUP_ACK);
        return -1;
    }

    if (TcpPreProcPktData(tcp, pbuf, pi, ret) != 0) {
        return -1;
    }

    // 接受对端FIN报文后，不再处理超窗数据报文，仍然接收处理重传的乱序数据报文
    if (TCP_HAS_RECV_FIN(tcp) && TcpSeqGt(pi->endSeq, tcp->rcvMax)) {
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
    SOCK_SetState(sk, SOCK_STATE_WRITE);
    DP_INC_TCP_STAT(child->wid, DP_TCP_ACCEPTS);

    SOCK_Lock(TcpSk2Sk(parent));

    LIST_REMOVE(&parent->uncomplete, child, childNode);
    LIST_INSERT_TAIL(&parent->complete, child, childNode);

    SOCK_WakeupRdSem(TcpSk2Sk(parent));
    SOCK_SetState(TcpSk2Sk(parent), SOCK_STATE_READ);

    SOCK_Unlock(TcpSk2Sk(parent));
}

static void TcpSetEstablish(TcpSk_t* tcp, TcpPktInfo_t* pktInfo)
{
    TcpDeactiveConKeepTimer(tcp);
    TcpDeactiveRexmitTimer(tcp);

    TcpSetState(tcp, TCP_ESTABLISHED);
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_CONNECTS);

    tcp->idleStart = (uint16_t)TcpGetSlowTick(tcp);
    tcp->lastChallengeAckTime = TcpGetRttTick(tcp);

    // 开启保活选项后，启动保活定时器;
    if (TcpSk2Sk(tcp)->keepalive != 0) {
        TcpActiveKeepTimer(tcp);
    }

    tcp->sndUna = pktInfo->ack;

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

    TcpNegotiateOpts(tcp, synOpts);

    TcpSetState(tcp, TCP_SYN_RECV);

    tcp->sndWl1 = pi->seq;
    tcp->sndWnd = pi->sndWnd;

    // 无论成功与否都加入重传定时器，统一在最后进行返回
    ret = TcpGenSynAckPkt(tcp);

    TcpActiveConKeepTimer(tcp);
    TcpActiveInitialRexmitTimer(tcp);

    return ret;
}

static Pbuf_t* TcpProcSynSent(TcpSk_t* tcp, DP_TcpHdr_t* tcpHdr, TcpPktInfo_t* pktInfo)
{
    TcpSynOpts_t opts = {0};
    Pbuf_t*   ret = NULL;

    if ((pktInfo->thFlags & DP_TH_ACK) != 0 && pktInfo->ack != tcp->sndNxt) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_DROP_CONTROL_PKTS);
        ret = TcpGenRstAckPkt(tcp);
        goto out;
    }

    // 没有syn标记，或者携带有数据报文，直接丢弃
    if ((pktInfo->thFlags & DP_TH_SYN) == 0 || pktInfo->dataLen != 0) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_DROP_CONTROL_PKTS);
        goto out;
    }

    if (TcpParseSynOpts((uint8_t*)(tcpHdr + 1), pktInfo->hdrLen - sizeof(DP_TcpHdr_t), &opts) != 0) {
        goto out;
    }
    TcpNegotiateOpts(tcp, &opts);

    if ((pktInfo->thFlags & DP_TH_ACK) == 0) {
        tcp->sndWl1 = pktInfo->seq;
        tcp->rcvNxt = pktInfo->endSeq;
        tcp->sndWnd = pktInfo->sndWnd;
        tcp->sndNxt = tcp->sndUna;
        TcpSetState(tcp, TCP_SYN_RECV); // 同时建链
        ret = TcpGenSynAckPkt(tcp);
        goto out;
    }

    tcp->irs    = pktInfo->seq;
    tcp->rcvNxt = pktInfo->endSeq;
    tcp->sndWnd = pktInfo->sndWnd << tcp->sndWs;
    tcp->rcvUp  = 0;
    tcp->sndWl1 = pktInfo->seq;
    pktInfo->tsEcho = tcp->tsVal;
    TcpSetEstablish(tcp, pktInfo);
    TcpUpdateRtt(tcp, pktInfo);
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_SEGS_TIMED);
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

static void SynRecvTryProcData(TcpSk_t* tcp, Pbuf_t* pbuf, TcpPktInfo_t* pktInfo)
{
    // 已经在之前判断过完全超窗的情况了，这里只能是部分超窗
    if (TcpSeqGt(pktInfo->endSeq, tcp->rcvWup + tcp->rcvWnd)) {
        (void)TcpProcOverWndData(tcp, pbuf, pktInfo, NULL);
    }
    tcp->rcvNxt = pktInfo->endSeq;
    PBUF_ChainPush(&tcp->rcvQue, pbuf);
    // SYNRECV一定会丢弃报文，这里需要添加下引用计数
    PBUF_REF(pbuf);
    TcpTsqAddQue(tcp, TCP_TSQ_RECV_DATA);
}

static int SynRecvPktCheck(TcpSk_t* tcp, DP_TcpHdr_t* tcpHdr, TcpPktInfo_t* pktInfo, Pbuf_t **ret)
{
    if (pktInfo->thFlags == DP_TH_SYN) {
        if (pktInfo->seq == tcp->irs && pktInfo->ack == 0) { // 重复的syn报文，立即触发synack响应
            tcp->sndNxt = tcp->sndUna;
            *ret = TcpGenSynAckPkt(tcp);
            return -1;
        }

        return -1;
    }

    if ((pktInfo->thFlags & (DP_TH_ACK)) == 0) {
        return -1;
    }

    // 完全超窗报文直接丢弃
    if (TcpSeqGt(pktInfo->seq, tcp->rcvWup + tcp->rcvWnd)) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_DROP_CONTROL_PKTS);
        return -1;
    }

    if (pktInfo->ack != tcp->sndMax) { // 非法的ack或者序号
        *ret = TcpGenRstPkt(tcp);
        return -1;
    }

    if (TcpParseOptions(tcp, (uint8_t*)(tcpHdr + 1), pktInfo) != 0) {
        return -1;
    }

    // 处理DeferAccept选项
    if (TcpCheckDeferAccept(tcp, pktInfo)) {
        return -1;
    }

    return 0;
}

Pbuf_t* TcpProcSynRecv(TcpSk_t* tcp, DP_TcpHdr_t* tcpHdr, Pbuf_t* pbuf, TcpPktInfo_t* pktInfo)
{
    Pbuf_t *ret = NULL;

    if (SynRecvPktCheck(tcp, tcpHdr, pktInfo, &ret) != 0) {
        return ret;
    }

    TcpUpdateRtt(tcp, pktInfo);

    TcpSetEstablish(tcp, pktInfo);

    if (tcp->parent != NULL) {
        TcpInsertListenerComplete(tcp->parent, tcp);
    } else {
        TcpTsqAddQue(tcp, TCP_TSQ_CONNECTED);
    }

    if (pktInfo->seq != tcp->rcvNxt) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_OUT_ORDER_PACKET);
        DP_ADD_TCP_STAT(tcp->wid, DP_TCP_RCV_OUT_ORDER_BYTE, pktInfo->dataLen);
        PBUF_REF(pbuf);
        // SYNRECV处理一定会丢弃报文，所以这里需要增加报文的引用
        TcpReassInsert(tcp, pbuf, pktInfo);
        return TcpGenAckPkt(tcp);
    }

    // 确认不是乱序报文后才更新
    tcp->sndWl1 = pktInfo->seq;
    // 通知用户接收数据
    if (pktInfo->dataLen > 0) {
        SynRecvTryProcData(tcp, pbuf, pktInfo);
    }

    if ((pktInfo->thFlags & (DP_TH_FIN)) != 0) { // 同时断链场景下，需要通知关闭fd
        tcp->rcvNxt = pktInfo->endSeq;
        TcpSetState(tcp, TCP_CLOSE_WAIT);
        TcpTsqAddQue(tcp, TCP_TSQ_RECV_FIN);
        return TcpGenAckPkt(tcp);
    }
    return NULL;
}

static void TcpProcRstErr(TcpSk_t* tcp)
{
    switch (tcp->state) {
        case TCP_CLOSED:
        case TCP_LISTEN:
        case TCP_SYN_SENT:
        case TCP_SYN_RECV:
            TcpSk2Sk(tcp)->error = ECONNREFUSED;
            break;
        case TCP_CLOSE_WAIT:
            // 在收到FIN之后收到RST，表示对端发送的FIN由调用close产生
            // 对于recv等接口，如果断链会在SOCK_PopRcvBuf返回0，不会在SOCK_Recvmsg中处理sk->error
            TcpSk2Sk(tcp)->error = EPIPE;
            break;
        default:
            TcpSk2Sk(tcp)->error = ECONNRESET;
    }
}

static Pbuf_t* TcpProcRst(TcpSk_t* tcp, TcpPktInfo_t* pi)
{
    uint32_t rcvMax;
    Pbuf_t *ret = NULL;
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_RST);
    if (tcp->state == TCP_SYN_SENT) { // syn报文，rst的序号需要和tcp记录的序号匹配
        // 收到不合法的RST报文直接丢弃
        if (tcp->rcvNxt != pi->seq || (pi->thFlags & DP_TH_ACK) == 0 || pi->ack != tcp->sndMax) {
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_INVALID_RST);
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_DROP_CONTROL_PKTS);
            return NULL;
        }
    } else {
        if (TcpSeqGt(pi->seq, tcp->rcvWup + tcp->rcvWnd)) { // 超窗的rst
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
        if (pi->seq != tcp->rcvNxt || (TCP_SACK_AVAILABLE(tcp) && pi->seq != rcvMax)) {
            ret = TcpTryGenChallengeAckPkt(tcp);
            if (ret != NULL) {
                DP_INC_TCP_STAT(tcp->wid, DP_TCP_RESPONSE_CHALLENGE_ACKS);
            }
            return ret;
        }
    }

    if (tcp->state < TCP_ESTABLISHED) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_CONN_DROPS);  // 连接建立失败的数量，收到SYN之前
    } else {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_DROPS);  // 连接建立失败的数量，收到SYN之后
    }

    // 在这里直接移除所有定时器和hash表
    TcpCleanUp(tcp);
    TcpProcRstErr(tcp);
    // 主动建链或者未建链就接收到RST报文设置状态为CLOSED
    if (tcp->parent == NULL || tcp->state < TCP_ESTABLISHED) {
        TcpSetState(tcp, TCP_CLOSED);
    // 被动建链且未被用户接受的socket接收到RST报文更新状态为ABORT
    } else {
        TcpSetState(tcp, TCP_ABORT);
    }

    TcpTsqAddQue(tcp, TCP_TSQ_RECV_RST);
    return NULL;
}

static void TcpProcDupAck(TcpSk_t* tcp)
{
    if (tcp->rexmitQue.bufLen == 0) { // 异常的重复ack报文
        return;
    }

    tcp->dupAckCnt++;
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_DUP_ACK);
    tcp->rttStartSeq = tcp->sndMax + 1;

    TcpCaDupAck(tcp);
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
            TcpSetState(tcp, TCP_FIN_WAIT2);
            TcpDeactiveRexmitTimer(tcp);
            if (TCP_IS_IN_KEEP(tcp)) {
                TcpDeactiveKeepTimer(tcp);
            }
            TcpActiveFinWaitTimer(tcp);
            break;
        case TCP_LAST_ACK:
            TcpCleanUp(tcp);
            TcpSetState(tcp, TCP_CLOSED);
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_CLOSED);
            TcpFreeSk(TcpSk2Sk(tcp));
            return -1;
        case TCP_CLOSING:
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

static inline void TcpNotifyWritable(TcpSk_t* tcp)
{
    if (TcpGetSndSpace(tcp) > TcpSk2Sk(tcp)->sndLowat) {
        TcpTsqAddQue(tcp, TCP_TSQ_SET_WRITABLE);
    }
}

static int TcpProcNomAck(TcpSk_t* tcp, TcpPktInfo_t* pi)
{
    if (TCP_IS_IN_PERSIST(tcp) && tcp->sndWnd > 0) {
        TcpDeactivePersistTimer(tcp);
    }

    if (TcpSeqGeq(pi->ack, tcp->rttStartSeq)) { // 重传报文不进行rtt计算
        TcpUpdateRtt(tcp, pi);
    }

    uint32_t acked = pi->ack - tcp->sndUna;
    tcp->sndUna = pi->ack;
    TcpCaAcked(tcp, acked, 0);
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
    if (tcp->caState == TCP_CA_RECOVERY) {
        TcpFastRecoryPkt(tcp);
    } else if (tcp->sndQue.bufLen > 0 || TcpSk2Sk(tcp)->sndBuf.bufLen > 0 || tcp->rexmitQue.bufLen > 0) {
        TcpTsqAddQue(tcp, TCP_TSQ_SEND_DATA);
    }

    TcpNotifyWritable(tcp);

    if (tcp->sndUna == tcp->sndMax) {
        if (tcp->state != TCP_ESTABLISHED) {
            if (TcpUpdateTcpState(tcp) != 0) {
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
    } else {
        // 坚持定时器只会发送一个字节，这里没有确认完数据，说明数据长度>1
        // 走到这里只可能是重传定时器
        TcpReactiveRexmitTimer(tcp);
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
static int TcpProcAck(TcpSk_t* tcp, TcpPktInfo_t* pi)
{
    ASSERT(TcpSeqGeq(pi->ack, tcp->sndUna));
    ASSERT(TcpSeqGeq(pi->seq, tcp->rcvNxt));
    ASSERT(tcp->state != TCP_SYN_SENT);
    ASSERT(tcp->state != TCP_SYN_RECV);

    // 从pi中拿到sack信息的指针，读取并转换成sackHole，填写/更新到tcp中
    if (TCP_SACK_AVAILABLE(tcp)) {
        if (TcpProcSackAck(tcp, pi) != 0) {
            return -1;
        }
    }

    if (pi->ack == tcp->sndUna) {
        if (TcpSeqGeq(pi->seq, tcp->sndWl1) && pi->sndWnd != tcp->sndWnd) { // 窗口更新报文
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_WND_UPDATE);
            tcp->sndWl1 = pi->seq;
            tcp->sndWnd = pi->sndWnd;
        } else if (pi->endSeq == pi->seq) {
            TcpProcDupAck(tcp);
            return -1;
        }
    } else {
        tcp->sndWl1 = pi->seq;
        tcp->sndWnd = pi->sndWnd;
    }

    return TcpProcNomAck(tcp, pi);
}

/*
走到这个函数只可能是窗口内的数据报文
*/
static int TcpProcData(TcpSk_t* tcp, Pbuf_t* pbuf, TcpPktInfo_t* pi)
{
    if (pi->seq != tcp->rcvNxt) {
        DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_OUT_ORDER_PACKET);
        DP_ADD_TCP_STAT(tcp->wid, DP_TCP_RCV_OUT_ORDER_BYTE, pi->dataLen);
        TcpReassInsert(tcp, pbuf, pi);
        return TCP_REPLY_POLICY_DUPACK;
    }
    DP_ADD_TCP_STAT(tcp->wid, DP_TCP_RCV_BYTE, pi->dataLen);
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_PACKET);
    PBUF_ChainPush(&tcp->rcvQue, pbuf);
    tcp->rcvNxt = pi->endSeq;

    TcpTsqAddQue(tcp, TCP_TSQ_RECV_DATA);

    if (tcp->reassQue.bufLen > 0) {
        if (TcpReass(tcp) > 0) {
            if (TCP_IS_IN_DELAY(tcp)) {
                TcpDeactiveDelayAckTimer(tcp);
            }
        }
        // 无论是否重组成功，都应该回复ACK
        return TCP_REPLY_POLICY_ACK;
    }

    // 延迟 ack 关闭，收到数据包达到限制发送 ack
    if (tcp->delayAckEnable == 0) {
        return ++tcp->accDataCnt >= tcp->accDataMax ? TCP_REPLY_POLICY_ACK : TCP_REPLY_POLICY_DATA;
    }

    // 延迟 ack 打开，收到 10 个 mss 数据包回复 ack
    if ((int)(tcp->rcvNxt - tcp->rcvWup) >= 10 * tcp->mss) {
        return TCP_REPLY_POLICY_ACK;
    }
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_DELAYED_ACK);
    TcpActiveDelayAckTimer(tcp);

    return TCP_REPLY_POLICY_DATA;
}

static int TcpProcFin(TcpSk_t* tcp, TcpPktInfo_t* pi, int policy)
{
    switch (tcp->state) {
        case TCP_ESTABLISHED:
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_FIN);
            TcpSetState(tcp, TCP_CLOSE_WAIT);
            break;
        case TCP_FIN_WAIT1:
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_FIN);
            TcpSetState(tcp, TCP_CLOSING);
            break;
        case TCP_FIN_WAIT2:
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_FIN);
            TcpSetState(tcp, TCP_TIME_WAIT);
            TcpDeactiveFinWaitTimer(tcp);
            TcpActiveMslTimer(tcp);
            break;
        case TCP_TIME_WAIT:
            // TIME_WAIT状态下接受到重复FIN报文应该重启2MSL定时器
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_RCV_RXMT_FIN);
            TcpReactiveMslTimer(tcp);
            break;
        case TCP_CLOSE_WAIT:
        case TCP_CLOSING:
            // 该两种状态下接收到重复FIN报文，返回重复ACK
            return TCP_REPLY_POLICY_DUPACK;
        case TCP_LAST_ACK:
            return policy;
        default:
            ASSERT(0); // 逻辑上保证不能走到这里
            break;
    }

    /* 乱序FIN报文待重组完成后上报事件。如果带有数据，TcpProcData处会将rcvNxt赋值为endSeq，这里补充判断为顺序报文。
     * 如果是乱序FIN报文，rcvNxt不会被修改，如果满足rcvNxt == endSeq条件则该报文所有序号都已经被接收，会在TcpPreProcPkt处丢弃，
     * 不会执行到此处。所以只有顺序收到的FIN报文才能使条件为真 */
    if ((tcp->rcvNxt == pi->seq) || (pi->dataLen > 0 && tcp->rcvNxt == pi->endSeq)) {
        tcp->rcvNxt = pi->endSeq;
        TcpTsqAddQue(tcp, TCP_TSQ_RECV_FIN);
    }

    tcp->rcvMax = pi->endSeq;

    return policy == TCP_REPLY_POLICY_DUPACK ? policy : TCP_REPLY_POLICY_ACK;
}

Pbuf_t* TcpProcReplyPolicy(TcpSk_t* tcp, int policy)
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

Pbuf_t* TcpInput(TcpSk_t* tcp, Pbuf_t* pbuf, DP_TcpHdr_t* tcpHdr, TcpPktInfo_t* pi)
{
    Pbuf_t* ret    = NULL;
    int     policy = 0;

    PBUF_SET_L4_OFF(pbuf);
    PBUF_CUT_HEAD(pbuf, pi->hdrLen);

    if ((pi->thFlags & DP_TH_RST) != 0) {
        ret = TcpProcRst(tcp, pi);
        goto drop;
    }

    if (tcp->state == TCP_SYN_SENT) {
        ret = TcpProcSynSent(tcp, tcpHdr, pi);
        goto drop;
    }

    if (tcp->state == TCP_SYN_RECV) {
        ret = TcpProcSynRecv(tcp, tcpHdr, pbuf, pi);
        goto drop;
    }

    if (TcpParseOptions(tcp, (uint8_t*)(tcpHdr + 1), pi) != 0) {
        goto drop;
    }

    pi->sndWnd <<= tcp->sndWs;

    if (TcpPreProcPkt(tcp, pbuf, pi, &ret) != 0) {
        goto drop;
    }

    // 建链状态下且开启保活定时器，此时更新保活定时器
    if (tcp->state >= TCP_ESTABLISHED && TcpSk2Sk(tcp)->keepalive != 0) {
        tcp->idleStart = (uint16_t)TcpGetSlowTick(tcp);
        TcpUpdateKeepTimer(tcp);
    }

    if (TcpProcAck(tcp, pi) < 0) {
        goto drop;
    }

    if (pi->dataLen > 0) {
        policy = TcpProcData(tcp, pbuf, pi);
        if (TCP_SACK_AVAILABLE(tcp)) {
            TcpUpdateSackList(tcp, pi);
        }
    }

    if ((pi->thFlags & DP_TH_FIN) != 0) {
        policy = TcpProcFin(tcp, pi, policy);
    }

    ret = TcpProcReplyPolicy(tcp, policy);

    if (pi->dataLen == 0) {
        PBUF_Free(pbuf);
    }

    return ret;

drop:
    PBUF_Free(pbuf);

    return ret;
}
