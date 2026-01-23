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

#ifndef __KNET_DTOE_FD_H__
#define __KNET_DTOE_FD_H__

#include <stdbool.h>
#include "dtoe_interface.h"
#include "knet_dtoe_events.h"
#include "knet_dtoe_api.h"
#include "knet_lock.h"

#define KNET_INVALID_FD (-1)
#define KNET_INVALID_EVENT_INDEX (-1)

/* 涉及多线程，成员变更考虑伪共享 */
struct KNET_Fd {
    void *dtoe_conn;
    int sockfd;
    struct knet_send_channel_events *send_channel;
    struct {
        uint16_t last_sn; // 上一个已完成的req的send_sn
        uint16_t comp_sn; // send_complete对应的finish_msn
        struct KnetReqListHead unack_req;
        struct KnetReqListHead free_req;
    } send;
    KNET_SpinLock send_lock;

    struct knet_recv_channel_events *recv_channel;
    int recvEventIndex; // 一次knet_poll_recv_channel调用中，该sockfd第一次事件触发对应结果数组的下标

    void *user_data;
    uint32_t recv_sn;
};

/**
 * @brief 初始化K-NET管理DTOE的FD资源
 * @retval 0 success, -1 failed
 */
int KNET_FdInit(void);

/**
 * @brief 释放K-NET管理DTOE的FD资源
 */
void KNET_FdDeinit(void);

/**
 * @brief 判断输入fd是否有效
 * @param osFd [IN] 连接的fd
 * @retval true success, false failed
 */
bool KNET_IsOsFdValid(int osFd);

/**
 * @brief 设置fd属性
 * @param sockfd [IN] 连接的fd
 * @param in [IN] 连接卸载需要的连接信息
 * @param out [IN] 连接卸载后DTOE返回的连接信息
 * @retval null
 */
void KNET_SetFdState(int sockfd, struct knet_offload_in *in, dtoe_offload_out_s *out);

/**
 * @brief 初始化freereq链表
 * @param sockfd [IN] 连接的fd
 * @retval 0成功，-1失败
 */
int KNET_InitFreeReq(int sockfd);

/**
 * @brief 去初始化freereq链表
 * @param sockfd [IN] 连接的fd
 * @retval void
 */
void KNET_UninitFreeReq(int sockfd);

/**
 * @brief 获取fd的userdata属性
 * @param sockfd [IN] 连接的fd
 * @retval knet user_data地址, 即fd map中的地址
 */
struct KNET_Fd *KNET_GetFdConnUserData(int sockfd);

/**
 * @brief 获取fd的conn
 * @param osFd [IN] 连接的fd
 * @retval void *连接卸载后DTOE返回的连接信息
 */
void *KNET_GetConnBySock(int sockfd);

/**
 * @brief 重置文件描述符的状态
 *
 * @param sockfd [IN] int。osFd 操作系统文件描述符
 */
void KNET_ResetFdState(int sockfd);
#endif