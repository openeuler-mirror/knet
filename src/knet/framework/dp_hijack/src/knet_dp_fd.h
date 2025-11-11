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
#ifndef __KNET_DP_FD_H__
#define __KNET_DP_FD_H__

#include <stdint.h>
#include "dp_posix_epoll_api.h"
#include "knet_types.h"
#include "knet_atomic.h"

#define INVALID_FD (-1)

enum FdState {
    FD_STATE_INVALID = 0, /* fd初始化状态 */
    FD_STATE_HIJACK,    /* fd已被协议栈劫持 */
};

enum FdType {
    FD_TYPE_INVALID, // calloc或memset初始化即为invalid，无需额外初始化。有功能依赖该值默认为0，未全面评估勿修改
    FD_TYPE_SOCKET,
    FD_TYPE_EPOLL,
    FD_TYPE_MAX,
};

struct EpollNotifyData {
    int eventFd;
    KNET_ATOMIC64_T active;  /* active为1表示已经被激活，无需再次唤醒。为0表示未激活，需要唤醒 */
};

struct KnetEpollNotify {
    DP_EpollNotify_t notify;
    struct EpollNotifyData data;
};

union FdPrivateData {
    struct KnetEpollNotify epollData;
};

struct KnetFd {
    enum FdState state;
    int dpFd;
    enum FdType  fdType;
    union FdPrivateData privateData;
};

bool IsFdValid(int osFd);
int OsFdToDpFd(int osFd);
bool IsFdHijack(int osFd);
void SetFdSocketState(enum FdState state, int osFd, int dpFd);
void ResetFdState(int osFd);
union FdPrivateData *GetFdPrivateData(int osFd);
void SetFdStateAndType(enum FdState state, int osFd, int dpFd, enum FdType type);
enum FdType GetFdType(int osFd);
int KnetFdInit(void);
void KnetFdDeinit(void);
int FdMaxGet(void);

#endif