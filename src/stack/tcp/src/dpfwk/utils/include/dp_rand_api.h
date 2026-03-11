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
 * @file dp_rand_api.h
 * @brief 随机数钩子函数注册相关
 */

#ifndef DP_RAND_API_H
#define DP_RAND_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup dp_rand 随机数 */

/**
 * @ingroup dp_rand
 * @brief 随机数生成接口
 *
 * @par 描述: 随机数生成接口
 * @attention
 * 需要是安全的随机数
 *
 * @retval 4字节数值 随机数

 * @see DP_RandIntHookReg
 */
typedef uint32_t (*DP_RandIntHook)(void);

/**
 * @ingroup dp_rand
 * 随机数操作集
 */
typedef struct {
    DP_RandIntHook randInt;
}  DP_RandomHooks_S;

/**
 * @ingroup dp_rand
 * @brief 随机数操作接口注册函数
 *
 * @par 描述: 随机数操作接口注册函数
 * @attention
 * 必须在DP协议栈初始化前进行注册，不允许重复注册
 *
 * @param randHook [IN]  随机数操作钩子<非NULL>
 *
 * @retval 0 成功
 * @retval 其他值 失败

 * @see DP_RandomHooks_S | DP_RandIntHook
 */
uint32_t DP_RandIntHookReg(DP_RandomHooks_S *randHook);

#ifdef __cplusplus
}
#endif
#endif
