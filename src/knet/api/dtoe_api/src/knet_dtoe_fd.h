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
#include "knet_dtoe_api.h"

#define KNET_INVALID_FD (-1)

struct KnetRecvChannel {
    struct knet_recv_channel channel;
    struct knet_recv_events *events;
    uint32_t maxevents;
    uint32_t curEventIndex;
};

struct KnetSendChannel {
    struct knet_send_channel channel;
    struct knet_send_events *events;
    uint32_t maxevents;
    uint32_t curEventIndex;
};

struct KNET_Fd {
    void *dtoeConn;
    int sockfd;
    uint32_t recvSn;
    struct KnetSendChannel *sendChannel;
    struct KnetRecvChannel *recvChannel;
    struct {
        uint16_t lastSn;
        uint16_t compSn;
        // struct list_xx unackReq;
        // struct list_xx freeReq;
    } send;

    void *user_data;
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
 * @param [IN] int。osFd 操作系统文件描述符
 */
void KNET_ResetFdState(int sockfd);
#endif