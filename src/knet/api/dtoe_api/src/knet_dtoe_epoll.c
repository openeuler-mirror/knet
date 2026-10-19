/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <unistd.h>

#include "knet_log.h"
#include "knet_dtoe_fd.h"
#include "knet_dtoe_epoll.h"

#define SET_ERRNO_RET(e)    \
    do {                        \
        errno = e;              \
        return -1;              \
    } while (0)                 \

#define KNET_SET_ERRNO(err) errno = (err);

#define EPOLL_ALLOWED_EVENTS (EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLRDHUP)

#define KNET_EPOLL_POLL_INTERVAL_MS 1 // POLL模式recv channel轮询降级等待的轮询间隔（毫秒）

static inline void KNET_EpollLock(struct KnetEpoll* ep)
{
    KNET_SpinlockLock(&ep->lock);
}

static inline void KNET_EpollUnlock(struct KnetEpoll* ep)
{
    KNET_SpinlockUnlock(&ep->lock);
}

void KNET_SetEpfd(int epfd)
{
    KNET_FdTypeSet(epfd, KNET_FD_TYPE_EPOLL);
    struct KnetEpoll *ep = KnetGetEpollFd(epfd);
    TAILQ_INIT(&ep->readyList);
    TAILQ_INIT(&ep->idleList);
    ep->lock.value = KNET_SPIN_UNLOCKED_VALUE; // fd号复用时重置锁状态，避免残留锁标志
    ep->sendCh = NULL;                         // 清除上一生命周期残留的channel指针，避免悬挂引用
    ep->recvCh = NULL;
}

static void RemoveEpList(struct KnetEpollItem* item)
{
    struct KnetEpoll* ep = item->ep;

    KNET_EpollLock(ep);

    if (item->ready != 0) {
        TAILQ_REMOVE(&ep->readyList, item, epItem);
    } else {
        TAILQ_REMOVE(&ep->idleList, item, epItem);
    }

    KNET_EpollUnlock(ep);
}

static void KNET_ResetEpList(struct KnetEpoll *ep, struct KnetEpListHead *list)
{
    while (!TAILQ_EMPTY(list)) {
        KNET_EpollLock(ep);
        struct KnetEpollItem *item = TAILQ_FIRST(list);
        KNET_EpollUnlock(ep);

        if (KNET_IsOsFdTcp(item->fd)) {
            struct KnetSock *sk = &KnetGetTcpFd(item->fd)->sk;
            KNET_SockLock(sk);

            // 若该 item 仍被 sock 关联，先清 notifyCtx，阻断后续 notify 再访问已释放内存
            if (sk->notifyCtx == item) {
                KNET_SockDisableNotify(sk);
                RemoveEpList(item);
                free(item);
            }
            
            KNET_SockUnlock(sk);
        }
    }
}

void KNET_ResetEpfd(int epfd)
{
    struct KnetEpoll *ep = KnetGetEpollFd(epfd);

    KNET_ResetEpList(ep, &ep->readyList);
    KNET_ResetEpList(ep, &ep->idleList);

    KNET_FdTypeSet(epfd, KNET_FD_TYPE_INVALID);
}

int knet_epoll_create(void)
{
    int epfd = epoll_create(1);
    if (epfd < 0) {
        KNET_ERR("Epoll create failed, ret %d, errno %d", epfd, errno);
        return epfd;
    }

    KNET_SetEpfd(epfd);

    KNET_DEBUG("Epoll create success, epfd %d", epfd);

    return epfd;
}

static uint8_t GetExceptState(struct epoll_event *event)
{
    uint8_t state = 0;

    if ((event->events & EPOLLIN) != 0) {
        state |= KNET_SOCK_STATE_READ;
    }
    if ((event->events & EPOLLOUT) != 0) {
        state |= KNET_SOCK_STATE_WRITE;
    }
    if ((event->events & EPOLLRDHUP) != 0) {
        state |= KNET_SOCK_STATE_CANTRCVMORE;
    }

    return state;
}

