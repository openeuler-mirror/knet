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

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <pthread.h>
#include "rte_timer.h"
#include "rte_ethdev.h"
#include "securec.h"

#include "knet_config.h"
#include "knet_log.h"

#define PROCESS_PATH_SIZE 1024
#define MAX_LOG_LEN 1024
#define THREAD_NAME_LEN 128
#define INVALID_PROC_NAME "invalid proc name"
#define NUM_2 2
#define KNET_LIMIT_LOG_DEFAULT_INTERNEL_IN_US (100 * 1000) /* 0.1s */
#define US_IN_SEC 1000000

#define PRINT_ERR(fmt, ...) printf("[ERR] %s|%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)

static KnetLogLevel g_logLevel = KNET_LOG_DEFAULT;
static pthread_mutex_t g_logMutex = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_knetLimitLogInterval = KNET_LIMIT_LOG_DEFAULT_INTERNEL_IN_US;

uint64_t KNET_GetTicksHz(void)
{
    return rte_get_timer_hz();
}

uint64_t KNET_GetTicks(void)
{
    return rte_get_timer_cycles();
}

void KNET_LogMutexLock(void)
{
    pthread_mutex_lock(&g_logMutex);
}

void KNET_LogMutexUnlock(void)
{
    pthread_mutex_unlock(&g_logMutex);
}

void KNET_LogLevelSet(KnetLogLevel logLevel)
{
    if (logLevel >= KNET_LOG_MAX) {
        KNET_ERR("logLevel %u not support", logLevel);
        return;
    }
    g_logLevel = logLevel;
}

KnetLogLevel KNET_LogLevelGet(void)
{
    return g_logLevel;
}

static const KnetLogLevelName g_logLevelName[] = {
    {KNET_LOG_ERR, "ERROR" },
    {KNET_LOG_WARN, "WARNING"},
    {KNET_LOG_INFO, "INFO"},
    {KNET_LOG_DEBUG, "DEBUG"},
};

static const char *GetSelfProcessName(char *name, size_t len)
{
    char path[PROCESS_PATH_SIZE] = {0};
    if (readlink("/proc/self/exe", path, sizeof(path) - 1) <= 0) {
        return INVALID_PROC_NAME;
    }

    char *pname = strrchr(path, '/');
    if (pname == NULL || path + strlen(path) <= pname + NUM_2) {
        return INVALID_PROC_NAME;
    }
    if (strlen(pname + 1) > len) {
        return INVALID_PROC_NAME;
    }
    int ret = strcpy_s(name, len, pname + 1);
    if (ret != 0) {
        return INVALID_PROC_NAME;
    }

    return name;
}

static const char *GetSelfThreadName(char *name, size_t len)
{
    if (len < THREAD_NAME_LEN) {
        return "invalid thread name";
    }
    int ret = prctl(PR_GET_NAME, name);
    if (ret < 0) {
        return "invalid thread name";
    }

    return name;
}

static void LogLockUnlock(void *arg)
{
    (void)arg;
    pthread_mutex_unlock(&g_logMutex);
}

static int KnetThreadSafeSyslog(int level, char logMsg[MAX_LOG_LEN])
{
    int ret;
    int origCancelType, origCancelState;
    ret = pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &origCancelType);
    if (ret != 0) {
        PRINT_ERR("Pthread set canceltype failed, ret %d", ret);
        return ret;
    }
    pthread_cleanup_push(LogLockUnlock, NULL);
    ret = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &origCancelState);
    if (ret != 0) {
        PRINT_ERR("Pthread set cancelstate failed, ret %d", ret);
        return ret;
    }

    pthread_mutex_lock(&g_logMutex);
    syslog(level, "%s", logMsg);
    pthread_mutex_unlock(&g_logMutex);

    ret = pthread_setcancelstate(origCancelState, NULL);
    if (ret != 0) {
        PRINT_ERR("Pthread restore canceltype failed, ret %d", ret);
    }
    pthread_cleanup_pop(1);  // Ensures cleanup happens even if thread is canceled
    ret = pthread_setcanceltype(origCancelType, NULL);
    if (ret != 0) {
        PRINT_ERR("Pthread restore cancelstate failed, ret %d", ret);
    }

    return 0;
}

