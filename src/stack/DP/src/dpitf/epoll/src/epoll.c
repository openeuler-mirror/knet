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

#include "dp_epoll.h"
#include "dp_errno.h"

#include "shm.h"
#include "fd.h"
#include "sock.h"
#include "utils_log.h"
#include "utils_base.h"
#include "utils_debug.h"
#include "utils_spinlock.h"

#define DP_EPOLLET_MODE 1

typedef struct DP_EpollEvent EpollEvent_t;

typedef LIST_HEAD(, EpollItem) EpollItemLists;

typedef struct {
    EpollItemLists ready;
    EpollItemLists idle;
    Spinlock_t     lock;
    DP_Sem_t     sem;
    int            waitCnt;
    int            wakeupCnt;
    Fd_t*          file;
    DP_EpollNotify_t userNotify;
} Epoll_t;

typedef struct EpollItem {
    LIST_ENTRY(EpollItem) node;

    int     fd;
    int     epfd;
    uint8_t state;
    uint8_t exceptState;
    uint8_t mode; // LT/ET/ONESHOT
    uint8_t ready;

    Epoll_t*     ep;
    EpollEvent_t ev;

    uint32_t notifedEvents;
} EpollItem_t;

static void DisableSockNotify(EpollItem_t* item)
{
    Sock_t*  sk;
    Fd_t*    skFile;
    if (FD_Get(item->fd, FD_TYPE_SOCKET, &skFile) != 0) {
        return;
    }

    sk = (Sock_t*)(skFile->priv);
    SOCK_DisableNotifySafe(sk);
    FD_Put(skFile);
}

static int DP_EpollClose(Epoll_t* ep)
{
    EpollItem_t* item;
    while (!LIST_IS_EMPTY(&ep->ready)) {
        item = LIST_FIRST(&ep->ready);
        DisableSockNotify(item);
        LIST_REMOVE((&ep->ready), item, node);
        SHM_FREE(item, DP_MEM_FREE);
    }
    while (!LIST_IS_EMPTY(&ep->idle)) {
        item = LIST_FIRST(&ep->idle);
        DisableSockNotify(item);
        LIST_REMOVE((&ep->idle), item, node);
        SHM_FREE(item, DP_MEM_FREE);
    }
    SHM_FREE(ep, DP_MEM_FREE);

    return 0;
}

static FdOps_t g_epMeth = { .close = (int (*)(void*))DP_EpollClose };

static Epoll_t* CreateEpoll(DP_EpollNotify_t* callback)
{
    size_t   allocSize;
    Epoll_t* ret;

    allocSize = sizeof(Epoll_t) + SEM_Size;
    /* 在该函数中全字段赋值，无需初始化 */
    ret       = SHM_MALLOC(allocSize, MOD_EPOLL, DP_MEM_FREE);
    if (ret == NULL) {
        DP_LOG_ERR("Malloc memory failed for epoll.");
        return ret;
    }

    ret->sem            = (ret + 1);
    ret->waitCnt         = 0;
    ret->wakeupCnt       = 0;
    ret->userNotify.data = callback->data;
    ret->userNotify.fn   = callback->fn;

    LIST_INIT_HEAD(&ret->idle);
    LIST_INIT_HEAD(&ret->ready);
    SPINLOCK_Init(&ret->lock);
    SEM_INIT(ret->sem);

    return ret;
}

static uint8_t GetExceptState(EpollEvent_t* event)
{
    uint8_t state = 0;

    if ((event->events & DP_EPOLLIN) != 0) {
        state |= SOCK_STATE_READ;
    }
    if ((event->events & DP_EPOLLOUT) != 0) {
        state |= SOCK_STATE_WRITE;
    }
    if ((event->events & DP_EPOLLRDHUP) != 0) {
        state |= SOCK_STATE_CANTRCVMORE;
    }

    return state;
}

