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

#ifndef K_NET_TCP_FDS_H
#define K_NET_TCP_FDS_H

#include <stdint.h>
#include <stdbool.h>
#include "dp_posix_epoll_api.h"
#include "knet_atomic.h"

#define INVALID_FD (-1)
#define KNET_INVALID_FD (-1)
#define KNET_ESTABLISHED_FD (1)
#define KNET_UNESTABLISHED_FD (0)

enum KNET_FdState {
    KNET_FD_STATE_INVALID = 0, /* fd初始化状态 */
    KNET_FD_STATE_HIJACK,    /* fd已被协议栈劫持 */
};

enum KNET_FdType {
    KNET_FD_TYPE_INVALID, // calloc或memset初始化即为invalid，无需额外初始化。有功能依赖该值默认为0，未全面评估勿修改
    KNET_FD_TYPE_SOCKET,
    KNET_FD_TYPE_EPOLL,
    KNET_FD_TYPE_MAX,
};

struct KNET_EpollNotifyData {
    int eventFd;
    KNET_ATOMIC64_T active;  /* active为1表示已经被激活，无需再次唤醒。为0表示未激活，需要唤醒 */
};

struct KNET_EpollNotify {
    DP_EpollNotify_t notify;
    struct KNET_EpollNotifyData data;
};

union KNET_FdPrivateData {
    struct KNET_EpollNotify epollData;
};

struct KNET_Fd {
    enum KNET_FdState state;
    int dpFd;
    enum KNET_FdType  fdType;
    int establishedFdFlag;
    bool epFdHasOsFd; // 共线程下，epfd监听了osfd就认为一直有osfd，没有维护epfd中每个osfd的事件，比较复杂
    uint64_t tid; // 目前仅维护epoll_ctl在共线程下不跨线程调用，存储tid
    union KNET_FdPrivateData privateData;
};

/**
 * @brief 检查文件描述符是否有效
 *
 * @param [IN] int。osFd 操作系统文件描述符
 * @return bool 如果文件描述符有效，返回true，否则返回false
 */
bool KNET_IsFdValid(int osFd);

/**
 * @brief 将操作系统文件描述符转换为数据路径文件描述符
 *
 * @param [IN] int。osFd 操作系统文件描述符
 * @return int 数据路径文件描述符
 */
int KNET_OsFdToDpFd(int osFd);

/**
 * @brief 检查文件描述符是否被劫持
 *
 * @param [IN] int。osFd 操作系统文件描述符
 * @return bool 如果文件描述符被劫持，返回true，否则返回false
 */
bool KNET_IsFdHijack(int osFd);

/**
 * @brief 设置文件描述符的套接字状态
 *
 * @param [IN] enum KNET_FdState。state 文件描述符状态
 * @param [IN] int。osFd 操作系统文件描述符
 * @param [IN] int。dpFd 数据路径文件描述符
 */
void KNET_SetFdSocketState(enum KNET_FdState state, int osFd, int dpFd);

/**
 * @brief 重置文件描述符的状态
 *
 * @param [IN] int。osFd 操作系统文件描述符
 */
void KNET_ResetFdState(int osFd);

/**
 * @brief 获取文件描述符的私有数据
 *
 * @param [IN] int。osFd 操作系统文件描述符
 * @return union KNET_FdPrivateData *，文件描述符的私有数据
 */
union KNET_FdPrivateData *KNET_GetFdPrivateData(int osFd);

/**
 * @brief 设置文件描述符的状态和类型
 *
 * @param [IN] enum KNET_FdState。state 文件描述符状态
 * @param [IN] int。osFd 操作系统文件描述符
 * @param [IN] int。dpFd 数据路径文件描述符
 * @param [IN] enum KNET_FdType。type 文件描述符类型
 */
void KNET_SetFdStateAndType(enum KNET_FdState state, int osFd, int dpFd, enum KNET_FdType type);

/**
 * @brief 获取文件描述符的类型
 *
 * @param [IN] int。osFd 操作系统文件描述符
 * @return enum KNET_FdType，文件描述符的类型
 */
enum KNET_FdType KNET_GetFdType(int osFd);

/**
 * @brief 初始化文件描述符模块
 *
 * @return int 0：表示成功，其他值表示失败
 */
int KNET_FdInit(void);

/**
 * @brief 反初始化文件描述符模块
 */
void KNET_FdDeinit(void);

/**
 * @brief 获取最大文件描述符
 *
 * @return 最大文件描述符
 */
int KNET_FdMaxGet(void);

/**
 * @brief 设置fd为连接状态
 *
 * @param [IN] int。osFd 操作系统文件描述符
 */
void KNET_SetEstablishedFdState(int osFd);

/**
 * @brief 获取fd为连接状态
 *
 * @param [IN] int。osFd 操作系统文件描述符
 * @return int 0：fd状态为非连接类型，1：fd为稳定连接类型
 */
int KNET_GetEstablishedFdState(int osFd);


void KNET_SetEpollFdTid(int osFd, uint64_t tid);

uint64_t KNET_GetEpollFdTid(int osFd);

void KNET_EpHasOsFdSet(int osFd);

bool KNET_EpfdHasOsfdGet(int osFd);

#endif // K_NET_TCP_FDS_H
