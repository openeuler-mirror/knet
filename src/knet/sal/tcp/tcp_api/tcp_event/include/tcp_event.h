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
#ifndef K_NET_TCP_EVENT_H
#define K_NET_TCP_EVENT_H

#include <sys/epoll.h>
#include <stdint.h>

#ifdef __cplusplus
}
#endif

#define KNET_EPOLL_MAX_NUM (((uint32_t)0xFFFFFFFF) / sizeof(struct epoll_event))


/**
 * @brief epoll实例创建
 * @param [IN] int size：监听容量
 * @return int 非负数：成功. -1：失败
 */
int KNET_DpEpollCreate(int size);

/**
 * @brief epoll实例创建
 * @param [IN] int flags：设置Epoll标志位
 * @return int 非负数：成功. -1：失败
 */
int KNET_DpEpollCreate1(int flags);

/**
 * @brief 控制 epoll 实例中文件描述符的监听行为，包括添加、修改或删除监听的文件描述符及其事件
 * @param [IN] int epfd：epoll 实例的文件描述符
 * @param [IN] int op：操作类型，指定要对文件描述符执行的操作，EPOLL_CTL_ADD/EPOLL_CTL_MOD/EPOLL_CTL_DEL
 * @param [IN] int sockfd：需要监听的socket文件描述符
 * @param [IN] struct epoll_event event：指向 epoll_event 结构体的指针，用于指定需要监听的事件类型和用户数据。
 * @return int 0：成功. -1：失败
 */
int KNET_DpEpollCtl(int epfd, int op, int sockfd, struct epoll_event *event);

/**
 * @brief 等待 epoll 实例中注册的文件描述符上的事件触发，并返回触发的事件信息
 * @param [IN] int epfd epoll：实例的文件描述符
 * @param [OUT] struct epoll_event *events：用于存储触发事件的数组指针
 * @param [IN] int maxevents：events数组的最大容量，必须大于 0
 * @param [IN] int timeout：等待事件的超时时间（毫秒）
 * @return > 0：返回触发事件的文件描述符数量. 0：超时时间内没有事件触发. -1：失败
 */
int KNET_DpEpollWait(int epfd, struct epoll_event *events, int maxevents, int timeout);

/**
 * @brief 等待 epoll 实例中注册的文件描述符上的事件触发，并返回触发的事件信息
 * @param [IN] int epfd epoll：实例的文件描述符
 * @param [OUT] struct epoll_event *events：用于存储触发事件的数组指针
 * @param [IN] int maxevents events：数组的最大容量，必须大于 0
 * @param [IN] int timeout：等待事件的超时时间（毫秒）
 * @param [IN] const sigset_t *sigmask：信号掩码指针，指定在等待期间需要阻塞的信号集。如果为 NULL，则行为与 epoll_wait 相同
 * @return > 0：返回触发事件的文件描述符数量. 0：超时时间内没有事件触发. -1：失败
 */
int KNET_DpEpollPwait(int epfd, struct epoll_event *events, int maxevents, int timeout, const sigset_t *sigmask);

/**
 * @brief 监听多个文件描述符上的事件，返回触发事件的文件描述符数量。适用于监听少量文件描述符
 * @param [IN/OUT] struct pollfd *fds：指向 pollfd 结构体数组的指针，每个结构体描述一个需要监听的文件描述符及其事件
 * @param [IN] nfds_t nfds：fds 数组的长度，即需要监听的文件描述符数量
 * @param [IN] int timeout：等待事件的超时时间
 * @return > 0：返回触发事件的文件描述符数量. 0：超时时间内没有事件触发. -1：失败
 */
int KNET_DpPoll(struct pollfd *fds, nfds_t nfds, int timeout);

/**
 * @brief 监听多个文件描述符上的事件，返回触发事件的文件描述符数量。支持更精确的超时时间和信号掩码
 * @param [IN/OUT] struct pollfd *fds：指向 pollfd 结构体数组的指针，每个结构体描述一个需要监听的文件描述符及其事件
 * @param [IN] nfds_t nfds：fds 数组的长度，即需要监听的文件描述符数量
 * @param [IN] const struct timespec *timeout_ts：指向 timespec 结构体的指针，指定等待事件的超时时间(秒和纳秒)
 * @param [IN] const sigset_t *sigmask：信号掩码指针，指定在等待期间需要阻塞的信号集
 * @return > 0：返回触发事件的文件描述符数量. 0：超时时间内没有事件触发. -1：失败
 */
int KNET_DpPPoll(struct pollfd *fds, nfds_t nfds, const struct timespec *timeoutTs, const sigset_t *sigmask);

/**
 * @brief 监听多个文件描述符上的事件，返回触发事件的文件描述符数量
 * @param [IN] int nfds：需要监听的文件描述符的最大值
 * @param [IN/OUT] fd_set *readfds：指向 fd_set 结构体的指针，用于监听可读事件的文件描述符集合。如果为 NULL，则不监听可读事件
 * @param [IN/OUT] fd_set *writefds：指向 fd_set 结构体的指针，用于监听可写事件的文件描述符集合。如果为 NULL，则不监听可读事件
 * @param [IN/OUT] fd_set *exceptfds：指向 fd_set 结构体的指针，用于监听异常事件的文件描述符集合。如果为 NULL，则不监听可读事件
 * @param [IN] struct timeval*timeout：指向 timeval 结构体的指针，指定等待事件的超时时间（秒和微秒）
 * @return > 0：返回触发事件的文件描述符数量. 0：超时时间内没有事件触发. -1：失败
 */
int KNET_DpSelect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);

/**
 * @brief 监听多个文件描述符上的事件，返回触发事件的文件描述符数量
 * @param [IN] int nfds：需要监听的文件描述符的最大值
 * @param [IN/OUT] fd_set *readfds：指向 fd_set 结构体的指针，用于监听可读事件的文件描述符集合。如果为 NULL，则不监听可读事件
 * @param [IN/OUT] fd_set *writefds：指向 fd_set 结构体的指针，用于监听可写事件的文件描述符集合。如果为 NULL，则不监听可读事件
 * @param [IN/OUT] fd_set *exceptfds：指向 fd_set 结构体的指针，用于监听异常事件的文件描述符集合。如果为 NULL，则不监听可读事件
 * @param [IN] const struct timespec *timeout_ts：指向 timespec 结构体的指针，指定等待事件的超时时间(秒和纳秒)
 * @param [IN] const sigset_t *sigmask：信号掩码指针，指定在等待期间需要阻塞的信号集
 * @return > 0：返回触发事件的文件描述符数量. 0：超时时间内没有事件触发. -1：失败
 */
int KNET_DpPSelect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timespec *timeout,
    const sigset_t *sigmask);


#ifdef __cplusplus
}
#endif
#endif // K_NET_TCP_SOCKET_H
