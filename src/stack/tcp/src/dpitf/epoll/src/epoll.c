/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <pthread.h>

#include "dp_epoll.h"
#include "dp_errno.h"

#include "shm.h"
#include "dp_fd.h"
#include "sock.h"
#include "utils_log.h"
#include "utils_base.h"
#include "utils_debug.h"
#include "utils_spinlock.h"
#include "utils_statistic.h"
#include "utils_cb_cnt.h"
#include "worker.h"

#define EP_UNACTIVE_PTR ((EpollItem_t*)-1)
#define EP_PRIVATE_BITS     (DP_EPOLLWAKEUP | DP_EPOLLET | DP_EPOLLONESHOT | DP_EPOLLEXCLUSIVE)

typedef struct DP_EpollEvent EpollEvent_t;

typedef LIST_HEAD(, EpollItem) EpollItemLists;

typedef struct EpollItem EpollItem_t;

typedef struct {
    pthread_spinlock_t lock; // 保护范围：ep->ovflist、ep->ready、item->linked、item->ovflistNext、waitCnt、wakeupCnt
    pthread_mutex_t ctlLock; // 保护范围：epoll_wait和epoll_ctl行为、保护item的内容更新、ep->epItemList
    uint8_t isClosed;
    DP_Sem_t     sem;
    int            waitCnt;
    int            wakeupCnt;
    uint32_t epWatchCnt; // 监听的fd个数
    EpollItemLists epItemList;
    Fd_t*          file;
    
    int32_t        wid;
    DP_EpollNotify_t userNotify;
    EpollItemLists ready;
    EpollItem_t*   ovflist;
    uint8_t        ovflistActive;
} Epoll_t;

typedef struct EpollItem {
    LIST_ENTRY(EpollItem) node;
    LIST_ENTRY(EpollItem) epiNode;

    Sock_t* sk;
    int     fd;
    int     epfd;
    uint8_t linked : 1;
    uint8_t res : 7;
    uint32_t reserved;

    Epoll_t*     ep;
    EpollEvent_t ev;
    EpollItem_t* ovflistNext;
} EpollItem_t;

static inline void EpollCtlLock(Epoll_t* ep)
{
    pthread_mutex_lock(&ep->ctlLock);
}

static inline void EpollCtlUnlock(Epoll_t* ep)
{
    pthread_mutex_unlock(&ep->ctlLock);
}

static inline void EpollLock(Epoll_t* ep)
{
    pthread_spin_lock(&ep->lock);
}

static inline void EpollUnlock(Epoll_t* ep)
{
    pthread_spin_unlock(&ep->lock);
}

static inline int EpItemIsLinked(EpollItem_t *item)
{
    return item->linked;
}

static SockNotify_t* GetEpollNotify(Sock_t* sk, int epfd)
{
    SockNotify_t* notify = NULL;

    LIST_FOREACH(&sk->notifyList, notify, node)
    {
        if (notify->notifyType == SOCK_NOTIFY_TYPE_EPOLL && notify->associateFd == epfd) {
            return notify;
        }
    }
    return NULL;
}

static void DisableSockNotify(EpollItem_t* item)
{
    Sock_t*  sk;
    Fd_t*    skFile;
    if (FD_Get(item->fd, FD_TYPE_SOCKET, &skFile) != 0) {
        return;
    }

    sk = (Sock_t*)(skFile->priv);
    SOCK_NotifyLock(sk);
    SockNotify_t* notify = GetEpollNotify(sk, item->epfd);
    if (notify != NULL) {
        LIST_REMOVE(&sk->notifyList, notify, node);
        OS_FREE(notify);
    }
    SOCK_NotifyUnlock(sk);
    FD_Put(skFile);
}

static int DP_EpollClose(Epoll_t* ep)
{
    // 加锁原因:若不加锁，epoll并发，UpdateEplist删除ep->idle链表元素，此时EpollClose会发现ep->ready为NULL。所以需加锁，且UpdateEpList要检查epoll是否已关闭
    EpollCtlLock(ep);
    ep->isClosed = true; // todo: dp epollwait和dp epollctl、维测getdetail，所有获取epfd上下文的地方 是不是也要检查epoll是否已关闭？
    EpollCtlUnlock(ep);

    EpollItem_t* item;

    while (!LIST_IS_EMPTY(&ep->epItemList)) {
        item = LIST_FIRST(&ep->epItemList);
        DisableSockNotify(item);
        LIST_REMOVE((&ep->epItemList), item, epiNode);
        --ep->epWatchCnt;
        SHM_FREE(item, DP_MEM_FREE);
    }

    if (ep->file != NULL) {
        FD_Free(ep->file);
    }

    pthread_mutex_destroy(&ep->ctlLock);
    pthread_spin_destroy(&ep->lock);
    SHM_FREE(ep, DP_MEM_FREE);
    (void)UTILS_DecCbCnt(&g_epollCbCnt);

    return 0;
}

