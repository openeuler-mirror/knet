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
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <malloc.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/time.h>

#include "dp_posix_socket_api.h"
#include "dp_posix_epoll_api.h"
#include "dp_debug_api.h"
#include "knet_log.h"
#include "tcp_fd.h"
#include "knet_osapi.h"
#include "knet_config.h"
#include "knet_init.h"
#include "knet_signal_tcp.h"
#include "knet_tcp_api_init.h"
#include "knet_cothread_inner.h"
#include "knet_thread.h"
#include "tcp_event.h"

#define DEFAULT_EVENT_NUM 512

static void EpollCallback(uint8_t *data)
{
    struct KNET_EpollNotifyData *notifyData = (struct KNET_EpollNotifyData *)data;
    int eventFd = notifyData->eventFd;
    int ret;

    /* 避免一直调用浪费CPU */
    ret = KNET_HalAtomicTestSet64(&notifyData->active);
    if (ret == 0) {
        return;
    }

    ret = eventfd_write(eventFd, 1);
    if (ret < 0) {
        KNET_ERR("OS eventFd %d write ret %d, errno %d, %s", eventFd, ret, errno, strerror(errno));
        return;
    }
}

static inline int ResetDpCallbackData(struct KNET_EpollNotifyData *notifyData)
{
    uint64_t value = 0;
    int oriErrno = errno;

    (void)eventfd_read(notifyData->eventFd, &value);
    errno = oriErrno;

    KNET_HalAtomicSet64(&notifyData->active, 0);
    return 0;
}

static inline int DPEpollCreateNotify(int osFd, int eventFd)
{
    DP_EpollNotify_t *notify = NULL;
    struct KNET_EpollNotifyData *data = NULL;

    notify = &KNET_GetFdPrivateData(osFd)->epollData.notify;
    data = &KNET_GetFdPrivateData(osFd)->epollData.data;

    data->eventFd = eventFd;
    KNET_HalAtomicSet64(&data->active, 0);

    notify->fn = EpollCallback;
    notify->data = (uint8_t *)data;

    KNET_DEBUG("Epoll create notify, osFd %d, eventFd %d", osFd, eventFd);
    return DP_EpollCreateNotify(1, notify);
}

static inline int RegEpollDepFuncs(void)
{
    // 初始化依赖的接口

    KNET_CHECK_AND_GET_OS_API(g_origOsApi.epoll_create, -1);
    KNET_CHECK_AND_GET_OS_API(g_origOsApi.epoll_ctl, -1);
    KNET_CHECK_AND_GET_OS_API(g_origOsApi.epoll_wait, -1);

    return 0;
}

static inline uint64_t GenPrivateEpollData(void)
{
    static bool init = false;
    static uint64_t epollPrivateData = 0;
    if (KNET_UNLIKELY(!init)) {
        epollPrivateData = KNET_GetCfg(CONF_TCP_EPOLL_DATA)->uint64Value;
        init = true;
    }

    return epollPrivateData;
}

static int EpollTrafficResourcesInit(void)
{
    int ret = KNET_TrafficResourcesInit();
    if (ret != 0) {
        errno = ENAVAIL;
        KNET_ERR("Traffic resources init failed, errno %d, %s", errno, strerror(errno));
        return -1;
    }
    return 0;
}

static int EpollCreatePrepare(void)
{
    /* 在协议栈使用epoll_create前，完成对打流相关资源的初始化 */
    int ret = 0;
    ret = EpollTrafficResourcesInit();
    if (ret != 0) {
        return -1;
    }

    ret = RegEpollDepFuncs();
    if (ret != 0) {
        KNET_ERR("Reg epoll funcs failed");
        return -1;
    }
    return 0;
}

void SetfdTid(int osFd)
{
    // 共线程模式下，worker线程记录epfd的tid，防止跨线程操作
    if (KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue == 1) {
        uint64_t tid = KNET_ThreadId();
        KNET_SetEpollFdTid(osFd, tid);
    }
}

