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
#include <sys/queue.h>
#include <sys/epoll.h>

#include "flexda_dtoe_interface.h"
#include "knet_dtoe_events.h"
#include "knet_dtoe_api.h"
#include "knet_lock.h"
#include "knet_dtoe_tcp.h"
#include "knet_dtoe_epoll.h"

#define KNET_INVALID_FD (-1)
#define KNET_INVALID_EVENT_INDEX (-1)

enum KNET_FD_TYPE {
    KNET_FD_TYPE_INVALID,
    KNET_FD_TYPE_TCP,
    KNET_FD_TYPE_EPOLL,
    KNET_FD_TYPE_INTERNAL, // 内交换
};

struct KNET_Fd {
    uint8_t type;
    union {
        struct KnetTcp tcp;
        struct KnetEpoll ep;
    };
};

extern struct KNET_Fd *g_knetDtoeFdMap;
extern int g_dtoeFdMax;

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
static inline bool KNET_IsOsFdValid(int osFd)
{
    return (osFd >= 0) && (osFd < g_dtoeFdMax);
}

static inline bool KNET_IsOsFdTcp(int osFd)
{
    return KNET_IsOsFdValid(osFd) && (g_knetDtoeFdMap[osFd].type == KNET_FD_TYPE_TCP);
}

static inline bool KNET_IsOsFdEpoll(int osFd)
{
    return KNET_IsOsFdValid(osFd) && (g_knetDtoeFdMap[osFd].type == KNET_FD_TYPE_EPOLL);
}

static inline void KNET_FdTypeSet(int osFd, uint8_t type)
{
    g_knetDtoeFdMap[osFd].type = type;
}

static inline uint8_t KNET_FdTypeGet(int osFd)
{
    return g_knetDtoeFdMap[osFd].type;
}

static inline struct KnetTcp *KnetGetTcpFd(int sockfd)
{
    return &g_knetDtoeFdMap[sockfd].tcp;
}

static inline struct KnetEpoll *KnetGetEpollFd(int epfd)
{
    return &g_knetDtoeFdMap[epfd].ep;
}

#endif
