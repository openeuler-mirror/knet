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

#include "knet_log.h"
#include "knet_init.h"

#define GET_POSIX_API_FAILED (-1)

static struct KNET_PosixApiOps g_posixApiOps = {0};

static inline void PosixApiOpsInit(struct KNET_PosixApiOps *ops)
{
    /* 目前只有tcp协议栈，无需判断协议类型 */
    /* todo：osApi初始化，通过配置文件获取协议类型，既然要初始化，那就要加锁了？如果还没初始化完成， */
    (void)KNET_PosixOpsApiInit(ops);
}

#define KNET_CHECK_AND_GET_POSIX_API(posixApi, ret) do {                                      \
        if ((posixApi) == NULL) {                                                             \
            PosixApiOpsInit(&g_posixApiOps);                                                                 \
            if ((posixApi) == NULL) {                                                         \
                errno = ELIBBAD;                                                             \
                KNET_ERR("Posix api ops init failed, errno %d, %s", errno, strerror(errno)); \
                return (ret);                                                                \
            }                                                                                \
        }                                                                                    \
    } while (0)

int KNET_Sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.sigaction, GET_POSIX_API_FAILED);
    return g_posixApiOps.sigaction(signum, act, oldact);
}

sighandler_t KNET_Signal(int signum, sighandler_t handler)
{
    if (g_posixApiOps.signal == NULL) {
        PosixApiOpsInit(&g_posixApiOps);
    }
    if (g_posixApiOps.signal == NULL) {
        KNET_ERR("signal api ops init failed.");
        return SIG_ERR;
    }

    return g_posixApiOps.signal(signum, handler);
}

int KNET_Socket(int domain, int type, int protocol)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.socket, GET_POSIX_API_FAILED);
    return g_posixApiOps.socket(domain, type, protocol);
}

int KNET_Listen(int sockfd, int backlog)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.listen, GET_POSIX_API_FAILED);
    return g_posixApiOps.listen(sockfd, backlog);
}

int KNET_Bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.bind, GET_POSIX_API_FAILED);
    return g_posixApiOps.bind(sockfd, addr, addrlen);
}

int KNET_Connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.connect, GET_POSIX_API_FAILED);
    return g_posixApiOps.connect(sockfd, addr, addrlen);
}

int KNET_Getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.getpeername, GET_POSIX_API_FAILED);
    return g_posixApiOps.getpeername(sockfd, addr, addrlen);
}

int KNET_Getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.getsockname, GET_POSIX_API_FAILED);
    return g_posixApiOps.getsockname(sockfd, addr, addrlen);
}

ssize_t KNET_Send(int sockfd, const void *buf, size_t len, int flags)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.send, GET_POSIX_API_FAILED);
    return g_posixApiOps.send(sockfd, buf, len, flags);
}

ssize_t KNET_Sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr,
    socklen_t addrlen)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.sendto, GET_POSIX_API_FAILED);
    return g_posixApiOps.sendto(sockfd, buf, len, flags, dest_addr, addrlen);
}

ssize_t KNET_Writev(int sockfd, const struct iovec *iov, int iovcnt)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.writev, GET_POSIX_API_FAILED);
    return g_posixApiOps.writev(sockfd, iov, iovcnt);
}

ssize_t KNET_Sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.sendmsg, GET_POSIX_API_FAILED);
    return g_posixApiOps.sendmsg(sockfd, msg, flags);
}

ssize_t KNET_Recv(int sockfd, void *buf, size_t len, int flags)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.recv, GET_POSIX_API_FAILED);
    return g_posixApiOps.recv(sockfd, buf, len, flags);
}

ssize_t KNET_Recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr,
    socklen_t *addrlen)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.recvfrom, GET_POSIX_API_FAILED);
    return g_posixApiOps.recvfrom(sockfd, buf, len, flags, src_addr, addrlen);
}

ssize_t KNET_Recvmsg(int sockfd, struct msghdr *msg, int flags)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.recvmsg, GET_POSIX_API_FAILED);
    return g_posixApiOps.recvmsg(sockfd, msg, flags);
}

ssize_t KNET_Readv(int sockfd, const struct iovec *iov, int iovcnt)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.readv, GET_POSIX_API_FAILED);
    return g_posixApiOps.readv(sockfd, iov, iovcnt);
}