int KNET_DpEpollCreate(int size)
{
    struct epoll_event ev = {0};
    if (EpollCreatePrepare() != 0) {
        return -1;
    }

    /* 在主线程等待退出的时候,走内核的创建 */
    if (!g_tcpInited || KNET_IsCothreadGoKernel()) {
        return g_origOsApi.epoll_create(size);
    }
    /* 信号退出流程中直接退出 */
    if (KNET_UNLIKELY(KNET_DpSignalGetWaitExit())) {
        errno = EPERM;
        KNET_WARN("Function epoll_create was not allowed to be called in signal exiting process, errno %d, %s",
            errno, strerror(errno));
        return -1;
    }

    int osFd = g_origOsApi.epoll_create(size);
    if (!KNET_IsFdValid(osFd)) {
        KNET_ERR("OS socket ret %d, errno %d, %s", osFd, errno, strerror(errno));
        goto kernel_epoll_create_err;
    }

    int eventFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (eventFd == -1) {
        KNET_ERR("Failed alloc eventfd, errno %d, %s", errno, strerror(errno));
        goto eventfd_create_err;
    }

    ev.events |= EPOLLIN;
    ev.data.u64 = GenPrivateEpollData();
    int ret = g_origOsApi.epoll_ctl(osFd, EPOLL_CTL_ADD, eventFd, &ev);
    if (ret < 0) {
        KNET_ERR("Epoll ctl eventFd %d ret %d, errno %d, %s", eventFd, ret, errno, strerror(errno));
        goto epoll_ctl_add_err;
    }

    int dpFd = DPEpollCreateNotify(osFd, eventFd);
    if (dpFd < 0) {
        KNET_ERR("DP_EpollCreateNotify ret %d, osfd %d, eventFd %d, errno %d, %s",
            dpFd, osFd, eventFd, errno, strerror(errno));
        goto dp_epoll_create_err;
    }

    KNET_SetFdStateAndType(KNET_FD_STATE_HIJACK, osFd, dpFd, KNET_FD_TYPE_EPOLL);
    SetfdTid(osFd);
    KNET_DEBUG("Epoll create success, osFd %d, dpFd %d, eventFd %d", osFd, dpFd, eventFd);

    return osFd;

dp_epoll_create_err:
epoll_ctl_add_err:
    g_origOsApi.close(eventFd);
eventfd_create_err:
    g_origOsApi.close(osFd);
kernel_epoll_create_err:
    return KNET_INVALID_FD;
}

int KNET_DpEpollCreate1(int flags)
{
    /* 在协议栈使用epoll_create1前，完成对打流相关资源的初始化 */
    int ret = EpollTrafficResourcesInit();
    if (ret != 0) {
        return -1;
    }
    if (!g_tcpInited || KNET_IsCothreadGoKernel()) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.epoll_create1, KNET_INVALID_FD);
        return g_origOsApi.epoll_create1(flags);
    }

    if ((unsigned int)flags & ~EPOLL_CLOEXEC) {
        KNET_ERR("Epoll create1 flags invalid");
        errno = EINVAL;
        return -1;
    }
    // 对于0和EPOLL_CLOEXEC标志位均通过knet epoll_create()实现
    return KNET_DpEpollCreate(1); // size参数没有使用仅做预留，只要大于0即可
}

/**
 * @brief 检查共线程场景下，worker线程的epfd是否跨线程操作
 *
 * @param epfd
 * @return int 0：未跨线程操作；-1：跨线程操作，并设置errno EINVAL
 */
int IsEpollFdCrossThreadInCo(int epfd)
{
    uint64_t tid = KNET_ThreadId();
    uint64_t epollFdTid = KNET_GetEpollFdTid(epfd);
    if (tid != epollFdTid) {
        KNET_ERR("Knet fd %d, dpfd %d epoll_ctl invalid thread, fdTid %ld, curTid %ld",
            epfd, KNET_OsFdToDpFd(epfd), epollFdTid, tid);
        errno = EINVAL; // 跨线程操作设置errno EINVAL
        return -1;
    }
    return 0;
}

