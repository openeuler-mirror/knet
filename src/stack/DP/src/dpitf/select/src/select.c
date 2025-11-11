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

#include "dp_select.h"
#include "dp_errno.h"

#include "fd.h"
#include "sock.h"
#include "shm.h"
#include "utils_log.h"
#include "utils_base.h"
#include "utils_debug.h"
#include "utils_spinlock.h"

#define FD_OFFSET_BIT       5
#define ENDLESS_WATING_TIME (-1)

static int FdsetGetOff(int fd, int* off, int* shift)
{
    int fdIdx = FD_GetRealFd(fd);
    if ((fdIdx < 0) || (fdIdx >= DP_FD_SETSIZE)) {
        return -1;
    }

    uint32_t tempFd = (uint32_t)fdIdx;
    *off            = (int)(tempFd >> FD_OFFSET_BIT);
    *shift          = (int)(tempFd & 0x1f);
    return 0;
}

void DP_FdClr(int fd, DP_FdSet_t* set)
{
    int off;
    int shift;

    if (set == NULL) {
        return;
    }

    if (FdsetGetOff(fd, &off, &shift) != 0) {
        return;
    }

    set->fdmask[off] &= ~((uint32_t)1 << shift);
}

int DP_FdIsSet(int fd, DP_FdSet_t* set)
{
    if (set == NULL) {
        return 0;
    }

    int off;
    int shift;

    if (FdsetGetOff(fd, &off, &shift) != 0) {
        return 0;
    }

    return ((set->fdmask[off] & ((uint32_t)1 << shift)) != 0) ? 1 : 0;
}

void DP_FdSet(int fd, DP_FdSet_t* set)
{
    if (set == NULL) {
        return;
    }

    int off;
    int shift;

    if (FdsetGetOff(fd, &off, &shift) != 0) {
        return;
    }

    set->fdmask[off] |= ((uint32_t)1 << shift);
}

void DP_FdZero(DP_FdSet_t* set)
{
    if (set == NULL) {
        return;
    }

    (void)memset_s(set, sizeof(*set), 0, sizeof(*set));
}

typedef struct {
    DP_Sem_t   sem;
    Spinlock_t   lock;

    int        nfds;
    DP_FdSet_t readfds;
    DP_FdSet_t writefds;
    DP_FdSet_t exceptfds;

    DP_FdSet_t outReadfds;
    DP_FdSet_t outWritefds;
    DP_FdSet_t outExceptfds;

    int readyFds;
} SelectCtx_t;

static inline void CopyFdsFromUser(
    SelectCtx_t* ctx, DP_FdSet_t* readfds, DP_FdSet_t* writefds, DP_FdSet_t* exceptfds)
{
    if (readfds != NULL) {
        ctx->readfds = *readfds;
    }
    if (writefds != NULL) {
        ctx->writefds = *writefds;
    }

    if (exceptfds != NULL) {
        ctx->exceptfds = *exceptfds;
    }
}

static inline void CopyFdsToUser(
    SelectCtx_t* ctx, DP_FdSet_t* readfds, DP_FdSet_t* writefds, DP_FdSet_t* exceptfds)
{
    if (readfds != NULL) {
        *readfds = ctx->outReadfds;
    }
    if (writefds != NULL) {
        *writefds = ctx->outWritefds;
    }

    if (exceptfds != NULL) {
        *exceptfds = ctx->outExceptfds;
    }
}

static SelectCtx_t* AllocSelectCtx(void)
{
    SelectCtx_t* ctx;
    size_t       allocSize;

    allocSize = sizeof(SelectCtx_t) + SEM_Size;

    ctx = SHM_MALLOC(allocSize, MOD_SELECT, DP_MEM_FREE);
    if (ctx == NULL) {
        DP_LOG_ERR("Malloc memory failed for select.");
        return NULL;
    }
    (void)memset_s(ctx, allocSize, 0, allocSize);

    ctx->sem  = (DP_Sem_t)(ctx + 1);

    return ctx;
}

