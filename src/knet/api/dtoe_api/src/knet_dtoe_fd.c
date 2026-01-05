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
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "knet_log.h"
#include "knet_dtoe_fd.h"

#define KNET_INVALID_SN (0)

struct KNET_Fd *g_knetDtoeFdMap = NULL;
static int g_dtoeFdMax = 0;


int KNET_FdInit(void)
{
    if (g_dtoeFdMax > 0) {
        KNET_DEBUG("DTOE fd module reinit");
        return 0;
    }

    struct rlimit limit = {0};
    // 获取当前的资源 limit
    if (getrlimit(RLIMIT_NOFILE, &limit) != 0) {
        KNET_ERR("Get Linux fd limit fail");
        return -1;
    }
    // 根据当前环境的ulimit -n 创建fdmap
    g_knetDtoeFdMap = (struct KNET_Fd *)calloc(limit.rlim_cur, sizeof(struct KNET_Fd));
    if (g_knetDtoeFdMap == NULL) {
        KNET_ERR("Alloc Dtoe fd mng failed, fd cur limit %u!", limit.rlim_cur);
        return -1;
    }
    g_dtoeFdMax = limit.rlim_cur;
    KNET_INFO("Dtoe fd max %d", g_dtoeFdMax);

    return 0;
}

void KNET_FdDeinit(void)
{
    free(g_knetDtoeFdMap);
    g_knetDtoeFdMap = NULL;
    g_dtoeFdMax = 0;
}

inline bool KNET_IsOsFdValid(int osFd)
{
    return (osFd >= 0) && (osFd < g_dtoeFdMax);
}

struct KNET_Fd *KNET_GetFdConnUserData(int sockfd)
{
    return &g_knetDtoeFdMap[sockfd];
}

void KNET_SetFdState(int sockfd, struct knet_offload_in *in, dtoe_offload_out_s *out)
{
    g_knetDtoeFdMap[sockfd].sockfd = sockfd;
    // 此处直接强转为struct KnetSendChannel *是因为knet_create_send_channel返回的也是KnetSendChannel地址，recv同理
    g_knetDtoeFdMap[sockfd].sendChannel = (struct KnetSendChannel *)(in->send_channel);
    g_knetDtoeFdMap[sockfd].recvChannel = (struct KnetRecvChannel *)(in->recv_channel);
    g_knetDtoeFdMap[sockfd].user_data = in->user_data;
    g_knetDtoeFdMap[sockfd].dtoeConn = out->dtoe_conn;
    g_knetDtoeFdMap[sockfd].recvSn = out->recv_sn;
    g_knetDtoeFdMap[sockfd].send.compSn = out->send_sn;
    g_knetDtoeFdMap[sockfd].send.lastSn = out->send_sn;
}

void *KNET_GetConnBySock(int sockfd)
{
    return g_knetDtoeFdMap[sockfd].dtoeConn;
}

void KNET_ResetFdState(int sockfd)
{
    g_knetDtoeFdMap[sockfd].dtoeConn = NULL;
    g_knetDtoeFdMap[sockfd].sockfd = KNET_INVALID_FD;
    g_knetDtoeFdMap[sockfd].recvSn = KNET_INVALID_SN;
    g_knetDtoeFdMap[sockfd].sendChannel = NULL;
    g_knetDtoeFdMap[sockfd].recvChannel = NULL;
    g_knetDtoeFdMap[sockfd].send.compSn = KNET_INVALID_SN;
    g_knetDtoeFdMap[sockfd].send.lastSn = KNET_INVALID_SN;
    g_knetDtoeFdMap[sockfd].user_data = NULL;
}