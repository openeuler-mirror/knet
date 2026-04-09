/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */


#include "securec.h"

#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>

#include "knet_lock.h"
#include "knet_tcp_symbols.h"
#include "knet_hash_table.h"
#include "knet_signal_tcp.h"

#include "tcp_fd.h"
#include "common.h"
#include "mock.h"

extern "C" {
extern bool g_tcpInited;
uint32_t KNET_SemInitHook(DP_Sem_t sem, int32_t flag, uint32_t value);
void KNET_SemDeinitHook(DP_Sem_t sem);
uint32_t KNET_SemWaitHook(DP_Sem_t sem, int timeout);
void DefaultExitHandler(int signum);
}

static void SigintHandler(int signum)
{
    printf("[INFO] Received signal %d\n", signum);
}

static void SigintHandler2(int signum)
{
    printf("[INFO2] Received signal %d\n", signum);
}

DTEST_CASE_F(SIGNAL, TEST_SIGINT_OLDFUNC, Init, Deinit)
{
    int ret = sigaction(SIGINT, NULL, NULL);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = true;

    struct sigaction oldAct = { 0 };
    struct sigaction act = { 0 };
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = SigintHandler;
    ret = sigaction(SIGINT, &act, &oldAct);
    DT_ASSERT_EQUAL(ret, 0);
 
    /* 第二次注册SIGINT，能正确返回oldAct的函数指针 */
    ret = sigaction(SIGINT, &act, &oldAct);
    DT_ASSERT_EQUAL(oldAct.sa_handler, SigintHandler);
 
    /* signal,sigaction相互覆盖判断 */
    void (*func)(int) = NULL;
    func = signal(SIGINT, SigintHandler2);
    DT_ASSERT_EQUAL(func, SigintHandler);

    act.sa_handler = SIG_DFL;
    ret = sigaction(SIGINT, &act, &oldAct);
    DT_ASSERT_EQUAL(oldAct.sa_handler, SigintHandler2);

    func = signal(SIGINT, SigintHandler);
    DT_ASSERT_EQUAL(func, SIG_DFL);

    g_tcpInited = false;
}

DTEST_CASE_F(SIGNAL, TEST_SIGALRM_OLDFUNC, Init, Deinit)
{
    int ret = sigaction(SIGALRM, NULL, NULL);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = true;

    struct sigaction oldAct = { 0 };
    struct sigaction act = { 0 };
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = SigintHandler;
    ret = sigaction(SIGALRM, &act, &oldAct);
    DT_ASSERT_EQUAL(ret, 0);
 
    /* 第二次注册SIGINT，能正确返回oldAct的函数指针 */
    ret = sigaction(SIGALRM, &act, &oldAct);
    DT_ASSERT_EQUAL(oldAct.sa_handler, SigintHandler);

    /* signal,sigaction相互覆盖判断 */
    void (*func)(int) = NULL;
    func = signal(SIGALRM, SigintHandler2);
    DT_ASSERT_EQUAL(func, SigintHandler);

    act.sa_handler = SIG_DFL;
    ret = sigaction(SIGALRM, &act, &oldAct);
    DT_ASSERT_EQUAL(oldAct.sa_handler, SigintHandler2);

    func = signal(SIGALRM, SigintHandler);
    DT_ASSERT_EQUAL(func, SIG_DFL);

    g_tcpInited = false;
}

#define KNET_USLEEPTIME 500000
#define KNET_SEMWAITTIME 1100
#define KNET_ALARMTIME 1

static void *ThreadFunc(void *arg)
{
    pid_t pid = (pid_t)arg;
    /* 等待0.5s */
    usleep(KNET_USLEEPTIME);
    kill(pid, SIGURG);
}

