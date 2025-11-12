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

#include "shm.h"
#include "worker.h"
#include "sock.h"
#include "pmgr.h"
#include "utils_base.h"
#include "utils_log.h"
#include "utils_cfg.h"
#include "utils_debug.h"
#include "utils_spinlock.h"

#include "tcp_types.h"
#include "tcp_out.h"
#include "tcp_timer.h"
#include "tcp_sock.h"
#include "tcp_tsq.h"

TcpTsq_t** g_tsq;

static size_t TcpTsqGetObjSize(void)
{
    return sizeof(TcpTsq_t);
}

static int TcpTsqInitMem(TcpTsq_t* tsq)
{
    tsq->txEvCnt = 0;

    LIST_INIT_HEAD(&tsq->tsqQue);
    LIST_INIT_HEAD(&tsq->tsqLockQue);

    PBUF_ChainInit(&tsq->backlog);
    (void)SPINLOCK_Init(&tsq->backLogLock);

    return SPINLOCK_Init(&tsq->lock);
}

static void TcpTsqDeinitTsqs(TcpTsq_t** tsqs, int cnt)
{
    for (int i = 0; i < cnt; i++) {
        SPINLOCK_Deinit(&tsqs[i]->lock);
        SPINLOCK_Deinit(&tsqs[i]->backLogLock);
    }
}

/*
处理socket层发送的事件：
1. 优先处理close
    触发disconnect
        syn_send: 关闭，后续靠rst相应，释放socket，并返回-1
        syn_recv: 关闭，后续靠rst相应，释放socket，并返回-1
        establish:
            lingerOn:
                lingerTime = 0，发送rst，释放socket，并返回-1
                lingerTime != 0, 待确认
            lingerOff：
                发送FIN
        不会存在其他状态
        返回0
2. 处理send, concat sk.sndBuf到tcp.sndQue
3. 处理connect，发送syn
4. 处理keepalive，空间keepalive启动/停止
5. 处理ack发送，窗口更新或者收取数据为空
*/

static int TcpTsqProcEvSlow(Sock_t* sk, TcpSk_t* tcp, uint32_t* tf)
{
    uint32_t tsqFlags = *tf;

    if ((tsqFlags & TCP_TSQ_DISCONNECT) != 0) {
        if ((tsqFlags & TCP_TSQ_CONNECT) != 0) {
            TcpCleanUp(tcp);
            TcpSetState(tcp, TCP_CLOSED);
            DP_INC_TCP_STAT(tcp->wid, DP_TCP_CLOSED);
            TcpFreeSk(sk);
            return -1;
        }

        // 如果处理过RST报文相关资源已经被释放过了，这里直接释放sk即可
        if (tcp->state == TCP_CLOSED) {
            TcpFreeSk(sk);
            return -1;
        }

        if (TcpDisconnect(sk) != 0) {
            return -1;
        }

        if (tcp->sndQue.bufLen == 0) {
            *tf &= ~TCP_TSQ_SEND;
        }
    }

    if ((tsqFlags & TCP_TSQ_CONNECT) != 0) {
        TcpDoConnecting(sk);
    }

    return 0;
}

static void TcpTsqProcNolockEv(Sock_t* sk, TcpSk_t* tcp, uint32_t tf)
{
    uint32_t tsqFlags = tf;
    if ((tsqFlags & TCP_TSQ_EV_SLOW) != 0) {
        if (TcpTsqProcEvSlow(sk, tcp, &tsqFlags) != 0) {
            return;
        }
    }

    if ((tsqFlags & TCP_TSQ_SEND_WND_UP) != 0) {
        // 避免重复发送 ACK 报文
        if (tcp->rcvNxt != tcp->rcvWup || tcp->rcvWnd != TcpGetRcvSpace(tcp)) {
            tsqFlags |= TCP_TSQ_SEND_FORCE;
        }
    }

    if (((tsqFlags & TCP_TSQ_SEND) != 0) && (tcp->state != TCP_CLOSED)) {
        TcpXmitData(tcp, (tsqFlags & TCP_TSQ_SEND_FORCE) != 0 ? 1 : 0);
    }

    if ((tsqFlags & TCP_TSQ_KEEPALIVE_OFF) != 0) {
        // 只在Established状态下更改保活定时器状态
        if (TCP_SHOULD_DEACTIVE_KEEP(tcp)) {
            TcpDeactiveKeepTimer(tcp);
        }
    }

    if ((tsqFlags & TCP_TSQ_KEEPALIVE_ON) != 0) {
        // 只在Established状态下更改保活定时器状态
        if (TCP_SHOULD_ACTIVE_KEEP(tcp)) {
            TcpActiveKeepTimer(tcp);
        }
    }
}

