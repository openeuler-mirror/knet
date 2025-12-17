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
#ifndef DP_NOTIFY_H
#define DP_NOTIFY_H

#include "sock.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DPITF_EPOLL
#define EPOLL_NOTIFY_ATTR ATTR_WEAK
#else
#define EPOLL_NOTIFY_ATTR
#endif

#ifndef DPITF_POLL
#define POLL_NOTIFY_ATTR ATTR_WEAK
#else
#define POLL_NOTIFY_ATTR
#endif

#ifndef DPITF_SELECT
#define SELECT_NOTIFY_ATTR ATTR_WEAK
#else
#define SELECT_NOTIFY_ATTR
#endif

extern void EPOLL_Notify(Sock_t* sk, void* ctx, uint8_t oldState, uint8_t newState, uint8_t event) EPOLL_NOTIFY_ATTR;

extern void POLL_Notify(Sock_t* sk, void* ctx, uint8_t oldState, uint8_t newState, uint8_t event) POLL_NOTIFY_ATTR;

extern void SELECT_Notify(Sock_t* sk, void* ctx, uint8_t oldState, uint8_t newState, uint8_t event) SELECT_NOTIFY_ATTR;

#ifdef __cplusplus
}
#endif
#endif