static uint32_t GetReadyEvents(struct KnetEpollItem* item)
{
    uint32_t events     = 0;
    uint8_t  readyState = item->state & item->expectState;

    if ((readyState & KNET_SOCK_STATE_READ) != 0) {
        events |= EPOLLIN;
    }

    if ((readyState & KNET_SOCK_STATE_WRITE) != 0) {
        events |= EPOLLOUT;
    }

    // DTOE检测到对端断开（收到FIN或RST报文）：
    // 订阅了EPOLLRDHUP则上送RDHUP；订阅了EPOLLIN则同时上送IN，用户knet_recv感知EOF(返回0)
    if ((item->state & KNET_SOCK_STATE_CANTRCVMORE) != 0) {
        if ((item->expectState & KNET_SOCK_STATE_CANTRCVMORE) != 0) {
            events |= EPOLLRDHUP;
        }
        if ((item->expectState & KNET_SOCK_STATE_READ) != 0) {
            events |= EPOLLIN;
        }
    }

    if ((item->state & KNET_SOCK_STATE_EXCEPTION) != 0) {
        events |= EPOLLERR;
    }
    return events;
}

static void InsertEpList(struct KnetEpollItem* item)
{
    struct KnetEpoll* ep = item->ep;

    item->ev.events = GetReadyEvents(item);

    KNET_EpollLock(ep);

    if (item->ev.events != 0) {
        TAILQ_INSERT_TAIL(&ep->readyList, item, epItem);
        item->ready = 1;
    } else {
        TAILQ_INSERT_TAIL(&ep->idleList, item, epItem);
        item->ready = 0;
    }

    KNET_EpollUnlock(ep);
}

static int CreateEpollItem(struct KnetSock *sk, struct KnetEpoll *ep, int epfd, int fd, struct epoll_event *event)
{
    if (sk->notifyCtx != NULL) {
        if (unlikely(sk->notifyType != KNET_SOCK_NOTIFY_TYPE_EPOLL)) {
            // 已被poll/user等其他类型通知占用，ctx布局不兼容，一律拒绝注册
            KNET_ERR("Fd %d notifyType %d is not epoll %d, cannot register to epfd %d", fd, sk->notifyType,
                     KNET_SOCK_NOTIFY_TYPE_EPOLL, epfd);
            return EEXIST;
        }
        if (unlikely(sk->notifyCtx->epfd == epfd)) {
            KNET_ERR("Fd %d is already registered with epfd %d", fd, epfd);
            return EEXIST;
        }
        KNET_ERR("Fd %d is already registered with another epfd %d, cannot register to epfd %d", fd,
                 sk->notifyCtx->epfd, epfd);
        return EEXIST;
    }

    struct KnetEpollItem *item = malloc(sizeof(struct KnetEpollItem));
    if (item == NULL) {
        KNET_ERR("Epfd %d fd %d epoll item malloc failed", epfd, fd);
        return ENOMEM;
    }

    item->fd = fd;
    item->epfd = epfd;
    item->state = sk->state;
    item->expectState = GetExceptState(event);
    item->ev.data = event->data;
    item->ep = ep;

    KNET_SockEnableNotify(sk, KNET_SOCK_NOTIFY_TYPE_EPOLL, item, epfd);
    InsertEpList(item);
    return 0;
}

static int DeleteEpollItem(struct KnetSock* sk, int epfd)
{
    struct KnetEpollItem *item = sk->notifyCtx;

    if (unlikely(item == NULL || sk->notifyType != KNET_SOCK_NOTIFY_TYPE_EPOLL || sk->associateFd != epfd)) {
        KNET_ERR("Delete epoll item failed, item is NULL or notifyType %d is not epoll or fd mismatch, sk_epfd %d, epfd %d",
            sk->notifyType, sk->associateFd, epfd);
        return ENOENT;
    }

    KNET_SockDisableNotify(sk);

    RemoveEpList(item);

    free(item);

    return 0;
}

static void UpdateEpList(struct KnetEpollItem *item, uint8_t newState, struct epoll_event *event)
{
    struct KnetEpoll* ep = item->ep;
    KNET_EpollLock(ep);

    if (event != NULL) {
        item->expectState    = GetExceptState(event);
        item->ev             = *event;
    }
    item->state = newState;

    uint32_t readyEvents = GetReadyEvents(item);
    item->ev.events = readyEvents;
    if (item->ready != 0 && readyEvents == 0) { // ready to idle
        TAILQ_REMOVE(&ep->readyList, item, epItem);
        TAILQ_INSERT_TAIL(&ep->idleList, item, epItem);
        item->ready = 0;
    } else if (item->ready == 0 && readyEvents != 0) { // idle to ready
        TAILQ_REMOVE(&ep->idleList, item, epItem);
        TAILQ_INSERT_TAIL(&ep->readyList, item, epItem);
        item->ready = 1;
    }

    KNET_EpollUnlock(ep);
}

