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
#ifndef SOCK_OPS_H
#define SOCK_OPS_H

#include <stdio.h>
#include <stdint.h>

#include "dp_socket_types_api.h"
#include "dp_debug_types_api.h"

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
#define SOCK_STATE_READ_ET       0x040
#define SOCK_STATE_WRITE_ET      0x080
#define SOCK_STATE_ET (SOCK_STATE_READ_ET | SOCK_STATE_WRITE_ET)

// 以下枚举如需修改顺序/新增，需要同步修改/补充对应ABN事件(DP_NOTIFY_RCVSYN_ERR等)
enum {
    SOCK_EVENT_NONE = 0,
    // TCP 被动建链收到 SYN 通知
    SOCK_EVENT_RCVSYN,
    // TCP 主动建链失败通知
    SOCK_EVENT_ACTIVE_CONNECTFAIL,
    // TCP 收到 FIN 通知(连接建立成功之后才会通知该事件)
    SOCK_EVENT_RCVFIN,
    // TCP 收到 RST 通知(连接建立成功之后才会通知该事件)
    SOCK_EVENT_RCVRST,
    // TCP 老化断链通知(连接建立成功之后才会通知该事件)
    SOCK_EVENT_DISCONNECTED,
    // 1. TCP 主动建链成功通知
    // 2. TCP 发送缓冲区有空间可以发送数据通知
    SOCK_EVENT_WRITE,
    // TCP 有数据需要用户接收时通知
    SOCK_EVENT_READ,
    // SOCK 控制块资源即将释放时通知
    SOCK_EVENT_FREE_SOCKCB,
    // 收到ICMPv6TOOBIG报文更新mtu通知
    SOCK_EVENT_UPDATE_MTU,
    SOCK_EVENT_MAX
};

enum {
    SOCK_NOTIFY_TYPE_NONE,
    SOCK_NOTIFY_TYPE_EPOLL,
    SOCK_NOTIFY_TYPE_POLL,
    SOCK_NOTIFY_TYPE_SELECT,
    SOCK_NOTIFY_TYPE_HOOK,
    SOCK_NOTIFY_TYPE_USER,
    SOCK_NOTIFY_TYPE_MAX
};

typedef void (*SOCK_NotifyFn_t)(Sock_t* sk, void* ctx, uint8_t oldState, uint8_t newState, uint8_t event);

int SOCK_Create(NS_Net_t* net, int domain, int type, int protocol, Sock_t** sk);

int SOCK_Connect(Sock_t* sk, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen);

int SOCK_Bind(Sock_t* sk, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen);

int SOCK_Listen(Sock_t* sk, int backlog);

int SOCK_Accept(Sock_t* sk, struct DP_Sockaddr* addr, DP_Socklen_t* addrlen, Sock_t** newSk);

int SOCK_Ioctl(Sock_t* sk, int request, void* arg);

int SOCK_Fcntl(Sock_t* sk, int cmd, int val);

ssize_t SOCK_Sendmsg(Sock_t* sk, const struct DP_Msghdr* msg, int flags, ssize_t totalLen);

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

void SOCK_ShowSocketInfo(Sock_t* sk);

int SOCK_GetSocketState(Sock_t* sk, DP_SocketState_t* state);

void SOCK_GetSocketDetails(Sock_t* sk, DP_SockDetails_t* details);

uint32_t SOCK_GetRWStateSafe(Sock_t* sk);

int SOCK_SetNotifyFn(int type, SOCK_NotifyFn_t notifyFn);

void SOCK_EnableNotify(Sock_t* sk, int type, void* ctx, int assocFd);
void SOCK_EnableNotifySafe(Sock_t* sk, int type, void* ctx, int assocFd);

void SOCK_DisableNotify(Sock_t* sk);
void SOCK_DisableNotifySafe(Sock_t* sk);

uint32_t SOCK_GetState(Sock_t* sk);

/**
 * @brief     注册arp表项recv处理回调
 * @attention 在packet创建过程中注册

 * @param  sk [IN] 设置回调的对应socket
 * @param  cb [IN] 回调接口，原型参见CP:nbc_arp_recv_proc
 * @retval 返回0 成功
 * @retval 返回-1 失败
 */
int SOCK_ArpProcHook(Sock_t* sk, void (*arpRecvHandle)(char *buf, uint32_t bufLen));

#ifdef __cplusplus
}
#endif
#endif
