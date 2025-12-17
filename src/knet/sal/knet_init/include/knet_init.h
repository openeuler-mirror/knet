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

#ifndef __KNET_INIT_H__
#define __KNET_INIT_H__

#include <poll.h>
#include <dlfcn.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <syslog.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <malloc.h>
#include <sys/timeb.h>
#include <sys/ioctl.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <errno.h>
#include <netinet/tcp.h>

#include "knet_types.h"
#include "knet_lock.h"

struct KNET_PosixApiOps {
    int (*socket)(int domain, int type, int protocol);
    int (*listen)(int sockfd, int backlog);
    int (*bind)(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
    int (*connect)(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
    int (*getpeername)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
    int (*getsockname)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
    ssize_t (*send)(int sockfd, const void *buf, size_t len, int flags);
    ssize_t (*sendto)(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr,
        socklen_t addrlen);
    ssize_t (*writev)(int sockfd, const struct iovec *iov, int iovcnt);
    ssize_t (*sendmsg)(int sockfd, const struct msghdr *msg, int flags);
    ssize_t (*recv)(int sockfd, void *buf, size_t len, int flags);
    ssize_t (*recvfrom)(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr,
        socklen_t *addrlen);
    ssize_t (*recvmsg)(int sockfd, struct msghdr *msg, int flags);
    ssize_t (*readv)(int sockfd, const struct iovec *iov, int iovcnt);
    int (*getsockopt)(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
    int (*setsockopt)(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
    int (*accept)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
    int (*accept4)(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags);
    int (*close)(int sockfd);
    int (*shutdown)(int sockfd, int how);
    ssize_t (*read)(int sockfd, void *buf, size_t count);
    ssize_t (*write)(int sockfd, const void *buf, size_t count);
    int (*epoll_create)(int size);
    int (*epoll_create1)(int flags);
    int (*epoll_ctl)(int epfd, int op, int sockfd, struct epoll_event *event);
    int (*epoll_wait)(int epfd, struct epoll_event *events, int maxevents, int timeout);
    int (*epoll_pwait)(int epfd, struct epoll_event *events, int maxevents, int timeout, const sigset_t *sigmask);
    int (*fcntl)(int sockfd, int cmd, ...);
    int (*fcntl64)(int sockfd, int cmd, ...);
    int (*poll)(struct pollfd *fds, nfds_t nfds, int timeout);
    int (*ppoll)(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout_ts, const sigset_t *sigmask);
    int (*select)(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
    int (*pselect)(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timespec *timeout,
        const sigset_t *sigmask);
    int (*ioctl)(int sockfd, unsigned long request, ...);
    int (*sigaction)(int signum, const struct sigaction *act, struct sigaction *oldact);
    sighandler_t (*signal)(int signum, sighandler_t handler);

    pid_t (*fork)(void);
};

/**
 * @brief KNET资源初始化
 *
 * @return int 0: 成功, -1: 失败
 */
int KNET_TrafficResourcesInit(void);

/**
 * @brief 手动结束KNET线程
 */
void KNET_SetDpdkAndStackThreadStop(void);

/**
 * @brief KNET posix接口钩子初始化
 * @param [OUT] ops
 * @return int 0: 成功, -1: 失败
 */
int KNET_PosixOpsApiInit(struct KNET_PosixApiOps *ops);

/**
 * @brief 对控制面和数据面线程加锁
 * @note 用于解决fork锁继承问题
 */
void KNET_AllThreadLock(void);
void KNET_AllThreadUnlock(void);
#endif