int KNET_Getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.getsockopt, GET_POSIX_API_FAILED);
    return g_posixApiOps.getsockopt(sockfd, level, optname, optval, optlen);
}

int KNET_Setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.setsockopt, GET_POSIX_API_FAILED);
    return g_posixApiOps.setsockopt(sockfd, level, optname, optval, optlen);
}

int KNET_Accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.accept, GET_POSIX_API_FAILED);
    return g_posixApiOps.accept(sockfd, addr, addrlen);
}

int KNET_Accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.accept4, GET_POSIX_API_FAILED);
    return g_posixApiOps.accept4(sockfd, addr, addrlen, flags);
}

int KNET_Close(int sockfd)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.close, GET_POSIX_API_FAILED);
    return g_posixApiOps.close(sockfd);
}

int KNET_Shutdown(int sockfd, int how)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.shutdown, GET_POSIX_API_FAILED);
    return g_posixApiOps.shutdown(sockfd, how);
}

ssize_t KNET_Read(int sockfd, void *buf, size_t count)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.read, GET_POSIX_API_FAILED);
    return g_posixApiOps.read(sockfd, buf, count);
}

ssize_t KNET_Write(int sockfd, const void *buf, size_t count)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.write, GET_POSIX_API_FAILED);
    return g_posixApiOps.write(sockfd, buf, count);
}

int KNET_EpollCreate(int size)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.epoll_create, GET_POSIX_API_FAILED);
    return g_posixApiOps.epoll_create(size);
}

int KNET_EpollCreate1(int flags)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.epoll_create1, GET_POSIX_API_FAILED);
    return g_posixApiOps.epoll_create1(flags);
}

int KNET_EpollCtl(int epfd, int op, int sockfd, struct epoll_event *event)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.epoll_ctl, GET_POSIX_API_FAILED);
    return g_posixApiOps.epoll_ctl(epfd, op, sockfd, event);
}

int KNET_EpollWait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.epoll_wait, GET_POSIX_API_FAILED);
    return g_posixApiOps.epoll_wait(epfd, events, maxevents, timeout);
}

int KNET_EpollPwait(int epfd, struct epoll_event *events, int maxevents, int timeout, const sigset_t *sigmask)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.epoll_pwait, GET_POSIX_API_FAILED);
    return g_posixApiOps.epoll_pwait(epfd, events, maxevents, timeout, sigmask);
}

int KNET_Fcntl(int sockfd, int cmd, ...)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.fcntl, GET_POSIX_API_FAILED);

    va_list va;
    va_start(va, cmd);
    long arg = va_arg(va, long);
    va_end(va);

    return g_posixApiOps.fcntl(sockfd, cmd, arg);
}

int KNET_Fcntl64(int sockfd, int cmd, ...)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.fcntl64, GET_POSIX_API_FAILED);

    va_list va;
    va_start(va, cmd);
    long arg = va_arg(va, long);
    va_end(va);

    return g_posixApiOps.fcntl64(sockfd, cmd, arg);
}

int KNET_Poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.poll, GET_POSIX_API_FAILED);
    return g_posixApiOps.poll(fds, nfds, timeout);
}

int KNET_Ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout_ts, const sigset_t *sigmask)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.ppoll, GET_POSIX_API_FAILED);
    return g_posixApiOps.ppoll(fds, nfds, timeout_ts, sigmask);
}

int KNET_Select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.select, GET_POSIX_API_FAILED);
    return g_posixApiOps.select(nfds, readfds, writefds, exceptfds, timeout);
}

int KNET_Pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timespec *timeout,
    const sigset_t *sigmask)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.pselect, GET_POSIX_API_FAILED);
    return g_posixApiOps.pselect(nfds, readfds, writefds, exceptfds, timeout, sigmask);
}

int KNET_Ioctl(int sockfd, unsigned long request, ...)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.ioctl, GET_POSIX_API_FAILED);

    va_list va;
    va_start(va, request);
    char *arg = va_arg(va, char *);
    va_end(va);

    return g_posixApiOps.ioctl(sockfd, request, arg);
}

pid_t KNET_Fork(void)
{
    KNET_CHECK_AND_GET_POSIX_API(g_posixApiOps.fork, GET_POSIX_API_FAILED);
    return g_posixApiOps.fork();
}