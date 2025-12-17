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

#include "utils_procfs.h"
#include "dp_fd.h"
#include "sock_ops.h"
#include "tcp_inet.h"
#include "tcp_timer.h"

enum {
    PROCFS_ESTABLISHED = 1,
    PROCFS_SYN_SENT,
    PROCFS_SYN_RECV,
    PROCFS_FIN_WAIT1,
    PROCFS_FIN_WAIT2,
    PROCFS_TIME_WAIT,
    PROCFS_CLOSE,
    PROCFS_CLOSE_WAIT,
    PROCFS_LAST_ACK,
    PROCFS_LISTEN,
    PROCFS_CLOSING,
};

static uint8_t g_tcpProcfsState[] = {
    [TCP_CLOSED]      = PROCFS_CLOSE,
    [TCP_LISTEN]      = PROCFS_LISTEN,
    [TCP_SYN_SENT]    = PROCFS_SYN_SENT,
    [TCP_SYN_RECV]    = PROCFS_SYN_RECV,
    [TCP_ESTABLISHED] = PROCFS_ESTABLISHED,
    [TCP_CLOSE_WAIT]  = PROCFS_CLOSE_WAIT,
    [TCP_FIN_WAIT1]   = PROCFS_FIN_WAIT1,
    [TCP_CLOSING]     = PROCFS_CLOSING,
    [TCP_LAST_ACK]    = PROCFS_LAST_ACK,
    [TCP_FIN_WAIT2]   = PROCFS_FIN_WAIT2,
    [TCP_TIME_WAIT]   = PROCFS_TIME_WAIT,
};

static uint16_t TcpGetTimerWhen(TcpSk_t* tcp)
{
    uint8_t timer = tcp->trType;
    uint16_t when = 0;

    switch (timer) {
        case TCP_TRTYPE_CONN:
        case TCP_TRTYPE_TIMEWAIT:
            when = tcp->expiredTick[TCP_TIMERID_SLOW];
            break;
        case TCP_TRTYPE_REXMIT:
        case TCP_TRTYPE_PERSIST:
            when = tcp->expiredTick[TCP_TIMERID_FAST];
            break;
        default:
            break;
    }

    return when;
}

static void TcpCopyProcfsInfo(DP_DevProcfs_t* info, Sock_t* sk)
{
    if (info == NULL) {
        return;
    }

    (void)memset_s(info, sizeof(DP_DevProcfs_t), 0, sizeof(DP_DevProcfs_t));
    DP_SockDetails_t details = {0};
    SOCK_GetSocketDetails(sk, &details);

    if (sk->family == DP_AF_INET) {
        InetSk_t* inetSk = TcpInetSk(sk);
        if (inetSk != NULL) {
            info->lAddr4 = inetSk->hashinfo.laddr;
            info->rAddr4 = inetSk->hashinfo.paddr;
            info->lPort  = inetSk->hashinfo.lport;
            info->rPort  = inetSk->hashinfo.pport;
        }
    }

    info->st       = g_tcpProcfsState[TcpSK(sk)->state];
    info->tx_queue = details.tcpDetails.baseDetails.sndQueSize;
    info->rx_queue = details.tcpDetails.baseDetails.rcvQueSize;
    info->tr       = TcpSK(sk)->trType;
    info->when     = TcpGetTimerWhen(TcpSK(sk));
    info->retrnsmt = details.tcpDetails.transDetails.backoff;
    info->timeout  = details.tcpDetails.transDetails.keepIdleCnt;
    info->ref      = sk->ref;
    info->cwnd     = details.tcpDetails.baseDetails.cwnd;
    info->ssthresh = details.tcpDetails.baseDetails.ssthresh > INT_MAX ? -1 :
        (int32_t)details.tcpDetails.baseDetails.ssthresh;
}

int DP_GetTcpProcfsInfo(DP_DevProcfs_t* info, int* count)
{
    Sock_t* sk;
    Fd_t*   file;
    int     sockfd;
    int     cur = 0;

    if (info == NULL || count == NULL) {
        DP_LOG_ERR("DP get net tcp procfs info failed, param null.");
        return -1;
    }

    for (int i = 0; i < FD_GetFileLimit() && cur < *count; i++) {
        sockfd = FD_GetUserFdByRealFd(i);
        if (FD_Get(sockfd, FD_TYPE_SOCKET, &file) != 0) {
            continue;
        }

        sk = (Sock_t *)(file->priv);
        if ((sk == NULL) || (sk->ops == NULL) ||
            (sk->ops->type != DP_SOCK_STREAM) || (sk->ops->protocol != DP_IPPROTO_TCP)) {
            FD_Put(file);
            continue;
        }

        SOCK_Lock(sk);

        TcpCopyProcfsInfo(&info[cur++], sk);

        SOCK_Unlock(sk);
        FD_Put(file);
    }

    *count = cur;
    return 0;
}