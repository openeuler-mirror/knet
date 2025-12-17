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
/**
 * @file dp_tcp.h
 * @brief TCP首部及其相关定义
 */

#ifndef DP_TCP_H
#define DP_TCP_H

#include "dp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DP_TH_FIN  0x01 //!< TCP FIN标准位
#define DP_TH_SYN  0x02 //!< TCP SYN标准位
#define DP_TH_RST  0x04 //!< TCP RST标准位
#define DP_TH_PUSH 0x08 //!< TCP PUSH标准位
#define DP_TH_ACK  0x10 //!< TCP ACK标准位
#define DP_TH_URG  0x20 //!< TCP URG标准位

typedef struct DP_TcpHdr {
    uint16_t sport;
    uint16_t dport;
    uint32_t seq;
    uint32_t ack;
#if DP_BYTE_ORDER == DP_LITTLE_ENDIAN
    uint8_t resv : 4;
    uint8_t off : 4;
#else
    uint8_t off : 4;
    uint8_t resv : 4;
#endif
    uint8_t  flags;
    uint16_t win;
    uint16_t chksum;
    uint16_t urg;
} DP_PACKED DP_TcpHdr_t;

#define DP_TCPOPT_EOL            0 //!< EOL选项
#define DP_TCPOPT_NOP            1 //!< NOP选项
#define DP_TCPOPT_MAXSEG         2 //!< MSS选项
#define DP_TCPOPT_WINDOW         3 //!< Window scale选项
#define DP_TCPOPT_SACK_PERMITTED 4 //!< SACK协商选项
#define DP_TCPOPT_SACK           5 //!< SACK选项
#define DP_TCPOPT_TIMESTAMP      8 //!< 时间戳选项

#define DP_TCPOLEN_MAXSEG         4 //!< MSS选项长度
#define DP_TCPOLEN_WINDOW         3 //!< Window scale选项长度
#define DP_TCPOLEN_SACK_PERMITTED 2 //!< SACK协商选项长度
#define DP_TCPOLEN_TIMESTAMP      10 //!< 时间戳选项长度
#define DP_TCPOLEN_TSTAMP_APPA    (DP_TCPOLEN_TIMESTAMP + 2) //!< 时间戳选项长度(RFC793 APPENDA定义)
#define DP_TCPOLEN_MAX            40 //!< TCP选项的最长长度

void TcpProcTsq(int wid);

#ifdef __cplusplus
}
#endif
#endif