static int InitSelectCtx(SelectCtx_t* ctx)
{
    if (SEM_INIT(ctx->sem) != 0) {
        return -1;
    }

    if (SPINLOCK_Init(&ctx->lock) != 0) {
        SEM_DEINIT(ctx->sem);
        return -1;
    }

    ctx->readyFds = 0;
    return 0;
}

static int CreateSelectCtx(
    int nfds, DP_FdSet_t* readfds, DP_FdSet_t* writefds, DP_FdSet_t* exceptfds, SelectCtx_t** out)
{
    SelectCtx_t* ctx = AllocSelectCtx();
    if (ctx == NULL) {
        DP_SET_ERRNO(ENOMEM);
        return -1;
    }
    if (InitSelectCtx(ctx) != 0) {
        SHM_FREE(ctx, DP_MEM_FREE);
        DP_SET_ERRNO(EFAULT);
        return -1;
    }
    ctx->nfds = nfds;
    CopyFdsFromUser(ctx, readfds, writefds, exceptfds);

    *out = ctx;
    return 0;
}

static void DestroySelectCtx(SelectCtx_t* ctx)
{
    SPINLOCK_Deinit(&ctx->lock);
    SEM_DEINIT(ctx->sem);
    SHM_FREE(ctx, DP_MEM_FREE);
}

static inline int Timeval2Ms(struct DP_Timeval* timeout)
{
    int ms = (int)(timeout->tv_sec * MSEC_PER_SEC + timeout->tv_usec / USEC_PER_MSEC);
    if (ms < 0) {
        return -1;
    }
    return ms;
}

static bool FdIsMonitored(int fd, SelectCtx_t* ctx)
{
    int idx = FD_GetRealFd(fd);
    if ((DP_FdIsSet(idx, &ctx->readfds) != 0) || (DP_FdIsSet(idx, &ctx->writefds) != 0) ||
        (DP_FdIsSet(idx, &ctx->exceptfds) != 0)) {
        return true;
    }
    return false;
}

static int SetEvents(int fd, SelectCtx_t* ctx, uint8_t state)
{
    int events = 0;
    int idx    = FD_GetRealFd(fd);

    SPINLOCK_Lock(&ctx->lock);

    if ((DP_FdIsSet(idx, &ctx->readfds) != 0) && ((state & SOCK_STATE_READ) != 0)) {
        DP_FdSet(idx, &ctx->outReadfds);
        events++;
    }

    if ((DP_FdIsSet(idx, &ctx->writefds) != 0) && ((state & SOCK_STATE_WRITE) != 0)) {
        DP_FdSet(idx, &ctx->outWritefds);
        events++;
    }

    if ((DP_FdIsSet(idx, &ctx->exceptfds) != 0) && ((state & SOCK_STATE_EXCEPTION) != 0)) {
        DP_FdSet(idx, &ctx->outExceptfds);
        events++;
    }

    if (events != 0) {
        ctx->readyFds++;
    }

    SPINLOCK_Unlock(&ctx->lock);

    return events;
}

static void ClrFdSet(int fd, SelectCtx_t* ctx)
{
    int idx = FD_GetRealFd(fd);

    SPINLOCK_Lock(&ctx->lock);

    if (DP_FdIsSet(idx, &ctx->readfds) != 0) {
        DP_FdClr(idx, &ctx->readfds);
    }

    if (DP_FdIsSet(idx, &ctx->writefds) != 0) {
        DP_FdClr(idx, &ctx->writefds);
    }

    if (DP_FdIsSet(idx, &ctx->exceptfds) != 0) {
        DP_FdClr(idx, &ctx->exceptfds);
    }

    SPINLOCK_Unlock(&ctx->lock);
}

void SELECT_Notify(Sock_t* sk, SelectCtx_t* ctx, uint8_t oldState, uint8_t newState)
{
    ASSERT(ctx != NULL);
    (void)oldState;

    SelectCtx_t* selectCtx = ctx;
    int          fd        = sk->associateFd;

    if ((newState & SOCK_STATE_CLOSE) != 0) {
        ClrFdSet(fd, selectCtx);
    } else if (SetEvents(fd, selectCtx, newState) == 0) {
        return;
    }

    SEM_SIGNAL(selectCtx->sem);
}