int EpollCtlNotEstablished(int epfd, int op, int sockfd, struct epoll_event *event, bool isEpfdCreadtedByCoWorker)
{
    // 只要是未建链的fd就会走内核的epoll_ctl，建议：建链的fd与流量fd分别使用不同的epfd去监听；
    if (KNET_IsFdValid(sockfd) && KNET_GetEstablishedFdState(sockfd) != KNET_ESTABLISHED_FD) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.epoll_ctl, KNET_INVALID_FD);
        int ret = g_origOsApi.epoll_ctl(epfd, op, sockfd, event);
        if (ret < 0) {
            KNET_ERR("Os epoll ctl failed ret %d, osEpfd %d osFd %d, errno %d, %s",
                ret, epfd, sockfd, errno, strerror(errno));
            return ret;
        }
        // 如果成功操作，需要同步设置共线程worker线程创建的epfd需要设置epFdHasOsFd属性
        if (isEpfdCreadtedByCoWorker) {
            KNET_EpHasOsFdSet(epfd);
            KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Knet epfd %d has osfd %d in cothread, which is not established",
                epfd, sockfd);
        }
        KNET_DEBUG("Os epoll ctl success epfd %d, fd %d, op %d, event %s, events %x, data %x", epfd, sockfd, op,
            (event != NULL) ? "not NULL" : "NULL", (event != NULL) ? event->events : 0,
            (event != NULL) ? event->data.u64 : 0);
    }
    return 0;
}

int CheckEpollEventData(int epfd, int op, int sockfd, struct epoll_event *event)
{
    /* 内核的epoll ctl事件data也需要保证不能碰撞到private data，否则会丢osFd的event */
    if (event != NULL && event->data.u64 == GenPrivateEpollData()) {
        KNET_ERR("Epoll ctl got unexpected event data, op %d osEpfd %d, dpEpfd %d, osFd %d",
            op, epfd, KNET_OsFdToDpFd(epfd), sockfd);
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int KNET_DpEpollCtl(int epfd, int op, int sockfd, struct epoll_event *event)
{
    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.epoll_ctl, KNET_INVALID_FD);
        return g_origOsApi.epoll_ctl(epfd, op, sockfd, event);
    }

    if (CheckEpollEventData(epfd, op, sockfd, event) < 0) {
        return -1;
    }

    // 对于共线程下worker线程创建的epfd，需要判断是否跨线程操作
    bool isEpfdCreadtedByCoWorker = (KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue == 1) && KNET_IsFdHijack(epfd);
    if (isEpfdCreadtedByCoWorker && IsEpollFdCrossThreadInCo(epfd) < 0) {
        return -1;
    }

    int ret = 0;
    if (!KNET_IsFdHijack(epfd) || !KNET_IsFdHijack(sockfd)) { // 内交换fd和未被劫持的fd走os
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "osEpfd %d osFd %d epoll_ctl is not hijacked, op %d, "
            "events %s, events 0x%x, data 0x%x", epfd, sockfd, op, (event != NULL) ? "not NULL" : "NULL",
            (event != NULL) ? event->events : 0, (event != NULL) ? event->data.u64 : 0);
        ret = g_origOsApi.epoll_ctl(epfd, op, sockfd, event);
        // 成功操作epctl后，如果是共线程worker线程创建的epfd需要设置epfd的epFdHasOsFd属性
        if ((ret == 0) && isEpfdCreadtedByCoWorker) {
            KNET_EpHasOsFdSet(epfd);
            KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Knet epfd %d has osfd %d in cothread", epfd, sockfd);
        }
        return ret;
    }
    if (EpollCtlNotEstablished(epfd, op, sockfd, event, isEpfdCreadtedByCoWorker) < 0) {
        return -1;
    }

    BEFORE_DPFUNC();
    ret = DP_PosixEpollCtl(KNET_OsFdToDpFd(epfd), op, KNET_OsFdToDpFd(sockfd), event);
    AFTER_DPFUNC();
    if (ret < 0) {
        KNET_ERR("DP_PosixEpollCtl ret %d, errno %d, %s, op %d, osEpfd %d, dpEpfd %d, osFd %d, dpFd %d, events %s,"
            "events %x, data %x", ret, errno, strerror(errno), op, epfd, KNET_OsFdToDpFd(epfd), sockfd,
            KNET_OsFdToDpFd(sockfd), (event != NULL) ? "not NULL" : "NULL", (event != NULL) ? event->events : 0,
            (event != NULL) ? event->data.u64 : 0);
        return ret;
    }

    KNET_DEBUG("Epoll ctl success, epfd %d, fd %d, epDpFd %d, dpFd %d, op %d, event %s, events %x, data %x",
        epfd, sockfd, KNET_OsFdToDpFd(epfd), KNET_OsFdToDpFd(sockfd), op, (event != NULL) ? "not NULL" : "NULL",
        (event != NULL) ? event->events : 0, (event != NULL) ? event->data.u64 : 0);

    return ret;
}

