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
#include "dp_socket_api.h"
#include "dp_errno.h"

#include "dp_fd.h"
#include "sock.h"
#include "sock_ops.h"
#include "utils_log.h"
#include "utils_debug.h"
#include "utils_statistic.h"
#include "worker.h"

static int DpCoThreadWidCheck(Sock_t* sk)
{
    if ((CFG_GET_VAL(DP_CFG_DEPLOYMENT) == DP_DEPLOYMENT_CO_THREAD) || (CFG_GET_VAL(CFG_NOLOCK) == DP_ENABLE))  {
        int32_t wid = WORKER_GetSelfId();
        if (wid < 0 || wid != sk->wid) {
            DP_LOG_DBG("close socket failed, sk wid = %d, cur wid = %d", sk->wid, wid);
            DP_ADD_ABN_STAT(DP_WORKER_MISS_MATCH);
            return 0;
        }
    }

    return 1;
}

static int DP_SocketCanClose(Sock_t* sk)
{
    return DpCoThreadWidCheck(sk);
}

static int DP_SocketCanFcntl(Sock_t* sk)
{
    return DpCoThreadWidCheck(sk);
}

static FdOps_t g_socketFdMeth = {
    .close = (int (*)(void*))SOCK_Close,
    .fcntl = (int (*)(void*, int, int))SOCK_Fcntl,
    .canClose = (int (*)(void*))DP_SocketCanClose,
    .canFcntl = (int (*)(void*))DP_SocketCanFcntl,
};

int DP_Socket(int domain, int type, int protocol)
{
    Sock_t* sk = NULL;
    Fd_t*   file;
    int     ret;
    int32_t wid = -1;

    if ((CFG_GET_VAL(DP_CFG_DEPLOYMENT) == DP_DEPLOYMENT_CO_THREAD) || (CFG_GET_VAL(CFG_NOLOCK) == DP_ENABLE))  {
        wid = WORKER_GetSelfId();
        if (wid < 0) {
            DP_LOG_DBG("Create socket failed, get wid error");
            DP_ADD_ABN_STAT(DP_WORKER_MISS_MATCH);
            return -1;
        }
    }

    file = FD_Alloc();
    if (file == NULL) {
        DP_LOG_ERR("Create socket failed, get fd file invalid.");
        DP_ADD_ABN_STAT(DP_SOCKET_FD_ERR);
        return -1;
    }

    uint32_t tempType = (uint32_t)type;
    uint32_t flags = (tempType & DP_SOCK_CLOEXEC) != 0 ? DP_FD_CLOEXEC : 0;
    uint16_t nonblock = (tempType & DP_SOCK_NONBLOCK) != 0 ? 1 : 0;
    tempType = tempType & ~(DP_SOCK_CLOEXEC | DP_SOCK_NONBLOCK);

    ret = SOCK_Create(NS_GetDftNet(), domain, (int)tempType, protocol, &sk);
    ASSERT(ret <= 0);
    if ((ret != 0) || (sk == NULL)) {
        FD_Free(file);
        DP_ADD_ABN_STAT(DP_SOCKET_CREATE_ERR);
        DP_SET_ERRNO(-ret);
        return -1;
    }

    file->flags = flags;
    file->type = FD_TYPE_SOCKET;
    file->priv = sk;
    file->ops  = &g_socketFdMeth;

    sk->sockType = (uint16_t)tempType; // 新增，注意初始化时该字段为0
    sk->file = file;
    sk->wid = wid;
    sk->nonblock = nonblock;
    /* 无锁场景下，worker之间没有共享资源，分流由产品完成，只检查worker内是否存在重复连接 */
    sk->glbHashTblIdx = (CFG_GET_VAL(CFG_NOLOCK) == DP_ENABLE) ? (uint8_t)wid : 0;

    return FD_GetUserFd(file);
}