static uint32_t GetReadyEvents(EpollItem_t* item)
{
    uint32_t events     = 0;
    uint8_t  readyState = item->state & item->exceptState;

    if ((readyState & SOCK_STATE_READ) != 0) {
        events |= DP_EPOLLIN;
    }

    if ((readyState & SOCK_STATE_WRITE) != 0) {
        events |= DP_EPOLLOUT;
    }

    if ((readyState & SOCK_STATE_CANTRCVMORE) != 0) {
        events |= DP_EPOLLRDHUP;
    }

    if ((item->state & SOCK_STATE_CANTRCVMORE) != 0 && (item->state & SOCK_STATE_CANTSENDMORE) != 0) {
        events |= DP_EPOLLHUP;
    }

    if ((item->state & SOCK_STATE_EXCEPTION) != 0) {
        events |= DP_EPOLLERR;
    }
    return events;
}

// 这里不用锁，无法保证时序
static void Wakeup(Epoll_t* ep)
{
    if (ep->waitCnt > ep->wakeupCnt) {
        SEM_SIGNAL(ep->sem);
    }

    if (ep->userNotify.fn != NULL) {
        ep->userNotify.fn(ep->userNotify.data);
    }
    ep->wakeupCnt++;
}

// SEM_WAIT调用SemWait，返回值为0或errno（正数）
static int Wait(Epoll_t* ep, int timeout)
{
    if (ep->waitCnt == 0) {
        ep->wakeupCnt = 0;
    }

    ep->waitCnt++;
    SPINLOCK_Unlock(&ep->lock);

    int ret = (int)SEM_WAIT(ep->sem, timeout);

    SPINLOCK_Lock(&ep->lock);
    ep->waitCnt--;

    return -ret;
}

static void InsertEpList(EpollItem_t* item)
{
    Epoll_t* ep = item->ep;

    SPINLOCK_Lock(&ep->lock);

    item->ev.events = GetReadyEvents(item);

    if (item->ev.events != 0) {
        LIST_INSERT_TAIL(&ep->ready, item, node);
        Wakeup(ep);
        item->ready = 1;
    } else {
        LIST_INSERT_TAIL(&ep->idle, item, node);
        item->ready = 0;
    }

    SPINLOCK_Unlock(&ep->lock);
}

static void UpdateEpList(EpollItem_t* item, uint8_t newState, EpollEvent_t* event)
{
    Epoll_t* ep = item->ep;

    if (event != NULL) {
        item->exceptState = GetExceptState(event);
        item->ev          = *event;
        item->mode        = (event->events & DP_EPOLLET) != 0 ? DP_EPOLLET_MODE : 0;
    }
    item->state = newState;

    uint32_t readyEvents = GetReadyEvents(item);
    // 边缘触发模式，获取未上报的事件
    if ((item->mode & DP_EPOLLET_MODE) != 0) {
        item->notifedEvents = item->notifedEvents & readyEvents; // 有事件从1->0时，需要从已上报事件中移除
        readyEvents = readyEvents & (~item->notifedEvents);
    }

    SPINLOCK_Lock(&ep->lock);

    item->ev.events = readyEvents;
    if (item->ready != 0 && readyEvents == 0) { // ready to idle
        LIST_REMOVE(&ep->ready, item, node);
        LIST_INSERT_TAIL(&ep->idle, item, node);
        item->ready = 0;
    } else if (item->ready == 0 && readyEvents != 0) { // idle to ready
        LIST_REMOVE(&ep->idle, item, node);
        LIST_INSERT_TAIL(&ep->ready, item, node);
        Wakeup(ep);
        item->ready = 1;
    }

    SPINLOCK_Unlock(&ep->lock);
}

static void RemoveEpList(EpollItem_t* item)
{
    Epoll_t* ep = item->ep;

    SPINLOCK_Lock(&ep->lock);

    if (item->ready != 0) {
        LIST_REMOVE(&ep->ready, item, node);
    } else {
        LIST_REMOVE(&ep->idle, item, node);
    }

    SPINLOCK_Unlock(&ep->lock);
}

