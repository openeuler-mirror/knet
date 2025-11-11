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

#ifndef SOCK_FNS_H
#define SOCK_FNS_H

#include <stdio.h>

#include "dp_socket_types_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Sock   Sock_t;
typedef struct NS_Net NS_Net_t;

#define SOCK_STATE_READ          0x001
#define SOCK_STATE_WRITE         0x002
#define SOCK_STATE_EXCEPTION     0x004
#define SOCK_STATE_CLOSE         0x008    // 独立状态，不会记录到sk->state中
#define SOCK_STATE_CANTSENDMORE  0x010    /* can't send more data to peer */
#define SOCK_STATE_CANTRCVMORE   0x020    /* can't receive more data from peer */

enum {
    SOCK_NOTIFY_TYPE_NONE,
    SOCK_NOTIFY_TYPE_EPOLL,
    SOCK_NOTIFY_TYPE_POLL,
    SOCK_NOTIFY_TYPE_SELECT,
    SOCK_NOTIFY_TYPE_USER,
    SOCK_NOTIFY_TYPE_MAX
};

typedef void (*SOCK_NotifyFn_t)(Sock_t* sk, void* ctx, uint8_t oldState, uint8_t newState);

int SOCK_Create(NS_Net_t* net, int domain, int type, int protocol, Sock_t** sk);

int SOCK_Connect(Sock_t* sk, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen);

int SOCK_Bind(Sock_t* sk, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen);

int SOCK_Listen(Sock_t* sk, int backlog);

int SOCK_Accept(Sock_t* sk, struct DP_Sockaddr* addr, DP_Socklen_t* addrlen, Sock_t** newSk);

int SOCK_Ioctl(Sock_t* sk, int request, void* arg);

int SOCK_Fcntl(Sock_t* sk, int cmd, int val);

ssize_t SOCK_Sendmsg(Sock_t* sk, const struct DP_Msghdr* msg, int flags);

ssize_t SOCK_Sendto(
    Sock_t* sk, const void* buf, size_t len, int flags, const struct DP_Sockaddr* dstAddr, DP_Socklen_t addrlen);

ssize_t SOCK_Recvmsg(Sock_t* sk, struct DP_Msghdr* msg, int flags);

ssize_t SOCK_Recvfrom(
    Sock_t* sk, void* buf, size_t len, int flags, struct DP_Sockaddr* srcAddr, DP_Socklen_t* addrlen);

int SOCK_Setsockopt(Sock_t* sk, int level, int optname, const void* optval, DP_Socklen_t optlen);

int SOCK_Getsockopt(Sock_t* sk, int level, int optname, void* optval, DP_Socklen_t* optlen);

int SOCK_Getpeername(Sock_t* sk, struct DP_Sockaddr* addr, DP_Socklen_t* addrlen);

int SOCK_Getsockname(Sock_t* sk, struct DP_Sockaddr* addr, DP_Socklen_t* addrlen);

int SOCK_Close(Sock_t* sk);

int SOCK_Shutdown(Sock_t* sk, int how);

uint32_t SOCK_GetRWStateSafe(Sock_t* sk);

int SOCK_SetNotifyFn(int type, SOCK_NotifyFn_t notifyFn);

void SOCK_EnableNotify(Sock_t* sk, int type, void* ctx, int assocFd);
void SOCK_EnableNotifySafe(Sock_t* sk, int type, void* ctx, int assocFd);

void SOCK_DisableNotify(Sock_t* sk);
void SOCK_DisableNotifySafe(Sock_t* sk);

#ifdef __cplusplus
}
#endif
#endif