static int UpdateEpollItem(struct KnetSock *sk, int epfd, struct epoll_event *event)
{
    struct KnetEpollItem *item = sk->notifyCtx;

    if (unlikely(item == NULL || sk->associateFd != epfd)) {
        KNET_ERR("UpdateEpollItem failed, item is NULL or fd mismatch, sk_epfd %d, epfd %d", sk->associateFd, epfd);
        return ENOENT;
    }

    UpdateEpList(item, item->state, event);

    return 0;
}

int knet_channel_bind_epoll(int epfd, struct knet_send_channel *send_channel, struct knet_recv_channel *recv_channel)
{
    if (unlikely(send_channel == NULL || recv_channel == NULL)) {
        KNET_ERR("K-NET channel bind epoll failed, epfd %d, send_channel or recv_channel is NULL", epfd);
        SET_ERRNO_RET(EINVAL);
    }

    struct KnetSendChannel *sendCh = (struct KnetSendChannel *)send_channel;
    if (unlikely(sendCh->schdMode != KNET_POLL_SCHD && sendCh->schdMode != KNET_EPOLL_SCHD)) {
        // 仅支持EPOLL/POLL两种调度模式，SWITCHABLE等其余模式事件获取机制未定义，一律拒绝
        KNET_ERR("K-NET channel bind epoll failed, epfd %d, send_channel schd_mod %d should be epoll %d or poll %d",
                 epfd, sendCh->schdMode, KNET_EPOLL_SCHD, KNET_POLL_SCHD);
        SET_ERRNO_RET(EINVAL);
    }
    if (unlikely(!KNET_IsOsFdEpoll(epfd))) {
        KNET_ERR("K-NET channel bind epoll failed, epfd %d not found", epfd);
        SET_ERRNO_RET(EINVAL);
    }
    struct KnetEpoll *ep = KnetGetEpollFd(epfd);

    struct KnetRecvChannel *recvCh = (struct KnetRecvChannel *)recv_channel;
    if (unlikely(recvCh->schdMode != KNET_EPOLL_SCHD && recvCh->schdMode != KNET_POLL_SCHD)) {
        // 仅支持EPOLL/POLL两种调度模式，SWITCHABLE等其余模式事件获取机制未定义，一律拒绝
        KNET_ERR("K-NET channel bind epoll failed, epfd %d, recv_channel schd_mod %d should be epoll %d or poll %d",
                 epfd, recvCh->schdMode, KNET_EPOLL_SCHD, KNET_POLL_SCHD);
        SET_ERRNO_RET(EINVAL);
    }

    // 驱动要求send/recv channel调度模式一致，bind处快速失败，避免offload阶段才报错
    if (unlikely(sendCh->schdMode != recvCh->schdMode)) {
        KNET_ERR("K-NET channel bind epoll failed, epfd %d, send schd_mod %d and recv schd_mod %d must be consistent",
                 epfd, sendCh->schdMode, recvCh->schdMode);
        SET_ERRNO_RET(EINVAL);
    }

    // EPOLL模式channel的事件fd是阻塞等待的唤醒源，须注册到OS epoll；
    // POLL模式下channel.epoll_fd无效，不可注册（垃圾fd可能撞上进程内无关的活fd产生伪事件），改由等待路径主动轮询收割
    if (recvCh->schdMode == KNET_EPOLL_SCHD) {
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.ptr = recvCh; // 以recvCh指针标识框架内部事件，EpollPollingLoops/EpollBlockWait据此过滤
        int ret = knet_epoll_ctl(epfd, EPOLL_CTL_ADD, recvCh->channel.epoll_fd, &ev);
        if (ret != 0) {
            KNET_ERR("K-NET channel bind epoll epoll_ctl failed, fd %d, ret %d, errno %d", recvCh->channel.epoll_fd,
                     ret, errno);
            return ret;
        }
    }
    if (sendCh->schdMode == KNET_EPOLL_SCHD) {
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.ptr = sendCh; // 以sendCh指针标识框架内部事件，等待路径据此过滤并触发send完成收割
        int ret = knet_epoll_ctl(epfd, EPOLL_CTL_ADD, sendCh->channel.epoll_fd, &ev);
        if (ret != 0) {
            KNET_ERR("K-NET channel bind epoll epoll_ctl failed, fd %d, ret %d, errno %d", sendCh->channel.epoll_fd,
                     ret, errno);
            if (recvCh->schdMode == KNET_EPOLL_SCHD) { // 回滚已注册的recv事件fd，避免残留内部注册
                (void)knet_epoll_ctl(epfd, EPOLL_CTL_DEL, recvCh->channel.epoll_fd, NULL);
            }
            return ret;
        }
    }

    ep->sendCh = (struct KnetSendChannel *)send_channel;
    ep->recvCh = (struct KnetRecvChannel *)recv_channel;

    KNET_DEBUG("K-NET channel bind epoll success, epfd %d, send schd_mod %d, recv schd_mod %d", epfd, sendCh->schdMode,
               recvCh->schdMode);

    return 0;
}