void EPOLL_Notify(Sock_t* sk, void* ctx, uint8_t oldState, uint8_t newState)
{
    EpollItem_t* item = (EpollItem_t*)ctx;

    ASSERT(ctx != NULL);
    (void)sk;
    (void)oldState;

    if ((newState & SOCK_STATE_CLOSE) != 0) {
        RemoveEpList(item);

        SHM_FREE(item, DP_MEM_FREE);

        return;
    }

    UpdateEpList(item, newState, NULL);
}

static int CreateEpollItem(Sock_t* sk, Epoll_t* ep, int fd, int epfd, EpollEvent_t* event)
{
    EpollItem_t* item;
    if (event == NULL) {
        return EFAULT;
    }

    // socket已注册过
    if (sk->notifyType == SOCK_NOTIFY_TYPE_EPOLL && sk->notifyCtx != NULL) {
        item = (EpollItem_t*)sk->notifyCtx;
        if (item->epfd == epfd) {
            DP_LOG_ERR("the supplied file descriptor fd is already registered with this epoll instance.");
            return EEXIST;
        }
    }

    /* 在该函数及InsertEpList中全字段赋值，无需初始化 */
    item = SHM_MALLOC(sizeof(*item), MOD_EPOLL, DP_MEM_FREE);
    if (item == NULL) {
        DP_LOG_ERR("Malloc memory failed for epoll item.");
        return ENOMEM;
    }

    item->fd            = fd;
    item->epfd          = epfd;
    item->state         = sk->state;
    item->exceptState   = GetExceptState(event);
    item->mode          = (event->events & DP_EPOLLET) != 0 ? DP_EPOLLET_MODE : 0;
    item->ev.data       = event->data;
    item->ep            = ep;
    item->notifedEvents = 0;

    SOCK_EnableNotify(sk, SOCK_NOTIFY_TYPE_EPOLL, item, epfd);

    InsertEpList(item);

    return 0;
}

static int EpollGetEvents(Epoll_t* ep, struct DP_EpollEvent* events, int maxevents)
{
    int cnt = 0;
    EpollItem_t* item;
    EpollItem_t* next;
    item = LIST_FIRST(&ep->ready);

    while (item != NULL && cnt < maxevents) {
        next = LIST_NEXT(item, node);

        events[cnt++] = item->ev;

        // 边缘触发模式下，需要记录已上报的事件，并挪入空闲队列
        if ((item->mode & DP_EPOLLET_MODE) != 0) {
            LIST_REMOVE(&ep->ready, item, node);
            LIST_INSERT_TAIL(&ep->idle, item, node);
            item->ready = 0;
            item->notifedEvents |= item->ev.events;
        }
        item = next;
    }
    return cnt;
}

static int UpdateEpollItem(Sock_t* sk, int epfd, EpollEvent_t* event)
{
    EpollItem_t* item = (EpollItem_t*)sk->notifyCtx;

    if (event == NULL) {
        return EFAULT;
    }

    if (item == NULL || sk->associateFd != epfd) {
        return ENOENT;
    }

    UpdateEpList(item, item->state, event);

    return 0;
}

static int DeleteEpollItem(Sock_t* sk, int epfd)
{
    EpollItem_t* item = (EpollItem_t*)sk->notifyCtx;

    if (item == NULL || sk->associateFd != epfd) {
        return ENOENT;
    }

    SOCK_DisableNotify(sk);

    RemoveEpList(item);

    SHM_FREE(item, DP_MEM_FREE);

    return 0;
}

static int EpollCreateWithCallback(int size, DP_EpollNotify_t* callback)
{
    Epoll_t* ep;
    Fd_t*    file;

    if (size <= 0) {
        DP_LOG_ERR("Epoll create failed, invalid size: %d.", size);
        DP_SET_ERRNO(EINVAL);
        return -1;
    }

    file = FD_Alloc();
    if (file == NULL) {
        DP_LOG_ERR("Epoll create failed, useless file.");
        return -1;
    }

    ep = CreateEpoll(callback);
    if (ep == NULL) {
        FD_Free(file);
        DP_SET_ERRNO(ENOMEM);
        return -1;
    }

    file->type = FD_TYPE_EPOLL;
    file->priv = ep;
    file->ops  = &g_epMeth;

    ep->file = file;

    return FD_GetUserFd(file);
}

