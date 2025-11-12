/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 调试信息相关对外接口
 */

#ifndef DP_DEBUG_API_H
#define DP_DEBUG_API_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup debug 维测信息
 * @ingroup debug
 */

/**
 * @ingroup debug
 * @brief 统计信息类型
 */
typedef enum {
    DP_STAT_TCP = 0,        /**< TCP相关统计 */
    DP_STAT_TCP_CONN,       /**< TCP连接状态统计 */
    DP_STAT_PKT,            /**< 协议栈各类报文统计 */
    DP_STAT_ABN,            /**< 协议栈异常打点统计 */
    DP_STAT_MEM,            /**< 协议栈内存使用统计 */
    DP_STAT_PBUF,           /**< 协议栈PBUF使用统计 */
    DP_STAT_MAX             /**< 统计最大枚举 */
} DP_StatType_t;

/**
 * @ingroup debug
 * @brief 维测统计信息打印到show接口钩子
 *
 * @par 描述:
 * 将维测统计信息打印到show接口钩子，用于信息查看或者问题定位
 * @attention
 *
 * @param type [IN]  统计信息类型<请参见DP_STAT_TYPE_E>
 * @param workerId [IN]  需要是有效worker，如果为-1则获取所有worker的统计信息
 * @param flag [IN]  调用时的用户自定义标识，在show钩子中透传。可以用于指定输出重定向方式
 *
 * @retval NA

 * @see DP_DebugShowHook | DP_DebugShowHookReg
 */
void DP_ShowStatistics(DP_StatType_t type, int workerId, uint32_t flag);

/**
 * @ingroup debug
 * @brief 维测统计信息获取协议栈使用的socket数量
 *
 * @par 描述:
 * 维测统计信息获取协议栈使用的socket数量，用于信息查看或者问题定位
 * @attention
 *
 * @param type [IN]  socket类型，预留，当前默认获取TCP socket数量
 *
 * @retval NA

 * @see DP_DebugShowHook | DP_DebugShowHookReg
 */
int DP_SocketCountGet(int type);

#ifdef __cplusplus
}
#endif
#endif