int knet_epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
    if (op < EPOLL_CTL_ADD || op > EPOLL_CTL_MOD) {
        KNET_ERR("Epoll op %d invalid, should be in range [%d, %d], epfd %d, fd %d", op, EPOLL_CTL_ADD, EPOLL_CTL_MOD, epfd, fd);
        SET_ERRNO_RET(EINVAL);
    }

    if (op != EPOLL_CTL_DEL) { // DEL时event可为NULL，无需检查
        if (unlikely(event == NULL)) {
            KNET_ERR("Epoll ctl event is NULL, epfd %d op %d fd %d", epfd, op, fd);
            SET_ERRNO_RET(EFAULT);
        }

        if (unlikely((event->events & ~EPOLL_ALLOWED_EVENTS) != 0)) {
            KNET_ERR("Events 0x%x not supported, epfd %d op %d fd %d", event->events, epfd, op, fd);
            SET_ERRNO_RET(EINVAL);
        }
    }

    if (unlikely(!KNET_IsOsFdEpoll(epfd))) {
        KNET_ERR("K-NET epoll ctl epfd %d invalid, op %d fd %d", epfd, op, fd);
        SET_ERRNO_RET(EINVAL);
    }
    
    if (unlikely(!KNET_IsOsFdValid(fd))) {
        KNET_ERR("K-NET epoll ctl fd %d invalid, epfd %d op %d", fd, epfd, op);
        SET_ERRNO_RET(EINVAL);
    }

    if (KNET_IsOsFdTcp(fd)) {
        struct KnetTcp *tcp = KnetGetTcpFd(fd);
        struct KnetEpoll *ep = KnetGetEpollFd(epfd);
        if (tcp->send_channel != ep->sendCh || tcp->recv_channel != ep->recvCh) {
            KNET_ERR("Fd %d send channel or recv channel not match epfd %d's", fd, epfd);
            SET_ERRNO_RET(EINVAL);
        }

        struct KnetSock *sk = &tcp->sk;
        int ret = 0;
        KNET_SockLock(sk);
        switch (op) {
            case EPOLL_CTL_ADD:
                ret = CreateEpollItem(sk, ep, epfd, fd, event);
                break;
            case EPOLL_CTL_DEL:
                ret = DeleteEpollItem(sk, epfd);
                break;
            case EPOLL_CTL_MOD:
                ret = UpdateEpollItem(sk, epfd, event);
                break;
            default:
                break;
        }
        KNET_SockUnlock(sk);

        KNET_DEBUG("K-NET epoll ctl TCP ret %d, fd %d epfd %d op %d data %x", ret, fd, epfd, op, event ? event->data.u64 : 0);

        KNET_SET_ERRNO(ret);
        return ret == 0 ? 0 : -1;
    } else {
        int ret = epoll_ctl(epfd, op, fd, event);
        if (ret < 0) {
            KNET_ERR("K-NET epoll ctl failed, epfd %d fd %d op %d ret %d errno %d", epfd, fd, op, ret, errno);
            return ret;
        }

        KNET_DEBUG("K-NET epoll ctl OS success, fd %d epfd %d op %d data %x", fd, epfd, op, event ? event->data.u64 : 0);
        return ret;
    }
}

static int EpollGetReadyEvents(struct KnetEpoll *ep, struct epoll_event *events, int maxevents)
{
    int count = 0;
    KNET_SpinlockLock(&ep->lock);

    // 检查readyList中的项，将它们的事件复制到events数组中
    struct KnetEpollItem *item = NULL;
    TAILQ_FOREACH(item, &ep->readyList, epItem) {
        if (count >= maxevents) {
            break;
        }
        events[count++] = item->ev;
    }

    KNET_SpinlockUnlock(&ep->lock);
    return count;
}

