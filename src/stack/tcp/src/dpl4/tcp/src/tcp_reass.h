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

#ifndef TCP_REASS_H
#define TCP_REASS_H

#include "tcp_types.h"

#include "pbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t seq;
    uint32_t endSeq;
    uint32_t ack;
    uint32_t sndWnd;
} TcpReassInfo_t;

static inline TcpReassInfo_t* TcpReassGetInfo(Pbuf_t* pbuf)
{
    ASSERT(PBUF_GET_CB_SIZE() >= sizeof(TcpReassInfo_t));

    return PBUF_GET_CB(pbuf, TcpReassInfo_t*);
}

int TcpReassInsert(TcpSk_t* tcp, Pbuf_t* pbuf, TcpPktInfo_t* pi);

uint32_t TcpReass(TcpSk_t* tcp);

#ifdef __cplusplus
}
#endif
#endif
