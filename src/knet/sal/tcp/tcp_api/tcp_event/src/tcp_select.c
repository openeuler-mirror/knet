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
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
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

#define POLL_INTERVAL 10
#define FAST_POLL_TIMES 5

struct SelectFdInfo {
    fd_set *readfds;
    fd_set *writefds;
    fd_set *exceptfds;
    struct pollfd *dpPollFds;
    int selectNfds;
    int dpPollNfds;
    int osPollRet; // os poll返回的event个数
    int dpPollRet; // dp轮询得到的event个数
};

static int SelectPollingOnce(struct pollfd *osPollFds, nfds_t osPollNfds, struct SelectFdInfo *fdInfo)
{
    if (osPollNfds > 0) { // 性能优化：osPollNfds为0时，不os poll
        fdInfo->osPollRet = g_origOsApi.poll(osPollFds, osPollNfds, 0);
        if (fdInfo->osPollRet < 0) {
            KNET_ERR("OS poll failed, ret %d, errno %d, error %s", fdInfo->osPollRet, errno, strerror(errno));
            return fdInfo->osPollRet;
        }
    }
    BEFORE_DPFUNC();
    fdInfo->dpPollRet = DP_PosixPoll(fdInfo->dpPollFds, fdInfo->dpPollNfds, 0);
    AFTER_DPFUNC();
    if (fdInfo->dpPollRet < 0) {
        KNET_DEBUG("Dp poll failed, ret %d, errno %d, error %s", fdInfo->dpPollRet, errno, strerror(errno));
        if (fdInfo->osPollRet > 0) {
            return fdInfo->osPollRet;
        }
        return fdInfo->dpPollRet;
    }

    return fdInfo->osPollRet + fdInfo->dpPollRet;
}

static inline bool KNET_CounterTimerIsTimeout(int64_t timeBeginMs, int64_t timeoutMs)
{
    struct timeval timeNow = { 0 };
    (void)gettimeofday(&timeNow, NULL);
    int64_t timeNowMs = (int64_t)timeNow.tv_sec * 1000 + timeNow.tv_usec / 1000; // 1000为时间转换倍数。无需考虑溢出，溢出需要2亿年
    if (timeNowMs > timeBeginMs + timeoutMs) {
        return true;
    }

    return false;
}

static int SelectPollingLoops(
    struct pollfd *osPollFds, nfds_t osPollNfds, int64_t timeoutMs, struct SelectFdInfo *fdInfo)
{
    /* 进来先获取一次，如果有事件直接返回，可以减少一次获取时间的开销 */
    int pollRet = SelectPollingOnce(osPollFds, osPollNfds, fdInfo);
    if (pollRet > 0) {
        return pollRet;
    } else if (pollRet < 0) {
        KNET_ERR("select polling failed, ret %d, osPollNfds %d, timeoutMs %lld", pollRet, osPollNfds, timeoutMs);
        return pollRet;
    }

    int pollTimes = 0;
    struct timeval timeBegin = {0};
    (void)gettimeofday(&timeBegin, NULL);
    int64_t timeBeginMs = (int64_t)timeBegin.tv_sec * 1000 + timeBegin.tv_usec / 1000;  // 1000为时间转换倍数。无需考虑溢出，溢出需要2亿年
    do {
        /* 主线程等待其他线程退出 */
        if (KNET_UNLIKELY(KNET_DpSignalGetWaitExit())) {
            break;
        }
        pollRet += SelectPollingOnce(osPollFds, osPollNfds, fdInfo);
        if (pollRet > 0) {
            return pollRet;
        } else if (pollRet < 0) {
            KNET_ERR("Select polling failed, ret %d, osPollNfds %d, timeoutMs %lld", pollRet, osPollNfds, timeoutMs);
            return pollRet;
        }
        if (pollTimes <= FAST_POLL_TIMES) {
            pollTimes++;
            continue;
        }
        usleep(POLL_INTERVAL);
    } while (timeoutMs < 0 || !KNET_CounterTimerIsTimeout(timeBeginMs, timeoutMs));

    return pollRet;
}
static void CheckEvent(uint8_t *events, bool checkRead, bool checkWrite, bool checkExcept, int fd)
{
    if (checkRead) {
        *events |= POLLIN;
        KNET_DEBUG("Fd %d set poll read", fd);
    }
    if (checkWrite) {
        *events |= POLLOUT;
        KNET_DEBUG("Fd %d set poll write", fd);
    }
    if (checkExcept) {
        *events |= POLLERR;
        KNET_DEBUG("Fd %d set poll err", fd);
    }
}

static int SelectPollFdsGet(struct pollfd *osPollFds, int *dpToOsFds, struct SelectFdInfo *fdInfo)
{
    int osPollNfds = 0;
    int dpPollNfds = 0;

    for (int fd = 0; fd < fdInfo->selectNfds; ++fd) {
        bool checkRead = (fdInfo->readfds != NULL) && FD_ISSET(fd, fdInfo->readfds);
        bool checkWrite = (fdInfo->writefds != NULL) && FD_ISSET(fd, fdInfo->writefds);
        bool checkExcept = (fdInfo->exceptfds != NULL) && FD_ISSET(fd, fdInfo->exceptfds);
        if (!checkRead && !checkWrite && !checkExcept) {
            continue;
        }

        uint8_t events = 0;
        CheckEvent(&events, checkRead, checkWrite, checkExcept, fd);

        struct pollfd *currentPollFdDp = NULL;
        struct pollfd *currentPollFdOs = NULL;
 
        if (KNET_GetFdType(fd) == KNET_FD_TYPE_SOCKET) {
            dpToOsFds[dpPollNfds] = fd;
            currentPollFdDp = &fdInfo->dpPollFds[dpPollNfds++];
            currentPollFdDp->fd = KNET_OsFdToDpFd(fd);
            currentPollFdDp->events = (short)events;
            currentPollFdDp->revents = 0;
            if (KNET_GetEstablishedFdState(fd) != KNET_ESTABLISHED_FD) {
                currentPollFdOs = &osPollFds[osPollNfds++];
                currentPollFdOs->fd = fd;
                currentPollFdOs->events = (short)events;
                currentPollFdOs->revents = 0;
            }
        } else {
            currentPollFdOs = &osPollFds[osPollNfds++];
            currentPollFdOs->fd = fd;
            currentPollFdOs->events = (short)events;
            currentPollFdOs->revents = 0;
        }
    }

    fdInfo->dpPollNfds = dpPollNfds;

    return osPollNfds;
}