static int DpEpollCoThreadCheck(Epoll_t* ep)
{
    if (CFG_GET_VAL(DP_CFG_DEPLOYMENT) == DP_DEPLOYMENT_DEFAULT) {
        return 1;
    }

    int32_t wid = WORKER_GetSelfId();
    if (wid < 0 || wid != ep->wid) {
        DP_LOG_ERR("close epoll failed, ep wid = %d, cur wid = %d", ep->wid, wid);
        DP_ADD_ABN_STAT(DP_WORKER_MISS_MATCH);
        return 0;
    }
    return 1;
}

static int DP_EpollCanClose(Epoll_t* ep)
{
    return DpEpollCoThreadCheck(ep);
}

static int DP_EpollCanFcntl(Epoll_t* ep)
{
    return DpEpollCoThreadCheck(ep);
}

static FdOps_t g_epMeth = {
    .close = (int (*)(void*))DP_EpollClose,
    .canClose = (int (*)(void*))DP_EpollCanClose,
    .canFcntl = (int (*)(void*))DP_EpollCanFcntl,
};

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
    ret->isClosed = false;
    ret->ovflist = EP_UNACTIVE_PTR;
    ret->ovflistActive = 0;

    LIST_INIT_HEAD(&ret->ready);
    LIST_INIT_HEAD(&ret->epItemList);
    ret->epWatchCnt = 0;
    pthread_spin_init(&ret->lock, PTHREAD_PROCESS_PRIVATE);
    pthread_mutex_init(&ret->ctlLock, NULL);

    if (SEM_INIT(ret->sem) != 0) {
        pthread_mutex_destroy(&ret->ctlLock);
        pthread_spin_destroy(&ret->lock);
        SHM_FREE(ret, DP_MEM_FREE);
        return NULL;
    }

    return ret;
}

static uint32_t SockState2Event(uint8_t state)
{
    uint32_t events = 0;
    if ((state & SOCK_STATE_READ) != 0) {
        events |= DP_EPOLLIN;
    }
    if ((state & SOCK_STATE_WRITE) != 0) {
        events |= DP_EPOLLOUT;
    }
    if ((state & SOCK_STATE_EXCEPTION) != 0) {
        events |= DP_EPOLLERR;
    }
    if ((state & SOCK_STATE_CANTRCVMORE) != 0) {
        events |= DP_EPOLLRDHUP;
    }
    if ((state & SOCK_STATE_CANTRCVMORE) != 0 && (state & SOCK_STATE_CANTSENDMORE) != 0) {
        events |= DP_EPOLLHUP;
    }
    return events;
}

static uint32_t epItemPoll(EpollItem_t* item)
{
    return SockState2Event(item->sk->state) & item->ev.events;
}

// 这里不用锁，无法保证时序
static void WakeupLocked(Epoll_t* ep)
{
    if (CFG_GET_VAL(DP_CFG_DEPLOYMENT) == DP_DEPLOYMENT_CO_THREAD) {
        return;
    }
    if (ep->waitCnt > ep->wakeupCnt) {
        EpollUnlock(ep);
        SEM_SIGNAL(ep->sem);
        EpollLock(ep);
    }
    ep->wakeupCnt++;
}

static void WakeupUnlocked(Epoll_t* ep)
{
    if (ep->userNotify.fn != NULL) {
        ep->userNotify.fn(ep->userNotify.data);
    }
}

// SEM_WAIT调用SemWait，返回值为0或errno（正数）
static int Wait(Epoll_t* ep, int timeout)
{
    if (CFG_GET_VAL(DP_CFG_DEPLOYMENT) == DP_DEPLOYMENT_CO_THREAD) {
        return 0;
    }

    if (ep->waitCnt == 0) {
        ep->wakeupCnt = 0;
    }

    ep->waitCnt++;
    EpollUnlock(ep);

    int ret = (int)SEM_WAIT(ep->sem, timeout);

    EpollLock(ep);
    ep->waitCnt--;

    if (ret == DP_ERR) {
        ret = EFAULT;
    }

    return -ret;
}