int DP_EpollCreate(int size)
{
    DP_EpollNotify_t callback = { .data = NULL, .fn = NULL };
    return EpollCreateWithCallback(size, &callback);
}

int DP_EpollCreateNotify(int size, DP_EpollNotify_t *callback)
{
    if (callback == NULL || callback->fn == NULL) {
        DP_LOG_ERR("Epoll crate notify failed, invalid callback.");
        DP_SET_ERRNO(EINVAL);
        return -1;
    }
    return EpollCreateWithCallback(size, callback);
}

int DP_EpollCtl(int epfd, int op, int fd, struct DP_EpollEvent* event)
{
    Epoll_t* ep;
    Sock_t*  sk;
    Fd_t*    epFile;
    Fd_t*    skFile;
    int      ret;

    if (op < DP_EPOLL_CTL_ADD || op > DP_EPOLL_CTL_MOD) {
        DP_LOG_ERR("Epoll ctle failed, invalid op: %d.", op);
        DP_SET_ERRNO(EINVAL);
        return -1;
    }

    if (fd == epfd) {
        DP_SET_ERRNO(EINVAL);
        return -1;
    }

    if ((ret = FD_Get(epfd, FD_TYPE_EPOLL, &epFile)) != 0) {
        DP_SET_ERRNO(-ret);
        return -1;
    }

    if ((ret = FD_Get(fd, FD_TYPE_SOCKET, &skFile)) != 0) {
        FD_Put(epFile);
        DP_SET_ERRNO(-ret);

        // 非socket fd
        if (ret == -ENOTSOCK) {
            DP_SET_ERRNO(EPERM);
        }
        return -1;
    }

    sk = (Sock_t*)(skFile->priv);
    ep = (Epoll_t*)(epFile->priv);

    if (sk == NULL || ep == NULL) {
        DP_SET_ERRNO(EBADF);
        goto out;
    }

    SOCK_Lock(sk);

    if (op == DP_EPOLL_CTL_ADD) {
        ret = CreateEpollItem(sk, ep, fd, epfd, event);
    } else if (op == DP_EPOLL_CTL_MOD) {
        ret = UpdateEpollItem(sk, epfd, event);
    } else {
        ret = DeleteEpollItem(sk, epfd);
    }

    SOCK_Unlock(sk);

    DP_SET_ERRNO(ret);

out:
    FD_Put(skFile);
    FD_Put(epFile);

    return ret == 0 ? 0 : -1;
}

int DP_EpollWait(int epfd, struct DP_EpollEvent* events, int maxevents, int timeout)
{
    Fd_t*        epFile;
    Epoll_t*     ep;
    int          ret;
    int          cnt;

    if (events == NULL) {
        DP_SET_ERRNO(EFAULT);
        return -1;
    }

    if ((maxevents <= 0) || (maxevents > DP_EPOLL_MAX_NUM)) {
        DP_SET_ERRNO(EINVAL);
        return -1;
    }

    ret = FD_Get(epfd, FD_TYPE_EPOLL, &epFile);
    if (ret != 0) {
        DP_SET_ERRNO(-ret);
        return -1;
    }

    ep = (Epoll_t*)(epFile->priv);

    SPINLOCK_Lock(&ep->lock);

    if (LIST_IS_EMPTY(&ep->ready)) {
        ret = Wait(ep, timeout);
        if (ret != 0) {
            ret = (ret == -ETIMEDOUT) ? 0 : ret;        // 超时情况下返回0，不设置错误码
            SPINLOCK_Unlock(&ep->lock);
            FD_Put(epFile);
            DP_SET_ERRNO(-ret);
            return (ret == 0) ? 0 : -1;
        }
    }

    cnt = EpollGetEvents(ep, events, maxevents);

    SPINLOCK_Unlock(&ep->lock);
    FD_Put(epFile);
    DP_SET_ERRNO(0);
    return cnt;
}
