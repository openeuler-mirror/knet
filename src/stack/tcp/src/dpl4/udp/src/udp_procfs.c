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
#include <securec.h>

#include "utils_procfs.h"
#include "dp_fd.h"
#include "sock_ops.h"
#include "udp_inet.h"

static void UdpCopyProcfsInfo(DP_DevProcfs_t* info, Sock_t* sk)
{
    if (info == NULL) {
        return;
    }

    (void)memset_s(info, sizeof(DP_DevProcfs_t), 0, sizeof(DP_DevProcfs_t));

    if (sk->family == DP_AF_INET) {
        InetSk_t* inetSk = UdpInetSk(sk);
        if (inetSk != NULL) {
            info->lAddr4 = inetSk->hashinfo.laddr;
            info->rAddr4 = inetSk->hashinfo.paddr;
            info->lPort  = inetSk->hashinfo.lport;
            info->rPort  = inetSk->hashinfo.pport;
        }
    }

    info->st       = 7; // 该字段通常为07 表示当前UDP未连接
    info->ref      = sk->ref;
}

static inline int UdpProcfsCheckParam(DP_DevProcfs_t* info, int* count)
{
    if (info == NULL || count == NULL) {
        return -1;
    }

    return 0;
}

int DP_GetUdpProcfsInfo(DP_DevProcfs_t* info, int* count)
{
    Sock_t* sk;
    Fd_t*   file;
    int     sockfd;
    int     cur = 0;

    if (UdpProcfsCheckParam(info, count) != 0) {
        DP_LOG_ERR("DP get net udp procfs info failed, param null.");
        return -1;
    }

    for (int i = 0; i < FD_GetFileLimit() && cur < *count; i++) {
        sockfd = FD_GetUserFdByRealFd(i);
        if (FD_Get(sockfd, FD_TYPE_SOCKET, &file) != 0) {
            continue;
        }

        sk = (Sock_t *)(file->priv);
        if ((sk == NULL) || (sk->ops == NULL) ||
            (sk->ops->type != DP_SOCK_DGRAM) || (sk->ops->protocol != DP_IPPROTO_UDP)) {
            FD_Put(file);
            continue;
        }

        SOCK_Lock(sk);

        UdpCopyProcfsInfo(&info[cur++], sk);

        SOCK_Unlock(sk);
        FD_Put(file);
    }

    *count = cur;
    return 0;
}