static int EpollPollingLoops(int epfd, struct KnetEpoll *ep, struct epoll_event *events, int maxevents)
{
    if (unlikely(events == NULL || maxevents <= 0)) {
        KNET_ERR("EpollPollingLoops events is NULL or maxevents <= 0, epfd %d, maxevents %d", epfd, maxevents);
        SET_ERRNO_RET(EINVAL);
    }
    // 每轮入口只收割一次：驱动侧本轮产生的完成事件在此统一转换成框架就绪事件
    KnetPollRecvChannel(ep->recvCh, maxevents);
    KnetPollSendChannel(ep->sendCh, maxevents);

    if (ep->recvCh->schdMode != KNET_EPOLL_SCHD) {
        // POLL模式无OS唤醒源，收割完直接返回框架就绪事件快照
        return EpollGetReadyEvents(ep, events, maxevents);
    }

    int osEpollRet = epoll_wait(epfd, events, maxevents, 0);
    if (osEpollRet < 0) {
        if (errno != EINTR) { // EINTR为信号打断的正常现象，降噪透传，由调用方决定是否重试
            KNET_ERR("Epoll wait failed, epfd %d, ret %d errno %d maxevents %d", epfd, osEpollRet, errno, maxevents);
        }
        return -1;
    }

    // 过滤channel内部事件（data.ptr==recvCh/sendCh）：入口已收割过本轮事件，这里仅消费唤醒，不再重复收割
    int osEventNum = 0;
    for (int i = 0; i < osEpollRet; i++) {
        if (events[i].data.ptr == ep->recvCh || events[i].data.ptr == ep->sendCh) {
            continue;
        }
        events[osEventNum++] = events[i];
    }

    int olEventNum = EpollGetReadyEvents(ep, &events[osEventNum], maxevents - osEventNum);

    return osEventNum + olEventNum;
}

static int EpollBlockWait(int epfd, struct KnetEpoll *ep, struct epoll_event *events, int maxevents, int timeout)
{
    if (unlikely(events == NULL || maxevents <= 0)) {
        KNET_ERR("EpollBlockWait events is NULL or maxevents <= 0, epfd %d, maxevents %d", epfd, maxevents);
        SET_ERRNO_RET(EINVAL);
    }

    // POLL模式recv channel无epoll唤醒源，阻塞等待退化为轮询降级：按固定间隔反复非阻塞收割，直到有事件或耗尽timeout
    if (ep->recvCh->schdMode != KNET_EPOLL_SCHD) {
        if (timeout < 0) {
            // 无限等待将退化为永不停止的轮询空转，拒绝以免烧CPU
            KNET_ERR("EpollBlockWait poll schd mode not support infinite wait, epfd %d, timeout %d", epfd, timeout);
            SET_ERRNO_RET(EINVAL);
        }

        int remaining = timeout;
        int ret = 0;
        while (remaining > 0) {
            int elapsed = (remaining < KNET_EPOLL_POLL_INTERVAL_MS) ? remaining : KNET_EPOLL_POLL_INTERVAL_MS;
            usleep(elapsed * 1000);
            remaining -= elapsed;

            ret = EpollPollingLoops(epfd, ep, events, maxevents);
            if (ret != 0) { // 有就绪事件(>0)或出错(<0)，退出轮询
                break;
            }
        }
        return ret;
    }

    struct epoll_event *osEvents = malloc(maxevents * sizeof(struct epoll_event));
    if (osEvents == NULL) {
        KNET_ERR("Alloc osEvents failed, epfd %d, maxevents %d", epfd, maxevents);
        errno = ENOMEM;
        return -1;
    }

    int ret = 0;
    do {
        int osEpollRet = epoll_wait(epfd, osEvents, maxevents, timeout);
        if (osEpollRet < 0) {
            if (errno != EINTR) { // EINTR为信号打断的正常现象，降噪透传，由调用方决定是否重试
                KNET_ERR("Epoll wait failed, epfd %d, ret %d errno %d maxevents %d timeout %d", epfd, osEpollRet, errno,
                         maxevents, timeout);
            }
            free(osEvents);
            return -1;
        }

        int osEventNum = 0;
        bool hasRecvChannelEvent = false;
        bool hasSendChannelEvent = false;

        // 循环osEvents数组过滤channel内部事件（用 data.ptr 标识，避免与用户 data.fd 冲突）
        for (int i = 0; i < osEpollRet; i++) {
            if (osEvents[i].data.ptr == ep->recvCh) {
                hasRecvChannelEvent = true;
            } else if (osEvents[i].data.ptr == ep->sendCh) {
                hasSendChannelEvent = true;
            } else {
                // 将其他osEvents的结果拷贝到入参events中
                events[osEventNum++] = osEvents[i];
            }
        }

        // 如果有recv channel事件，处理接收通道
        if (hasRecvChannelEvent) {
            KnetPollRecvChannel(ep->recvCh, maxevents);
        }
        // POLL模式send channel无事件fd，须每轮无条件轮询收割；EPOLL模式由send channel事件触发收割
        if (ep->sendCh->schdMode != KNET_EPOLL_SCHD || hasSendChannelEvent) {
            KnetPollSendChannel(ep->sendCh, maxevents);
        }

        int olEventNum = EpollGetReadyEvents(ep, &events[osEventNum], maxevents - osEventNum);
        ret = osEventNum + olEventNum;

        // 兜底：被唤醒(osEpollRet>0)却无就绪事件，且是无限等待 → 假唤醒，继续阻塞
        // (osEpollRet==0 才是真超时，此时才允许返回 0)
    } while (ret == 0 && timeout < 0);
    
    free(osEvents);
    return ret;
}

