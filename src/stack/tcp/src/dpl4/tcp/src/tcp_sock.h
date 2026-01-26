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

#include <unistd.h>
#include <pthread.h>

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

#define TCP_FLAGS_CLOSED 0x01  // 用户已经调用了 close
#define TCP_FLAGS_FREED  0x02  // tcp 已经释放
#define TCP_FLAGS_CLEANUP 0x04  // tcp 已经调用 cleanup
#define TCP_FLAGS_ABORT  0x08  // tcp 已经调用 Abort
#define TCP_FLAGS_RCVRST  0x10  // tcp 已经收到 RST
#define TCP_FLAGS_TSQ_EXCEPT 0x20 // tcp 已经处理了异常事件

#define TCP_SET_CLOSED(tcp)       ((tcp)->flags |= TCP_FLAGS_CLOSED)
#define TCP_IS_CLOSED(tcp)        (((tcp)->flags & TCP_FLAGS_CLOSED) != 0)
#define TCP_SET_FREED(tcp)       ((tcp)->flags |= TCP_FLAGS_FREED)
#define TCP_IS_FREED(tcp)        (((tcp)->flags & TCP_FLAGS_FREED) != 0)
#define TCP_SET_CLEANUP(tcp)       ((tcp)->flags |= TCP_FLAGS_CLEANUP)
#define TCP_IS_CLEANUP(tcp)        (((tcp)->flags & TCP_FLAGS_CLEANUP) != 0)
#define TCP_SET_ABORT(tcp)       ((tcp)->flags |= TCP_FLAGS_ABORT)
#define TCP_SET_RCVRST(tcp)       ((tcp)->flags |= TCP_FLAGS_RCVRST)
#define TCP_SET_TSQ_EXCEPT(tcp)       ((tcp)->flags |= TCP_FLAGS_TSQ_EXCEPT)

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

void TcpFreeUnconnectedSk(Sock_t* sk);

int TcpShutdown(Sock_t* sk, int how);

int TcpClose(Sock_t* sk);

void TcpCleanUp(TcpSk_t* tcp);

void TcpRemoveFromParentList(TcpSk_t* tcp);

TcpSk_t* TcpReuse(TcpSk_t* tcp, DP_TcpHdr_t* tcpHdr, TcpPktInfo_t* pi);

// 结束 tcp 连接并释放内存
static inline void TcpDone(TcpSk_t* tcp)
{
    DP_INC_TCP_STAT(tcp->wid, DP_TCP_CLOSED);

    if (!TCP_IS_CLEANUP(tcp)) {
        TcpCleanUp(tcp);
    }

    if (tcp->parent != NULL) {
        TcpRemoveFromParentList(tcp);
    }

    TcpSetState(tcp, TCP_CLOSED);
    TcpFreeSk(TcpSk2Sk(tcp));
}

enum {
    TCP_PRIV_OPS_INSERT_HASH,
    TCP_PRIV_OPS_REMOVE_HASH, // 移除hash表
};

static inline uint16_t TcpGetRsvPort(uint16_t minPort, uint16_t range)
{
    if (range == 0) {
        return minPort;
    }
    return minPort + RAND_GEN() % range;
}
extern TcpFamilyOps_t* g_tcpInetOps;
extern TcpFamilyOps_t* g_tcpInet6Ops;

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

static inline void TcpConnectRemove(TcpSk_t* tcp)
{
    if (TcpSk2Sk(tcp)->family == DP_AF_INET) {
        g_tcpInetOps->connectTblRemove(TcpSk2Sk(tcp));
    } else {
        g_tcpInet6Ops->connectTblRemove(TcpSk2Sk(tcp));
    }
}

extern __thread long int g_tcpCacheTid;
static inline long int TcpGetTid(void)
{
    if (g_tcpCacheTid == 0) {
        g_tcpCacheTid = (long int)pthread_self();
    }

    return g_tcpCacheTid;
}

#ifdef __cplusplus
}
#endif
#endif
