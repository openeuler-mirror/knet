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
#ifndef UTILS_LOG_H
#define UTILS_LOG_H

#include <stdio.h>
#include "dp_log_api.h"
// #include "dp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

extern DP_LogHook g_fnLogOut;
extern DP_LogLevel_E g_logLevel;

/* release版本不打印函数名和行号 */
#ifdef DP_DEBUG
#define DP_LOGOUT(func, line, level, levelInfo, fmt, ...) do { \
    if ((g_fnLogOut != NULL) && ((level) <= g_logLevel)) { \
        g_fnLogOut ("%s[%d]|%s " fmt "\n", func, line, levelInfo, ##__VA_ARGS__); \
    } \
} while (0)
#else
#define DP_LOGOUT(func, line, level, levelInfo, fmt, ...) do { \
    if ((g_fnLogOut != NULL) && ((level) <= g_logLevel)) { \
        g_fnLogOut("%s " fmt "\n", levelInfo, ##__VA_ARGS__); \
    } \
} while (0)
#endif

#define CRIT_STR  "[CRIT]"
#define ERR_STR   "[ERR]"
#define WARN_STR  "[WARN]"
#define INFO_STR  "[INFO]"
#define DBG_STR   "[DEBUG]"

/* 定义不同等级日志宏 */
#define DP_LOG_CRIT(fmt, ...) DP_LOGOUT(__func__, __LINE__, DP_LOG_LEVEL_CRITICAL, CRIT_STR, fmt, ##__VA_ARGS__)
#define DP_LOG_ERR(fmt, ...)  DP_LOGOUT(__func__, __LINE__, DP_LOG_LEVEL_ERROR, ERR_STR, fmt, ##__VA_ARGS__)
#define DP_LOG_WARN(fmt, ...) DP_LOGOUT(__func__, __LINE__, DP_LOG_LEVEL_WARNING, WARN_STR, fmt, ##__VA_ARGS__)
#define DP_LOG_INFO(fmt, ...) DP_LOGOUT(__func__, __LINE__, DP_LOG_LEVEL_INFO, INFO_STR, fmt, ##__VA_ARGS__)
#define DP_LOG_DBG(fmt, ...)  DP_LOGOUT(__func__, __LINE__, DP_LOG_LEVEL_DEBUG, DBG_STR, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif
#endif
