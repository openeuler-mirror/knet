/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: time系统时间相关声明与配置
 */

#ifndef DP_CLOCK_API_H
#define DP_CLOCK_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup dp_clock 时钟管理 */

/**
 * @ingroup dp_clock
 * @brief 获取时间的时钟类型
 */
typedef enum {
    DP_CLOCK_REALTIME = 0,
    DP_CLOCK_MONOTONIC_COARSE
} DP_ClockId_E;

/**
 * @ingroup dp_clock
 * @brief 获取当前时间戳的回调类型
 * @attention 应当线程安全
 * @param clockId [IN] 时钟类型
 * @param seconds [OUT] 当前时间戳，秒
 * @param nanoseconds [OUT] 当前时间戳秒级下的毫秒
 * @retval #DP_OK 成功
 * @retval #DP_ERR 出错

 */
typedef uint32_t (*DP_ClockGetTimeHook)(DP_ClockId_E clockId, int64_t *seconds, int64_t *nanoseconds);

/**
 * @ingroup dp_clock
 * @brief 注册获取毫秒级时钟回调
 * @attention 非线程安全
 * @param hook [IN] 获取毫秒级时钟回调
 * @retval #DP_OK 成功
 * @retval #DP_ERR 参数错误

 * @see DP_ClockGetTimeHook
 */
uint32_t DP_ClockReg(DP_ClockGetTimeHook timeHook);

#ifdef __cplusplus
}
#endif
#endif
