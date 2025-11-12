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

#include "dp_socket_types_api.h"

#include "dp_errno.h"

#include "fd.h"
#include "sock.h"
#include "sock_ops.h"
#include "utils_log.h"
#include "utils_debug.h"

static FdOps_t g_socketFdMeth = { .close = (int (*)(void*))SOCK_Close };

int DP_Socket(int domain, int type, int protocol)
{
    Sock_t* sk = NULL;
    Fd_t*   file;
    int     ret;

    file = FD_Alloc();
    if (file == NULL) {
        DP_LOG_ERR("Create socket failed, get fd file invalid.");
        return -1;
    }

    ret = SOCK_Create(NULL, domain, type, protocol, &sk);
    ASSERT(ret <= 0);
    if ((ret != 0) || (sk == NULL)) {
        FD_Free(file);
        DP_SET_ERRNO(-ret);
        return -1;
    }

    file->type = FD_TYPE_SOCKET;
    file->priv = sk;
    file->ops  = &g_socketFdMeth;

    sk->file = file;

    return FD_GetUserFd(file);
}

static int GetSockFromFd(int fd, Fd_t** file, Sock_t** sk)
{
    int ret = FD_Get(fd, FD_TYPE_SOCKET, file);
    if (ret != 0) {
        return ret;
    }

    *sk = (Sock_t *)((*file)->priv);
    if ((*sk == NULL) || ((*sk)->ops == NULL)) {
        DP_LOG_ERR("Get sock from fd failed, sk or skOps NULL.");
        FD_Put(*file);
        return -EINVAL;
    }
    return 0;
}

int DP_Connect(int sockfd, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen)
{
    Sock_t* sk;
    Fd_t*   file;
    int     ret;

    ret = GetSockFromFd(sockfd, &file, &sk);
    if (ret != 0) {
        DP_SET_ERRNO(-ret);
        return -1;
    }

    ret = SOCK_Connect(sk, addr, addrlen);
    ASSERT(ret <= 0);

    FD_Put(file);

    DP_SET_ERRNO(-ret);

    return ret < 0 ? -1 : 0;
}

ssize_t DP_Sendmsg(int sockfd, const struct DP_Msghdr* msg, int flags)
{
    Sock_t* sk;
    Fd_t*   file;
    ssize_t ret;

    ret = (ssize_t)GetSockFromFd(sockfd, &file, &sk);
    if (ret != 0) {
        DP_SET_ERRNO((int)(-ret));
        return -1;
    }

    ret = SOCK_Sendmsg(sk, msg, flags);

    FD_Put(file);

    if (ret < 0) {
        DP_SET_ERRNO((int)(-ret));
        return -1;
    }

    DP_SET_ERRNO(0);
    return ret;
}

ssize_t DP_Sendto(
    int sockfd, const void* buf, size_t len, int flags, const struct DP_Sockaddr* dstAddr, DP_Socklen_t addrlen)
{
    Sock_t* sk;
    Fd_t*   file;
    ssize_t ret;

    ret = (ssize_t)GetSockFromFd(sockfd, &file, &sk);
    if (ret != 0) {
        DP_SET_ERRNO((int)(-ret));
        return -1;
    }

    ret = SOCK_Sendto(sk, buf, len, flags, dstAddr, addrlen);

    FD_Put(file);

    if (ret < 0) {
        DP_SET_ERRNO((int)(-ret));
        return -1;
    }

    DP_SET_ERRNO(0);
    return ret;
}

ssize_t DP_Send(int sockfd, const void* buf, size_t len, int flags)
{
    return DP_Sendto(sockfd, buf, len, flags, NULL, 0);
}

ssize_t DP_Write(int sockfd, const void *buf, size_t count)
{
    return DP_Send(sockfd, buf, count, 0);
}

ssize_t DP_Writev(int sockfd, const struct DP_Iovec *iov, int iovcnt)
{
    struct DP_Msghdr msg;

    if (iovcnt == 0) {
        return 0;
    }

    if (iovcnt < 0 || iovcnt > MAX_IOV_CNT) {
        DP_SET_ERRNO(EINVAL);
        DP_LOG_ERR("Sk writev failed, iov cnt is invalid.");
        return -1;
    }

    if (iov == NULL) {
        DP_LOG_ERR("Sk writev failed, iov invalid.");
        DP_SET_ERRNO(EFAULT);
        return -1;
    }

    msg.msg_name       = NULL;
    msg.msg_namelen    = 0;
    msg.msg_control    = NULL;
    msg.msg_controllen = 0;
    msg.msg_iov        = (struct DP_Iovec *)iov;
    msg.msg_iovlen     = (size_t)iovcnt;

    return DP_Sendmsg(sockfd, &msg, 0);
}