static void InsertEpList(EpollItem_t* item)
{
    Epoll_t* ep = item->ep;

    uint32_t revents = epItemPoll(item);
    int needWakeup = 0;

    EpollLock(ep);
    if (revents != 0) {
        LIST_INSERT_TAIL(&ep->ready, item, node);
        item->linked = 1;
        needWakeup = 1;
    }

    LIST_INSERT_TAIL(&ep->epItemList, item, epiNode);
    ++ep->epWatchCnt;

    if (needWakeup) {
        WakeupLocked(ep);
    }
    EpollUnlock(ep);

    if (needWakeup) {
        WakeupUnlocked(ep);
    }
}

static int UpdateEpListMod(EpollItem_t* item, EpollEvent_t* event)
{
    Epoll_t* ep = item->ep;
    int needWakeup = 0;

    item->ev.events = event->events; // need barrier below
    item->ev.data = event->data; // protected by ctlLock

    /*
	 * The following barrier has two effects:
	 *
	 * 1) Flush epi changes above to other CPUs.  This ensures
	 *    we do not miss events from ep_poll_callback if an
	 *    event occurs immediately after we call f_op->poll().
	 *    We need this because we did not take ep->wq.lock while
	 *    changing epi above (but ep_poll_callback does take
	 *    ep->wq.lock).
	 *
	 * 2) We also need to ensure we do not miss _past_ events
	 *    when calling f_op->poll().  This barrier also
	 *    pairs with the barrier in wq_has_sleeper (see
	 *    comments for wq_has_sleeper).
	 *
	 * This barrier will now guarantee ep_poll_callback or f_op->poll
	 * (or both) will notice the readiness of an item.
	 */
    __atomic_thread_fence(__ATOMIC_SEQ_CST);

    if (epItemPoll(item) == 0) {
        return 0;
    }

    EpollLock(ep);
    if (!EpItemIsLinked(item)) {
        LIST_INSERT_TAIL(&ep->ready, item, node);
        item->linked = 1;
        needWakeup = 1;
    }

    if (needWakeup) {
        WakeupLocked(ep);
    }
    EpollUnlock(ep);

    if (needWakeup) {
        WakeupUnlocked(ep);
    }
    return needWakeup;
}

static int UpdateEpListNotify(EpollItem_t* item, uint8_t newState)
{
    Epoll_t* ep = item->ep;
    int needWakeup = 0;
    uint32_t sockEvent = SockState2Event(newState);

    EpollLock(ep); // 隐式包含读屏障
    if (!(item->ev.events & ~EP_PRIVATE_BITS)) {
        EpollUnlock(ep);
        return 0;
    }

    if (!(sockEvent & item->ev.events)) {
        EpollUnlock(ep);
        return 0;
    }

    if (ep->ovflistActive) {
        if (item->ovflistNext == EP_UNACTIVE_PTR) {
            item->ovflistNext = ep->ovflist;
            ep->ovflist = item;
        }
    } else {
        if (!EpItemIsLinked(item)) {
            LIST_INSERT_TAIL(&ep->ready, item, node);
            item->linked = 1;
            needWakeup = 1;
        }
    }

    if (needWakeup) {
        WakeupLocked(ep);
    }
    EpollUnlock(ep);

    if (needWakeup) {
        WakeupUnlocked(ep);
    }
    return needWakeup;
}

static void EpDelRemoveEpList(EpollItem_t* item)
{
    Epoll_t* ep = item->ep;

    EpollLock(ep);
    if (EpItemIsLinked(item)) {
        LIST_REMOVE(&ep->ready, item, node);
        item->linked = 0;
    }
    EpollUnlock(ep);

    LIST_REMOVE(&ep->epItemList, item, epiNode);
    --ep->epWatchCnt;
}

static void RemoveEpList(EpollItem_t* item)
{
    Epoll_t* ep = item->ep;

    EpollCtlLock(ep);
    if (ep->isClosed == true) {
        EpollCtlUnlock(ep);
        return;
    }
    LIST_REMOVE(&ep->epItemList, item, epiNode);
    --ep->epWatchCnt;

    EpollLock(ep);
    if (EpItemIsLinked(item)) {
        LIST_REMOVE(&ep->ready, item, node);
        item->linked = 0;
    }
    EpollUnlock(ep);

    EpollCtlUnlock(ep);

    SHM_FREE(item, DP_MEM_FREE);
}

