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
#define _GNU_SOURCE
#include <poll.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <malloc.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "dp_posix_socket_api.h"
#include "dp_posix_poll_api.h"
#include "dp_debug_api.h"
#include "knet_log.h"
#include "tcp_fd.h"
#include "knet_osapi.h"
#include "knet_init.h"
#include "knet_signal_tcp.h"
#include "knet_tcp_api_init.h"
#include "tcp_event_inner.h"
#include "tcp_event.h"

static int DpPollHelper(struct pollfd *fds, nfds_t nfds, int timeout)
{
    struct pollfd osPollFds[nfds];
    struct pollfd dpPollFds[nfds];
    int osPollNfds = 0;
    int dpPollNfds = 0;
    int dpPollIdx2FdsIdx[nfds]; // dp轮询到的事件，映射到fds的索引
    int osPollIdx2FdsIdx[nfds]; // os轮询到的事件，映射到fds的索引

    // 区分os轮询和dp轮询的fd
    struct pollfd *curPollFd = NULL;
    for (int i = 0; i < nfds; ++i) {
        if (fds[i].fd < 0) {
            continue;
        }

        if (KNET_GetFdType(fds[i].fd) == KNET_FD_TYPE_SOCKET) { // 默认前提：只有hijack fd才能设置为FD_TYPE_SOCKET
            curPollFd = &dpPollFds[dpPollNfds];
            curPollFd->fd = KNET_OsFdToDpFd(fds[i].fd);
            curPollFd->events = fds[i].events;
            curPollFd->revents = 0;
            dpPollIdx2FdsIdx[dpPollNfds] = i;
            ++dpPollNfds;
            if (KNET_GetEstablishedFdState(fds[i].fd) != KNET_ESTABLISHED_FD) {
                curPollFd = &osPollFds[osPollNfds];
                curPollFd->fd = fds[i].fd;
                curPollFd->events = fds[i].events;
                curPollFd->revents = 0;
                osPollIdx2FdsIdx[osPollNfds] = i;
                ++osPollNfds;
            }
        } else {
            curPollFd = &osPollFds[osPollNfds];
            curPollFd->fd = fds[i].fd;
            curPollFd->events = fds[i].events;
            curPollFd->revents = 0;
            osPollIdx2FdsIdx[osPollNfds] = i;
            ++osPollNfds;
        }
    }

    if (dpPollNfds == 0) { // 性能优化：无hijackFd，直接走os
        return g_origOsApi.poll(fds, nfds, timeout);
    }

    struct SelectFdInfo fdInfo = {0};
    fdInfo.dpPollNfds = dpPollNfds;
    fdInfo.dpPollFds = dpPollFds;

    int pollRet = SelectPollingLoops(osPollFds, osPollNfds, timeout, &fdInfo);
    if (pollRet < 0) {
        return pollRet;
    }

    if (fdInfo.osPollRet > 0) {
        for (int i = 0; i < osPollNfds; ++i) {
            fds[osPollIdx2FdsIdx[i]].revents = osPollFds[i].revents;
        }
    }
    if (fdInfo.dpPollRet > 0) {
        for (int i = 0; i < dpPollNfds; ++i) {
            fds[dpPollIdx2FdsIdx[i]].revents = dpPollFds[i].revents;
        }
    }

    return pollRet;
}

int KNET_DpPoll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    KNET_CHECK_AND_GET_OS_API(g_origOsApi.poll, KNET_INVALID_FD);
    if (!g_tcpInited) {
        return g_origOsApi.poll(fds, nfds, timeout);
    }

    /* 后续nfds作为变长数组的长度，这里必须合法性校验 */
    if (nfds <= 0 || nfds > KNET_POLL_MAX_NUM) {
        KNET_ERR("Invalid events, nfds %u", nfds);
        errno = EINVAL;
        return -1;
    }

    if (fds == NULL) {
        errno = EFAULT;
        KNET_ERR("Poll invalid param, get Null fds");
        return -1;
    }

    return DpPollHelper(fds, nfds, timeout);
}

KNET_STATIC int SigDpPoll(const sigset_t *sigmask, struct pollfd *fds, nfds_t nfds, int64_t timeout)
{
    sigset_t oriMask;
    if (sigmask != NULL) {
        pthread_sigmask(SIG_SETMASK, sigmask, &oriMask);
    }
    int ret = KNET_DpPoll(fds, nfds, timeout);
    if (sigmask != NULL) {
        pthread_sigmask(SIG_SETMASK, &oriMask, NULL);
    }
    return ret;
}

int KNET_DpPPoll(struct pollfd *fds, nfds_t nfds, const struct timespec *timeoutTs, const sigset_t *sigmask)
{
    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.ppoll, KNET_INVALID_FD);
        return g_origOsApi.ppoll(fds, nfds, timeoutTs, sigmask);
    }

    int64_t timeout;
    if (timeoutTs == NULL) {
        timeout = -1;
    } else {
        timeout = timeoutTs->tv_sec * SEC_2_M_SEC + timeoutTs->tv_nsec / M_SEC_2_N_SEC;
        if (timeout < 0) {
            errno = EINVAL;
            return -1;
        }
    }
    
    return SigDpPoll(sigmask, fds, nfds, timeout);
}