static int EnableNotify(int fd, SelectCtx_t* ctx)
{
    Sock_t* sk;
    Fd_t*   skFile;
    int     ret;

    if ((ret = FD_Get(fd, FD_TYPE_SOCKET, &skFile)) != 0) {
        DP_SET_ERRNO(-ret);
        return -1;
    }

    sk = (Sock_t*)skFile->priv;

    SOCK_Lock(sk);

    if (SetEvents(fd, ctx, sk->state) == 0) {
        // 还没有事件
        SOCK_EnableNotify(sk, SOCK_NOTIFY_TYPE_SELECT, ctx, fd);
    }

    SOCK_Unlock(sk);

    FD_Put(skFile);

    return 0;
}

static void DisableNotify(int fd)
{
    Sock_t* sk;
    Fd_t*   skFile;
    int     ret;

    if ((ret = FD_Get(fd, FD_TYPE_SOCKET, &skFile)) != 0) {
        // fd 可能已经关闭，不做处理
        return;
    }

    sk = (Sock_t*)skFile->priv;

    SOCK_DisableNotifySafe(sk);

    FD_Put(skFile);
}

static void FdsDisableNotify(SelectCtx_t* ctx, int nfds)
{
    for (int i = FD_GetFdOffset(); i < nfds; i++) {
        if (!FdIsMonitored(i, ctx)) {
            continue;
        }
        DisableNotify(i);
    }
}

static int FdsEnableNotify(SelectCtx_t* ctx)
{
    int i;
    int last;

    for (i = FD_GetFdOffset(), last = FD_GetFdOffset(); i < ctx->nfds; i++, last++) {
        if (!FdIsMonitored(i, ctx)) {
            continue;
        }
        if (EnableNotify(i, ctx) != 0) {
            goto err;
        }
    }
    return 0;

err:
    FdsDisableNotify(ctx, last);
    return -1;
}

static int Wait(SelectCtx_t* ctx, int timeout)
{
    int ret;

    if (timeout == 0) {
        return 0;
    }

    SPINLOCK_Lock(&ctx->lock);

    if (ctx->readyFds > 0) {
        SPINLOCK_Unlock(&ctx->lock);
        return 0;
    }
    SPINLOCK_Unlock(&ctx->lock);

    ret = (int)SEM_WAIT(ctx->sem, timeout);
    if (ret != 0 && ret != ETIMEDOUT) {
        DP_SET_ERRNO(ret);
        return -1;
    }
    return 0;
}

int DP_Select(
    int nfds, DP_FdSet_t* readfds, DP_FdSet_t* writefds, DP_FdSet_t* exceptfds, struct DP_Timeval* timeout)
{
    SelectCtx_t* ctx;
    int waitMs = ENDLESS_WATING_TIME;
    int ret;
    int realNfds = FD_GetRealFd(nfds);
    if ((realNfds <= 0) || (realNfds > FD_GetFileLimit())) {
        DP_SET_ERRNO(EINVAL);
        return -1;
    }

    if (timeout != NULL) {
        waitMs = Timeval2Ms(timeout);
        if (waitMs < 0) {
            DP_SET_ERRNO(EINVAL);
            return -1;
        }
    }

    if (CreateSelectCtx(nfds, readfds, writefds, exceptfds, &ctx) != 0) {
        return -1;
    }

    if (FdsEnableNotify(ctx) != 0) {
        ret = -1;
        goto out;
    }

    ret = Wait(ctx, waitMs);
    FdsDisableNotify(ctx, ctx->nfds);
    if (ret == 0) {
        CopyFdsToUser(ctx, readfds, writefds, exceptfds);
        ret = ctx->readyFds;
    }

out:
    DestroySelectCtx(ctx);
    return ret;
}
