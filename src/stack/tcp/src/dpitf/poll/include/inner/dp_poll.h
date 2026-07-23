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
#ifndef DP_POLL_H
#define DP_POLL_H

#ifdef __cplusplus
extern "C" {
#endif


typedef unsigned long int DP_Nfds_t;

struct DP_Pollfd {
    int fd;
    short int events;
    short int revents;
};

#ifndef DP_POSIX_POLL_API_H
typedef void (*DP_PollNotifyFn_t)(void *);

typedef struct DP_PollNotify {
    DP_PollNotifyFn_t fn;
    void *data; /* 回调函数私有数据协议栈不关心 */
} DP_PollNotify_t;
#endif // DP_POSIX_POLL_API_H

#define DP_POLLIN       0x001
#define DP_POLLOUT      0x004
#define DP_POLLERR      0x008
#define DP_POLLHUP      0x010
#define DP_POLLRDHUP    0x2000

// 以下事件暂不支持
#define DP_POLLPRI      0x002
#define DP_POLLNVAL     0x020
#define DP_POLLRDNORM   0x040
#define DP_POLLRDBAND   0x080
#define DP_POLLWRNORM   0x100
#define DP_POLLWRBAND   0x200
#define DP_POLLMSG      0x400
#define DP_POLL_REMOVE  0x1000

int DP_Poll(struct DP_Pollfd* fds, DP_Nfds_t nfds, int timeout);

#ifndef DP_POSIX_POLL_API_H
int DP_PollCreateNotify(struct DP_Pollfd* fds, DP_Nfds_t nfds, DP_PollNotify_t* notify, void **pollCtx);
void DP_PollDestroyNotify(void *pollCtx);
#endif // DP_POSIX_POLL_API_H

#ifdef __cplusplus
}
#endif
#endif
