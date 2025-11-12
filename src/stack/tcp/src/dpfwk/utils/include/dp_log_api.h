/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 数据面日志相关对外接口
 */

#ifndef DP_LOG_API_H
#define DP_LOG_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup dp_log 数据面日志 */

/**
 * @ingroup dp_log
 * @brief 日志输出的等级
 */
typedef enum DP_LogLevel_Etag {
    DP_LOG_LEVEL_CRITICAL, /**< 严重级别 */
    DP_LOG_LEVEL_ERROR,    /**< 错误级别 */
    DP_LOG_LEVEL_WARNING,  /**< 告警级别 */
    DP_LOG_LEVEL_INFO,     /**< 信息级别 */
    DP_LOG_LEVEL_DEBUG,    /**< 调试级别 */
} DP_LogLevel_E;

/**
 * @ingroup dp_log
 * @brief 日志输出钩子原型。
 *
 * @par 描述: 日志输出钩子。
 * @attention
 * @li
 *
 * @param fmt [IN]  格式化字符串<非空指针>
 * @param ... [IN]  可变参数
 * @par 依赖:
 *     <ul><li>dp_log_api.h：该接口声明所在的头文件。</li></ul>

 * @see DP_LogHookReg
 */
typedef void (*DP_LogHook)(const char *fmt, ...);

/**
 * @ingroup dp_log
 * @brief 日志打印接口注册函数
 *
 * @par 描述: 日志打印接口注册函数
 * @attention
 * 必须在DP协议栈初始化前进行注册
 *
 * @param fnHook [IN]  日志打印钩子<非空指针>
 *
 * @retval 0 成功
 * @retval 错误码 失败
 * @par 依赖:
 *     <ul><li>dp_log_api.h：该接口声明所在的头文件。</li></ul>
 * @see DP_LogHook
 */
uint32_t DP_LogHookReg(DP_LogHook fnHook);

/**
 * @ingroup dp_log
 * @brief 设置日志打印级别。
 *
 * @par 描述: 设置日志打印级别，只有小于设置的级别的日志会调用注册的日志钩子打印，内部默认的日志级别为DP_LOG_LEVEL_ERROR。
 * @attention
 * NA
 *
 * @param logLevel [IN]  日志级别<请参见DP_LogLevel_E>
 *
 * @retval 0 成功
 * @retval 错误码 失败
 * @see DP_LogLevelGet
 */
void DP_LogLevelSet(DP_LogLevel_E logLevel);

/**
 * @ingroup dp_log
 * @brief 获取日志打印级别。
 *
 * @par 描述: 获取日志打印级别。
 * @attention
 * NA
 *
 * @param 无
 *
 * @retval 返回值 日志级别<请参见DP_LogLevel_E>
 * @see DP_LogLevelSet
 */
uint32_t DP_LogLevelGet(void);

#ifdef __cplusplus
}
#endif

#endif
