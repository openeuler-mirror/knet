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
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "securec.h"

#include "knet_log.h"
#include "knet_dtoe_fd.h"


void KNET_SockNotify(struct KnetSock *sk, void *ctx, uint8_t oldState, uint8_t newState, uint8_t event)
{
    KNET_EpollNotify(sk, ctx, oldState, newState, event);
}

void KNET_SockEnableNotify(struct KnetSock *sk, uint8_t type, struct KnetEpollItem *ctx, int associateFd)
{
    sk->notifyCtx = ctx;
    sk->notifyType = type;
    sk->associateFd = associateFd;
}

void KNET_SockDisableNotify(struct KnetSock *sk)
{
    sk->notifyType = KNET_SOCK_NOTIFY_TYPE_NONE;
    sk->notifyCtx = NULL;
    sk->associateFd = -1;
}

void KNET_SockDisableNotifySafe(struct KnetSock *sk)
{
    KNET_SockLock(sk);
    KNET_SockDisableNotify(sk);
    KNET_SockUnlock(sk);
}