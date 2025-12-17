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


#ifndef __KNET_LOG_H__
#define __KNET_LOG_H__

#include <syslog.h>
#include "knet_types.h"

// 定义输出日志的模块名字，使得在输出日志中统一
#define KNET_LOG_MODULE_NAME "libknet"
#define KNET_LIMIT_LOG_DEFAULT_INTERNEL_IN_MS (100) /* 0.1s */

typedef enum {
    KNET_LOG_EMERG = LOG_EMERG,
    KNET_LOG_ERR = LOG_ERR,
    KNET_LOG_WARN = LOG_WARNING,
    KNET_LOG_DEFAULT = KNET_LOG_WARN,
    KNET_LOG_INFO = LOG_INFO,
    KNET_LOG_DEBUG = LOG_DEBUG,
    KNET_LOG_MAX,
} KNET_LogLevel;

/**
 * @brief 日志输出函数
 *
 * @param function 调用处函数名
 * @param line 调用处行数
 * @param level 日志级别
 * @param format 日志信息
 */
void KNET_Log(const char *function, int line, int level, const char *format, ...);

/**
 * @brief 输出INFO级别日志，不做日志级别判断
 *
 * @param format 日志信息
 */
void KNET_LogNormal(const char *format, ...);

/**
 * @brief 日志服务初始化
 */
void KNET_LogInit(void);

/**
 * @brief 日志服务关闭
 */
void KNET_LogUninit(void);

/**
 * @brief 协议栈日志输出钩子函数
 *
 * @param format 日志信息
 */
void KNET_LogFixLenOutputHook(const char* format, ...);

/**
 * @brief 获取当前日志级别
 *
 * @return KNET_LogLevel
 */
KNET_LogLevel KNET_LogLevelGet(void);

/**
 * @brief 设置日志级别
 *
 * @param logLevel
 */
void KNET_LogLevelSet(KNET_LogLevel logLevel);

/**
 * @brief 根据传入的字符串设置日志级别
 * @attention 必须在配置文件解析之后执行，仅由config模块调用
 */
void KNET_LogLevelSetByStr(const char *levelStr);

/**
 * @brief 获取互斥锁
 */
void KNET_LogMutexLock(void);
/**
 * @brief 释放互斥锁
 *
 */
void KNET_LogMutexUnlock(void);

/**
 * @brief 获取当前时间毫秒数
 *
 * @return uint64_t
 */
uint64_t KNET_GetCurrentTimeMillis(void);

/**
 * @brief 日志输出宏，已填充函数名、行号、日志级别
 */
#define KNET_LOG(level, fmt, args...) KNET_Log(__func__, __LINE__, level, fmt, ##args)

#define KNET_ERR(fmt, args...) KNET_Log(__func__, __LINE__, KNET_LOG_ERR, "[ERR] " fmt, ##args)

#define KNET_WARN(fmt, args...) KNET_Log(__func__, __LINE__, KNET_LOG_WARN, "[WARN] " fmt, ##args)

#define KNET_INFO(fmt, args...) KNET_Log(__func__, __LINE__, KNET_LOG_INFO, "[INFO] " fmt, ##args)

#define KNET_DEBUG(fmt, args...) KNET_Log(__func__, __LINE__, KNET_LOG_DEBUG, "[DEBUG] " fmt, ##args)

/**
 * @brief 日志代码行级限频，并支持打印出被限频的次数
 */
#define KNET_LOG_LINE_LIMIT(logLevel, fmt, args...)                                                                  \
    do {                                                                                                             \
        if (KNET_LogLevelGet() < logLevel) {                                                                         \
            break;                                                                                                   \
        }                                                                                                            \
        static __thread uint32_t limitTimes##__func__##__LINE__ = 0;                                                 \
        static __thread uint64_t last##__func__##__LINE__ = 0;                                                       \
        uint64_t now = KNET_GetCurrentTimeMillis();                                                                  \
        if ((now - last##__func__##__LINE__) < (KNET_LIMIT_LOG_DEFAULT_INTERNEL_IN_MS)) {                            \
            ++limitTimes##__func__##__LINE__;                                                                        \
            break;                                                                                                   \
        }                                                                                                            \
        last##__func__##__LINE__ = now;                                                                              \
                                                                                                                     \
        switch (logLevel) {                                                                                          \
            case KNET_LOG_ERR:                                                                                       \
                KNET_Log(__func__, __LINE__, logLevel,                                                               \
                    "[ERR | limitTimes %u]  " fmt, limitTimes##__func__##__LINE__, ##args);                          \
                break;                                                                                               \
            case KNET_LOG_WARN:                                                                                      \
                KNET_Log(__func__, __LINE__, logLevel,                                                               \
                    "[WARN | limitTimes %u]  " fmt, limitTimes##__func__##__LINE__, ##args);                         \
                break;                                                                                               \
            case KNET_LOG_INFO:                                                                                      \
                KNET_Log(__func__, __LINE__, logLevel,                                                               \
                    "[INFO | limitTimes %u]  " fmt, limitTimes##__func__##__LINE__, ##args);                         \
                break;                                                                                               \
            default:                                                                                                 \
                KNET_Log(__func__, __LINE__, logLevel,                                                               \
                    "[DEBUG | limitTimes %u]  " fmt, limitTimes##__func__##__LINE__, ##args);                        \
        }                                                                                                            \
        limitTimes##__func__##__LINE__ = 0;                                                                          \
    } while (0)                                                                                                      \

#endif // __KNET_LOG_H__