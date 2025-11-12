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

#include "dp_posix_socket_api.h"
#include "dp_socket.h"

int DP_PosixSocket(int domain, int type, int protocol)
{
    return DP_Socket(domain, type, protocol);
}

int DP_PosixBind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    return DP_Bind(sockfd, (const struct DP_Sockaddr *)addr, (DP_Socklen_t)addrlen);
}

int DP_PosixConnect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    return DP_Connect(sockfd, (const struct DP_Sockaddr *)addr, (DP_Socklen_t)addrlen);
}

ssize_t DP_PosixSend(int sockfd, const void* buf, size_t len, int flags)
{
    return DP_Send(sockfd, buf, len, flags);
}

int DP_PosixGetpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    return DP_Getpeername(sockfd, (struct DP_Sockaddr *)addr, (DP_Socklen_t *)addrlen);
}

int DP_PosixGetsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    return DP_Getsockname(sockfd, (struct DP_Sockaddr *)addr, (DP_Socklen_t *)addrlen);
}

int DP_PosixGetsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen)
{
    return DP_Getsockopt(sockfd, level, optname, optval, (DP_Socklen_t *)optlen);
}

int DP_PosixSetsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
    return DP_Setsockopt(sockfd, level, optname, optval, (DP_Socklen_t)optlen);
}

int DP_PosixAccept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    return DP_Accept(sockfd, (struct DP_Sockaddr *)addr, (DP_Socklen_t *)addrlen);
}

ssize_t DP_PosixRecvmsg(int sockfd, struct msghdr *msg, int flags)
{
    return DP_Recvmsg(sockfd, (struct DP_Msghdr *)msg, flags);
}

ssize_t DP_PosixSendmsg(int sockfd, const struct msghdr *msg, int flags)
{
    return DP_Sendmsg(sockfd, (struct DP_Msghdr *)msg, flags);
}

ssize_t DP_PosixSendto(int sockfd, const void *buf, size_t len, int flags,
    const struct sockaddr *destAddr, socklen_t addrlen)
{
    return DP_Sendto(sockfd, buf, len, flags, (struct DP_Sockaddr *)destAddr, (DP_Socklen_t)addrlen);
}

ssize_t DP_PosixWrite(int sockfd, const void *buf, size_t count)
{
    return DP_Write(sockfd, buf, count);
}

ssize_t DP_PosixWritev(int fd, const struct iovec *iov, int iovcnt)
{
    return DP_Writev(fd, (const struct DP_Iovec *)iov, iovcnt);
}

ssize_t DP_PosixRecv(int sockfd, void* buf, size_t len, int flags)
{
    return DP_Recv(sockfd, buf, len, flags);
}

ssize_t DP_PosixRecvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *srcAddr, socklen_t *addrlen)
{
    return DP_Recvfrom(sockfd, buf, len, flags, (struct DP_Sockaddr *)srcAddr, (DP_Socklen_t *)addrlen);
}

ssize_t DP_PosixRead(int sockfd, void *buf, size_t count)
{
    return DP_Read(sockfd, buf, count);
}

ssize_t DP_PosixReadv(int fd, const struct iovec *iov, int iovcnt)
{
    return DP_Readv(fd, (const struct DP_Iovec *)iov, iovcnt);
}

int DP_PosixClose(int fd)
{
    return DP_Close(fd);
}

int DP_PosixShutdown(int sockfd, int how)
{
    return DP_Shutdown(sockfd, how);
}

int DP_PosixListen(int sockfd, int backlog)
{
    return DP_Listen(sockfd, backlog);
}

int DP_PosixIoctl(int fd, int request, void* arg)
{
    return DP_Ioctl(fd, request, arg);
}

int DP_PosixFcntl(int fd, int cmd, int val)
{
    return DP_Fcntl(fd, cmd, val);
}
