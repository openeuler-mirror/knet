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
/**
 * @file dp_sem_api.h
 * @brief 信号量钩子函数注册相关
 */

#ifndef DP_SEM_API_H
#define DP_SEM_API_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup dp_sem 信号量模块 */

typedef void* DP_Sem_t;

/**
 * @ingroup dp_sem
 * @brief 信号量初始化钩子
 *
 * @par 描述: 信号量初始化钩子
 * @attention
 * NA
 *
 * @param[IN] sem 信号量指针
 * @param[IN] flag 信号量类型，暂不使用
 * @param[IN] value 信号量的初始值，暂不使用
 *
 * @return uint32_t
 * @retval 成功返回 0,失败返回 1
 * @remarks #DP_SEM_BINARY_TYPE 类型信号量 value范围[0 , 1]
 * @remarks #DP_SEM_MUTEX_TYPE 类型信号量 value值无效，传0即可
 * @since V100R024C10
 */
typedef uint32_t (*DP_SemInitHook)(DP_Sem_t sem, int32_t flag, uint32_t value);

/**
 * @ingroup dp_sem
 * @brief 信号量去初始化钩子
 *
 * @par 描述: 信号量去初始化钩子
 * @attention
 * NA
 *
 * @param[IN] sem 信号量指针
 *
 * @retval NA
 * @since V100R024C00
 * @see DP_SemHookReg
*/
typedef void (*DP_SemDeinitHook)(DP_Sem_t sem);

/**
 * @ingroup dp_sem
 * @brief 信号量PV操作钩子
 *
 * @par 描述: 信号量PV操作钩子
 * @attention
 * NA
 *
 * @param[IN] sem 信号量指针
 *
 * @retval 成功返回0,失败返回其他
 * @since V100R024C00
 * @see DP_SemHookReg
*/
typedef uint32_t (*DP_SemPVHook)(DP_Sem_t sem);

/**
 * @ingroup dp_sem
 * @brief 信号量超时等待钩子
 *
 * @par 描述: 信号量超时等待钩子
 * @attention
 * NA
 *
 * @param[IN] sem 信号量指针
 * @param[IN] timeout 超时时间,单位ms,传入-1永久等待
 *
 * @retval 成功返回0，超时返回ETIMEDOUT，信号中断返回EINTR
 * @since V100R024C00
 * @see DP_SemHookReg
*/
typedef uint32_t (*DP_SemTimeWaitHook)(DP_Sem_t sem, int32_t timeout);

/**
 * @ingroup dp_sem
 * 信号量操作集
 */
typedef struct {
    DP_SemInitHook initHook;          /**< 初始化钩子，必需 */
    DP_SemDeinitHook deinitHook;      /**< 去初始化钩子，必需 */
    DP_SemPVHook postHook;            /**< 唤醒信号量钩子，必需 */
    DP_SemPVHook waitHook;            /**< 等待信号量钩子，暂不使用 */
    DP_SemPVHook tryWaitHook;         /**< 尝试等待信号量钩子，暂不使用 */
    DP_SemTimeWaitHook timeWaitHook;  /**< 超时等待信号量钩子，必需 */
    uint16_t semSize;                  /**< 信号量结构体大小，必需 */
} DP_SemHooks_S;

/**
 * @ingroup dp_sem
 * @brief 信号量方法注册接口
 *
 * @par 描述: 信号量方法注册接口
 * @attention
 * 必须在DP协议栈初始化前进行注册，不允许重复注册
 *
 * @param pHooks [IN]  信号量操作钩子<非NULL>
 *
 * @retval 0 成功
 * @retval 其他值 失败
 * @since V100R024C10
 * @see DP_SemHookReg | DP_SemHooks_S
*/
uint32_t DP_SemHookReg(const DP_SemHooks_S *pHooks);

#ifdef __cplusplus
}
#endif

#endif