int knet_epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    if (unlikely(!KNET_IsOsFdEpoll(epfd))) {
        KNET_ERR("K-NET epoll wait failed, epfd %d is not K-NET epoll fd", epfd);
        SET_ERRNO_RET(EINVAL);
    }

    if (unlikely(events == NULL || maxevents <= 0)) {
        KNET_ERR("K-NET epoll wait events is NULL or maxevents <= 0, epfd %d, maxevents %d", epfd, maxevents);
        SET_ERRNO_RET(EINVAL);
    }

    struct KnetEpoll *ep = KnetGetEpollFd(epfd);
    if (ep->recvCh == NULL || ep->sendCh == NULL) {
        KNET_ERR("K-NET epoll wait failed, epfd %d recvCh or sendCh is NULL, need to bind channel first", epfd);
        SET_ERRNO_RET(EINVAL);
    }

    int ret = EpollPollingLoops(epfd, ep, events, maxevents);
    if (ret > 0) {
        KNET_DEBUG("K-NET epoll wait epfd %d non-block poll got %d ready events, timeout %d", epfd, ret, timeout);
        return ret;
    }
    if (unlikely(ret < 0)) {
        KNET_ERR("K-NET epoll wait epfd %d non-block poll failed, ret %d errno %d timeout %d", epfd, ret, errno, timeout);
        return ret; // epoll_wait 出错，直接上抛，不再进入阻塞等待
    }

    // ret == 0：当前无就绪事件且未出错，进入阻塞等待
    KNET_DEBUG("K-NET epoll wait epfd %d no ready events, enter block wait, timeout %d", epfd, timeout);

    return EpollBlockWait(epfd, ep, events, maxevents, timeout);
}

void KNET_EpollNotify(struct KnetSock *sk, void *ctx, uint8_t oldState, uint8_t newState, uint8_t event)
{
    struct KnetEpollItem *item = (struct KnetEpollItem *)ctx;

    (void)oldState;
    (void)event;

    if ((newState & KNET_SOCK_STATE_CLOSE) != 0) {
        RemoveEpList(item);

        // 释放 item 前清除回指，避免 sk->notifyCtx 指向已释放内存
        sk->notifyCtx = NULL;
        sk->notifyType = KNET_SOCK_NOTIFY_TYPE_NONE;
        free(item);

        return;
    }

    UpdateEpList(item, newState, NULL);
}

// 供fd复位路径调用：安全回收仍挂在epoll链表上的item
// （被动关闭等未经过knet_prepare_close的路径，item无人摘除会悬挂在readyList/idleList）
void KNET_EpollRemoveItem(struct KnetSock *sk)
{
    KNET_SockLock(sk);

    if (sk->notifyType == KNET_SOCK_NOTIFY_TYPE_EPOLL && sk->notifyCtx != NULL) {
        struct KnetEpollItem *item = sk->notifyCtx;

        RemoveEpList(item);

        // 清除回指，避免sk->notifyCtx指向已释放内存
        sk->notifyCtx = NULL;
        sk->notifyType = KNET_SOCK_NOTIFY_TYPE_NONE;
        sk->associateFd = -1;

        free(item);
    }

    KNET_SockUnlock(sk);
}
