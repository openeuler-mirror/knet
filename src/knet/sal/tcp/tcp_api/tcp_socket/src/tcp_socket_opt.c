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
#include <rte_ethdev.h>
#include <linux/sockios.h>
#include <linux/ethtool.h>

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

int KNET_DpGetsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen)
{
    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.getsockopt, KNET_INVALID_FD);
        return g_origOsApi.getsockopt(sockfd, level, optname, optval, optlen);
    }

    if (!KNET_IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d getsockopt is not hijacked", sockfd);
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.getsockopt, KNET_INVALID_FD);
        return g_origOsApi.getsockopt(sockfd, level, optname, optval, optlen);
    }

    if (level == SOL_NETLINK) {
        return g_origOsApi.getsockopt(sockfd, level, optname, optval, optlen);
    }
    
    BEFORE_DPFUNC();
    int ret = DP_PosixGetsockopt(KNET_OsFdToDpFd(sockfd), level, optname, optval, optlen);
    AFTER_DPFUNC();
    if (ret < 0) {
        KNET_ERR("DP_PosixGetsockopt ret %d, errno %d, %s, level %d, optname %d",
            ret, errno, strerror(errno), level, optname);
    }
    return ret;
}

int KNET_DpSetsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.setsockopt, KNET_INVALID_FD);
        return g_origOsApi.setsockopt(sockfd, level, optname, optval, optlen);
    }

    if (!KNET_IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d setsockopt is not hijacked, level %d, optname %d",
            sockfd, level, optname);
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.setsockopt, KNET_INVALID_FD);
        return g_origOsApi.setsockopt(sockfd, level, optname, optval, optlen);
    }

    if (level == SOL_NETLINK) {
        return g_origOsApi.setsockopt(sockfd, level, optname, optval, optlen);
    }

    if (optname == SO_TIMESTAMPING) {
        return 0; // 打桩实现设置成功，不做任何处理。
    }

    BEFORE_DPFUNC();
    int ret = DP_PosixSetsockopt(KNET_OsFdToDpFd(sockfd), level, optname, optval, optlen);
    AFTER_DPFUNC();
    if (ret < 0) {
        KNET_ERR("DP_PosixSetsockopt ret %d, errno %d, %s, osFd %d, dpFd %d, level %d, optname %d, optlen %u",
            ret, errno, strerror(errno), sockfd, KNET_OsFdToDpFd(sockfd), level, optname, optlen);
        return ret;
    }

    ret = g_origOsApi.setsockopt(sockfd, level, optname, optval, optlen);
    if (ret < 0) {
        KNET_ERR("OS set sock opt failed ret %d, errno %d, %s, osFd %d, dpFd %d, level %d, optname %d, optlen %u",
            ret, errno, strerror(errno), sockfd, KNET_OsFdToDpFd(sockfd), level, optname, optlen);
    }

    return ret;
}


static int FcntlProcessArg(int sockfd, int cmd, long *arg)
{
    /* get操作不带参数arg会给个随机值,这里直接置0 */
    if (cmd == F_GETFL || cmd == F_GETFD) {
        *arg = 0;
    }

    // DP_PosixFcntl函数最后参数为int类型，进行适配
    if (*arg > INT32_MAX || *arg < 0) {
        KNET_ERR("Fcntl arg is larger then INT32_MAX or smaller then 0 which is not support");
        errno = EINVAL;
        return -1;
    }

    return 0;
}

