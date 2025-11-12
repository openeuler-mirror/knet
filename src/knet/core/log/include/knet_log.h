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

typedef enum {
    KNET_LOG_EMERG = LOG_EMERG,    // 用于进程信号退出时设置不让LOG打印
    KNET_LOG_ERR = LOG_ERR,
    KNET_LOG_WARN = LOG_WARNING,
    KNET_LOG_DEFAULT = KNET_LOG_WARN,
    KNET_LOG_INFO = LOG_INFO,
    KNET_LOG_DEBUG = LOG_DEBUG,
    KNET_LOG_MAX,
} KnetLogLevel;

typedef struct {
    KnetLogLevel level;
    const char *levelName;
} KnetLogLevelName;

void KNET_Log(const char *function, int line, int level, const char *format, ...);
void KNET_LogLimit(const char *function, int line, int level, const char *format, ...);
void KNET_LogNormal(const char *format, ...);
void KNET_LogInit(void);
void KNET_LogUninit(void);
void KNET_FixLenOutputHook(const char* format, ...);
KnetLogLevel KNET_LogLevelGet(void);
void KNET_LogLevelSet(KnetLogLevel logLevel);
void KNET_LogLevelConfigure(void);
void KNET_LogMutexLock(void);
void KNET_LogMutexUnlock(void);

uint64_t KNET_GetTicksHz(void);
uint64_t KNET_GetTicks(void);

#define KNET_LOG(level, fmt, args...) KNET_Log(__func__, __LINE__, level, fmt, ##args)

#define KNET_ERR(fmt, args...) KNET_Log(__func__, __LINE__, KNET_LOG_ERR, "[ERR] " fmt, ##args)

#define KNET_WARN(fmt, args...) KNET_Log(__func__, __LINE__, KNET_LOG_WARN, "[WARN] " fmt, ##args)

#define KNET_INFO(fmt, args...) KNET_Log(__func__, __LINE__, KNET_LOG_INFO, "[INFO] " fmt, ##args)

#define KNET_DEBUG(fmt, args...) KNET_Log(__func__, __LINE__, KNET_LOG_DEBUG, "[DEBUG] " fmt, ##args)

#define KNET_LOG_LIMIT(level, fmt, args...) KNET_LogLimit(__func__, __LINE__, level, fmt, ##args)

#define KNET_ERR_LIMIT(fmt, args...) KNET_LogLimit(__func__, __LINE__, KNET_LOG_ERR, "[ERR] " fmt, ##args)

#define KNET_WARN_LIMIT(fmt, args...) KNET_LogLimit(__func__, __LINE__, KNET_LOG_WARN, "[WARN] " fmt, ##args)

#define KNET_INFO_LIMIT(fmt, args...) KNET_LogLimit(__func__, __LINE__, KNET_LOG_INFO, "[INFO] " fmt, ##args)

#define KNET_DEBUG_LIMIT(fmt, args...) KNET_LogLimit(__func__, __LINE__, KNET_LOG_DEBUG, "[DEBUG] " fmt, ##args)

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
        uint64_t now = KNET_GetTicks();                                                                              \
        if ((now - last##__func__##__LINE__) < (KNET_GetTicksHz() / 10)) { /* KNET_GetTicksHz() / 10表示100ms */     \
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