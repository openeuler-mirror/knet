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
#include <securec.h>

#include "dp_poll.h"
#include "dp_errno.h"

#include "dp_fd.h"
#include "sock.h"
#include "shm.h"
#include "utils_log.h"
#include "utils_base.h"
#include "utils_debug.h"
#include "utils_spinlock.h"
#include "utils_statistic.h"
#include "worker.h"

typedef struct {
    DP_Sem_t        sem;
    Spinlock_t        lock;
    struct DP_Pollfd* fds;
    DP_Nfds_t         nfds;
    int               readyFds;
    DP_PollNotify_t  userNotify;
} PollCtx_t;

static PollCtx_t* AllocPollCtx(DP_Nfds_t nfds)
{
    size_t     allocSize;
    PollCtx_t* ctx = NULL;
    allocSize      = sizeof(PollCtx_t) + SEM_Size;

    ctx = OS_MALLOC(allocSize);
    if (ctx == NULL) {
        DP_LOG_ERR("Malloc memory failed for poll.");
        return NULL;
    }
    (void)memset_s(ctx, allocSize, 0, allocSize);

    ctx->sem  = (DP_Sem_t)(ctx + 1);
    ctx->nfds = nfds;

    return ctx;
}

static int InitPollCtx(PollCtx_t* ctx, struct DP_Pollfd* fds, DP_Nfds_t nfds)
{
    if (SEM_INIT(ctx->sem) != 0) {
        return -EINTR;
    }

    SPINLOCK_Init(&ctx->lock);

    ctx->fds     = fds;
    ctx->nfds    = nfds;
    ctx->readyFds = 0;
    return 0;
}

static int CreatePollCtx(struct DP_Pollfd* fds, DP_Nfds_t nfds, PollCtx_t** out)
{
    PollCtx_t* ctx = AllocPollCtx(nfds);
    if (ctx == NULL) {
        DP_SET_ERRNO(ENOMEM);
        return -1;
    }

    int ret = InitPollCtx(ctx, fds, nfds);
    if (ret != 0) {
        DP_SET_ERRNO(-ret);
        OS_FREE(ctx);
        return -1;
    }

    *out = ctx;
    return 0;
}

static void DestroyPollCtx(PollCtx_t* ctx)
{
    SPINLOCK_Deinit(&ctx->lock);
    SEM_DEINIT(ctx->sem);
    OS_FREE(ctx);
}

static inline uint8_t GetExpectState(struct DP_Pollfd* pollFd)
{
    uint8_t state = 0;

    if (((unsigned short)pollFd->events & DP_POLLIN) != 0) {
        state |= SOCK_STATE_READ;
    }
    if (((unsigned short)pollFd->events & DP_POLLOUT) != 0) {
        state |= SOCK_STATE_WRITE;
    }
    if (((unsigned short)pollFd->events & DP_POLLRDHUP) != 0) {
        state |= SOCK_STATE_CANTRCVMORE;
    }

    return state;
}

static inline int SetRevents(struct DP_Pollfd* pollFd, uint8_t state)
{
    uint8_t expectState = GetExpectState(pollFd);
    uint8_t readyState  = state & expectState;

    if ((readyState & SOCK_STATE_READ) != 0) {
        pollFd->revents = (short)((unsigned short)pollFd->revents | DP_POLLIN);
    }

    if ((readyState & SOCK_STATE_WRITE) != 0) {
        pollFd->revents = (short)((unsigned short)pollFd->revents | DP_POLLOUT);
    }

    if ((readyState & SOCK_STATE_CANTRCVMORE) != 0) {
        pollFd->revents = (short)((unsigned short)pollFd->revents | DP_POLLRDHUP);
    }

    if ((state & SOCK_STATE_CANTRCVMORE) != 0 && (state & SOCK_STATE_CANTSENDMORE) != 0) {
        pollFd->revents = (short)((unsigned short)pollFd->revents | DP_POLLHUP);
    }

    if ((state & SOCK_STATE_EXCEPTION) != 0) {
        pollFd->revents = (short)((unsigned short)pollFd->revents | DP_POLLERR);
    }

    return pollFd->revents;
}

void POLL_Notify(Sock_t* sk, void* ctx, uint8_t oldState, uint8_t newState, uint8_t event, uint64_t associateFd)
{
    ASSERT(ctx != NULL);
    (void)sk;
    (void)oldState;
    (void)event;

    PollCtx_t* pollCtx = (PollCtx_t*) ctx;

    SPINLOCK_Lock(&pollCtx->lock);

    struct DP_Pollfd* pollFd = (struct DP_Pollfd*)(uintptr_t) associateFd;
    if ((newState & SOCK_STATE_CLOSE) != 0) {
        pollFd->revents = (short)((unsigned short)pollFd->revents | DP_POLLRDHUP);
    } else {
        SetRevents(pollFd, newState);
    }
    if (pollFd->revents != 0) {
        pollCtx->readyFds++;
    }
    
    SPINLOCK_Unlock(&pollCtx->lock);

    if (pollCtx->readyFds != 0) {
        if (pollCtx->userNotify.fn != NULL) {
            pollCtx->userNotify.fn(pollCtx->userNotify.data);
        } else {
            SEM_SIGNAL(pollCtx->sem);
        }
    }
}