void EPOLL_Notify(Sock_t* sk, void* ctx, uint8_t oldState, uint8_t newState, uint8_t event, uint64_t associateFd)
{
    EpollItem_t* item = (EpollItem_t*)ctx;

    ASSERT(ctx != NULL);
    (void)sk;
    (void)oldState;
    (void)event;
    (void)associateFd;

    if ((newState & SOCK_STATE_CLOSE) != 0) {
        RemoveEpList(item);
        return;
    }

    UpdateEpListNotify(item, newState);
}

static int CreateEpollItem(Sock_t* sk, Epoll_t* ep, int fd, int epfd, EpollEvent_t* event)
{
    SOCK_NotifyLock(sk);
    // socket已注册过
    SockNotify_t* notify = GetEpollNotify(sk, epfd);
    if (notify != NULL) {
        SOCK_NotifyUnlock(sk);
        DP_LOG_ERR("the supplied file descriptor fd is already registered with this epoll instance.");
        return EEXIST;
    }

    /* 在该函数及InsertEpList中全字段赋值，无需初始化 */
    EpollItem_t* item = SHM_MALLOC(sizeof(*item), MOD_EPOLL, DP_MEM_FREE);
    if (item == NULL) {
        SOCK_NotifyUnlock(sk);
        DP_LOG_ERR("Malloc memory failed for epoll item.");
        return ENOMEM;
    }

    item->sk = sk;
    item->fd             = fd;
    item->epfd           = epfd;
    item->ev             = *event;
    item->ep             = ep;
    item->linked         = 0;
    item->ovflistNext    = EP_UNACTIVE_PTR;

    EpollCtlLock(ep); // 执行SOCK_EnableNotify后，item就对epoll ctl可见，所以需要加锁保护item
    int ret = SOCK_EnableNotify(sk, SOCK_NOTIFY_TYPE_EPOLL, item, epfd);
    if (ret != 0) {
        SOCK_NotifyUnlock(sk);
        EpollCtlUnlock(ep);
        DP_LOG_ERR("CreateEpollItem failed, SOCK_EnableNotify failed, fd = %d, epfd = %d.", fd, epfd);
        SHM_FREE(item, DP_MEM_FREE);
        return ret;
    }
    SOCK_NotifyUnlock(sk);

    InsertEpList(item);
    EpollCtlUnlock(ep);
    return ret;
}

static int EpollGetEvents(Epoll_t* ep, EpollItemLists* txlist, struct DP_EpollEvent* events, int maxevents)
{
    int cnt = 0;
    EpollItem_t* item;
    EpollItem_t* next;

    item = LIST_FIRST(txlist);
    while (item != NULL && cnt < maxevents) {
        next = LIST_NEXT(item, node);

        uint32_t revents = epItemPoll(item);

        if (revents != 0) {
            events[cnt].events = revents;
            events[cnt].data = item->ev.data;
            cnt++;

            if ((item->ev.events & DP_EPOLLONESHOT) != 0) {
                item->ev.events &= EP_PRIVATE_BITS;
                LIST_REMOVE(txlist, item, node);
                item->linked = 0;
            } else if ((item->ev.events & DP_EPOLLET) != 0) {
                LIST_REMOVE(txlist, item, node);
                item->linked = 0;
            }
            // LT mode: keep in txlist, will be re-injected to ready list
        } else {
            LIST_REMOVE(txlist, item, node);
            item->linked = 0;
        }

        item = next;
    }

    return cnt;
}

static int UpdateEpollItem(Sock_t* sk, Epoll_t* ep, int epfd, EpollEvent_t* event)
{
    SOCK_NotifyLock(sk);
    SockNotify_t* notify = GetEpollNotify(sk, epfd);
    if (notify != NULL) {
        EpollItem_t* item = (EpollItem_t*) notify->notifyCtx;
        EpollCtlLock(ep);
        SOCK_NotifyUnlock(sk);
        UpdateEpListMod(item, event);
        EpollCtlUnlock(ep);
        return 0;
    }
    SOCK_NotifyUnlock(sk);

    DP_LOG_ERR("UpdateEpollItem failed, item is NULL, epfd = %d.", epfd);
    return ENOENT;
}