static int GetSockFromFd(int fd, Fd_t** file, Sock_t** sk)
{
    int ret = FD_GetOptRef(fd, FD_TYPE_SOCKET, file);
    if (ret != 0) {
        DP_LOG_DBG("GetSockFromFd failed, FD_Get failed.");
        DP_ADD_ABN_STAT(DP_SOCK_GET_FD_ERR);
        return ret;
    }

    *sk = (Sock_t *)((*file)->priv);
    if ((*sk == NULL) || ((*sk)->ops == NULL)) {
        DP_LOG_DBG("Get sock from fd failed, sk or skOps NULL.");
        DP_ADD_ABN_STAT(DP_SOCK_GET_SK_NULL);
        FD_PutOptRef(*file);
        return -EINVAL;
    }

    if ((CFG_GET_VAL(DP_CFG_DEPLOYMENT) == DP_DEPLOYMENT_CO_THREAD) || (CFG_GET_VAL(CFG_NOLOCK) == DP_ENABLE))  {
        int32_t wid = WORKER_GetSelfId();
        if (wid != (*sk)->wid) {
            DP_LOG_DBG("Get sock from fd failed, wid unmatched: sk->wid = %d, cur wid = %d", (*sk)->wid, wid);
            DP_ADD_ABN_STAT(DP_WORKER_MISS_MATCH);
            FD_PutOptRef(*file);
            return -EINVAL;
        }
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
        DP_ADD_ABN_STAT(DP_CONN_GET_SOCK_ERR);
        DP_SET_ERRNO(-ret);
        return -1;
    }

    ret = SOCK_Connect(sk, addr, addrlen);
    ASSERT(ret <= 0);

    FD_PutOptRef(file);

    DP_SET_ERRNO(-ret);

    return ret < 0 ? -1 : 0;
}

ssize_t DP_Sendmsg(int sockfd, const struct DP_Msghdr* msg, int flags)
{
    Sock_t* sk;
    Fd_t*   file;
    ssize_t ret;
    uint32_t msgFlags = (uint32_t)flags;

    ret = (ssize_t)GetSockFromFd(sockfd, &file, &sk);
    if (UTILS_UNLIKELY(ret != 0)) {
        DP_ADD_ABN_STAT(DP_SENDMSG_GET_SOCK_ERR);
        DP_SET_ERRNO((int)(-ret));
        return -1;
    }

    if (UTILS_UNLIKELY(((uint32_t)flags & DP_MSG_ZEROCOPY) != 0)) {     // 清零零拷贝标志位
        msgFlags = ((uint32_t)flags & ~DP_MSG_ZEROCOPY);
    }

    ret = SOCK_Sendmsg(sk, msg, (int)msgFlags);

    FD_PutOptRef(file);

    if (UTILS_UNLIKELY(ret < 0)) {
        DP_SET_ERRNO((int)(-ret));
        return -1;
    }

    DP_SET_ERRNO(0);
    return ret;
}

