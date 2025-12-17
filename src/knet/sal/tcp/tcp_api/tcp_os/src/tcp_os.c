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
#include <dlfcn.h>
#include <syslog.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#include "knet_types.h"
#include "knet_log.h"
#include "knet_osapi.h"
#include "knet_init.h"
#include "knet_signal_tcp.h"
#include "knet_tcp_api_init.h"

KNET_STATIC bool g_isForkedParent = true;

bool KNET_DpIsForkedParent(void)
{
    return g_isForkedParent;
}

int KNET_DpSigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
    KNET_CHECK_AND_GET_OS_API(g_origOsApi.sigaction, -1);

    return KNET_DpSignalDoSigaction(signum, act, oldact);
}

sighandler_t KNET_DpSignal(int signum, sighandler_t handler)
{
    if (g_origOsApi.signal == NULL) {
        GetOrigFunc();
    }
    if (g_origOsApi.signal == NULL) {
        KNET_ERR("Load system symbol failed.");
        return SIG_ERR;
    }

    return KNET_DpSignalDoSignal(signum, handler);
}

pid_t KNET_DpFork(void)
{
    KNET_CHECK_AND_GET_OS_API(g_origOsApi.fork, -1);

    if (!g_tcpInited) {
        KNET_AllThreadLock();
        KNET_LogMutexLock();
        pid_t pid = g_origOsApi.fork();
        KNET_LogMutexUnlock();
        KNET_AllThreadUnlock();
        return pid;
    }

    BEFORE_DPFUNC();
    KNET_AllThreadLock();
    KNET_LogMutexLock();
    pid_t pid = g_origOsApi.fork();
    if (pid == 0) {  // child
        g_isForkedParent = false;
        KNET_LogMutexUnlock();
        KNET_AllThreadUnlock();
        KNET_LogLevelSet(KNET_LOG_EMERG); // 子进程不打印日志
        g_tcpInited = false; // 子进程直接使用os接口
        KNET_INFO("Fork child");
    } else if (pid > 0) {  // parent
        g_isForkedParent = true;
        KNET_LogMutexUnlock();
        KNET_AllThreadUnlock();
    } else {
        KNET_LogMutexUnlock();
        KNET_AllThreadUnlock();
        KNET_ERR("Fork failed");
    }
    AFTER_DPFUNC();
    return pid;
}