static int DeleteEpollItem(Sock_t* sk, int epfd)
{
    SockNotify_t* notify = GetEpollNotify(sk, epfd);
    if (notify != NULL) {
        EpollItem_t* item = (EpollItem_t*) notify->notifyCtx;
        EpDelRemoveEpList(item);
        SHM_FREE(item, DP_MEM_FREE);
        
        LIST_REMOVE(&sk->notifyList, notify, node);
        OS_FREE(notify);
        return 0;
    }

    DP_LOG_ERR("DeleteEpollItem failed, can't find item, epfd = %d.", epfd);
    return ENOENT;
}

static int EpollCreateWithCallback(int size, DP_EpollNotify_t* callback)
{
    Epoll_t* ep;
    Fd_t*    file;
    int32_t  wid = -1;

    if (size <= 0) {
        DP_LOG_DBG("Epoll create failed, invalid size: %d.", size);
        DP_SET_ERRNO(EINVAL);
        return -1;
    }

    if (CFG_GET_VAL(DP_CFG_DEPLOYMENT) == DP_DEPLOYMENT_CO_THREAD) {
        wid = WORKER_GetSelfId();
        if (wid < 0) {
            DP_LOG_DBG("Create epoll socket co thread failed, get wid error, wid = %d", wid);
            DP_ADD_ABN_STAT(DP_WORKER_MISS_MATCH);
            return -EFAULT;
        }
    }

    file = FD_Alloc();
    if (file == NULL) {
        DP_LOG_ERR("Epoll create failed, useless file.");
        return -1;
    }

    if (UTILS_IncCbCnt(&g_epollCbCnt, (uint32_t)CFG_GET_VAL(DP_CFG_EPOLLCB_MAX)) != 0) {
        DP_LOG_ERR("The num of epoll exceed maxlimit configured.");
        DP_ADD_ABN_STAT(DP_EPOLL_CREATE_FULL);
        FD_Free(file);
        DP_SET_ERRNO(EMFILE);
        return -1;
    }

    ep = CreateEpoll(callback);
    if (ep == NULL) {
        (void)UTILS_DecCbCnt(&g_epollCbCnt);
        FD_Free(file);
        DP_SET_ERRNO(ENOMEM);
        return -1;
    }

    file->type = FD_TYPE_EPOLL;
    file->priv = ep;
    file->ops  = &g_epMeth;

    ep->file = file;
    ep->wid = wid;

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
        DP_LOG_DBG("Epoll crate notify failed, invalid callback.");
        DP_SET_ERRNO(EINVAL);
        return -1;
    }
    return EpollCreateWithCallback(size, callback);
}

static int GetEpollFromFd(int fd, Fd_t** file, Epoll_t** ep)
{
    Fd_t* tmpFile = NULL;
    int ret = FD_Get(fd, FD_TYPE_EPOLL, &tmpFile);
    if (ret != 0) {
        DP_LOG_DBG("GetEpollFromFd failed by get epoll fd failed, ret = %d.", ret);
        return ret;
    }

    Epoll_t* tmpEp = (Epoll_t *)(tmpFile->priv);
    if (tmpEp == NULL) {
        DP_LOG_DBG("GetEpollFromFd failed, sk or skOps NULL.");
        FD_Put(tmpFile);
        return -EBADF;
    }

    if (CFG_GET_VAL(DP_CFG_DEPLOYMENT) == DP_DEPLOYMENT_CO_THREAD) {
        int32_t wid = WORKER_GetSelfId();
        if (wid != tmpEp->wid) {
            DP_LOG_DBG("Get epoll from fd failed, wid unmatched: sk->wid = %d, cur wid = %d", tmpEp->wid, wid);
            DP_ADD_ABN_STAT(DP_WORKER_MISS_MATCH);
            FD_Put(tmpFile);
            return -EINVAL;
        }
    }
    *file = tmpFile;
    *ep = tmpEp;

    return 0;
}