DTEST_CASE_F(SIGNAL, TEST_SIGNAL_KNET_SEM_HOOK_SARESTART, NULL, NULL)
{
    int ret = 0;
    struct sigaction act = { 0 };
    sem_t sem;

    ret = KNET_SemInitHook(&sem, 0, 0);
    DT_ASSERT_EQUAL(ret, 0);
 
    sigemptyset(&act.sa_mask);
    act.sa_handler = SigintHandler;

    /* 触发alarm信号 */
    act.sa_flags = 0;
    sigaction(SIGALRM, &act, NULL);
    alarm(KNET_ALARMTIME);
    /* 设置3秒超时 */
    ret = KNET_SemWaitHook(&sem, KNET_SEMWAITTIME);
    DT_ASSERT_EQUAL(ret, EINTR);
    DT_ASSERT_EQUAL(errno, EINTR);

    act.sa_flags = SA_RESTART;
    alarm(KNET_ALARMTIME);
    sigaction(SIGALRM, &act, NULL);
    ret = KNET_SemWaitHook(&sem, KNET_SEMWAITTIME);
    DT_ASSERT_EQUAL(ret, ETIMEDOUT);
    DT_ASSERT_EQUAL(errno, ETIMEDOUT);

    /* 触发sigint信号 */
    act.sa_flags = 0;
    sigaction(SIGALRM, &act, NULL);
    alarm(KNET_ALARMTIME);
    /* 设置3秒超时 */
    ret = KNET_SemWaitHook(&sem, KNET_SEMWAITTIME);
    DT_ASSERT_EQUAL(ret, EINTR);
    DT_ASSERT_EQUAL(errno, EINTR);

    act.sa_flags = SA_RESTART;
    alarm(KNET_ALARMTIME);
    sigaction(SIGALRM, &act, NULL);
    ret = KNET_SemWaitHook(&sem, KNET_SEMWAITTIME);
    DT_ASSERT_EQUAL(ret, ETIMEDOUT);
    DT_ASSERT_EQUAL(errno, ETIMEDOUT);

    /* 触发默认行为为忽略的信号 */
    pid_t pid = getpid();
    pthread_t thread;
    act.sa_flags = 0;
    sigaction(SIGURG, &act, NULL);
    ret = pthread_create(&thread, NULL, ThreadFunc, (void *)pid);
    DT_ASSERT_EQUAL(ret, 0);
    ret = KNET_SemWaitHook(&sem, KNET_SEMWAITTIME);
    DT_ASSERT_EQUAL(ret, EINTR);
    DT_ASSERT_EQUAL(errno, EINTR);
    pthread_join(thread, NULL);

    act.sa_flags = SA_RESTART;
    sigaction(SIGURG, &act, NULL);
    ret = pthread_create(&thread, NULL, ThreadFunc, (void *)pid);
    DT_ASSERT_EQUAL(ret, 0);
    ret = KNET_SemWaitHook(&sem, KNET_SEMWAITTIME);
    DT_ASSERT_EQUAL(ret, ETIMEDOUT);
    DT_ASSERT_EQUAL(errno, ETIMEDOUT);
    pthread_join(thread, NULL);

    /* 恢复默认 */
    act.sa_handler = SIG_DFL;
    act.sa_flags = 0;
    sigaction(SIGALRM, &act, NULL);
    sigaction(SIGURG, &act, NULL);
    KNET_SemDeinitHook(&sem);
}

struct ThreadArgs {
    pid_t pid;
    int signum;
    int priv;
};

static void *ThreadFuncAllSignal(void *arg)
{
    struct ThreadArgs *args = (struct ThreadArgs *)arg;
    /* 等待0.5s */
    usleep(KNET_USLEEPTIME);
    args->priv = args->signum;
    kill(getpid(), args->signum);
    printf("thread killed %d\n", args->signum);
}

DTEST_CASE_F(SIGNAL, TEST_SIGNAL_KNET_SEM_HOOK_SIGACTION_SIGQUIT, NULL, NULL)
{
    int ret = 0;
    struct sigaction act = { 0 };
    struct sigaction oldact = { 0 };
    sem_t sem;

    ret = KNET_SemInitHook(&sem, 0, 0);
    DT_ASSERT_EQUAL(ret, 0);

    struct ThreadArgs args = { 0 };
    pthread_t thread;
    args.pid = getpid();

    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = SigintHandler;
    sigset_t set, oldset;
    sigprocmask(0, NULL, &oldset);
    sigemptyset(&set);
    sigaddset(&set, SIGQUIT);
    sigprocmask(SIG_UNBLOCK, &set, NULL);
 
    (void)memset_s(&oldact, sizeof(oldact), 0, sizeof(oldact));
    ret = sigaction(SIGQUIT, &act, &oldact);

    args.signum = SIGQUIT;
    ret = pthread_create(&thread, NULL, ThreadFuncAllSignal, (void *)&args);
    DT_ASSERT_EQUAL(ret, 0);
    /* 设置5秒超时 */
    ret = KNET_SemWaitHook(&sem, KNET_SEMWAITTIME);
    DT_ASSERT_EQUAL(ret, EINTR);
    DT_ASSERT_EQUAL(errno, EINTR);
    pthread_join(thread, NULL);
}
 
