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
#include <net/if.h>

#include "dp_posix_socket_api.h"
#include "knet_config.h"
#include "knet_osapi.h"
#include "knet_dpdk_init.h"
#include "knet_tcp_api_init.h"
#include "tcp_fd.h"
#include "tcp_os.h"
#include "knet_signal_tcp.h"
#include "knet_init.h"
#include "tcp_socket.h"

ssize_t KNET_DpSend(int sockfd, const void *buf, size_t len, int flags)
{
    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.send, KNET_INVALID_FD);
        return g_origOsApi.send(sockfd, buf, len, flags);
    }

    if (!KNET_IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d send is not hijacked, len %zu, flags %d", sockfd, len, flags);
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.send, KNET_INVALID_FD);
        return g_origOsApi.send(sockfd, buf, len, flags);
    }

    BEFORE_DPFUNC();
    ssize_t ret = DP_PosixSend(KNET_OsFdToDpFd(sockfd), buf, len, flags);
    AFTER_DPFUNC();
    if (ret < 0 && errno != EAGAIN) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "OsFd %d dpFd %d DP_PosixSend ret %zd, errno %d, %s, len %zu, flags %d",
            sockfd, KNET_OsFdToDpFd(sockfd), ret, errno, strerror(errno), len, flags);
    }
    return ret;
}

ssize_t KNET_DpSendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr,
    socklen_t addrlen)
{
    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.sendto, KNET_INVALID_FD);
        return g_origOsApi.sendto(sockfd, buf, len, flags, dest_addr, addrlen);
    }

    if (!KNET_IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d sendto is not hijacked, len %zu, flags %d", sockfd, len, flags);
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.sendto, KNET_INVALID_FD);
        return g_origOsApi.sendto(sockfd, buf, len, flags, dest_addr, addrlen);
    }
    BEFORE_DPFUNC();
    ssize_t ret = DP_PosixSendto(KNET_OsFdToDpFd(sockfd), buf, len, flags, dest_addr, addrlen);
    AFTER_DPFUNC();
    if (ret < 0 && errno != EAGAIN) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR,
            "OsFd %d dpFd %d DP_PosixSendto ret %zd, errno %d, %s, len %zu, flags %d, addrlen %u",
            sockfd, KNET_OsFdToDpFd(sockfd), ret, errno, strerror(errno), len, flags, addrlen);
    }
    return ret;
}

ssize_t KNET_DpWritev(int sockfd, const struct iovec *iov, int iovcnt)
{
    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.writev, KNET_INVALID_FD);
        return g_origOsApi.writev(sockfd, iov, iovcnt);
    }

    if (!KNET_IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d writev is not hijacked, iovcnt %d", sockfd, iovcnt);
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.writev, KNET_INVALID_FD);
        return g_origOsApi.writev(sockfd, iov, iovcnt);
    }
    BEFORE_DPFUNC();
    ssize_t ret = DP_PosixWritev(KNET_OsFdToDpFd(sockfd), iov, iovcnt);
    AFTER_DPFUNC();
    if (ret < 0 && errno != EAGAIN) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "OsFd %d dpFd %d DP_PosixWritev ret %d, errno %d, %s, iovcnt %d",
            sockfd, KNET_OsFdToDpFd(sockfd), ret, errno, strerror(errno), iovcnt);
    }
    return ret;
}

ssize_t KNET_DpSendmsg(int sockfd, const struct msghdr *msg, int flags)
{
    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.sendmsg, KNET_INVALID_FD);
        return g_origOsApi.sendmsg(sockfd, msg, flags);
    }

    if (!KNET_IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d sendmsg is not hijacked, flags %d", sockfd, flags);
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.sendmsg, KNET_INVALID_FD);
        return g_origOsApi.sendmsg(sockfd, msg, flags);
    }

    BEFORE_DPFUNC();
    ssize_t ret = DP_PosixSendmsg(KNET_OsFdToDpFd(sockfd), msg, flags);
    AFTER_DPFUNC();
    if (ret < 0 && errno != EAGAIN) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "OsFd %d dpFd %d DP_PosixSendmsg ret %zd, errno %d, %s, flags %d",
            sockfd, KNET_OsFdToDpFd(sockfd), ret, errno, strerror(errno), flags);
    }
    return ret;
}

ssize_t KNET_DpRecv(int sockfd, void *buf, size_t len, int flags)
{
    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.recv, KNET_INVALID_FD);
        return g_origOsApi.recv(sockfd, buf, len, flags);
    }

    if (!KNET_IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d recv is not hijacked, len %zu, flags %d", sockfd, len, flags);
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.recv, KNET_INVALID_FD);
        return g_origOsApi.recv(sockfd, buf, len, flags);
    }
    BEFORE_DPFUNC();
    ssize_t ret = DP_PosixRecv(KNET_OsFdToDpFd(sockfd), buf, len, flags);
    AFTER_DPFUNC();
    if (ret < 0 && errno != EAGAIN) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "OsFd %d dpFd %d DP_PosixRecv ret %zd, errno %d, %s, len %zu, flags %d",
            sockfd, KNET_OsFdToDpFd(sockfd), ret, errno, strerror(errno), len, flags);
    }
    return ret;
}

