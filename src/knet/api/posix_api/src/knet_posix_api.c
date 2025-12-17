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

#include <stdarg.h>

#include "knet_socket_bridge.h"

KNET_API int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
    return KNET_Sigaction(signum, act, oldact);
}

KNET_API sighandler_t signal(int signum, sighandler_t handler)
{
    return KNET_Signal(signum, handler);
}

KNET_API int socket(int domain, int type, int protocol)
{
    return KNET_Socket(domain, type, protocol);
}

KNET_API int listen(int sockfd, int backlog)
{
    return KNET_Listen(sockfd, backlog);
}

KNET_API int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    return KNET_Bind(sockfd, addr, addrlen);
}

KNET_API int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    return KNET_Connect(sockfd, addr, addrlen);
}

KNET_API int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    return KNET_Getpeername(sockfd, addr, addrlen);
}

KNET_API int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    return KNET_Getsockname(sockfd, addr, addrlen);
}

KNET_API ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
    return KNET_Send(sockfd, buf, len, flags);
}

KNET_API ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr,
    socklen_t addrlen)
{
    return KNET_Sendto(sockfd, buf, len, flags, dest_addr, addrlen);
}

KNET_API ssize_t writev(int sockfd, const struct iovec *iov, int iovcnt)
{
    return KNET_Writev(sockfd, iov, iovcnt);
}

KNET_API ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
    return KNET_Sendmsg(sockfd, msg, flags);
}

KNET_API ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
    return KNET_Recv(sockfd, buf, len, flags);
}

KNET_API ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr,
    socklen_t *addrlen)
{
    return KNET_Recvfrom(sockfd, buf, len, flags, src_addr, addrlen);
}

KNET_API ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)
{
    return KNET_Recvmsg(sockfd, msg, flags);
}

KNET_API ssize_t readv(int sockfd, const struct iovec *iov, int iovcnt)
{
    return KNET_Readv(sockfd, iov, iovcnt);
}

KNET_API int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen)
{
    return KNET_Getsockopt(sockfd, level, optname, optval, optlen);
}

KNET_API int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
    return KNET_Setsockopt(sockfd, level, optname, optval, optlen);
}

KNET_API int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    return KNET_Accept(sockfd, addr, addrlen);
}

KNET_API int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
    return KNET_Accept4(sockfd, addr, addrlen, flags);
}

KNET_API int close(int sockfd)
{
    return KNET_Close(sockfd);
}

KNET_API int shutdown(int sockfd, int how)
{
    return KNET_Shutdown(sockfd, how);
}

KNET_API ssize_t read(int sockfd, void *buf, size_t count)
{
    return KNET_Read(sockfd, buf, count);
}

KNET_API ssize_t write(int sockfd, const void *buf, size_t count)
{
    return KNET_Write(sockfd, buf, count);
}

KNET_API int epoll_create(int size)
{
    return KNET_EpollCreate(size);
}

KNET_API int epoll_create1(int flags)
{
    return KNET_EpollCreate1(flags);
}

KNET_API int epoll_ctl(int epfd, int op, int sockfd, struct epoll_event *event)
{
    return KNET_EpollCtl(epfd, op, sockfd, event);
}

KNET_API int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    return KNET_EpollWait(epfd, events, maxevents, timeout);
}

KNET_API int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout, const sigset_t *sigmask)
{
    return KNET_EpollPwait(epfd, events, maxevents, timeout, sigmask);
}

KNET_API int fcntl(int sockfd, int cmd, ...)
{
    va_list va;
    va_start(va, cmd);
    long arg = va_arg(va, long);
    va_end(va);

    return KNET_Fcntl(sockfd, cmd, arg);
}

KNET_API int fcntl64(int sockfd, int cmd, ...)
{
    va_list va;
    va_start(va, cmd);
    long arg = va_arg(va, long);
    va_end(va);

    return KNET_Fcntl64(sockfd, cmd, arg);
}

KNET_API int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    return KNET_Poll(fds, nfds, timeout);
}

KNET_API int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout_ts, const sigset_t *sigmask)
{
    return KNET_Ppoll(fds, nfds, timeout_ts, sigmask);
}

KNET_API int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
    return KNET_Select(nfds, readfds, writefds, exceptfds, timeout);
}

KNET_API int pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timespec *timeout,
    const sigset_t *sigmask)
{
    return KNET_Pselect(nfds, readfds, writefds, exceptfds, timeout, sigmask);
}

KNET_API int ioctl(int sockfd, unsigned long request, ...)
{
    va_list va;
    va_start(va, request);
    char *arg = va_arg(va, char *);
    va_end(va);

    return KNET_Ioctl(sockfd, request, arg);
}

KNET_API pid_t fork(void)
{
    return KNET_Fork();
}