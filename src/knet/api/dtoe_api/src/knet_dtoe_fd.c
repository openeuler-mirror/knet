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
#include "securec.h"

#include "knet_log.h"
#include "knet_dtoe_fd.h"

#define KNET_INVALID_SN (0)
#define KNET_INITIAL_FREEREQ_SIZE 128

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
    // 此处直接强转为struct knet_send_channel_events *是因为knet_dtoe_create_send_channel返回的也是struct knet_send_channel地址，recv同理
    g_knetDtoeFdMap[sockfd].send_channel = (struct knet_send_channel_events *)(in->send_channel);
    g_knetDtoeFdMap[sockfd].recv_channel = (struct knet_recv_channel_events *)(in->recv_channel);
    g_knetDtoeFdMap[sockfd].user_data = in->user_data;
    g_knetDtoeFdMap[sockfd].dtoe_conn = out->dtoe_conn;
    g_knetDtoeFdMap[sockfd].recv_sn = out->recv_sn;
    g_knetDtoeFdMap[sockfd].send.comp_sn = out->send_sn;
    g_knetDtoeFdMap[sockfd].send.last_sn = out->send_sn;
}

int KNET_InitFreeReq(int sockfd)
{
    TAILQ_INIT(&g_knetDtoeFdMap[sockfd].send.free_req);
    TAILQ_INIT(&g_knetDtoeFdMap[sockfd].send.unack_req);

    // 初始化锁
    memset_s(&g_knetDtoeFdMap[sockfd].send_lock, sizeof(KNET_SpinLock), 0, sizeof(KNET_SpinLock));
    for (int i = 0; i < KNET_INITIAL_FREEREQ_SIZE; i++) {
        KnetReqNode* node = (KnetReqNode*) calloc(1, sizeof(KnetReqNode));
        if (node == NULL) {
            KNET_ERR("Failed to allocate KnetReqNode %d", i);
            // 释放已分配的节点
            while (!TAILQ_EMPTY(&g_knetDtoeFdMap[sockfd].send.free_req)) {
                KnetReqNode* n = TAILQ_FIRST(&g_knetDtoeFdMap[sockfd].send.free_req);
                TAILQ_REMOVE(&g_knetDtoeFdMap[sockfd].send.free_req, n, node);
                free(n);
            }
            return -1;
        }
        node->wr_id = 0;
        node->send_sn = 0;

        // 添加到空闲队列
        TAILQ_INSERT_TAIL(&g_knetDtoeFdMap[sockfd].send.free_req, node, node);
    }
    KNET_DEBUG("KNET_Fd create with %d free request nodes", KNET_INITIAL_FREEREQ_SIZE);
    return 0;
}

void KNET_UninitFreeReq(int sockfd)
{
    KNET_SpinlockLock(&g_knetDtoeFdMap[sockfd].send_lock);
    // 释放freereq已分配的节点
    while (!TAILQ_EMPTY(&g_knetDtoeFdMap[sockfd].send.free_req)) {
        KnetReqNode* n = TAILQ_FIRST(&g_knetDtoeFdMap[sockfd].send.free_req);
        TAILQ_REMOVE(&g_knetDtoeFdMap[sockfd].send.free_req, n, node);
        free(n);
    }

    // 释放unackreq已分配的节点，防止未收到ack直接关闭泄露
    while (!TAILQ_EMPTY(&g_knetDtoeFdMap[sockfd].send.unack_req)) {
        KnetReqNode* n = TAILQ_FIRST(&g_knetDtoeFdMap[sockfd].send.unack_req);
        TAILQ_REMOVE(&g_knetDtoeFdMap[sockfd].send.unack_req, n, node);
        free(n);
    }
    KNET_SpinlockUnlock(&g_knetDtoeFdMap[sockfd].send_lock);
    KNET_DEBUG("KNET_Fd Uninit with %d free/unack request nodes", KNET_INITIAL_FREEREQ_SIZE);
}

void *KNET_GetConnBySock(int sockfd)
{
    return g_knetDtoeFdMap[sockfd].dtoe_conn;
}

void KNET_ResetFdState(int sockfd)
{
    g_knetDtoeFdMap[sockfd].dtoe_conn = NULL;
    g_knetDtoeFdMap[sockfd].sockfd = KNET_INVALID_FD;
    g_knetDtoeFdMap[sockfd].recv_sn = KNET_INVALID_SN;
    g_knetDtoeFdMap[sockfd].send_channel = NULL;
    g_knetDtoeFdMap[sockfd].recv_channel = NULL;
    g_knetDtoeFdMap[sockfd].send.comp_sn = KNET_INVALID_SN;
    g_knetDtoeFdMap[sockfd].send.last_sn = KNET_INVALID_SN;
    g_knetDtoeFdMap[sockfd].user_data = NULL;
}