static int GetSocketFromFd(int fd, Fd_t** file, Sock_t** sk)
{
    Fd_t* tmpFile = NULL;
    int ret = FD_Get(fd, FD_TYPE_SOCKET, &tmpFile);
    if (ret != 0) {
        DP_LOG_ERR("GetSocketFromFd failed by get socket fd failed, ret = %d.", ret);
        if (ret == -ENOTSOCK) {
            ret = -EPERM;
        }
        return ret;
    }

    Sock_t* tmpSk = (Sock_t *)(tmpFile->priv);
    if (tmpSk == NULL) {
        DP_LOG_ERR("Get sock from fd failed, sk or skOps NULL.");
        FD_Put(tmpFile);
        return -EBADF;
    }

    if (CFG_GET_VAL(DP_CFG_DEPLOYMENT) == DP_DEPLOYMENT_CO_THREAD) {
        int32_t wid = WORKER_GetSelfId();
        if (wid != tmpSk->wid) {
            DP_LOG_DBG("Get sock from fd failed, wid unmatched: sk->wid = %d, cur wid = %d", tmpSk->wid, wid);
            DP_ADD_ABN_STAT(DP_WORKER_MISS_MATCH);
            FD_Put(tmpFile);
            return -EINVAL;
        }
    }
    *file = tmpFile;
    *sk = tmpSk;

    return 0;
}

int DP_EpollCtl(int epfd, int op, int fd, struct DP_EpollEvent* event)
{
    Epoll_t* ep;
    Sock_t*  sk;
    Fd_t*    epFile;
    Fd_t*    skFile;
    int      ret;

    if (op < DP_EPOLL_CTL_ADD || op > DP_EPOLL_CTL_MOD) {
        DP_LOG_ERR("DP_EpollCtl failed, invalid op: %d.", op);
        DP_SET_ERRNO(EINVAL);
        return -1;
    }

    if (op != DP_EPOLL_CTL_DEL && event == NULL) {
        DP_LOG_ERR("DP_EpollCtl failed, event is NULL, epfd %d op %d fd %d.", epfd, op, fd);
        DP_SET_ERRNO(EFAULT);
        return -1;
    }

    if (fd == epfd) {
        DP_LOG_ERR("DP_EpollCtl failed, fd == epfd.");
        DP_SET_ERRNO(EINVAL);
        return -1;
    }

    ret = GetEpollFromFd(epfd, &epFile, &ep);
    if (ret != 0) {
        DP_SET_ERRNO(-ret);
        return -1;
    }

    ret = GetSocketFromFd(fd, &skFile, &sk);
    if (ret != 0) {
        FD_Put(epFile);
        DP_SET_ERRNO(-ret);
        return -1;
    }

    if (op == DP_EPOLL_CTL_ADD) {
        struct DP_EpollEvent ev = *event;
        ev.events |= DP_EPOLLERR | DP_EPOLLHUP;
        ret = CreateEpollItem(sk, ep, fd, epfd, &ev);
    } else if (op == DP_EPOLL_CTL_MOD) {
        struct DP_EpollEvent ev = *event;
        ev.events |= DP_EPOLLERR | DP_EPOLLHUP;
        ret = UpdateEpollItem(sk, ep, epfd, &ev);
    } else {
        SOCK_NotifyLock(sk);
        EpollCtlLock(ep);
        ret = DeleteEpollItem(sk, epfd);
        EpollCtlUnlock(ep);
        SOCK_NotifyUnlock(sk);
    }

    DP_SET_ERRNO(ret);

    FD_Put(skFile);
    FD_Put(epFile);

    return ret == 0 ? 0 : -1;
}

static inline int EpEventsAvailable(Epoll_t *ep)
{
    return !LIST_IS_EMPTY(&ep->ready) || ep->ovflistActive;
}