static int TcpTsqProcSkEv(Sock_t* sk, uint32_t tsqFlags)
{
    if ((tsqFlags & TCP_TSQ_RECV_FIN) != 0) {
        SOCK_CLR_RECV_MORE(sk);
        SOCK_WakeupRdSem(sk);
        SOCK_SetState(sk, SOCK_STATE_READ | SOCK_STATE_WRITE | SOCK_STATE_CANTRCVMORE);
    }

    if ((tsqFlags & TCP_TSQ_ABORT) != 0) {
        TcpXmitRstAckPkt(TcpSK(sk));
    }

    if ((tsqFlags & TCP_TSQ_RECV_UNEXPECT) != 0) {
        SOCK_CLR_RECV_MORE(sk);
        SOCK_CLR_SEND_MORE(sk);
        SOCK_SET_CONN_REFUSED(sk);

        SOCK_WakeupRdSem(sk);
        SOCK_WakeupWrSem(sk);
        SOCK_SetState(sk, SOCK_STATE_READ | SOCK_STATE_WRITE | SOCK_STATE_EXCEPTION |
                      SOCK_STATE_CANTRCVMORE | SOCK_STATE_CANTSENDMORE);
        TcpSk_t *tcp = TcpSK(sk);
        TcpCleanUp(tcp);
        // 用户调用close同时也接收了RST报文，这时可以直接释放
        if (SOCK_IS_CLOSED(sk) || tcp->parent != NULL) {
            if (tcp->parent != NULL) {
                TcpRemoveFromParentList(tcp);
            }
            SOCK_Unlock(sk);
            TcpFreeSk(sk);
            return -1;
        }
        TcpSetState(tcp, TCP_CLOSED);
        // 无论是收到RST报文或者是定时器超时这两种情况下都无需继续去处理剩下的事件
        SOCK_Unlock(sk);
        return -1;
    }

    if ((tsqFlags & TCP_TSQ_CONNECTED) != 0) {
        SOCK_SET_RECV_MORE(sk);
        SOCK_SET_SEND_MORE(sk);
        SOCK_SET_CONNECTED(sk);

        SOCK_WakeupWrSem(sk);
        SOCK_SetState(sk, SOCK_STATE_WRITE);
    }

    return 0;
}

static bool TcpIsSndQueNeedConcat(Sock_t* sk)
{
    if (sk->sndBuf.bufLen <= 0) {
        return false;
    }

    /* 当可发送窗口比tcp发送队列长度还小时，不迁移pbuf，等用户下一次发送数据或者收到对端确认数据后再迁移。
     * 这样处理的目的是在用户发送小数据并发生拥塞的场景可以尽量利用每个pbuf的空间，并且避免拥塞解除后发送的
     * pbuf链长度太长 */
    if (TcpCalcFreeWndSize(TcpSK(sk)) < TcpSK(sk)->sndQue.bufLen) {
        return false;
    }

    /* 可发送窗口足够大，但是数据长度小时cork选项和nagle算法都可能导致数据不会立即被发送，此时也不迁移pbuf */
    if (!TcpCanSendPbuf(TcpSK(sk), (uint32_t)(sk->sndBuf.bufLen + TcpSK(sk)->sndQue.bufLen), TcpSK(sk)->mss, 0)) {
        return false;
    }

    return true;
}