struct EpollCtlBlock {
    int maxEvents;
    int epfd;
    int *kernelEventCnt;
    int *dpEventCnt;
    struct epoll_event *kernelEvents;
    struct epoll_event *dpEvents;
};

bool IsEpfdNeedOsEpollWait(int epfd)
{
    // 开启共线程worker线程创建的epfd有内核fd，或者未开启共线程，进行内核epoll_wait
    return (KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue == 1 && KNET_EpfdHasOsfdGet(epfd)) ||
        KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue == 0;
}

static int PollEpollEventImmediately(struct EpollCtlBlock *epollCb)
{
    int dpEventCnt = 0, maxEvents = epollCb->maxEvents, epfd = epollCb->epfd;

    int kernelEventCnt = 0;
    if (IsEpfdNeedOsEpollWait(epfd)) {
        kernelEventCnt = g_origOsApi.epoll_wait(epfd, epollCb->kernelEvents, maxEvents, 0);
        if (kernelEventCnt < 0) {
            KNET_ERR("Epoll wait failed ret %d, errno %d, %s, epfd %d, maxevents %d",
                kernelEventCnt, errno, strerror(errno), epfd, maxEvents);
            return kernelEventCnt;
        }
    }

    /*
     * 如果这里有eventfd的事件会占掉一个kernelEvents的位置
     * 但eventfd的事件只作通知，不返回给用户，所以这里需要算出真正os epoll事件的个数
     */
    int realKernelEventCnt = 0;
    for (int i = 0; i < kernelEventCnt; ++i) {
        if (epollCb->kernelEvents[i].data.u64 == GenPrivateEpollData()) {
            continue;
        }
        epollCb->dpEvents[realKernelEventCnt].data = epollCb->kernelEvents[i].data;
        epollCb->dpEvents[realKernelEventCnt].events = epollCb->kernelEvents[i].events;

        ++realKernelEventCnt;
    }
    *epollCb->kernelEventCnt = realKernelEventCnt;
    KNET_DEBUG("PollEpollEventImmediately: epfd %d, dpfd %d, maxEvents %d, kernelEventCnt %d, realKernelEventCnt %d",
        epfd, KNET_OsFdToDpFd(epfd), maxEvents, kernelEventCnt, realKernelEventCnt);

    *epollCb->dpEventCnt = 0;
    if (KNET_UNLIKELY(maxEvents == realKernelEventCnt)) {
        return 0;
    }
    BEFORE_DPFUNC();
    dpEventCnt = DP_PosixEpollWait(KNET_OsFdToDpFd(epfd), &epollCb->dpEvents[realKernelEventCnt],
        maxEvents - realKernelEventCnt, 0);
    AFTER_DPFUNC();
    if (dpEventCnt < 0) {
        KNET_ERR("DP_PosixEpollWait failed ret %d, errno %d, %s, epfd %d, dpEpfd %d, maxevents %d",
            dpEventCnt, errno, strerror(errno), epfd, KNET_OsFdToDpFd(epfd), maxEvents - realKernelEventCnt);
        /* 要返回正常，因为已经从os epoll成功获取了数据，如果返回失败会导致事件丢失 */
        return 0;
    }
    *epollCb->dpEventCnt = dpEventCnt;

    KNET_DEBUG("PollEpollEventImmediately: epfd %d, dpfd %d, maxEvents %d, dpEventCnt %d",
        epfd, KNET_OsFdToDpFd(epfd), maxEvents - realKernelEventCnt, dpEventCnt);

    return 0;
}

