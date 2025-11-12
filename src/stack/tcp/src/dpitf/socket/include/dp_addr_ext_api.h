/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 地址扩展相关接口
 */

#ifndef DP_ADDR_EXT_API_H
#define DP_ADDR_EXT_API_H

#include "dp_socket_types_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup addr_ext 地址扩展
 * @ingroup socket
 */

/**
 * @ingroup addr_ext
 * @brief 地址事件类型
 */
typedef enum {
    DP_ADDR_EVENT_CREATE = 0, // 地址绑定
    DP_ADDR_EVENT_RELEASE, // 地址释放
    DP_ADDR_EVENT_MAX,
} DP_AddrEventType_t;

/**
 * @ingroup addr_ext
 * @brief 地址事件信息
 */
typedef struct {
    int protocol; // 协议
    struct DP_Sockaddr localAddr; // 本端地址
    DP_Socklen_t localAddrLen; // 本端地址长度
} DP_AddrEvent_t;

/**
 * @ingroup addr_ext
 * @brief 地址事件钩子，通知地址事件
 *
 * @param type [IN] 事件类型
 * @param addrEvent [IN] 事件信息
 * @retval 0 成功
 * @retval 错误码 失败

 */
typedef int (*DP_AddrEventHook_t)(DP_AddrEventType_t type, const DP_AddrEvent_t* addrEvent);

/**
 * @ingroup addr_ext
 * @brief 地址事件钩子
 */
typedef struct {
    DP_AddrEventHook_t eventNotify;
} DP_AddrHooks_t;

/**
 * @ingroup addr_ext
 * @brief 注册地址相关钩子
 *
 * @attention
 *
 * @param addrHooks [IN] 地址事件钩子
 * @retval 0 成功
 * @retval -1 失败

 */
int DP_AddrHooksReg(DP_AddrHooks_t* addrHooks);

#ifdef __cplusplus
}
#endif
#endif
