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
    uint8_t  thFlags;
} TcpReassInfo_t;

static inline TcpReassInfo_t* TcpReassGetInfo(Pbuf_t* pbuf)
{
    ASSERT(PBUF_GET_CB_SIZE() >= sizeof(TcpReassInfo_t));

    return PBUF_GET_CB(pbuf, TcpReassInfo_t*);
}

/*
  Tcp 重组报文插入
  前提：
  1. 将 fin 报文作为数据报文处理

  这里需要处理三种报文：
  1. 纯数据报文
  2. 带 fin 的数据报文
  3. 纯 fin 报文

  处理关键逻辑：
  1. 收到报文按照序号进行插入
  2. 有重叠部分，裁剪掉待插入报文的重叠数据后再插入
  3. 带 fin 报文序号作为数据的结束，该序号后面的所有报文都丢弃

 */
int TcpReassInsert(TcpSk_t* tcp, Pbuf_t* pbuf, TcpPktInfo_t* pi);

uint32_t TcpReass(TcpSk_t* tcp, TcpPktInfo_t* pi);

#ifdef __cplusplus
}
#endif
#endif
