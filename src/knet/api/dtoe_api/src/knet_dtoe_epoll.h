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

#ifndef __KNET_DTOE_EPOLL_H__
#define __KNET_DTOE_EPOLL_H__

#include <stdbool.h>
#include <sys/queue.h>

#include "flexda_dtoe_interface.h"
#include "knet_dtoe_sock.h"

TAILQ_HEAD(KnetEpListHead, KnetEpollItem);

struct KnetEpoll {
    struct KnetEpListHead readyList;
    struct KnetEpListHead idleList;
    KNET_SpinLock lock;

    struct KnetSendChannel *sendCh;
    struct KnetRecvChannel *recvCh;
};

struct KnetEpollItem {
    TAILQ_ENTRY(KnetEpollItem) epItem;
    int fd;
    int epfd;
    uint8_t state;
    uint8_t ready;
    uint8_t expectState;
    struct epoll_event ev;
    struct KnetEpoll *ep;
};

void KNET_ResetEpfd(int epfd);
void KNET_EpollNotify(struct KnetSock *sk, void *ctx, uint8_t oldState, uint8_t newState, uint8_t event);

/**
 * @brief 安全回收仍挂在epoll链表上的item（持sk锁，校验notifyType为epoll）
 * @param sk [IN] 目标sock
 * @note 供fd复位路径调用，覆盖被动关闭等未经过knet_prepare_close的场景
 */
void KNET_EpollRemoveItem(struct KnetSock *sk);

#endif // __KNET_DTOE_EPOLL_H__