ssize_t DP_ZSendmsg(int sockfd, const struct DP_ZMsghdr* msg, int flags)
{
    if (UTILS_UNLIKELY(CFG_GET_VAL(DP_CFG_ZERO_COPY) == 0)) {
        DP_LOG_DBG("Zero copy send msg failed, zero copy not enable.");
        return -1;
    }

    Sock_t* sk;
    Fd_t*   file;
    ssize_t ret;
    uint32_t msgFlags;

    ret = (ssize_t)GetSockFromFd(sockfd, &file, &sk);
    if (UTILS_UNLIKELY(ret != 0)) {
        DP_ADD_ABN_STAT(DP_ZSENDMSG_GET_SOCK_ERR);
        DP_SET_ERRNO((int)(-ret));
        return -1;
    }

    msgFlags = ((uint32_t)flags | DP_MSG_ZEROCOPY | DP_MSG_DONTWAIT);       // 零拷贝写默认非阻塞

    ret = SOCK_Sendmsg(sk, (const struct DP_Msghdr*)msg, (int)msgFlags);

    FD_PutOptRef(file);

    if (UTILS_UNLIKELY(ret < 0)) {
        DP_LOG_DBG("DP_ZSendmsg failed, errno = %d.", (int)(-ret));
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
    if (UTILS_UNLIKELY(ret != 0)) {
        DP_ADD_ABN_STAT(DP_SENDTO_GET_SOCK_ERR);
        DP_SET_ERRNO((int)(-ret));
        return -1;
    }

    ret = SOCK_Sendto(sk, buf, len, flags, dstAddr, addrlen);

    FD_PutOptRef(file);

    if (UTILS_UNLIKELY(ret < 0)) {
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
        DP_LOG_DBG("Sk writev failed, iov cnt is invalid.");
        return -1;
    }

    if (iov == NULL) {
        DP_LOG_DBG("Sk writev failed, iov invalid.");
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
    uint32_t msgFlags = (uint32_t)flags;

    ret = (ssize_t)GetSockFromFd(sockfd, &file, &sk);
    if (UTILS_UNLIKELY(ret != 0)) {
        DP_ADD_ABN_STAT(DP_RCVMSG_FAILED);
        DP_SET_ERRNO((int)(-ret));
        return -1;
    }

    if (UTILS_UNLIKELY(((uint32_t)flags & DP_MSG_ZEROCOPY) != 0)) {     // 清零零拷贝标志位
        msgFlags = ((uint32_t)flags & ~DP_MSG_ZEROCOPY);
    }

    ret = SOCK_Recvmsg(sk, msg, (int)msgFlags);

    FD_PutOptRef(file);

    if (UTILS_UNLIKELY(ret < 0)) {
        DP_SET_ERRNO((int)(-ret));
        return -1;
    }

    DP_SET_ERRNO(0);
    return ret;
}

ssize_t DP_ZRecvmsg(int sockfd, struct DP_ZMsghdr* msg, int flags)
{
    if (CFG_GET_VAL(DP_CFG_ZERO_COPY) == 0) {
        DP_LOG_ERR("Zero copy recv msg failed, zero copy not enable.");
        return -1;
    }

    Sock_t* sk;
    Fd_t*   file;
    ssize_t ret;
    uint32_t msgFlags;

    ret = (ssize_t)GetSockFromFd(sockfd, &file, &sk);
    if (UTILS_UNLIKELY(ret != 0)) {
        DP_ADD_ABN_STAT(DP_ZRCVMSG_FAILED);
        DP_SET_ERRNO((int)(-ret));
        return -1;
    }

    msgFlags = ((uint32_t)flags | DP_MSG_ZEROCOPY);

    ret = SOCK_Recvmsg(sk, (struct DP_Msghdr*)msg, (int)msgFlags);

    FD_PutOptRef(file);

    if (UTILS_UNLIKELY(ret < 0)) {
        DP_LOG_DBG("DP_ZRecvmsg failed, errno = %d.", (int)(-ret));
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
    if (UTILS_UNLIKELY(ret != 0)) {
        DP_ADD_ABN_STAT(DP_RCVFROM_GET_SOCK_ERR);
        DP_SET_ERRNO((int)(-ret));
        return -1;
    }

    ret = SOCK_Recvfrom(sk, buf, len, flags, srcAddr, addrlen);

    FD_PutOptRef(file);

    if (UTILS_UNLIKELY(ret < 0)) {
        DP_ADD_ABN_STAT(DP_RCVFROM_FAILED);
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
    if (UTILS_UNLIKELY(iovcnt == 0)) {
        return 0;
    }

    if (UTILS_UNLIKELY(iov == NULL)) {
        DP_SET_ERRNO(EFAULT);
        DP_LOG_DBG("Sk readv failed, iov invalid.");
        return -1;
    } else if (UTILS_UNLIKELY(iovcnt > MAX_IOV_CNT || iovcnt < 0)) {
        DP_SET_ERRNO(EINVAL);
        DP_LOG_DBG("Sk readv failed, iov cnt too big.");
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
        DP_ADD_ABN_STAT(DP_BIND_GET_SOCK_ERR);
        DP_SET_ERRNO(-ret);
        return -1;
    }

    ret = SOCK_Bind(sk, addr, addrlen);
    ASSERT(ret <= 0);

    FD_PutOptRef(file);

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

    FD_PutOptRef(file);

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
        DP_ADD_ABN_STAT(DP_ACCEPT_GET_SOCK_ERR);
        DP_SET_ERRNO(-ret);
        return -1;
    }

    newFile = FD_Alloc();
    if (newFile == NULL) {
        DP_LOG_ERR("Create socket failed, get fd file invalid.");
        FD_PutOptRef(file);
        DP_ADD_ABN_STAT(DP_ACCEPT_FD_ERR);
        return -1;
    }

    ret = SOCK_Accept(sk, addr, addrlen, &csk);
    ASSERT(ret <= 0);
    if (ret < 0) {
        FD_Free(newFile);
        FD_PutOptRef(file);
        DP_ADD_ABN_STAT(DP_ACCEPT_CREATE_ERR);
        DP_SET_ERRNO(-ret);
        return -1;
    }

    newFile->type = FD_TYPE_SOCKET;
    newFile->priv = csk;
    newFile->ops  = &g_socketFdMeth;

    csk->file = newFile;

    FD_PutOptRef(file);

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

    FD_PutOptRef(file);

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

    FD_PutOptRef(file);

    DP_SET_ERRNO(-ret);

    return ret == 0 ? 0 : -1;
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

    ret = SOCK_Setsockopt(sk, level, optname, optval, optlen);

    FD_PutOptRef(file);
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

    ret = SOCK_Getsockopt(sk, level, optname, optval, optlen);

    FD_PutOptRef(file);
    DP_SET_ERRNO(-ret);

    return ret < 0 ? -1 : 0;
}

// 记录遍历socket的起点socket fd(协议栈真实fd)
static int g_socketFdStart = 0;

// 支持去初始化恢复初始值
void SockResetFdIdx(void)
{
    g_socketFdStart = 0;
}

int DP_GetSocketStatus(DP_SocketInfo_t *para)
{
    int     ret;
    int     userFd;
    uint32_t num;
    uint32_t idx = 0;

    if ((para == NULL) || (para->socketList == NULL) || (para->socketNum == 0)) {
        DP_LOG_ERR("DP_GetSocketStatus failed, invalid para.");
        return -1;
    }

    num = para->socketNum;

    if (para->resetFlag != 0) {
        DP_LOG_INFO("DP_GetSocketStatus look up socket start 0.");
        g_socketFdStart = 0;
    }

    while ((num > 0) && (g_socketFdStart < FD_GetFileLimit())) {
        uint32_t skSndBuf = 0;
        DP_Socklen_t bufLen = sizeof(DP_Socklen_t);
        userFd = FD_GetUserFdByRealFd(g_socketFdStart);
        ret = DP_Getsockopt(userFd, DP_SOL_SOCKET, DP_SO_SNDBUF, (void *)&skSndBuf, &bufLen);
        if (ret == 0) {
            para->socketList[idx++] = userFd;
            num--;
        }

        g_socketFdStart++;
    }

    // 如果没有可用socket则返回错误
    if (idx == 0) {
        DP_LOG_INFO("DP_GetSocketStatus failed, no userFd.");
        return -1;
    }

    para->socketNum = idx;

    return 0;
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

    FD_PutOptRef(file);

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

    FD_PutOptRef(file);

    DP_SET_ERRNO(-ret);

    return ret < 0 ? -1 : 0;
}

void DP_ShowSocketInfoByFd(int fd)
{
    Sock_t* sk;
    Fd_t*   file;
    int     ret;

    if (DEBUG_SHOW == NULL) {
        DP_LOG_ERR("DP_ShowSocketInfoByFd is unavailable without show hook registered.");
        return;
    }

    ret = GetSockFromFd(fd, &file, &sk);
    if (ret != 0) {
        return;
    }
    SOCK_Lock(sk);

    SOCK_ShowSocketInfo(sk);

    SOCK_Unlock(sk);
    FD_PutOptRef(file);
}

int DP_GetSocketDetails(int fd, DP_SockDetails_t* details)
{
    if (details == NULL) {
        DP_LOG_DBG("get sock details: details is NULL.");
        DP_SET_ERRNO(EINVAL);
        return -1;
    }

    Fd_t* file;
    int ret = FD_Get(fd, FD_TYPE_SOCKET, &file);
    if (ret != 0) {
        DP_LOG_DBG("get sock details: get fd failed.");
        DP_SET_ERRNO(-ret);
        return -1;
    }

    Sock_t* sk = (Sock_t *)(file->priv);
    if ((sk == NULL) || (sk->ops == NULL)) {
        DP_LOG_DBG("get sock details: sk or skOps NULL.");
        DP_SET_ERRNO(EINVAL);
        FD_Put(file);
        return -1;
    }

    SOCK_Lock(sk);

    SOCK_GetSocketDetails(sk, details);

    SOCK_Unlock(sk);
    FD_Put(file);
    return 0;
}

int DP_GetSocketState(int fd, DP_SocketState_t* state)
{
    Sock_t* sk;
    Fd_t*   file;

    if (state == NULL) {
        DP_LOG_DBG("get sock state: sock state is NULL.");
        DP_SET_ERRNO(EINVAL);
        return -1;
    }
    int ret = FD_Get(fd, FD_TYPE_SOCKET, &file);
    if (ret != 0) {
        DP_LOG_DBG("get sock state: get fd failed.");
        DP_SET_ERRNO(-ret);
        return -1;
    }

    sk = (Sock_t *)(file->priv);
    if ((sk == NULL) || (sk->ops == NULL)) {
        DP_LOG_DBG("get sock state: sk or skOps NULL.");
        FD_Put(file);
        DP_SET_ERRNO(EINVAL);
        return -1;
    }

    SOCK_Lock(sk);

    ret = SOCK_GetSocketState(sk, state);
    if (ret != 0) {
        DP_LOG_DBG("get sock state failed.");
        DP_SET_ERRNO(-ret);
        SOCK_Unlock(sk);
        FD_Put(file);
        return -1;
    }

    SOCK_Unlock(sk);
    FD_Put(file);
    return 0;
}
