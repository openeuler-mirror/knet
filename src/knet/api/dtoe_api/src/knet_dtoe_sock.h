/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef __KNET_DTOE_SOCK_H__
#define __KNET_DTOE_SOCK_H__

#include <stdbool.h>
#include <sys/queue.h>
#include <sys/epoll.h>

#include "flexda_dtoe_interface.h"
#include "knet_dtoe_events.h"
#include "knet_dtoe_api.h"
#include "knet_lock.h"

#define KNET_SOCK_STATE_READ          0x001
#define KNET_SOCK_STATE_WRITE         0x002
#define KNET_SOCK_STATE_EXCEPTION     0x004
#define KNET_SOCK_STATE_CLOSE         0x008    
#define KNET_SOCK_STATE_CANTSENDMORE  0x010    /* can't send more data to peer */
#define KNET_SOCK_STATE_CANTRCVMORE   0x020    /* can't receive more data from peer */
#define KNET_SOCK_STATE_READ_ET       0x040
#define KNET_SOCK_STATE_WRITE_ET      0x080
#define KNET_SOCK_STATE_ET (KNET_SOCK_STATE_READ_ET | KNET_SOCK_STATE_WRITE_ET)

enum {
    KNET_SOCK_NOTIFY_TYPE_NONE,
    KNET_SOCK_NOTIFY_TYPE_EPOLL,
    KNET_SOCK_NOTIFY_TYPE_POLL,
    KNET_SOCK_NOTIFY_TYPE_SELECT,
    KNET_SOCK_NOTIFY_TYPE_HOOK,
    KNET_SOCK_NOTIFY_TYPE_USER,
    KNET_SOCK_NOTIFY_TYPE_MAX
};

enum {
    KNET_SOCK_EVENT_NONE = 0,
    // TCP 收到 FIN 通知(连接建立成功之后才会通知该事件)
    KNET_SOCK_EVENT_RCVFIN,
    // TCP 收到 RST 通知(连接建立成功之后才会通知该事件)
    KNET_SOCK_EVENT_RCVRST,
    // TCP 老化断链通知(连接建立成功之后才会通知该事件)
    KNET_SOCK_EVENT_DISCONNECTED,
    // 1. TCP 主动建链成功通知
    // 2. TCP 发送缓冲区有空间可以发送数据通知
    KNET_SOCK_EVENT_WRITE,
    // TCP 有数据需要用户接收时通知
    KNET_SOCK_EVENT_READ,
    // SOCK 控制块资源即将释放时通知
    KNET_SOCK_EVENT_FREE_SOCKCB,
    KNET_SOCK_EVENT_MAX
};

struct KnetSock {
    int fd;
    int associateFd;
    int iovCnt;
    uint8_t state;
    uint8_t notifyType;
    struct KnetEpollItem *notifyCtx;
    KNET_SpinLock lock;
};

void KNET_SockNotify(struct KnetSock *sk, void *ctx, uint8_t oldState, uint8_t newState, uint8_t event);

void KNET_SockEnableNotify(struct KnetSock *sk, uint8_t type, struct KnetEpollItem *ctx, int associateFd);

void KNET_SockDisableNotifySafe(struct KnetSock *sk);

void KNET_SockDisableNotify(struct KnetSock *sk);

static inline void KNET_SockLock(struct KnetSock* sk)
{
    KNET_SpinlockLock(&sk->lock);
}

static inline void KNET_SockUnlock(struct KnetSock* sk)
{
    KNET_SpinlockUnlock(&sk->lock);
}

static inline void KNET_SockSetStateLocked(struct KnetSock *sk, uint8_t state)
{
    uint8_t old = sk->state;
    if ((old & state) == state) {
        return;
    }
    sk->state |= state;

    if (sk->notifyCtx != NULL) {
        KNET_SockNotify(sk, sk->notifyCtx, old, sk->state, KNET_SOCK_EVENT_NONE);
    }
}

static inline void KNET_SockSetState(struct KnetSock *sk, uint8_t state)
{
    KNET_SockLock(sk);
    KNET_SockSetStateLocked(sk, state);
    KNET_SockUnlock(sk);
}

static inline void KNET_SockUnsetState(struct KnetSock* sk, uint8_t state)
{
    KNET_SockLock(sk);

    uint8_t old = sk->state;
    if ((old & state) == 0) {
        KNET_SockUnlock(sk);
        return;
    }
    sk->state &= ~state;

    if (sk->notifyCtx != NULL) {
        KNET_SockNotify(sk, sk->notifyCtx, old, sk->state, KNET_SOCK_EVENT_NONE);
    }

    KNET_SockUnlock(sk);
}

#endif