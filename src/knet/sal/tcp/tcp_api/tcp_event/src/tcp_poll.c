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
#include "tcp_event.h"

#define DEFAULT_EVENT_NUM 512
static uint32_t g_epollEvents[] = {
    EPOLLIN, EPOLLOUT, EPOLLERR, EPOLLHUP, EPOLLPRI, EPOLLRDHUP, EPOLLRDNORM, EPOLLRDBAND, EPOLLWRNORM, EPOLLWRBAND
    };
static uint32_t g_pollEvents[] = {
    POLLIN, POLLOUT, POLLERR, POLLHUP, POLLPRI, POLLRDHUP, POLLRDNORM, POLLRDBAND, POLLWRNORM, POLLWRBAND
    };
static uint32_t g_eventNUms = sizeof(g_pollEvents) / sizeof(g_pollEvents[0]);

static uint32_t PollEvent2Epoll(short int events)
{
    uint32_t epollEvents = 0;

    for (uint32_t idx = 0; idx < g_eventNUms; idx++) {
        if ((g_pollEvents[idx] & (uint32_t)events) && g_pollEvents[idx] != POLLPRI) {
            epollEvents |= g_epollEvents[idx];
        }
    }
    if ((uint32_t)events & POLLPRI) {
        // epollEvents |= EPOLLPRI; // tcp协议栈不支持
        KNET_DEBUG("EPOLLPRI is not support");
    }

    return epollEvents;
}

static short int EpollEvent2Poll(uint32_t events)
{
    unsigned short int pollEvents = 0;
    for (uint32_t idx = 0; idx < g_eventNUms; idx++) {
        if (g_epollEvents[idx] & (uint32_t)events) {
            pollEvents |= g_pollEvents[idx];
        }
    }

    return (short int)pollEvents;
}

KNET_STATIC int TraversePollEvenet2Epoll(struct pollfd *fds, const nfds_t nfds, int epfd)
{
    struct epoll_event ev = { 0 };
    for (nfds_t i = 0; i < nfds; i++) {
        ev.events = PollEvent2Epoll(fds[i].events);
        ev.data.fd = fds[i].fd;
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, fds[i].fd, &ev) == -1) {
            KNET_ERR("Poll calls epoll_ctl failed, epfd %d, errno %d, %s", epfd, errno, strerror(errno));
            close(epfd);
            return -1;
        }
    }
    return 0;
}

static int PollHelper(struct pollfd *fds, const nfds_t nfds, int timeout)
{
    int epfd = KNET_DpEpollCreate(nfds);
    if (epfd == -1) {
        KNET_ERR("Poll call epoll_create ret fd %d, errno %d, %s", epfd, errno, strerror(errno));
        return -1;
    }

    /* 遍历转换poll为epoll事件 */
    if (TraversePollEvenet2Epoll(fds, nfds, epfd) == -1) {
        /* 异常情况已经打印日志、关闭epfd，返回异常值，直接一路传回异常值 */
        return -1;
    }

    struct epoll_event *events = calloc(nfds, sizeof(struct epoll_event));
    if (events == NULL) {
        KNET_ERR("Malloc events failed");
        close(epfd);
        errno = ENOMEM;
        return -1;
    }

    int numEvents = KNET_DpEpollWait(epfd, events, nfds, timeout);
    if (numEvents < 0) {
        KNET_ERR("Poll call epoll_wait epfd %d, ret %d, errno %d, %s", epfd, numEvents, errno, strerror(errno));
        free(events);
        close(epfd);
        return -1;
    } else if (numEvents == 0 && timeout == -1) {
        KNET_ERR("Poll get no events, nfds %d, timeout %d", nfds, timeout);
    }

    for (nfds_t i = 0; i < nfds; ++i) {
        fds[i].revents = 0; // 与内核行为一致，用户无需置revents = 0，没有event时内核会将revents置0
        for (int j = 0; j < numEvents; ++j) {
            if (fds[i].fd == events[j].data.fd) {
                fds[i].revents = EpollEvent2Poll(events[j].events);
                break;
            }
        }
    }
    KNET_DEBUG("Poll ret %d, nfds %d, timeout %d", numEvents, nfds, timeout);
    free(events);
    close(epfd);
    return numEvents;
}

int KNET_DpPoll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    /* 后续nfds作为变长数组的长度，这里必须合法性校验 */
    if (nfds <= 0 || nfds > KNET_EPOLL_MAX_NUM) {
        KNET_ERR("Invalid events, nfds %u", nfds);
        errno = EINVAL;
        return -1;
    }

    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.poll, KNET_INVALID_FD);
        return g_origOsApi.poll(fds, nfds, timeout);
    }

    if (fds == NULL) {
        errno = EFAULT;
        KNET_ERR("Poll invalid param, get Null fds");
        return -1;
    }

    return PollHelper(fds, nfds, timeout);
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