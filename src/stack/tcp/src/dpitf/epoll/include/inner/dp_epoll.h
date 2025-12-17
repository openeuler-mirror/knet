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
#ifndef DP_EPOLL_H
#define DP_EPOLL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef union DP_EpollData {
    void*    ptr;
    int      fd;
    uint32_t u32;
    uint64_t u64;
} DP_EpollData_t;

#ifdef __x86_64

struct DP_EpollEvent {
    uint32_t       events; /* Epoll events */
    DP_EpollData_t data;   /* User data variable */
} __attribute__((packed));

#else

struct DP_EpollEvent {
    uint32_t       events; /* Epoll events */
    DP_EpollData_t data;   /* User data variable */
};

#endif

#ifndef DP_POSIX_EPOLL_API_H

typedef void (*DP_EpollNotifyFn_t)(uint8_t *);

typedef struct DP_EpollNotify {
    DP_EpollNotifyFn_t fn;
    uint8_t*           data; /* 回调函数私有数据协议栈不关心 */
} DP_EpollNotify_t;

#endif

#define DP_EPOLL_CTL_ADD 1 /* 注册新的fd到epfd */
#define DP_EPOLL_CTL_DEL 2 /* 从epfd移除fd */
#define DP_EPOLL_CTL_MOD 3 /* 修改已注册的fd的监听事件 */

#define DP_EPOLLIN     0x00000001
#define DP_EPOLLPRI    0x00000002
#define DP_EPOLLOUT    0x00000004
#define DP_EPOLLERR    0x00000008
#define DP_EPOLLHUP    0x00000010
#define DP_EPOLLNVAL   0x00000020
#define DP_EPOLLRDNORM 0x00000040
#define DP_EPOLLRDBAND 0x00000080
#define DP_EPOLLWRNORM 0x00000100
#define DP_EPOLLWRBAND 0x00000200
#define DP_EPOLLMSG    0x00000400
#define DP_EPOLLRDHUP  0x00002000

#define DP_EPOLLWAKEUP  (1U << 29)
#define DP_EPOLLONESHOT (1U << 30)
#define DP_EPOLLET      (1U << 31)

/* EPOLL监控的最大事件数 */
#define DP_EPOLL_MAX_NUM (int)(((uint32_t)0xFFFFFFFF) / sizeof(struct DP_EpollEvent))

int DP_EpollCreate(int size);

int DP_EpollCreateNotify(int size, DP_EpollNotify_t *callback);

int DP_EpollCtl(int epfd, int op, int fd, struct DP_EpollEvent* event);

int DP_EpollWait(int epfd, struct DP_EpollEvent* events, int maxevents, int timeout);

#ifdef __cplusplus
}
#endif
#endif