#define SIGUNKNOWN1 32
#define SIGUNKNOWN2 33
DTEST_CASE_F(SIGNAL, TEST_SIGNAL_KNET_SEM_HOOK_SIGACTION_ALL, NULL, NULL)
{
    int ret = 0;
    struct sigaction act = { 0 };
    struct sigaction oldact = { 0 };
    sem_t sem;

    ret = KNET_SemInitHook(&sem, 0, 0);
    DT_ASSERT_EQUAL(ret, 0);

    struct ThreadArgs args = { 0 };
    pthread_t thread;
    args.pid = getpid();

    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = SigintHandler;

    int sigArr[3] = {SIGINT, SIGQUIT, SIGTERM};
    int sigCount = sizeof(sigArr) / sizeof(sigArr[0]);
    for (int i = 0; i < sigCount; i++) {
        if (sigArr[i] == SIGBUS || sigArr[i] == SIGFPE || sigArr[i] == SIGSEGV) {
            /* 框架会在初始化时注册这几个信号,为不影响其正常功能这里跳过 */
            continue;
        }
        (void)memset_s(&oldact, sizeof(oldact), 0, sizeof(oldact));
        ret = sigaction(sigArr[i], &act, &oldact);
        if (ret != 0) {
            switch (sigArr[i]) {
                case SIGKILL:
                case SIGSTOP:
                case SIGUNKNOWN1:
                case SIGUNKNOWN2:
                    break;
                default:
                    /* 除以上几种信号外，其他信号都应该注册成功 */
                    DT_ASSERT_EQUAL(1, 0);
                    break;
            }
            printf("sigaction %d fail\n", sigArr[i]);
            continue;
        }
        args.signum = sigArr[i];
        ret = pthread_create(&thread, NULL, ThreadFuncAllSignal, (void *)&args);
        DT_ASSERT_EQUAL(ret, 0);
        /* 设置3秒超时 */
        ret = KNET_SemWaitHook(&sem, KNET_SEMWAITTIME);
        DT_ASSERT_EQUAL(ret, EINTR);
        DT_ASSERT_EQUAL(errno, EINTR);
        DT_ASSERT_EQUAL(args.priv, sigArr[i]);
        pthread_join(thread, NULL);
    }

    act.sa_handler = SIG_DFL;
    for (int i = 0; i < sigCount; i++) {
        if (sigArr[i] == SIGBUS || sigArr[i] == SIGFPE || sigArr[i] == SIGSEGV) {
            /* 框架会在初始化时注册这几个信号,为不影响其正常功能这里跳过 */
            continue;
        }
        (void)memset_s(&oldact, sizeof(oldact), 0, sizeof(oldact));
        ret = sigaction(sigArr[i], &act, &oldact);
        if (ret != 0) {
            switch (sigArr[i]) {
                case SIGKILL:
                case SIGSTOP:
                case SIGUNKNOWN1:
                case SIGUNKNOWN2:
                    break;
                default:
                    /* 除以上几种信号外，其他信号都应该注册成功 */
                    DT_ASSERT_EQUAL(1, 0);
                    break;
            }
            continue;
        }
        DT_ASSERT_EQUAL(SigintHandler, oldact.sa_handler);
    }

    for (int i = 0; i < sigCount; i++) {
        if (sigArr[i] == SIGBUS || sigArr[i] == SIGFPE || sigArr[i] == SIGSEGV) {
            /* 框架会在初始化时注册这几个信号,为不影响其正常功能这里跳过 */
            continue;
        }
        (void)memset_s(&oldact, sizeof(oldact), 0, sizeof(oldact));
        ret = sigaction(sigArr[i], &act, &oldact);
        if (ret != 0) {
            switch (sigArr[i]) {
                case SIGKILL:
                case SIGSTOP:
                case SIGUNKNOWN1:
                case SIGUNKNOWN2:
                    break;
                default:
                    /* 除以上几种信号外，其他信号都应该注册成功 */
                    DT_ASSERT_EQUAL(1, 0);
                    break;
            }
            continue;
        }
        DT_ASSERT_EQUAL(SIG_DFL, oldact.sa_handler);
    }
    KNET_SemDeinitHook(&sem);
}
 
