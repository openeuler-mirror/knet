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

#ifndef TCP_CC_H
#define TCP_CC_H

#include "tcp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 按照一般性建议，当前默认初始值为 10 * mss */
#define TCP_INIT_CWND 10

typedef enum {
    TCP_CA_OPEN,
    TCP_CA_DISORDER,
    TCP_CA_CWR,
    TCP_CA_RECOVERY,
    TCP_CA_LOSS,
} TcpCaState_t;

#define TCP_IS_IN_RECOVERY(tcp) ((tcp)->caState == TCP_CA_RECOVERY)

static inline uint32_t TcpGetInitCwnd(TcpSk_t* tcp)
{
    return tcp->mss * TCP_INIT_CWND;
}

static inline int TcpCaInit(TcpSk_t* tcp)
{
    tcp->cwnd = tcp->mss * TCP_INIT_CWND;

    return tcp->caMeth->caInit(tcp);
}

static inline void TcpCaDeinit(TcpSk_t* tcp)
{
    if (tcp->caMeth->caDeinit != NULL) {
        tcp->caMeth->caDeinit(tcp);
    }
}

static inline void TcpCaRestart(TcpSk_t* tcp)
{
    tcp->caMeth->caRestart(tcp);
}

static inline void TcpCaAcked(TcpSk_t* tcp, uint32_t acked, uint32_t rtt)
{
    tcp->caMeth->caAcked(tcp, acked, rtt);
}

static inline void TcpCaDupAck(TcpSk_t* tcp)
{
    return tcp->caMeth->caDupAck(tcp);
}

static inline void TcpCaTimeout(TcpSk_t* tcp)
{
    tcp->caMeth->caTimeout(tcp);
}

const TcpCaMeth_t* TcpCaGet(int algId);

#ifdef __cplusplus
}
#endif
#endif
