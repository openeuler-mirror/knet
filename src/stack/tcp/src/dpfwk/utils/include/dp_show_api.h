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
 * @file dp_show_api.h
 * @brief 数据面维测统计相关对外接口
 */

#ifndef DP_SHOW_API_H
#define DP_SHOW_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup show 维测接口注册
 * @ingroup debug
 */

/**
 * @ingroup show
 * @brief 维测信息show钩子原型。
 *
 * @par 描述: 维测信息show钩子原型。
 * @attention
 * @li
 *
 * @param flag [IN]  用户调用show接口时的标识
 * @param output [IN]  输出的维测信息字符串
 * @param len [IN]  输出的维测信息长度

 * @retval 0 成功
 * @retval 错误码 失败

 * @see DP_DebugShowHookReg
 */
typedef uint32_t (*DP_DebugShowHook)(uint32_t flag, char *output, uint32_t len);

/**
 * @ingroup show
 * @brief 维测信息打印接口注册函数
 *
 * @par 描述: 维测信息打印接口注册函数，供show接口打印信息使用。
 * @attention
 * 必须在DP协议栈初始化前进行注册，不允许重复注册
 *
 * @param hook [IN]  维测信息输出接口钩子<非空指针>
 *
 * @retval 0 成功
 * @retval #错误码 失败

 * @see DP_DebugShowHook
 */
uint32_t DP_DebugShowHookReg(DP_DebugShowHook hook);

#ifdef __cplusplus
}
#endif

#endif