static int TcpProcSkEv(Sock_t* sk, uint32_t tsqFlags)
{
    if (TcpIsSndQueNeedConcat(sk)) {
        PBUF_ChainConcat(&TcpSK(sk)->sndQue, &sk->sndBuf);

        if (TcpGetSndSpace(TcpSK(sk)) < sk->sndLowat) {
            SOCK_UnsetState(sk, SOCK_STATE_WRITE);
        }
    }

    if (TcpSK(sk)->rcvQue.bufLen > 0 && SOCK_CAN_RECV_MORE(sk)) {
        DP_ADD_PKT_STAT(TcpSK(sk)->wid, DP_PKT_RECV_BUF_IN, TcpSK(sk)->rcvQue.pktCnt);
        PBUF_ChainConcat(&sk->rcvBuf, &TcpSK(sk)->rcvQue);
    }

    if ((tsqFlags & TCP_TSQ_SK_SLOW) != 0) {
        if (TcpTsqProcSkEv(sk, tsqFlags) != 0) {
            return -1;
        }
    }

    if ((tsqFlags & TCP_TSQ_RECV) != 0) {
        SOCK_UpdateRcvState(sk);
    }

    if ((tsqFlags & TCP_TSQ_SET_WRITABLE) != 0) {
        if ((sk->state & SOCK_STATE_WRITE) == 0) {
            SOCK_SetState(sk, SOCK_STATE_WRITE);
            SOCK_WakeupWrSem(sk);    // 通知用户可写
        }
    }

    return 0;
}

static void TcpProcTsqLockQue(TcpTsq_t* tsq)
{
    TcpSk_t*      tcp;
    TcpSk_t*      next;
    TcpListHead_t tempHead;

    // 未在锁内访问，存在并发问题，但这里仅判断是否为空，忽略多线程问题
    if (tsq->tsqLockQue.first == NULL) {
        return;
    }

    LIST_INIT_HEAD(&tempHead);

    if (SPINLOCK_TryLock(&tsq->lock) != 0) {
        return;
    }

    LIST_CONCAT(&tempHead, &tsq->tsqLockQue, txEvNode);
    SPINLOCK_Unlock(&tsq->lock);

    for (tcp = LIST_FIRST(&tempHead); tcp != NULL; tcp = next) {
        Sock_t*  sk = TcpSk2Sk(tcp);
        uint32_t tsqFlags;

        SOCK_Lock(sk);

        ASSERT(SOCK_IS_CLOSED(sk) || SOCK_IS_CONN_REFUSED(sk) ||
            SOCK_IS_CONNECTING(sk) || SOCK_IS_CONNECTED(sk));

        next = LIST_NEXT(tcp, txEvNode);

        tsqFlags = tcp->tsqFlags;
        if (tsqFlags != 0) {
            tcp->tsqFlags = 0;
            LIST_REMOVE(&tsq->tsqQue, tcp, rxEvNode);
        }

        tsqFlags |= tcp->tsqFlagsLock; // 这个字段必须在锁内访问
        tcp->tsqFlagsLock = 0;

        if ((tsqFlags & TCP_TSQ_IN_SKLOCK) != 0) {
            if (TcpProcSkEv(sk, tsqFlags) != 0) {
                // 该场景下在内部完成解锁，释放sk，此处接着处理下个事件
                continue;
            }
        }

        SOCK_Unlock(sk);

        TcpTsqProcNolockEv(sk, tcp, tsqFlags);
    }
}

static void TcpProcTsqQue(TcpTsq_t* tsq)
{
    TcpSk_t* tcp;
    TcpSk_t* next;

    for (tcp = LIST_FIRST(&tsq->tsqQue); tcp != NULL; tcp = next) {
        uint32_t tsqFlags;

        next          = LIST_NEXT(tcp, rxEvNode);
        tsqFlags      = tcp->tsqFlags;
        tcp->tsqFlags = 0;

        if ((tsqFlags & TCP_TSQ_IN_SKLOCK) != 0) {
            SOCK_Lock(TcpSk2Sk(tcp));
            if (TcpProcSkEv(TcpSk2Sk(tcp), tsqFlags) != 0) {
                // 该场景下在内部完成解锁，释放sk，此处接着处理下个事件
                continue;
            }
            SOCK_Unlock(TcpSk2Sk(tcp));
        }

        TcpTsqProcNolockEv(TcpSk2Sk(tcp), tcp, tsqFlags);
    }

    LIST_INIT_HEAD(&tsq->tsqQue);
}

void TcpTsqInetInsertBacklog(int wid, Pbuf_t *pbuf)
{
    TcpTsq_t* tsq = g_tsq[wid];
    SPINLOCK_Lock(&tsq->backLogLock);
    PBUF_REF(pbuf);
    PBUF_ChainPush(&tsq->backlog, pbuf);
    SPINLOCK_Unlock(&tsq->backLogLock);
}