static int EnableNotify(struct DP_Pollfd* pollFd, PollCtx_t* ctx)
{
    Sock_t* sk;
    Fd_t*   skFile;
    int     ret;

    if ((ret = FD_Get(pollFd->fd, FD_TYPE_SOCKET, &skFile)) != 0) {
        DP_LOG_DBG("EnableNotify failed, get socket fd failed.");
        DP_SET_ERRNO(EFAULT);
        return -1;
    }

    sk = (Sock_t*)skFile->priv;

    SOCK_Lock(sk);

    if (SetRevents(pollFd, sk->state) == 0) {
        // 还没有事件
        if (SOCK_EnableNotify(sk, SOCK_NOTIFY_TYPE_POLL, ctx, (uint64_t)(uintptr_t)pollFd) != 0) {
            DP_LOG_DBG("EnableNotify failed.");
            SOCK_Unlock(sk);
            FD_Put(skFile);
            return -1;
        }
    }

    SOCK_Unlock(sk);

    FD_Put(skFile);

    return 0;
}

static void DisableNotifySafe(Sock_t *sk, struct DP_Pollfd *pollFd)
{
    SOCK_Lock(sk);
    SockNotify_t *notify = NULL;
    SockNotify_t *next = NULL;
    for (notify = LIST_FIRST(&sk->notifyList); notify != NULL; notify = next) {
        next = LIST_NEXT(notify, node);
        if (notify->notifyType == SOCK_NOTIFY_TYPE_POLL && notify->associateFd == (uint64_t)(uintptr_t)pollFd) {
            LIST_REMOVE(&sk->notifyList, notify, node);
            OS_FREE(notify);
            break;
        }
    }
    SOCK_Unlock(sk);
}

static void DisableNotify(struct DP_Pollfd *pollFd)
{
    Sock_t* sk;
    Fd_t*   skFile;
    int     ret;

    if ((ret = FD_Get(pollFd->fd, FD_TYPE_SOCKET, &skFile)) != 0) {
        // fd 可能已经关闭，不做处理
        return;
    }

    sk = (Sock_t*)skFile->priv;

    DisableNotifySafe(sk, pollFd);

    FD_Put(skFile);
}

static void FdsDisableNotify(PollCtx_t* ctx, DP_Nfds_t nfds)
{
    for (DP_Nfds_t i = 0; i < nfds; i++) {
        DisableNotify(&ctx->fds[i]);
    }
}

static int FdsEnableNotify(PollCtx_t* ctx)
{
    DP_Nfds_t         i;
    DP_Nfds_t         last;
    int               readyFds = 0;
    struct DP_Pollfd* pollFd;

    for (i = 0, last = 0; i < ctx->nfds; i++, last++) {
        pollFd          = &ctx->fds[i];
        pollFd->revents = 0;

        if (pollFd->fd < 0) {
            continue;
        }

        if (EnableNotify(pollFd, ctx) != 0) {
            goto err;
        }

        if (pollFd->revents != 0) {
            readyFds++;
        }
    }

    if (readyFds == 0) {
        return 0;
    }

    SPINLOCK_Lock(&ctx->lock);

    ctx->readyFds += readyFds;

    SPINLOCK_Unlock(&ctx->lock);

    return 0;

err:
    FdsDisableNotify(ctx, last);
    return -1;
}

static int Wait(PollCtx_t* ctx, int timeout)
{
    int ret;

    SPINLOCK_Lock(&ctx->lock);

    if (ctx->readyFds > 0) {
        SPINLOCK_Unlock(&ctx->lock);
        return 0;
    }
    SPINLOCK_Unlock(&ctx->lock);

    ret = (int)SEM_WAIT(ctx->sem, timeout);
    if (ret == DP_ERR) {
        DP_SET_ERRNO(EFAULT);
        return -1;
    }
    if (ret != 0 && ret != ETIMEDOUT) {
        DP_SET_ERRNO(ret);
        return -1;
    }
    return 0;
}