DTEST_CASE_F(SIGNAL, TEST_SIGNAL_KNET_SEM_HOOK_SIGNAL_ALL, NULL, NULL)
{
    int ret = 0;
    void (*oldHandler)(int) = NULL;
    sem_t sem;

    ret = KNET_SemInitHook(&sem, 0, 0);
    DT_ASSERT_EQUAL(ret, 0);
 
    struct ThreadArgs args = { 0 };
    pthread_t thread;
    args.pid = getpid();

    int sigArr[3] = {SIGINT, SIGQUIT, SIGTERM};
    int sigCount = sizeof(sigArr) / sizeof(sigArr[0]);
    for (int i = 0; i < sigCount; i++) {
        if (sigArr[i] == SIGBUS || sigArr[i] == SIGFPE || sigArr[i] == SIGSEGV) {
            /* 框架会在初始化时注册这几个信号,为不影响其正常功能这里跳过 */
            continue;
        }
        oldHandler = signal(sigArr[i], SigintHandler);
        if (oldHandler == SIG_ERR) {
            switch (sigArr[i]) {
                case SIGKILL:
                case SIGSTOP:
                case SIGUNKNOWN1:
                case SIGUNKNOWN2:
                    break;
                default:
                    /* 除以上几种信号外，其他信号都应该注册成功 */
                    DT_ASSERT_EQUAL(1, 0);
                    break;
            }
            printf("signal %d fail\n", sigArr[i]);
            continue;
        }
        DT_ASSERT_EQUAL(SIG_DFL, oldHandler);
 
        args.signum = sigArr[i];
        ret = pthread_create(&thread, NULL, ThreadFuncAllSignal, (void *)&args);
        DT_ASSERT_EQUAL(ret, 0);
        /* 设置5秒超时 */
        ret = KNET_SemWaitHook(&sem, KNET_SEMWAITTIME);
        DT_ASSERT_EQUAL(ret, EINTR);
        DT_ASSERT_EQUAL(errno, EINTR);
        DT_ASSERT_EQUAL(args.priv, sigArr[i]);
        pthread_join(thread, NULL);
    }

    for (int i = 0; i < sigCount; i++) {
        if (sigArr[i] == SIGBUS || sigArr[i] == SIGFPE || sigArr[i] == SIGSEGV) {
            /* 框架会在初始化时注册这几个信号,为不影响其正常功能这里跳过 */
            continue;
        }
        oldHandler = signal(sigArr[i], SIG_DFL);
        if (oldHandler == SIG_ERR) {
            switch (sigArr[i]) {
                case SIGKILL:
                case SIGSTOP:
                case SIGUNKNOWN1:
                case SIGUNKNOWN2:
                    break;
                default:
                    /* 除以上几种信号外，其他信号都应该注册成功 */
                    DT_ASSERT_EQUAL(1, 0);
                    break;
            }
            printf("signal %d fail\n", sigArr[i]);
            continue;
        }
        DT_ASSERT_EQUAL(SigintHandler, oldHandler);
    }

    for (int i = 0; i < sigCount; i++) {
        if (sigArr[i] == SIGBUS || sigArr[i] == SIGFPE || sigArr[i] == SIGSEGV) {
            /* 框架会在初始化时注册这几个信号,为不影响其正常功能这里跳过 */
            continue;
        }
        oldHandler = signal(sigArr[i], SIG_DFL);
        if (oldHandler == SIG_ERR) {
            switch (sigArr[i]) {
                case SIGKILL:
                case SIGSTOP:
                case SIGUNKNOWN1:
                case SIGUNKNOWN2:
                    break;
                default:
                    /* 除以上几种信号外，其他信号都应该注册成功 */
                    DT_ASSERT_EQUAL(1, 0);
                    break;
            }
            printf("signal %d fail\n", sigArr[i]);
            continue;
        }
        DT_ASSERT_EQUAL(SIG_DFL, oldHandler);
    }

    KNET_SemDeinitHook(&sem);
}

static int g_testSignalTrigerCount = 0;
static void SigintHandler3(int signum)
{
    g_testSignalTrigerCount += 1;
    printf("[INFO3] Received signal %d, triger count %d\n", signum, g_testSignalTrigerCount);
}

DTEST_CASE_F(SIGNAL, TEST_SIGNAL_DELAY, NULL, NULL)
{
    int ret = 0;
    sem_t sem;
    ret = KNET_SemInitHook(&sem, 0, 0);
    (void)signal(SIGINT, SigintHandler3);

    struct ThreadArgs args = { 0 };
    args.pid = getpid();
    args.signum = SIGINT;

    pthread_t thread;
    ret = pthread_create(&thread, NULL, ThreadFuncAllSignal, (void *)&args);
    DT_ASSERT_EQUAL(ret, 0);

    errno = 0;
    /* 设置3秒超时 */
    BEFORE_DPFUNC();
    ret = KNET_SemWaitHook(&sem, KNET_SEMWAITTIME);
    DT_ASSERT_EQUAL(ret, EINTR);
    DT_ASSERT_EQUAL(errno, EINTR);
    DT_ASSERT_EQUAL(args.priv, SIGINT);
    DT_ASSERT_EQUAL(g_testSignalTrigerCount, 0);  // 此时还没触发信号
    AFTER_DPFUNC();
    DT_ASSERT_EQUAL(g_testSignalTrigerCount, 1);
}

DTEST_CASE_F(SIGNAL, TEST_SIGNAL_CLEAR_CURSIG, NULL, NULL)
{
    struct SignalTriggerTimes *triggerTimes = KNET_DpSignalTriggerTimesGet();
    DT_ASSERT_NOT_EQUAL(triggerTimes->knetSignalEnterCnt, 0);
    DefaultExitHandler(SIGALRM);
    KNET_DpSignalClearSigDelayCurSig();
    int curSig = KNET_DpSignalGetCurSig();
    DT_ASSERT_NOT_EQUAL(curSig, 0);
    KNET_DpSignalClearCurSig();
    curSig = KNET_DpSignalGetCurSig();
    DT_ASSERT_EQUAL(curSig, 0);
}