static void TcpProcBackLog(TcpTsq_t *tsq, int wid)
{
    if (PBUF_CHAIN_IS_EMPTY(&tsq->backlog)) {
        return;
    }
    PBUF_Chain_t temp;
    PBUF_ChainInit(&temp);
    SPINLOCK_Lock(&tsq->backLogLock);
    PBUF_ChainConcat(&temp, &tsq->backlog);
    SPINLOCK_Unlock(&tsq->backLogLock);

    while (!PBUF_CHAIN_IS_EMPTY(&temp)) {
        Pbuf_t *pbuf = PBUF_CHAIN_POP(&temp);
        PBUF_SET_WID(pbuf, (uint8_t)wid);
        PBUF_SET_QUE_ID(pbuf, 0);
        PMGR_Dispatch(pbuf);
    }
}

static void TcpProcTsq(int wid)
{
    TcpTsq_t* tsq = g_tsq[wid];

    TcpProcBackLog(tsq, wid);

    TcpProcTsqLockQue(tsq);

    TcpProcTsqQue(tsq);
}

static WORKER_Work_t g_tsqEntry = {
    .type = WORKER_WORK_TYPE_FIX,
    .task = {
        .fixWork = TcpProcTsq,
    },
    .map = WORKER_BITMAP_ALL,
    .next = NULL,
};

int TcpTsqInit(int slave)
{
    size_t    allocSize;
    int       wcnt = CFG_GET_VAL(DP_CFG_WORKER_MAX);
    TcpTsq_t* tsq;

    SHM_REG("tcp_event", g_tsq);

    WORKER_AddWork(&g_tsqEntry);

    if (slave != 0) {
        return 0;
    }

    if (g_tsq != NULL) {
        return -1;
    }

    allocSize = sizeof(TcpTsq_t*) * wcnt;
    allocSize += TcpTsqGetObjSize() * (size_t)wcnt;

    g_tsq = SHM_MALLOC(allocSize, MOD_TCP, DP_MEM_FREE);
    if (g_tsq == NULL) {
        DP_LOG_ERR("Malloc memory failed for tcp tsq.");
        return -1;
    }
    (void)memset_s(g_tsq, allocSize, 0, allocSize);

    tsq = (TcpTsq_t*)PTR_NEXT(g_tsq, sizeof(TcpTsq_t*) * wcnt);

    for (int i = 0; i < wcnt; i++) {
        g_tsq[i] = tsq;

        if (TcpTsqInitMem(tsq) != 0) {
            TcpTsqDeinitTsqs(g_tsq, i);
            SHM_FREE(g_tsq, DP_MEM_FREE);
            g_tsq = NULL;
            return -1;
        }

        tsq = (TcpTsq_t*)PTR_NEXT(tsq, TcpTsqGetObjSize());
    }

    return 0;
}

void TcpTsqDeinit(int slave)
{
    int wcnt;

    if (slave != 0) {
        return;
    }

    if (g_tsq == NULL) {
        return;
    }

    wcnt = CFG_GET_VAL(DP_CFG_WORKER_MAX);
    TcpTsqDeinitTsqs(g_tsq, wcnt);
    SHM_FREE(g_tsq, DP_MEM_FREE);
    g_tsq = NULL;
}

void TcpTsqTryRemoveLockQue(TcpSk_t* tcp)
{
    if (tcp->wid < 0) {
        return;
    }
    TcpTsq_t* tsq = g_tsq[tcp->wid];
    SPINLOCK_Lock(&tsq->lock);
    if (LIST_REMOVE_CHECK(&tsq->tsqLockQue, tcp, txEvNode) != 0) {
        SPINLOCK_Unlock(&tsq->lock);
        return;
    }
    LIST_REMOVE(&tsq->tsqLockQue, tcp, txEvNode);
    SPINLOCK_Unlock(&tsq->lock);
}

void TcpTsqTryRemoveNoLockQue(TcpSk_t* tcp)
{
    if (tcp->wid < 0) {
        return;
    }
    TcpTsq_t* tsq = g_tsq[tcp->wid];
    if (LIST_REMOVE_CHECK(&tsq->tsqQue, tcp, rxEvNode) != 0) {
        return;
    }
    LIST_REMOVE(&tsq->tsqQue, tcp, rxEvNode);
}