static int EpollWaitHelperBlock(struct EpollCtlBlock* epollCb, int timeout)
{
    int epfd = epollCb->epfd;
    int maxevents = epollCb->maxEvents;

    /* no events, then call blocked epoll_wait */
    KNET_DEBUG("Epoll fd %d os epoll wait start, maxevents %d timeout %d", epfd, maxevents, timeout);
    if (IsEpfdNeedOsEpollWait(epfd)) {
        *epollCb->kernelEventCnt = g_origOsApi.epoll_wait(epfd, epollCb->kernelEvents, maxevents, timeout);
        if (*epollCb->kernelEventCnt < 0) {
            KNET_ERR("Epoll fd %d epoll_wait failed ret %d, maxevents %d, timeout %d, errno %d, %s",
                epfd, *epollCb->kernelEventCnt, maxevents, timeout, errno, strerror(errno));
            return *epollCb->kernelEventCnt;
        }
    }

    int realKernelEventCnt = 0;
    for (int i = 0; i < *epollCb->kernelEventCnt; ++i) {
        if (epollCb->kernelEvents[i].data.u64 == GenPrivateEpollData()) {
            continue;
        }
        epollCb->dpEvents[realKernelEventCnt].data = epollCb->kernelEvents[i].data;
        epollCb->dpEvents[realKernelEventCnt].events = epollCb->kernelEvents[i].events;
 
        ++realKernelEventCnt;
    }
    KNET_DEBUG("EpollBlock: epfd %d dpEpfd %d maxevents %d timeout %d kernelEventCnt %d, realKernelEventCnt %d",
        epfd, KNET_OsFdToDpFd(epfd), maxevents, timeout, *epollCb->kernelEventCnt, realKernelEventCnt);
    *epollCb->kernelEventCnt = realKernelEventCnt;

    *epollCb->dpEventCnt = 0;
    int maxEvents = epollCb->maxEvents;
    if (KNET_UNLIKELY(maxEvents == realKernelEventCnt)) {
        return 0;
    }
    KNET_DEBUG("Epoll fd %d dpEpfd %d DP epoll wait start", epfd, KNET_OsFdToDpFd(epfd));
    BEFORE_DPFUNC();
    *epollCb->dpEventCnt = DP_PosixEpollWait(KNET_OsFdToDpFd(epfd), &epollCb->dpEvents[realKernelEventCnt],
        maxevents - *epollCb->kernelEventCnt, 0);
    AFTER_DPFUNC();
    if (*epollCb->dpEventCnt < 0) {
        KNET_ERR("DP_PosixEpollWait failed ret %d, errno %d, %s, epfd %d, dpEpfd %d, "
            "dpMaxevents %d, maxevents %d, kernelEventCnt %d",
            *epollCb->dpEventCnt, errno, strerror(errno), epfd, KNET_OsFdToDpFd(epfd),
            maxevents - *epollCb->kernelEventCnt, maxevents, *epollCb->kernelEventCnt);
        return *epollCb->dpEventCnt;
    }

    KNET_DEBUG("EpollWaitHelperBlock: epfd %d, dpfd %d, maxEvents %d, dpEventCnt %d",
        epfd, KNET_OsFdToDpFd(epfd), maxevents - *epollCb->kernelEventCnt, *epollCb->dpEventCnt);

    return 0;
}

