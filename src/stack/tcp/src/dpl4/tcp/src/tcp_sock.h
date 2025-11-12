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

#ifndef TCP_SOCK_H
#define TCP_SOCK_H

#include "tcp_types.h"
#include "tcp_tsq.h"
#include "dp_tcp.h"

#include "utils_spinlock.h"

#include "tcp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief tcp socket公共处理接口
 */

void TcpInitTcpSk(TcpSk_t* tcp);

void TcpInitChildTcpSk(Sock_t* newsk, Sock_t* parent, Pbuf_t* pbuf, DP_TcpHdr_t* tcpHdr);

uint32_t TcpGetRcvMax(TcpSk_t* tcp);

int TcpCanConnect(Sock_t* sk);

void TcpDoConnecting(Sock_t* sk);

int TcpAccept(Sock_t* sk, struct DP_Sockaddr* addr, DP_Socklen_t* addrlen, Sock_t** newSk);

int TcpDisconnect(Sock_t* sk);

int TcpSetSockOpt(Sock_t* sk, int level, int optName, const void* optVal, DP_Socklen_t optLen);

int TcpGetSockOpt(Sock_t* sk, int level, int optName, void* optVal, DP_Socklen_t* optLen);

int TcpSetKeepAlive(Sock_t *sk, int enable);

ssize_t TcpSendmsg(Sock_t* sk, const struct DP_Msghdr* msg, int flags,
                   size_t msgDataLen, size_t* index, size_t* offset);
ssize_t TcpRecvmsg(Sock_t* sk, struct DP_Msghdr* msg, int flags, size_t msgDataLen);

void TcpFreeSk(Sock_t* sk);

int TcpClose(Sock_t* sk);
void TcpCleanUp(TcpSk_t* tcp);
void TcpRemoveFromParentList(TcpSk_t* tcp);
enum {
    TCP_PRIV_OPS_INSERT_HASH,
    TCP_PRIV_OPS_REMOVE_HASH, // 移除hash表
};

static inline uint16_t TcpGetRsvPort(uint16_t minPort, uint16_t range)
{
    return minPort + RAND_GEN() % range;
}
extern TcpFamilyOps_t* g_tcpInetOps;
extern TcpFamilyOps_t* g_tcpInet6Ops;
extern TcpCfgCtx_t g_tcpCfgCtx;

static inline void TcpInsertListener(Sock_t* sk)
{
    if (sk->family == DP_AF_INET) {
        g_tcpInetOps->listenerInsert(sk);
    } else {
        g_tcpInet6Ops->listenerInsert(sk);
    }
}

static inline void TcpRemoveListener(Sock_t* sk)
{
    if (sk->family == DP_AF_INET) {
        g_tcpInetOps->listenerRemove(sk);
    } else {
        g_tcpInet6Ops->listenerRemove(sk);
    }
}

static inline void TcpWaitIdle(Sock_t* sk)
{
    if (sk->family == DP_AF_INET) {
        g_tcpInetOps->waitIdle(sk);
    } else {
        g_tcpInet6Ops->waitIdle(sk);
    }
}

static inline void TcpGlobalRemove(Sock_t* sk)
{
    if (sk->family == DP_AF_INET) {
        g_tcpInetOps->globalRemove(sk);
    } else {
        g_tcpInet6Ops->globalRemove(sk);
    }
}

#ifdef __cplusplus
}
#endif
#endif