int DP_EpollWait(int epfd, struct DP_EpollEvent* events, int maxevents, int timeout)
{
    Fd_t*        epFile;
    Epoll_t*     ep;
    int          ret;
    int          cnt = 0;
    EpollItemLists txlist;

    if (events == NULL) {
        DP_LOG_DBG("DP_EpollWait failed, event is NULL.");
        DP_SET_ERRNO(EFAULT);
        return -1;
    }

    if ((maxevents <= 0) || (maxevents > DP_EPOLL_MAX_NUM)) {
        DP_LOG_DBG("DP_EpollWait failed, invalid maxevents, maxevents = %d.", maxevents);
        DP_SET_ERRNO(EINVAL);
        return -1;
    }

    ret = GetEpollFromFd(epfd, &epFile, &ep);
    if (ret != 0) {
        DP_SET_ERRNO(-ret);
        return -1;
    }

    EpollLock(ep);
    bool eventAvail = EpEventsAvailable(ep);
    if (eventAvail) {
        EpollUnlock(ep);
        goto fetchEvents;
    } else if (timeout == 0) {
        EpollUnlock(ep);
        goto finish;
    } else { // 无事件，睡眠等待
        ret = Wait(ep, timeout);
        if (ret != 0) {
            ret = (ret == -ETIMEDOUT) ? 0 : ret; // 超时情况下返回0，不设置错误码
            EpollUnlock(ep);
            FD_Put(epFile);
            DP_SET_ERRNO(-ret);
            return (ret == 0) ? 0 : -1;
        }
        EpollUnlock(ep);
    }

fetchEvents:
    EpollCtlLock(ep);
    EpollLock(ep);

    // Steal phase: move ready list to txlist, activate ovflist
    LIST_INIT_HEAD(&txlist);
    LIST_CONCAT(&txlist, &ep->ready, node);
    ep->ovflist = NULL;
    ep->ovflistActive = 1;

    EpollUnlock(ep);

    // Transfer phase: process events without lock
    cnt = EpollGetEvents(ep, &txlist, events, maxevents);

    // Reinject phase: move ovflist and remaining txlist items back to ready list
    EpollLock(ep);

    // Reinject ovflist items
    EpollItem_t* ovflistItem;
    EpollItem_t* ovflistNext;
    for (ovflistItem = ep->ovflist; ovflistItem != NULL; ovflistItem = ovflistNext) {
        ovflistNext = ovflistItem->ovflistNext;
        ovflistItem->ovflistNext = EP_UNACTIVE_PTR;
        if (!EpItemIsLinked(ovflistItem)) {
            LIST_INSERT_TAIL(&ep->ready, ovflistItem, node);
            ovflistItem->linked = 1;
        }
    }

    // Deactivate ovflist
    ep->ovflist = EP_UNACTIVE_PTR;
    ep->ovflistActive = 0;

    // Reinject remaining txlist items (LT items that were kept)
    LIST_CONCAT(&ep->ready, &txlist, node);

    EpollUnlock(ep);
    EpollCtlUnlock(ep);

finish:
    FD_Put(epFile);
    DP_SET_ERRNO(0);
    return cnt;
}

static void ListGetDetails(EpollItemLists* list, DP_EpollDetails_t* details, int len, int* cnt)
{
    if (LIST_IS_EMPTY(list)) {
        return;
    }
    EpollItem_t* item = LIST_FIRST(list);
    EpollItem_t* next;
    while (item != NULL && *cnt < INT_MAX) {
        next = LIST_NEXT(item, epiNode);
        if (*cnt < len) {
            details[*cnt].fd = (uint32_t)item->fd;
            details[*cnt].expectEvents = item->ev.events;
            details[*cnt].readyEvents = epItemPoll(item);
            details[*cnt].notifiedEvents = 0;
            details[*cnt].shoted = (!!(item->ev.events & DP_EPOLLONESHOT));
            details[*cnt].eventData.u64 = item->ev.data.u64;
        }
        (*cnt)++;
        item = next;
    }
}

static int EpollGetDetails(Epoll_t* ep, DP_EpollDetails_t* details, int max)
{
    int cnt = 0;
    ListGetDetails(&ep->epItemList, details, max, &cnt);
    return cnt;
}

int DP_GetEpollDetails(int epFd, DP_EpollDetails_t* details, int len, int *wid)
{
    if ((wid == NULL) || (len < 0) || (len != 0 && details == NULL)) {
        DP_LOG_DBG("param invalid.");
        DP_SET_ERRNO(EINVAL);
        return -1;
    }

    Fd_t* epFile;
    int ret = FD_Get(epFd, FD_TYPE_EPOLL, &epFile);
    if (ret != 0) {
        DP_LOG_DBG("get epoll fd failed, ret = %d.", ret);
        DP_SET_ERRNO(-ret);
        return -1;
    }

    Epoll_t* ep = (Epoll_t *)(epFile->priv);
    if (ep == NULL) {
        DP_LOG_DBG("ep is NULL.");
        FD_Put(epFile);
        DP_SET_ERRNO(EBADF);
        return -1;
    }

    EpollCtlLock(ep);
    EpollLock(ep);
    *wid = ep->wid;
    int cnt = EpollGetDetails(ep, details, len);
    EpollUnlock(ep);
    EpollCtlUnlock(ep);

    FD_Put(epFile);
    DP_SET_ERRNO(0);
    return cnt;
}