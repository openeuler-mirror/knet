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
#include <sys/syscall.h>
#include <pthread.h>
#include <time.h>
#include "securec.h"

#include "knet_config.h"
#include "knet_utils.h"
#include "knet_log.h"

#define PROCESS_PATH_SIZE 1024
#define MAX_LOG_LEN 1024
#define INVALID_PROC_NAME "invalid proc name"
#define NUM_2 2
#define MILLISECONDS_PER_SECOND 1000
#define NANOS_PER_MILLISECOND 1000000

#define PRINT_ERR(fmt, ...) printf("[ERR] %s|%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)

typedef struct {
    KNET_LogLevel level;
    const char *levelName;
} LogLevelName;

static KNET_LogLevel g_logLevel = KNET_LOG_DEFAULT;
static pthread_mutex_t g_logMutex = PTHREAD_MUTEX_INITIALIZER;
static const LogLevelName g_logLevelName[] = {
    {KNET_LOG_ERR, "ERROR" },
    {KNET_LOG_WARN, "WARNING"},
    {KNET_LOG_INFO, "INFO"},
    {KNET_LOG_DEBUG, "DEBUG"},
};

uint64_t KNET_GetCurrentTimeMillis(void)
{
    struct timespec ts = {0};
    int ret = clock_gettime(CLOCK_MONOTONIC, &ts);
    if (ret != 0) {
        KNET_ERR("K-NET clock gettime error, ret %d, errno %d, %s", ret, errno, strerror(errno));
        return KNET_ERROR;
    }
    // 将时间转换为毫秒，timespec在系统时间正常的情况下不会为负值。
    uint64_t milliseconds = (uint64_t)ts.tv_sec * MILLISECONDS_PER_SECOND
        + (uint64_t)ts.tv_nsec / NANOS_PER_MILLISECOND;
    return milliseconds;
}


void KNET_LogMutexLock(void)
{
    pthread_mutex_lock(&g_logMutex);
}

void KNET_LogMutexUnlock(void)
{
    pthread_mutex_unlock(&g_logMutex);
}

void KNET_LogLevelSet(KNET_LogLevel logLevel)
{
    if (logLevel >= KNET_LOG_MAX) {
        KNET_ERR("logLevel %u not support", logLevel);
        return;
    }
    g_logLevel = logLevel;
}

KNET_LogLevel KNET_LogLevelGet(void)
{
    return g_logLevel;
}

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

static void LogLockUnlock(void *arg)
{
    (void)arg;
    pthread_mutex_unlock(&g_logMutex);
}

static int ThreadSafeSyslog(int level, char logMsg[MAX_LOG_LEN])
{
    int origCancelType;
    int ret = pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &origCancelType);
    if (ret != 0) {
        PRINT_ERR("Pthread set canceltype failed, ret %d", ret);
        return ret;
    }
    int origCancelState;
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

static int LogMessage(const char *function, int line, int level, const char *format, va_list va)
{
    char newFormat[MAX_LOG_LEN] = {0};
    char procName[KNET_THREAD_NAME_LEN] = {0};
    char thread[KNET_THREAD_NAME_LEN] = {0};

    int ret = sprintf_s(newFormat, MAX_LOG_LEN, "[%s:%d | %s:%ld] "
#ifdef KNET_DEBUG_BUILD
        "%s[%d]|"
#endif
        "%s\n",
        GetSelfProcessName(procName, KNET_THREAD_NAME_LEN), getpid(),
        KNET_GetSelfThreadName(thread, KNET_THREAD_NAME_LEN), syscall(__NR_gettid),
#ifdef KNET_DEBUG_BUILD
        function, line,
#endif
        format);
    if (ret <= 0 || ret >= sizeof(newFormat)) {
        PRINT_ERR("Sprintf failed ret %d, errno %d", ret, errno);
        return ret;
    }

    char logMsg[MAX_LOG_LEN] = {0};
    ret = vsprintf_s(logMsg, MAX_LOG_LEN, newFormat, va);
    if (ret == -1) {
        PRINT_ERR("LogMsg size exceeds MAX_LOG_LEN size %d, errno %d", MAX_LOG_LEN, errno);
        return ret;
    }

    ret = ThreadSafeSyslog(level, logMsg);
    if (ret != 0) {
        PRINT_ERR("Failed to write log in a thread-safe manner, ret %d errno %d", ret, errno);
        return ret;
    }

#ifdef KNET_DEBUG_BUILD
    (void)printf("%s", logMsg);
#endif

    return ret;
}

void KNET_LogFixLenOutputHook(const char* format, ...)
{
    va_list args;
    va_start(args, format);

    char buf[MAX_LOG_LEN] = {0};
    int ret = vsprintf_s(buf, MAX_LOG_LEN, format, args);
    if (ret < 0) {
        PRINT_ERR("%s vsprintf failed, ret %d", __func__, ret);
        va_end(args);
        return;
    }
    va_end(args);

    char procName[KNET_THREAD_NAME_LEN] = {0};
    char logMsg[MAX_LOG_LEN] = {0};
    char thread[KNET_THREAD_NAME_LEN] = {0};

    ret = sprintf_s(logMsg, MAX_LOG_LEN, "[%s:%d | %s:%ld] %s\n",
        GetSelfProcessName(procName, sizeof(procName)), getpid(),
        KNET_GetSelfThreadName(thread, KNET_THREAD_NAME_LEN), syscall(__NR_gettid),
        buf);
    if (ret <= 0 || ret >= sizeof(logMsg)) {
        PRINT_ERR("Sprintf failed ret %d, errno %d", ret, errno);
        return;
    }

    ret = ThreadSafeSyslog(g_logLevel, logMsg);
    if (ret != 0) {
        PRINT_ERR("Failed to write log in a thread-safe manner, ret %d, errno %d", ret, errno);
        return;
    }
}

void KNET_Log(const char *function, int line, int level, const char *format, ...)
{
    if (likely(g_logLevel < level)) {
        return;
    }

    va_list va;
    int oriErrno = errno;
    va_start(va, format);
    (void)LogMessage(function, line, level, format, va);
    va_end(va);
    errno = oriErrno;
}

void KNET_LogNormal(const char *format, ...)
{
    va_list va;
    va_start(va, format);
    char logMsg[MAX_LOG_LEN] = {0};
    int ret = vsprintf_s(logMsg, MAX_LOG_LEN, format, va);
    if (ret == -1) {
        PRINT_ERR("LogMsg size exceeds MAX_LOG_LEN size %d errno %d", MAX_LOG_LEN, errno);
    } else {
        ThreadSafeSyslog(LOG_INFO, logMsg);
    }
    va_end(va);
}

void KNET_LogLevelSetByStr(const char *levelStr)
{
    KNET_LogLevelSet(KNET_LOG_DEFAULT);

    size_t len = sizeof(g_logLevelName) / sizeof(g_logLevelName[0]);
    for (size_t i = 0; i < len; ++i) {
        if (strcmp(levelStr, g_logLevelName[i].levelName) == 0) {
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