int KNET_DpFcntl(int sockfd, int cmd, ...)
{
    va_list va;
    va_start(va, cmd);
    long arg = va_arg(va, long);
    va_end(va);

    KNET_CHECK_AND_GET_OS_API(g_origOsApi.fcntl, KNET_INVALID_FD);
    if (!g_tcpInited) {
        return g_origOsApi.fcntl(sockfd, cmd, arg);
    }

    if (!KNET_IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d fcntl is not hijacked, cmd %d", sockfd, cmd);
        return g_origOsApi.fcntl(sockfd, cmd, arg);
    }

    int ret = FcntlProcessArg(sockfd, cmd, &arg);
    if (ret < 0) {
        KNET_ERR("Fcntl osFd %d arg is larger then INT32_MAX or smaller then 0 which is not support, cmd %d",
            sockfd, cmd);
        return -1;
    }

    BEFORE_DPFUNC();
    ret = DP_PosixFcntl(KNET_OsFdToDpFd(sockfd), cmd, (int)arg);
    AFTER_DPFUNC();
    if (ret < 0) {
        KNET_ERR("DP_PosixFcntl ret %d, errno %d, %s, osFd %d, dpFd %d, cmd %d",
            ret, errno, strerror(errno), sockfd, KNET_OsFdToDpFd(sockfd), cmd);
        return ret;
    }

    // 内核标志位和DP保持一致
    ret = g_origOsApi.fcntl(sockfd, cmd, arg);
    if (ret < 0) {
        KNET_ERR("OS fcntl ret %d, errno %d, %s, osFd %d, dpFd %d, cmd %d",
            ret, errno, strerror(errno), sockfd, KNET_OsFdToDpFd(sockfd), cmd);
    }

    return ret;
}

int KNET_DpFcntl64(int sockfd, int cmd, ...)
{
    va_list va;
    va_start(va, cmd);
    long arg = va_arg(va, long);
    va_end(va);

    return KNET_DpFcntl(sockfd, cmd, arg);
}

static int HandleEthtoolRequest(int sockfd, unsigned long request, char *arg)
{
    int ret;
    struct ifreq *ifr = (struct ifreq *)arg;
    ret = g_origOsApi.ioctl(sockfd, request, arg);
    if (ret < 0) {
        KNET_ERR("Ioctl ret %d, errno %d, %s, osFd %d, request %ld", ret, errno, strerror(errno), sockfd, request);
        return ret;
    }
    // 打桩实现
    struct rte_eth_link link = {0};
    rte_eth_link_get_nowait(KNET_GetNetDevCtx()->xmitPortId, &link);
    if (ifr && ifr->ifr_data && strcmp(ifr->ifr_name, "knet_tap0") == 0) {
        struct ethtool_cmd *ecmd = (struct ethtool_cmd *)ifr->ifr_data;
        if (ecmd->cmd == ETHTOOL_GSET) {
            ecmd->speed = link.link_speed;
            ecmd->duplex = link.link_duplex;
            ecmd->autoneg = link.link_autoneg;
        }
    }
    return ret;
}

static int DpDefaultIoctl(int sockfd, unsigned long request, char *arg)
{
    int ret;
    BEFORE_DPFUNC();
    ret = DP_PosixIoctl(KNET_OsFdToDpFd(sockfd), request, arg);
    AFTER_DPFUNC();
    if (ret < 0) {
        KNET_ERR("DP_PosixIoctl ret %d, errno %d, %s, osFd %d, dpFd %d, request %lu",
            ret, errno, strerror(errno), sockfd, KNET_OsFdToDpFd(sockfd), request);
    }
    return ret;
}

int KNET_DpIoctl(int sockfd, unsigned long request, ...)
{
    va_list va;
    va_start(va, request);
    char *arg = va_arg(va, char *);
    va_end(va);

    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.ioctl, KNET_INVALID_FD);
        return g_origOsApi.ioctl(sockfd, request, arg);
    }

    if (!KNET_IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d ioctl is not hijacked, request %lu", sockfd, request);
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.ioctl, KNET_INVALID_FD);
        return g_origOsApi.ioctl(sockfd, request, arg);
    }

    switch (request) {
        case SIOCGIFCONF:
        case SIOCGIFHWADDR:
        case SIOCGIFNETMASK:
        case SIOCGIFBRDADDR:
        case SIOCGIFINDEX:
        case SIOCGIFFLAGS:
            return g_origOsApi.ioctl(sockfd, request, arg);
        case SIOCETHTOOL:
            return HandleEthtoolRequest(sockfd, request, arg);
        default:
            return DpDefaultIoctl(sockfd, request, arg);
    }
}