ssize_t DP_Recvmsg(int sockfd, struct DP_Msghdr* msg, int flags)
{
    Sock_t* sk;
    Fd_t*   file;
    ssize_t ret;

    ret = (ssize_t)GetSockFromFd(sockfd, &file, &sk);
    if (ret != 0) {
        DP_SET_ERRNO((int)(-ret));
        return -1;
    }

    ret = SOCK_Recvmsg(sk, msg, flags);

    FD_Put(file);

    if (ret < 0) {
        DP_SET_ERRNO((int)(-ret));
        return -1;
    }

    DP_SET_ERRNO(0);
    return ret;
}

ssize_t DP_Recvfrom(
    int sockfd, void* buf, size_t len, int flags, struct DP_Sockaddr* srcAddr, DP_Socklen_t* addrlen)
{
    Sock_t* sk;
    Fd_t*   file;
    ssize_t ret;

    ret = (ssize_t)GetSockFromFd(sockfd, &file, &sk);
    if (ret != 0) {
        DP_SET_ERRNO((int)(-ret));
        return -1;
    }

    ret = SOCK_Recvfrom(sk, buf, len, flags, srcAddr, addrlen);

    FD_Put(file);

    if (ret < 0) {
        DP_SET_ERRNO((int)(-ret));
        return -1;
    }

    DP_SET_ERRNO(0);
    return ret;
}

ssize_t DP_Recv(int sockfd, void* buf, size_t len, int flags)
{
    return DP_Recvfrom(sockfd, buf, len, flags, NULL, NULL);
}

ssize_t DP_Read(int sockfd, void *buf, size_t count)
{
    return DP_Recv(sockfd, buf, count, 0);
}

ssize_t DP_Readv(int sockfd, const struct DP_Iovec *iov, int iovcnt)
{
    struct DP_Msghdr msg;
    if (iovcnt == 0) {
        return 0;
    }

    if (iov == NULL) {
        DP_SET_ERRNO(EFAULT);
        DP_LOG_ERR("Sk readv failed, iov invalid.");
        return -1;
    } else if (iovcnt > MAX_IOV_CNT || iovcnt < 0) {
        DP_SET_ERRNO(EINVAL);
        DP_LOG_ERR("Sk readv failed, iov cnt too big.");
        return -1;
    }

    msg.msg_name       = NULL;
    msg.msg_namelen    = 0;
    msg.msg_control    = NULL;
    msg.msg_controllen = 0;
    msg.msg_iov        = (struct DP_Iovec *)iov;
    msg.msg_iovlen     = (size_t)iovcnt;

    return DP_Recvmsg(sockfd, &msg, 0);
}

int DP_Bind(int sockfd, const DP_Sockaddr_t* addr, DP_Socklen_t addrlen)
{
    Sock_t* sk;
    Fd_t*   file;
    int     ret;

    ret = GetSockFromFd(sockfd, &file, &sk);
    if (ret != 0) {
        DP_SET_ERRNO(-ret);
        return -1;
    }

    ret = SOCK_Bind(sk, addr, addrlen);
    ASSERT(ret <= 0);

    FD_Put(file);

    DP_SET_ERRNO(-ret);

    return ret < 0 ? -1 : 0;
}

int DP_Listen(int sockfd, int backlog)
{
    Sock_t* sk;
    Fd_t*   file;
    int     ret;

    ret = GetSockFromFd(sockfd, &file, &sk);
    if (ret != 0) {
        DP_SET_ERRNO(-ret);
        return -1;
    }

    ret = SOCK_Listen(sk, backlog);
    ASSERT(ret <= 0);

    FD_Put(file);

    DP_SET_ERRNO(-ret);

    return ret < 0 ? -1 : 0;
}

int DP_Accept(int sockfd, struct DP_Sockaddr* addr, DP_Socklen_t* addrlen)
{
    Sock_t* sk;
    Sock_t* csk = NULL;
    Fd_t*   file;
    Fd_t*   newFile;
    int     ret;

    ret = GetSockFromFd(sockfd, &file, &sk);
    if (ret != 0) {
        DP_SET_ERRNO(-ret);
        return -1;
    }

    newFile = FD_Alloc();
    if (newFile == NULL) {
        DP_LOG_ERR("Create socket failed, get fd file invalid.");
        FD_Put(file);
        return -1;
    }

    ret = SOCK_Accept(sk, addr, addrlen, &csk);
    ASSERT(ret <= 0);
    if ((ret < 0) || (csk == NULL)) {
        FD_Free(newFile);
        FD_Put(file);
        DP_SET_ERRNO(-ret);
        return -1;
    }

    newFile->type = FD_TYPE_SOCKET;
    newFile->priv = csk;
    newFile->ops  = &g_socketFdMeth;

    csk->file = newFile;

    FD_Put(file);

    return FD_GetUserFd(newFile);
}