static void OsFdSet(struct pollfd *osPollFds, fd_set *fds, unsigned short event, int idx)
{
    if (fds != NULL && (osPollFds[idx].revents & event) != 0) {
        FD_SET(osPollFds[idx].fd, fds);
    }
}

static void SelectOsPollResultGet(
    struct pollfd *osPollFds, int osPollNfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds)
{
    for (int i = 0; i < osPollNfds; i++) {
        OsFdSet(osPollFds, readfds, POLLIN, i);
        OsFdSet(osPollFds, writefds, POLLOUT, i);
        OsFdSet(osPollFds, exceptfds, POLLERR, i);
    }
}

static void DpFdSet(int *dpToOsFds, fd_set *fds, uint32_t event, int idx, unsigned short revents)
{
    if (fds != NULL && (revents & event) != 0) {
        FD_SET(dpToOsFds[idx], fds);
    }
}

static void SelectDpPollResultGet(
    struct SelectFdInfo *fdInfo, int *dpToOsFds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds)
{
    for (int i = 0; i < fdInfo->dpPollNfds; i++) {
        unsigned short revents = (unsigned short)fdInfo->dpPollFds[i].revents;
        DpFdSet(dpToOsFds, readfds, POLLIN, i, revents);
        DpFdSet(dpToOsFds, writefds, POLLOUT, i, revents);
        DpFdSet(dpToOsFds, exceptfds, POLLERR, i, revents);
    }
}

static void SelectFdInfoInit(struct SelectFdInfo *fdInfo, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
    struct pollfd *dpPollFds, int nfds)
{
    fdInfo->readfds = readfds;
    fdInfo->writefds = writefds;
    fdInfo->exceptfds = exceptfds;
    fdInfo->dpPollFds = dpPollFds;
    fdInfo->selectNfds = nfds;
}

static void ZeroFds(fd_set *readfds, fd_set *writefds, fd_set *exceptfds)
{
    if (readfds != NULL) {
        FD_ZERO(readfds);
    }
    if (writefds != NULL) {
        FD_ZERO(writefds);
    }
    if (exceptfds != NULL) {
        FD_ZERO(exceptfds);
    }
}

int KNET_DpSelect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.select, KNET_INVALID_FD);
        return g_origOsApi.select(nfds, readfds, writefds, exceptfds, timeout);
    }

    /* timeoutMs为-1表示select永久阻塞等待 */
    int64_t timeoutMs = (timeout == NULL) ? -1 : timeout->tv_sec * 1000 + timeout->tv_usec / 1000;
    if (nfds < 0 || nfds > FD_SETSIZE || (timeout != NULL && timeoutMs < 0)) {
        KNET_ERR("Select failed, invalid param. nfds %d, timeoutMs %lld", nfds, timeoutMs);
        errno = EINVAL;
        return -1;
    }

    struct pollfd osPollFds[nfds];
    struct pollfd dpPollFds[nfds];
    int dpToOsFds[nfds];
    struct SelectFdInfo fdInfo = {0};
    SelectFdInfoInit(&fdInfo, readfds, writefds, exceptfds, dpPollFds, nfds);

    int osPollNfds = SelectPollFdsGet(osPollFds, dpToOsFds, &fdInfo);
    if (fdInfo.dpPollNfds == 0) {  // 性能优化：无hijackFd，直接走os
        return g_origOsApi.select(nfds, readfds, writefds, exceptfds, timeout);
    }

    ZeroFds(readfds, writefds, exceptfds);

    int pollRet = SelectPollingLoops(osPollFds, osPollNfds, timeoutMs, &fdInfo);
    if (pollRet <= 0) {
        return pollRet;
    }

    /* 性能优化：仅有os poll结果时才去轮询赋值 */
    if (fdInfo.osPollRet > 0) {
        SelectOsPollResultGet(osPollFds, osPollNfds, readfds, writefds, exceptfds);
    }
    if (fdInfo.dpPollRet > 0) {
        SelectDpPollResultGet(&fdInfo, dpToOsFds, readfds, writefds, exceptfds);
    }

    return pollRet;
}

int KNET_DpPSelect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timespec *timeout,
    const sigset_t *sigmask)
{
    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.pselect, KNET_INVALID_FD);
        return g_origOsApi.pselect(nfds, readfds, writefds, exceptfds, timeout, sigmask);
    }

    sigset_t oriMask = {0};
    struct timeval *t = NULL;
    struct timeval selectTime = {0};
    if (timeout != NULL) {
        selectTime.tv_sec = timeout->tv_sec;
        selectTime.tv_usec = timeout->tv_nsec / SEC_2_M_SEC;
        t = &selectTime;
    }
    if (sigmask != NULL) {
        pthread_sigmask(SIG_SETMASK, sigmask, &oriMask);
    }
    int ret = KNET_DpSelect(nfds, readfds, writefds, exceptfds, t);
    if (sigmask != NULL) {
        pthread_sigmask(SIG_SETMASK, &oriMask, NULL);
    }
    return ret;
}