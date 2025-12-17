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
} PollCtx_t;

static inline int CopyFdsFromUser(PollCtx_t* ctx, struct DP_Pollfd* fds, DP_Nfds_t nfds)
{
    if (memcpy_s(ctx->fds, ctx->nfds * sizeof(struct DP_Pollfd), fds, nfds * sizeof(struct DP_Pollfd)) != 0) {
        return -1;
    }
    return 0;
}

static inline int CopyFdsToUser(PollCtx_t* ctx, struct DP_Pollfd* fds, DP_Nfds_t nfds)
{
    if (memcpy_s(fds, nfds * sizeof(struct DP_Pollfd), ctx->fds, ctx->nfds * sizeof(struct DP_Pollfd)) != 0) {
        return -1;
    }
    return 0;
}

static PollCtx_t* AllocPollCtx(DP_Nfds_t nfds)
{
    size_t     allocSize;
    PollCtx_t* ctx = NULL;
    allocSize      = sizeof(PollCtx_t) + sizeof(struct DP_Pollfd) * nfds + SEM_Size;

    ctx = SHM_MALLOC(allocSize, MOD_POLL, DP_MEM_FREE);
    if (ctx == NULL) {
        DP_LOG_ERR("Malloc memory failed for poll.");
        return NULL;
    }
    (void)memset_s(ctx, allocSize, 0, allocSize);

    ctx->sem  = (DP_Sem_t)(ctx + 1);
    ctx->fds  = (struct DP_Pollfd*)((uint8_t*)ctx->sem + SEM_Size);
    ctx->nfds = nfds;

    return ctx;
}

static int InitPollCtx(PollCtx_t* ctx, struct DP_Pollfd* fds, DP_Nfds_t nfds)
{
    if (SEM_INIT(ctx->sem) != 0) {
        return -EINTR;
    }

    SPINLOCK_Init(&ctx->lock);

    if (CopyFdsFromUser(ctx, fds, nfds) != 0) {
        DP_LOG_ERR("InitPollCtx failed, copy err.");
        SPINLOCK_Deinit(&ctx->lock);
        SEM_DEINIT(ctx->sem);
        return -ENOMEM;
    }
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
        SHM_FREE(ctx, DP_MEM_FREE);
        return -1;
    }

    *out = ctx;
    return 0;
}

static void DestroyPollCtx(PollCtx_t* ctx)
{
    SPINLOCK_Deinit(&ctx->lock);
    SEM_DEINIT(ctx->sem);
    SHM_FREE(ctx, DP_MEM_FREE);
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

void POLL_Notify(Sock_t* sk, void* ctx, uint8_t oldState, uint8_t newState, uint8_t event)
{
    ASSERT(ctx != NULL);
    (void)oldState;
    (void)event;

    PollCtx_t* pollCtx = ctx;
    int        fd      = sk->associateFd;

    SPINLOCK_Lock(&pollCtx->lock);

    for (DP_Nfds_t i = 0; i < pollCtx->nfds; i++) {
        struct DP_Pollfd* pollFd = &pollCtx->fds[i];
        if (pollFd->fd == fd) {
            if ((newState & SOCK_STATE_CLOSE) != 0) {
                pollFd->revents = (short)((unsigned short)pollFd->revents | DP_POLLRDHUP);
            } else {
                SetRevents(pollFd, newState);
            }
            if (pollFd->revents != 0) {
                pollCtx->readyFds++;
            }
        }
    }

    if (pollCtx->readyFds != 0) {
        SPINLOCK_Unlock(&pollCtx->lock);
        SEM_SIGNAL(pollCtx->sem);
    } else {
        SPINLOCK_Unlock(&pollCtx->lock);
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
        SOCK_EnableNotify(sk, SOCK_NOTIFY_TYPE_POLL, ctx, pollFd->fd);
    }

    SOCK_Unlock(sk);

    FD_Put(skFile);

    return 0;
}

static int DisableNotify(struct DP_Pollfd* pollFd)
{
    Sock_t* sk;
    Fd_t*   skFile;
    int     ret;

    if ((ret = FD_Get(pollFd->fd, FD_TYPE_SOCKET, &skFile)) != 0) {
        // fd 可能已经关闭，不做处理
        return 0;
    }

    sk = (Sock_t*)skFile->priv;

    SOCK_DisableNotifySafe(sk);

    FD_Put(skFile);

    return 0;
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

    if (ret != 0) {
        DP_LOG_DBG("DP_Poll failed by wait err, errno = %d.", errno);
        goto out;
    }

    ret = CopyFdsToUser(ctx, fds, nfds);
    if (ret == 0) {
        ret = ctx->readyFds;
    }

out:
    DestroyPollCtx(ctx);
    return ret;
}
