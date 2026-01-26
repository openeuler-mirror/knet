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
#ifndef TCP_OUT_H
#define TCP_OUT_H

#include "netdev.h"
#include "dp_tcp.h"
#include "pbuf.h"
#include "tcp_cookie.h"
#include "tcp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IP_INET_MAX_HDR_LEN 128

// 发送FIN且被ack的状态
#define TCP_CANT_REXMIT(tcp) ((tcp)->state == TCP_FIN_WAIT2 || (tcp)->state == TCP_TIME_WAIT || \
                              (tcp)->state == TCP_CLOSED)

uint16_t TcpCalcTxCksum(uint32_t pseudoHdrCksum, Pbuf_t* pbuf);

Pbuf_t* TcpGenCtrlPkt(TcpSk_t* tcp, uint8_t thflags, int upWnd);

// 在tcp处理时触发的控制报文使用
#define TcpGenSynAckPkt(tcp) TcpGenCtrlPkt((tcp), DP_TH_SYN | DP_TH_ACK, 1)
#define TcpGenAckPkt(tcp)    TcpGenCtrlPkt((tcp), DP_TH_ACK, 1)
#define TcpGenRstPkt(tcp)    TcpGenCtrlPkt((tcp), DP_TH_RST, 0)
#define TcpGenRstAckPkt(tcp) TcpGenCtrlPkt((tcp), DP_TH_RST | DP_TH_ACK, 0)
#define TcpGenDupAckPkt(tcp) TcpGenCtrlPkt((tcp), DP_TH_ACK, 0)

Pbuf_t* TcpGenRstPktByPkt(DP_TcpHdr_t* origTcpHdr, TcpPktInfo_t* pi);

Pbuf_t* TcpGenCookieSynAckPktByPkt(Pbuf_t* pbuf, TcpSk_t* parent, TcpPktInfo_t* pi, TcpSynOpts_t* opts, uint32_t iss);

// 计算可用发送窗口
uint32_t TcpCalcFreeWndSize(TcpSk_t* tcp);

// 判断报文是否满足发送条件
bool TcpCanSendPbuf(TcpSk_t* tcp, uint32_t pktLen, uint16_t mss, int force);

// 发送顺序报文
int TcpXmitData(TcpSk_t* tcp, uint8_t force, int isNeedRst);

// 发送控制报文，根据TCP状态发送报文
void TcpXmitCtrlPkt(TcpSk_t* tcp, uint8_t thflags);

// ICMPv6TooBig调用
void TcpRexmitAll(TcpSk_t* tcp);
// RTO时调用
void TcpRexmitPkt(TcpSk_t* tcp);
// 重传报文
void TcpRexmitQue(TcpSk_t* tcp, int isNeedRst);

#define TCP_IS_FAST_REXMIT(tcp) ((tcp)->dupAckCnt == (tcp)->reorderCnt)
#define TCP_IS_FAST_RECOVERY(tcp) ((tcp)->dupAckCnt > (tcp)->reorderCnt)

void TcpFastRexmitPkt(TcpSk_t* tcp);

void TcpFastRecoryPkt(TcpSk_t* tcp);

// 快速重传Sack
int TcpFastRexmitSack(TcpSk_t* tcp);

// 发送探测报文
void TcpSndProbePkt(TcpSk_t* tcp);

// 以下接口为独立传输报文使用
#define TcpXmitSynPkt(tcp)    TcpXmitCtrlPkt((tcp), DP_TH_SYN)
#define TcpXmitSynAckPkt(tcp) TcpXmitCtrlPkt((tcp), DP_TH_SYN | DP_TH_ACK)
#define TcpXmitAckPkt(tcp)    TcpXmitCtrlPkt((tcp), DP_TH_ACK)
#define TcpXmitRstPkt(tcp)    TcpXmitCtrlPkt((tcp), DP_TH_RST)
#define TcpXmitRstAckPkt(tcp) TcpXmitCtrlPkt((tcp), DP_TH_RST | DP_TH_ACK)
#define TcpXmitFinAckPkt(tcp) TcpXmitCtrlPkt((tcp), DP_TH_FIN | DP_TH_ACK)

#ifdef __cplusplus
}
#endif
#endif