static int KnetLog(const char *function, int line, int level, const char *format, va_list va)
{
    char newFormat[MAX_LOG_LEN] = {0};
    char procName[THREAD_NAME_LEN] = {0};
    char thread[THREAD_NAME_LEN] = {0};

    int ret = sprintf_s(newFormat, MAX_LOG_LEN, "[%s:%d | %s:%ld] "
#ifdef KNET_DEBUG_BUILD
        "%s[%d]|"
#endif
        "%s\n",
        GetSelfProcessName(procName, THREAD_NAME_LEN), getpid(),
        GetSelfThreadName(thread, THREAD_NAME_LEN), syscall(__NR_gettid),
#ifdef KNET_DEBUG_BUILD
        function, line,
#endif
        format);
    if (ret <= 0 || ret >= sizeof(newFormat)) {
        (void)printf("Sprintf failed ret:%d errno:%d\n", ret, errno);
        return ret;
    }

    char logMsg[MAX_LOG_LEN] = {0};
    ret = vsprintf_s(logMsg, MAX_LOG_LEN, newFormat, va);
    if (ret == -1) {
        (void)printf("LogMsg size exceeds MAX_LOG_LEN size :%d errno:%d", MAX_LOG_LEN, errno);
        return ret;
    }

    ret = KnetThreadSafeSyslog(level, logMsg);
    if (ret != 0) {
        PRINT_ERR("Failed to write log in a thread-safe manner, ret:%d errno:%d", ret, errno);
        return ret;
    }

#ifdef KNET_DEBUG_BUILD
    (void)printf("%s", logMsg);
#endif

    return ret;
}

void KNET_FixLenOutputHook(const char* format, ...)
{
    int ret;
    char buf[MAX_LOG_LEN] = {0};
    char procName[THREAD_NAME_LEN] = {0};
    char thread[THREAD_NAME_LEN] = {0};
    char logMsg[MAX_LOG_LEN] = {0};

    va_list args;
    va_start(args, format);
    ret = vsprintf_s(buf, MAX_LOG_LEN, format, args);
    if (ret < 0) {
        (void)printf("%s: vsprintf failed, ret %d", __func__, ret);
        va_end(args);
        return;
    }
    va_end(args);

    ret = sprintf_s(logMsg, MAX_LOG_LEN, "[%s:%d | %s:%ld] %s\n",
        GetSelfProcessName(procName, sizeof(procName)), getpid(),
        GetSelfThreadName(thread, THREAD_NAME_LEN), syscall(__NR_gettid),
        buf);
    if (ret <= 0 || ret >= sizeof(logMsg)) {
        (void)printf("Sprintf failed ret:%d errno:%d\n", ret, errno);
        return;
    }

    ret = KnetThreadSafeSyslog(g_logLevel, logMsg);
    if (ret != 0) {
        PRINT_ERR("Failed to write log in a thread-safe manner, ret:%d errno:%d", ret, errno);
        return;
    }
}

void KNET_Log(const char *function, int line, int level, const char *format, ...)
{
    if (g_logLevel < level) {
        return;
    }

    va_list va;

    va_start(va, format);
    (void)KnetLog(function, line, level, format, va);
    va_end(va);
}

void KNET_LogLimit(const char *function, int line, int level, const char *format, ...)
{
    if (g_logLevel < level) {
        return;
    }

    static __thread uint64_t last = 0;
    uint64_t now = KNET_GetTicks();
    if ((now - last) < g_knetLimitLogInterval * (KNET_GetTicksHz() / US_IN_SEC)) {
        return;
    }
    last = now;

    va_list va;

    va_start(va, format);
    (void)KnetLog(function, line, level, format, va);
    va_end(va);
}

void KNET_LogNormal(const char *format, ...)
{
    int ret;

    va_list va;
    va_start(va, format);
    char logMsg[MAX_LOG_LEN] = {0};
    ret = vsprintf_s(logMsg, MAX_LOG_LEN, format, va);
    if (ret == -1) {
        (void)printf("LogMsg size exceeds MAX_LOG_LEN size :%d errno:%d\n", MAX_LOG_LEN, errno);
    } else {
        KnetThreadSafeSyslog(LOG_INFO, logMsg);
    }
    va_end(va);
}

/**
 * @attention 必须在配置文件解析之后执行
 */
void KNET_LogLevelConfigure(void)
{
    KNET_LogLevelSet(KNET_LOG_DEFAULT);
    const char *confLevel = KNET_GetCfg(CONF_COMMON_LOG_LEVEL).strValue;
    
    size_t len = sizeof(g_logLevelName) / sizeof(g_logLevelName[0]);
    for (size_t i = 0; i < len; ++i) {
        if (strcmp(confLevel, g_logLevelName[i].levelName) == 0) {
            KNET_LogLevelSet(g_logLevelName[i].level);
            return;
        }
    }
    KNET_ERR("Log level not support, use default log level %u", KNET_LOG_DEFAULT);
}

void KNET_LogInit()
{
    openlog(KNET_LOG_MODULE_NAME, LOG_PID | LOG_CONS | LOG_NDELAY, LOG_USER);
    (void)setlogmask(LOG_MASK(LOG_ERR) | LOG_MASK(LOG_WARNING) | LOG_MASK(LOG_INFO) | LOG_MASK(LOG_DEBUG));
}

void KNET_LogUninit(void)
{
    closelog();
}