ssize_t KNET_DpRecvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr,
    socklen_t *addrlen)
{
    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.recvfrom, KNET_INVALID_FD);
        return g_origOsApi.recvfrom(sockfd, buf, len, flags, src_addr, addrlen);
    }

    if (!KNET_IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d recvfrom is not hijacked", sockfd);
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.recvfrom, KNET_INVALID_FD);
        return g_origOsApi.recvfrom(sockfd, buf, len, flags, src_addr, addrlen);
    }
    BEFORE_DPFUNC();
    ssize_t ret = DP_PosixRecvfrom(KNET_OsFdToDpFd(sockfd), buf, len, flags, src_addr, addrlen);
    AFTER_DPFUNC();
    if (ret < 0 && errno != EAGAIN) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "OsFd %d dpFd %d DP_PosixRecvfrom ret %zd, errno %d, %s, len %zu, flags %d",
            sockfd, KNET_OsFdToDpFd(sockfd), ret, errno, strerror(errno), len, flags);
    }
    return ret;
}

ssize_t KNET_DpRecvmsg(int sockfd, struct msghdr *msg, int flags)
{
    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.recvmsg, KNET_INVALID_FD);
        return g_origOsApi.recvmsg(sockfd, msg, flags);
    }

    if (!KNET_IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d recvmsg is not hijacked, flags %d", sockfd, flags);
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.recvmsg, KNET_INVALID_FD);
        return g_origOsApi.recvmsg(sockfd, msg, flags);
    }
    BEFORE_DPFUNC();
    ssize_t ret = DP_PosixRecvmsg(KNET_OsFdToDpFd(sockfd), msg, flags);
    AFTER_DPFUNC();
    if (ret < 0 && errno != EAGAIN) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "OsFd %d dpFd %d DP_PosixRecvmsg ret %zd, errno %d, %s, flags %d",
            sockfd, KNET_OsFdToDpFd(sockfd), ret, errno, strerror(errno), flags);
    }
    return ret;
}

ssize_t KNET_DpReadv(int sockfd, const struct iovec *iov, int iovcnt)
{
    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.readv, KNET_INVALID_FD);
        return g_origOsApi.readv(sockfd, iov, iovcnt);
    }

    if (!KNET_IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d readv is not hijacked, iovcnt %d", sockfd, iovcnt);
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.readv, KNET_INVALID_FD);
        return g_origOsApi.readv(sockfd, iov, iovcnt);
    }
    BEFORE_DPFUNC();
    ssize_t ret = DP_PosixReadv(KNET_OsFdToDpFd(sockfd), iov, iovcnt);
    AFTER_DPFUNC();
    if (ret < 0 && errno != EAGAIN) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "OsFd %d dpFd %d DP_PosixReadv ret %zd, errno %d, %s, iovcnt %d",
            sockfd, KNET_OsFdToDpFd(sockfd), ret, errno, strerror(errno), iovcnt);
    }
    return ret;
}

ssize_t KNET_DpRead(int sockfd, void *buf, size_t count)
{
    if (!g_tcpInited) {
        KNET_DEBUG("Dp is not initialized, fd %d go os", sockfd);
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.read, KNET_INVALID_FD);
        return g_origOsApi.read(sockfd, buf, count);
    }

    if (!KNET_IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d read is not hijacked, count %zu", sockfd, count);
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.read, KNET_INVALID_FD);
        return g_origOsApi.read(sockfd, buf, count);
    }
    BEFORE_DPFUNC();
    ssize_t ret = DP_PosixRead(KNET_OsFdToDpFd(sockfd), buf, count);
    AFTER_DPFUNC();
    if (ret < 0 && errno != EAGAIN) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "DP_PosixRead ret %d, osFd %d, dpFd %d, errno %d, %s, count %zu",
            ret, sockfd, KNET_OsFdToDpFd(sockfd), errno, strerror(errno), count);
    }

    return ret;
}

ssize_t KNET_DpWrite(int sockfd, const void *buf, size_t count)
{
    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.write, KNET_INVALID_FD);
        return g_origOsApi.write(sockfd, buf, count);
    }

    if (!KNET_IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d write is not hijacked, count %zu", sockfd, count);
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.write, KNET_INVALID_FD);
        return g_origOsApi.write(sockfd, buf, count);
    }
    BEFORE_DPFUNC();
    ssize_t ret = DP_PosixWrite(KNET_OsFdToDpFd(sockfd), buf, count);
    AFTER_DPFUNC();
    if (ret < 0 && errno != EAGAIN) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "OsFd %d dpFd %d DP_PosixWrite ret %d, errno %d, %s, count %zu",
            sockfd, KNET_OsFdToDpFd(sockfd), ret, errno, strerror(errno), count);
    }

    return ret;
}