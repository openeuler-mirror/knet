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

#ifndef __KNET_SOCKET_BRIDGE_H__
#define __KNET_SOCKET_BRIDGE_H__

#include <signal.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#include "knet_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 入参同标准posix函数入参描述一致 */
int KNET_Sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
sighandler_t KNET_Signal(int signum, sighandler_t handler);
int KNET_Socket(int domain, int type, int protocol);
int KNET_Listen(int sockfd, int backlog);
int KNET_Bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int KNET_Connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int KNET_Getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int KNET_Getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
ssize_t KNET_Send(int sockfd, const void *buf, size_t len, int flags);
ssize_t KNET_Sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr,
    socklen_t addrlen);
ssize_t KNET_Writev(int sockfd, const struct iovec *iov, int iovcnt);
ssize_t KNET_Sendmsg(int sockfd, const struct msghdr *msg, int flags);
ssize_t KNET_Recv(int sockfd, void *buf, size_t len, int flags);
ssize_t KNET_Recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr,
    socklen_t *addrlen);
ssize_t KNET_Recvmsg(int sockfd, struct msghdr *msg, int flags);
ssize_t KNET_Readv(int sockfd, const struct iovec *iov, int iovcnt);
int KNET_Getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
int KNET_Setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
int KNET_Accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int KNET_Accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags);
int KNET_Close(int sockfd);
int KNET_Shutdown(int sockfd, int how);
ssize_t KNET_Read(int sockfd, void *buf, size_t count);
ssize_t KNET_Write(int sockfd, const void *buf, size_t count);
int KNET_EpollCreate(int size);
int KNET_EpollCreate1(int flags);
int KNET_EpollCtl(int epfd, int op, int sockfd, struct epoll_event *event);
int KNET_EpollWait(int epfd, struct epoll_event *events, int maxevents, int timeout);
int KNET_EpollPwait(int epfd, struct epoll_event *events, int maxevents, int timeout, const sigset_t *sigmask);
int KNET_Fcntl(int sockfd, int cmd, ...);
int KNET_Fcntl64(int sockfd, int cmd, ...);
int KNET_Poll(struct pollfd *fds, nfds_t nfds, int timeout);
int KNET_Ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout_ts, const sigset_t *sigmask);
int KNET_Select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
int KNET_Pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timespec *timeout,
    const sigset_t *sigmask);
int KNET_Ioctl(int sockfd, unsigned long request, ...);
pid_t KNET_Fork(void);

#ifdef __cplusplus
}
#endif
#endif // __KNET_SOCKET_BRIDGE_H__