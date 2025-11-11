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

#ifndef TCP_TSQ_H
#define TCP_TSQ_H

#include "utils_base.h"
#include "utils_spinlock.h"

#include "tcp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TCP_TSQ_CONNECT       0x1 // 主动建链，触发协议栈发送syn
#define TCP_TSQ_CONNECTED     0x2 // 主动建链完成后，更新sock状态，事件通知及唤醒接口
#define TCP_TSQ_DISCONNECT    0x4 // close处理，主动建链及被动建链socket，触发资源释放、fin、rst等动作
#define TCP_TSQ_SEND_FORCE    0x8 // 必须发送一个报文，可以是数据报文也可以是纯ACK
#define TCP_TSQ_SEND_DATA     0x10 // 用户发送数据
#define TCP_TSQ_RECV_DATA     0x20 // 协议栈接收到数据
#define TCP_TSQ_KEEPALIVE_ON  0x40 // keepalive更新
#define TCP_TSQ_KEEPALIVE_OFF 0x80 // keepalive关闭
#define TCP_TSQ_RECV_FIN      0x100 // 协议栈接收FIN，更新sock为不可读，读事件，并唤醒阻塞接口
#define TCP_TSQ_RECV_RST      0x200 // 协议栈接收RST，更新sock为不可读不可写，读事件，并唤醒阻塞接口，设置error
#define TCP_TSQ_SEND_MORE     0x400 // 协议栈收到ACK，发送数据报文
#define TCP_TSQ_SEND_WND_UP   0x800 // 尝试发送窗口更新报文
#define TCP_TSQ_ABORT         0x1000 // 定时器超时事件
#define TCP_TSQ_SET_WRITABLE  0x2000 // 可写事件

#define TCP_TSQ_IN_SKLOCK \
    (TCP_TSQ_CONNECTED | TCP_TSQ_SEND_DATA | TCP_TSQ_RECV_DATA | TCP_TSQ_RECV_FIN | TCP_TSQ_RECV_RST | \
     TCP_TSQ_ABORT | TCP_TSQ_SET_WRITABLE)
#define TCP_TSQ_SEND (TCP_TSQ_SEND_FORCE | TCP_TSQ_SEND_DATA | TCP_TSQ_SEND_MORE | TCP_TSQ_SEND_WND_UP)
#define TCP_TSQ_RECV (TCP_TSQ_RECV_FIN | TCP_TSQ_RECV_RST | TCP_TSQ_RECV_DATA)
#define TCP_TSQ_RECV_UNEXPECT (TCP_TSQ_RECV_RST | TCP_TSQ_ABORT)

#define TCP_TSQ_EV_SLOW (TCP_TSQ_CONNECT | TCP_TSQ_DISCONNECT | TCP_TSQ_KEEPALIVE_ON | TCP_TSQ_KEEPALIVE_OFF)
#define TCP_TSQ_SK_SLOW (TCP_TSQ_CONNECTED | TCP_TSQ_RECV_FIN | TCP_TSQ_RECV_RST | TCP_TSQ_ABORT)

int TcpTsqInit(int slave);

void TcpTsqDeinit(int slave);

void TcpTsqTryRemoveLockQue(TcpSk_t* tcp);
void TcpTsqTryRemoveNoLockQue(TcpSk_t* tcp);

void TcpTsqInetInsertBacklog(int wid, Pbuf_t *pbuf);

typedef struct {
    TcpListHead_t tsqQue;

    Spinlock_t    lock;
    TcpListHead_t tsqLockQue;
    int           txEvCnt;

    Spinlock_t    backLogLock;
    PBUF_Chain_t  backlog;
} TcpTsq_t;

extern TcpTsq_t** g_tsq;

// socket层添加tsq事件
static inline void TcpTsqAddLockQue(TcpSk_t* tcp, uint32_t tsqFlags)
{
    TcpTsq_t* tsq = g_tsq[tcp->wid];

    if (tcp->tsqFlagsLock == 0) {
        SPINLOCK_Lock(&tsq->lock);
        LIST_INSERT_TAIL(&tsq->tsqLockQue, tcp, txEvNode);
        SPINLOCK_Unlock(&tsq->lock);
    }

    tcp->tsqFlagsLock |= tsqFlags;
}

static inline void TcpTsqAddQue(TcpSk_t* tcp, int tsqFlags)
{
    TcpTsq_t* tsq = g_tsq[tcp->wid];

    if (tcp->tsqFlags == 0) {
        LIST_INSERT_TAIL(&tsq->tsqQue, tcp, rxEvNode);
    }

    tcp->tsqFlags |= (uint32_t)tsqFlags;
}

#ifdef __cplusplus
}
#endif
#endif
