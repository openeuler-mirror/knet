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
#ifndef TCP_SACK_H
#define TCP_SACK_H

#include "tcp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TCP_OPTLEN_BASE_SACK      2 //!< SACK选项基础长度，不包含控制块
#define TCP_OPTLEN_SACK_PERBLOCK  8 //!< SACK块每个大小
#define TCP_OPTLEN_SACK_APPA      4 //!< SACK选项对齐长度

#define TCP_SACK_HOLE_IS_FULL(tcp) (((tcp)->sackInfo->sackHoleNum) >= ((TcpSk2Sk(tcp)->sndHiwat) / ((tcp)->mss)))

void TcpInitSackInfo(TcpSackInfo_t** sackInfo);

void TcpDeinitSackInfo(TcpSackInfo_t* sackInfo);

int TcpProcSackAck(TcpSk_t* tcp, TcpPktInfo_t* pi);

void TcpUpdateSackList(TcpSk_t* tcp, TcpPktInfo_t* pi);

void TcpClearSackHole(TcpSackHoleHead* sackHoleHead);

void TcpClearSackInfo(TcpSackInfo_t *sackInfo, uint32_t sndUna);

Pbuf_t* TcpGetRexmitSack(TcpSk_t* tcp, uint32_t* sndSeq);

#ifdef __cplusplus
}
#endif

#endif
