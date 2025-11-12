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

#ifndef TCP_IN_H
#define TCP_IN_H

#include "tcp_types.h"
#include "tcp_reass.h"
#include "tcp_cookie.h"
#include "dp_tcp.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
DUP ACK标记：
1. 接收到乱序报文，立即回复一个重复ACK

TRYDATA标记：如果tcp->sndQue > 0，则触发数据发送，否则生成一个ack
1. 收到顺序报文，需要回复ACK
2. 收到ACK报文，可以继续发送数据
3. 收到窗口扩大报文


*/
#define TCP_REPLY_POLICY_DATA   0
#define TCP_REPLY_POLICY_DUPACK 1
#define TCP_REPLY_POLICY_ACK    2

#define TCP_HAS_RECV_FIN(tcp) ((tcp)->state == TCP_CLOSING || (tcp)->state == TCP_CLOSE_WAIT || \
                               (tcp)->state == TCP_TIME_WAIT)

int TcpInitPktInfo(Pbuf_t* pbuf, DP_TcpHdr_t* tcpHdr, TcpPktInfo_t* pi);

int TcpParseSynOpts(uint8_t* opt, int optLen, TcpSynOpts_t* synOpts);

void TcpAdjustCookieRtt(TcpSk_t* tcp, TcpPktInfo_t* pi);

Pbuf_t* TcpProcListen(TcpSk_t* tcp, TcpSynOpts_t* synOpts, TcpPktInfo_t* pi);

Pbuf_t* TcpProcSynRecv(TcpSk_t* tcp, DP_TcpHdr_t* tcpHdr, Pbuf_t* pbuf, TcpPktInfo_t* pktInfo);

Pbuf_t* TcpInput(TcpSk_t* tcp, Pbuf_t* pbuf, DP_TcpHdr_t* tcpHdr, TcpPktInfo_t* pi);
#ifdef __cplusplus
}
#endif
#endif
