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

#ifndef TCP_INET_H
#define TCP_INET_H

#include "tcp_types.h"
#include "dp_tcp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TcpInetSk(sk)           ((InetSk_t*)(((TcpSk_t*)(sk)) + 1))
#define TcpInetSk2Sk(inetSk)    (Sock_t*)((uint8_t*)(inetSk) - sizeof(TcpSk_t))
#define TcpInetTcpSK(inetSk) TcpSK(TcpInetSk2Sk(inetSk))

static inline int TcpInetDevIsUp(Sock_t* sk)
{
    INET_FlowInfo_t* flow = &TcpInetSk(sk)->flow;
    if ((INET_GetDevByFlow(flow)->ifflags & DP_IFF_UP) == 0) {
        return -1;
    }
    return 0;
}

uint32_t TcpInetGenIsn(Sock_t* sk);

int TcpInetInit(void);

void TcpInetDeinit(void);

#ifdef __cplusplus
}
#endif
#endif