int DP_Shutdown(int sockfd, int how)
{
    Sock_t* sk;
    Fd_t*   file;
    int     ret;

    ret = GetSockFromFd(sockfd, &file, &sk);
    if (ret != 0) {
        DP_SET_ERRNO(-ret);
        return -1;
    }

    ret = SOCK_Shutdown(sk, how);
    ASSERT(ret <= 0);

    FD_Put(file);

    DP_SET_ERRNO(-ret);

    return ret < 0 ? -1 : 0;
}

int DP_Ioctl(int fd, int request, void* arg)
{
    Sock_t* sk;
    Fd_t*   file;
    int     ret;

    ret = GetSockFromFd(fd, &file, &sk);
    if (ret != 0) {
        ret = (ret == -ENOTSOCK) ? -EBADF : ret;
        DP_SET_ERRNO(-ret);
        return -1;
    }

    ret = SOCK_Ioctl(sk, request, arg);
    ASSERT(ret <= 0);

    FD_Put(file);

    DP_SET_ERRNO(-ret);

    return ret == 0 ? 0 : -1;
}

int DP_Fcntl(int fd, int cmd, int val)
{
    Fd_t*   file;
    Sock_t* sk;
    int     ret = 0;

    ret = GetSockFromFd(fd, &file, &sk);
    if (ret != 0) {
        DP_SET_ERRNO(-ret);
        return -1;
    }

    ret = SOCK_Fcntl(sk, cmd, val);

    FD_Put(file);

    if (ret < 0) {
        DP_SET_ERRNO(-ret);
    }

    return ret < 0 ? -1 : ret;
}

int DP_Setsockopt(int sockfd, int level, int optname, const void* optval, DP_Socklen_t optlen)
{
    Sock_t* sk;
    Fd_t*   file;
    int     ret;

    ret = GetSockFromFd(sockfd, &file, &sk);
    if (ret != 0) {
        DP_SET_ERRNO(-ret);
        return -1;
    }
    SOCK_Lock(sk);

    switch (level) {
        case DP_SOL_SOCKET:
            ret = SOCK_Setsockopt(sk, level, optname, optval, optlen);
            break;
        case DP_IPPROTO_IP:
        case DP_IPPROTO_TCP:
        case DP_IPPROTO_UDP:
            ret = -ENOPROTOOPT;
            if (sk->ops->setsockopt != NULL) {
                ret = sk->ops->setsockopt(sk, level, optname, optval, optlen);
            }
            break;
        default:
            ret = -ENOPROTOOPT;
            break;
    }

    ASSERT(ret <= 0);

    SOCK_Unlock(sk);
    FD_Put(file);
    DP_SET_ERRNO(-ret);

    return ret < 0 ? -1 : 0;
}

int DP_Getsockopt(int sockfd, int level, int optname, void* optval, DP_Socklen_t* optlen)
{
    Sock_t* sk;
    Fd_t*   file;
    int     ret;

    ret = GetSockFromFd(sockfd, &file, &sk);
    if (ret != 0) {
        DP_SET_ERRNO(-ret);
        return -1;
    }

    SOCK_Lock(sk);

    switch (level) {
        case DP_SOL_SOCKET:
            ret = SOCK_Getsockopt(sk, level, optname, optval, optlen);
            break;
        case DP_IPPROTO_IP:
        case DP_IPPROTO_TCP:
        case DP_IPPROTO_UDP:
            ret = -EOPNOTSUPP;
            if (sk->ops->getsockopt != NULL) {
                ret = sk->ops->getsockopt(sk, level, optname, optval, optlen);
            }
            break;
        default:
            ret = -EOPNOTSUPP;
            break;
    }

    ASSERT(ret <= 0);

    SOCK_Unlock(sk);
    FD_Put(file);
    DP_SET_ERRNO(-ret);

    return ret < 0 ? -1 : 0;
}

int DP_Getpeername(int sockfd, struct DP_Sockaddr *addr, DP_Socklen_t *addrlen)
{
    Sock_t* sk;
    Fd_t*   file;
    int     ret;

    ret = GetSockFromFd(sockfd, &file, &sk);
    if (ret != 0) {
        DP_SET_ERRNO(-ret);
        return -1;
    }

    ret = SOCK_Getpeername(sk, addr, addrlen);

    FD_Put(file);

    DP_SET_ERRNO(-ret);

    return ret < 0 ? -1 : 0;
}

int DP_Getsockname(int sockfd, struct DP_Sockaddr *addr, DP_Socklen_t *addrlen)
{
    Sock_t* sk;
    Fd_t*   file;
    int     ret;

    ret = GetSockFromFd(sockfd, &file, &sk);
    if (ret != 0) {
        DP_SET_ERRNO(-ret);
        return -1;
    }

    ret = SOCK_Getsockname(sk, addr, addrlen);

    FD_Put(file);

    DP_SET_ERRNO(-ret);

    return ret < 0 ? -1 : 0;
}