static int PollFd(struct DP_Pollfd* pollFd, int32_t wid)
{
    pollFd->revents = 0;
    Sock_t* sk;
    Fd_t*   skFile;
    int     ret;
    if ((ret = FD_Get(pollFd->fd, FD_TYPE_SOCKET, &skFile)) != 0) {
        DP_LOG_DBG("PollFd failed, get socket fd failed.");
        DP_SET_ERRNO(EFAULT);
        return -1;
    }
    sk = (Sock_t*)skFile->priv;

    if (CFG_GET_VAL(DP_CFG_DEPLOYMENT) == DP_DEPLOYMENT_CO_THREAD) {
        if (sk->wid != wid) {
            FD_Put(skFile);
            DP_SET_ERRNO(EBADF);
            DP_LOG_DBG("poll fd fail, get wid error");
            DP_ADD_ABN_STAT(DP_WORKER_MISS_MATCH);
            return -1;
        }
    }

    // 读取 sk 状态，不需要加锁
    SetRevents(pollFd, sk->state);

    FD_Put(skFile);

    return 0;
}

static int PollOnce(struct DP_Pollfd* fds, DP_Nfds_t nfds)
{
    DP_Nfds_t i;
    int readys = 0;

    int32_t wid = -1;
    if (CFG_GET_VAL(DP_CFG_DEPLOYMENT) == DP_DEPLOYMENT_CO_THREAD) {
        wid = WORKER_GetSelfId();
        if (wid < 0) {
            DP_SET_ERRNO(EFAULT);
            DP_LOG_DBG("poll once fail, get wid error");
            DP_ADD_ABN_STAT(DP_WORKER_MISS_MATCH);
            return -1;
        }
    }

    for (i = 0; i < nfds; i++) {
        if (fds[i].fd < 0) {
            fds[i].revents = 0;
            continue;
        }
        if (PollFd(&fds[i], wid) < 0) {
            return -1;
        }
        if (fds[i].revents != 0) {
            readys++;
        }
    }

    return readys;
}

int DP_Poll(struct DP_Pollfd* fds, DP_Nfds_t nfds, int timeout)
{
    PollCtx_t* ctx;
    int        ret = -1;

    if (fds == NULL) {
        DP_LOG_DBG("Poll failed, invalid parameter.");
        DP_SET_ERRNO(EFAULT);
        return -1;
    }

    if (nfds > (DP_Nfds_t)FD_GetFileLimit()) {
        DP_LOG_DBG("Poll failed, invalid parameter.");
        DP_SET_ERRNO(EINVAL);
        return -1;
    }

    if (timeout == 0) {
        // 只是查询 fd 的状态
        return PollOnce(fds, nfds);
    }

    if (CreatePollCtx(fds, nfds, &ctx) != 0) {
        return -1;
    }

    if (FdsEnableNotify(ctx) != 0) {
        goto out;
    }

    ret = Wait(ctx, timeout);
    FdsDisableNotify(ctx, ctx->nfds);
    if (errno == ETIMEDOUT) { // SEM_WAIT信号超时有errno，但是poll超时errno为0，所以需要恢复为0
        DP_SET_ERRNO(0);
    }

    if (ret != 0) {
        DP_LOG_DBG("DP_Poll failed by wait err, errno = %d.", errno);
        goto out;
    }

    ret = ctx->readyFds;

out:
    DestroyPollCtx(ctx);
    return ret;
}

int DP_PollCreateNotify(struct DP_Pollfd* fds, DP_Nfds_t nfds, DP_PollNotify_t* notify, void** pollCtx)
{
    PollCtx_t *ctx;
    if (fds == NULL || notify == NULL || notify->fn == NULL || pollCtx == NULL) {
        DP_SET_ERRNO(EINVAL);
        return -1;
    }

    if (nfds > (DP_Nfds_t)FD_GetFileLimit()) {
        DP_LOG_DBG("Poll create notify failed, invalid parameter, nfds %d exceeds file limit %d.", nfds, FD_GetFileLimit());
        DP_SET_ERRNO(EINVAL);
        return -1;
    }

    if (CreatePollCtx(fds, nfds, &ctx) != 0) {
        return -1;
    }

    ctx->userNotify = *notify;

    if (FdsEnableNotify(ctx) != 0) {
        DestroyPollCtx(ctx);
        return -1;
    }

    /* 注册过程中若已有 fd 就绪，立即触发一次回调，避免调用方无谓阻塞 */
    SPINLOCK_Lock(&ctx->lock);
    int ready = ctx->readyFds;
    DP_PollNotifyFn_t notifyFn = ctx->userNotify.fn;
    void* notifyData = ctx->userNotify.data;
    SPINLOCK_Unlock(&ctx->lock);
    if (ready > 0 && notifyFn != NULL) {
        notifyFn(notifyData); // todo: 后续优化为返回值告知KNET，不要通过notifyFn。若已经有就绪的fd，则直接DP_Poll。
    }

    *pollCtx = ctx;
    return 0;
}

void DP_PollDestroyNotify(void* pollCtx)
{
    if (pollCtx == NULL) {
        return;
    }
    PollCtx_t* ctx = (PollCtx_t*)pollCtx;
    FdsDisableNotify(ctx, ctx->nfds);
    DestroyPollCtx(ctx);
}