static int EpollWaitHelper(int epfd, struct epoll_event *events, const int maxevents, int timeout)
{
    int ret = -1;
    struct epoll_event defKernelEvent[DEFAULT_EVENT_NUM];
    struct epoll_event *kernelEvents = defKernelEvent;

    if (maxevents > DEFAULT_EVENT_NUM) {
        kernelEvents = malloc(sizeof(struct epoll_event) * maxevents);
        if (KNET_UNLIKELY(kernelEvents == NULL)) {
            KNET_ERR("Malloc kernelEvents failed, epfd %d, dpEpfd %d", epfd, KNET_OsFdToDpFd(epfd));
            errno = ENOMEM;
            return -1;
        }
    }
    int kernelEventCnt = 0, dpEventCnt = 0;

    struct EpollCtlBlock epollCb = {.epfd = epfd, .maxEvents = maxevents, .kernelEventCnt = &kernelEventCnt,
        .dpEventCnt = &dpEventCnt, .kernelEvents = kernelEvents, .dpEvents = events};

    ret = PollEpollEventImmediately(&epollCb);
    if (ret < 0) {
        goto release;
    }

    if (dpEventCnt == 0 && kernelEventCnt == 0 && timeout != 0) {
        ret = EpollWaitHelperBlock(&epollCb, timeout);
        if (ret < 0) {
            goto release;
        }
    }

    // epoll_wait没有事件产生也会调用，是不是可以加条件有一个事件产生了才会执行reset
    // 如果开了共线程，协议栈没有唤醒流程，无需reset；不开共线程；都需要reset
    if (KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue == 0) {
        ResetDpCallbackData(&KNET_GetFdPrivateData(epfd)->epollData.data);
    }

release:
    if (maxevents > DEFAULT_EVENT_NUM) {
        free(kernelEvents);
    }

    return kernelEventCnt + dpEventCnt;
}

int KNET_DpEpollWait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.epoll_wait, KNET_INVALID_FD);
        return g_origOsApi.epoll_wait(epfd, events, maxevents, timeout);
    }

    if (!KNET_IsFdHijack(epfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Epoll fd %d epoll_wait is not hijacked", epfd);
        return g_origOsApi.epoll_wait(epfd, events, maxevents, timeout);
    }

    /* 后续maxevents作为变长数组的长度，这里必须合法性校验 */
    if (events == NULL || maxevents <= 0 || maxevents > KNET_EPOLL_MAX_NUM) {
        KNET_ERR("Epoll fd %d epoll_wait invalid events, maxevents %d", epfd, maxevents);
        errno = EINVAL;
        return -1;
    }

    return EpollWaitHelper(epfd, events, maxevents, timeout);
}

KNET_STATIC int SigDpEpollWait(int epfd, struct epoll_event *events, int maxevents, int timeout,
                                    const sigset_t *sigmask)
{
    sigset_t oriMask = {0};
    if (sigmask != NULL) {
        pthread_sigmask(SIG_SETMASK, sigmask, &oriMask);
    }
    int ret = KNET_DpEpollWait(epfd, events, maxevents, timeout);
    if (sigmask != NULL) {
        pthread_sigmask(SIG_SETMASK, &oriMask, NULL);
    }
    return ret;
}

int KNET_DpEpollPwait(int epfd, struct epoll_event *events, int maxevents, int timeout, const sigset_t *sigmask)
{
    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.epoll_pwait, KNET_INVALID_FD);
        return g_origOsApi.epoll_pwait(epfd, events, maxevents, timeout, sigmask);
    }

    return SigDpEpollWait(epfd, events, maxevents, timeout, sigmask);
}