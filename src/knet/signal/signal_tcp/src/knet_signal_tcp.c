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
#include <syslog.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <errno.h>

#include "knet_types.h"
#include "knet_log.h"
#include "knet_osapi.h"
#include "knet_init.h"
#include "tcp_os.h"
#include "knet_lock.h"

#include "knet_signal_tcp.h"

#define KNET_SIGDFL   1
#define KNET_SIGIGN   2
#define KNET_SIGOTHER 3

__thread struct KnetDpSignalFlags g_knetDpSignalFlags = {0};
static bool g_knetInitSignalRegistering = false; // knet初始化时注册信号标记
static struct sigaction g_userSignalHandler[_NSIG + 1] = { 0 }; // 回给用户注册的回调
static struct sigaction g_knetSignalHandler[_NSIG + 1] = { 0 }; // knet实际会调用的回调

KNET_STATIC bool g_tcpWaitExit = false;

static struct SignalTriggerTimes g_signalTriggerTimes = {0};
 
struct SignalTriggerTimes* KNET_DpSignalTriggerTimesGet(void)
{
    return &g_signalTriggerTimes;
}

KNET_STATIC void DefaultExitHandler(int signum)
{
    switch (signum) {
        case SIGINT:
        case SIGQUIT:
        case SIGTERM:
            exit(0);
        default:
            break;
    }

    return;
}

static void DefaultOtherHandler(int signum)
{
    (void)raise(signum);
    return;
}

static void KnetSigHandler(int signum)
{
    /* 不带SA_SIGINFO标记触发信号后实际会调用到该函数 */
    g_knetDpSignalFlags.inSigHandler = true;
    g_knetDpSignalFlags.sigDelayCurSig = signum;
    g_knetDpSignalFlags.curSig = signum;

    KNET_SpinlockLock(&g_signalTriggerTimes.lock);
    ++g_signalTriggerTimes.knetSignalEnterCnt;
    KNET_SpinlockUnlock(&g_signalTriggerTimes.lock);

    KNET_LogLevel origLogLevel = KNET_LogLevelGet();
    KNET_LogLevelSet(KNET_LOG_EMERG); // 信号处理函数流程不打印日志
    switch (signum) {
        case SIGINT:
        case SIGQUIT:
        case SIGTERM:
            /* 在dp流程中,置标志位延后处理 */
            if (g_knetDpSignalFlags.sigDelay) {
                g_knetDpSignalFlags.curExitSig = signum;
                g_knetDpSignalFlags.sigExitTriggered = true;
            } else {
                /* 如果带了SA_RESTART标记,说明希望继续阻塞,将sig置0后续判断不需要退出阻塞流程 */
                if ((uint32_t)g_knetSignalHandler[signum].sa_flags & SA_RESTART) {
                    g_knetDpSignalFlags.sigDelayCurSig = 0;
                    g_knetDpSignalFlags.curSig = 0;
                }
                /* 不在dp流程中,直接调用用户回调函数 */
                if (g_knetSignalHandler[signum].sa_handler != NULL) {
                    g_knetSignalHandler[signum].sa_handler(signum);
                }
            }
            break;
        default:
            /* 如果带了SA_RESTART标记,说明希望继续阻塞,将sig置0后续判断不需要退出阻塞流程 */
            if ((uint32_t)g_knetSignalHandler[signum].sa_flags & SA_RESTART) {
                g_knetDpSignalFlags.sigDelayCurSig = 0;
                g_knetDpSignalFlags.curSig = 0;
            }
            if (g_knetSignalHandler[signum].sa_handler != NULL) {
                g_knetSignalHandler[signum].sa_handler(signum);
            }
            break;
    }
    KNET_LogLevelSet(origLogLevel);

    KNET_SpinlockLock(&g_signalTriggerTimes.lock);
    ++g_signalTriggerTimes.knetSignalExitCnt;
    KNET_SpinlockUnlock(&g_signalTriggerTimes.lock);

    g_knetDpSignalFlags.inSigHandler = false;
    return;
}

static void KnetSigHandlerSigInfo(int signum, siginfo_t *info, void *secret)
{
    /*
    约束: 对于带SIGINFO标记的信号处理,因为无法通过判断用户回调里是否会退出进程而
          提前关闭我们的线程(对于redis场景需要提前关闭knet的线程,否则会报错),因
          此添加约束:对于所有注册带SIGINFO的默认行为会退出的信号,执行完后都会统
          一退出进程。
    */
    KNET_SpinlockLock(&g_signalTriggerTimes.lock);
    ++g_signalTriggerTimes.knetSignalEnterCnt;
    KNET_SpinlockUnlock(&g_signalTriggerTimes.lock);

    KNET_LogLevel origLogLevel = KNET_LogLevelGet();
    KNET_LogLevelSet(KNET_LOG_EMERG); // 信号处理函数流程不打印日志

    if (KNET_DpIsForkedParent()) {
        KNET_SetDpdkAndStackThreadStop();
        usleep(KNET_SIGQUIT_WAIT); // 等待10ms
    }
    if (g_knetSignalHandler[signum].sa_sigaction != NULL) {
        g_knetSignalHandler[signum].sa_sigaction(signum, info, secret);
    }

    KNET_LogLevelSet(origLogLevel);

    KNET_SpinlockLock(&g_signalTriggerTimes.lock);
    ++g_signalTriggerTimes.knetSignalExitCnt;
    KNET_SpinlockUnlock(&g_signalTriggerTimes.lock);

    /* 防止用户回调函数里没有退出的想法,这里强行让程序退出 */
    (void)raise(signum);
    return;
}

static int RegSigHandlerToOS(int signum, const struct sigaction *act, bool isSignal)
{
    int ret = 0;

    /* signal注册 */
    if (isSignal == true) {
        errno = 0;
        (void)g_origOsApi.signal(signum, act->sa_handler);
        if (errno != 0) {
            KNET_ERR("Signum %d handler registered OTHER failed, errno %d, %s.", signum, errno, strerror(errno));
            ret = -1;
        }
        return ret;
    }

    ret = g_origOsApi.sigaction(signum, act, NULL);
    if (ret != 0) {
        KNET_ERR("Regist knet signal %d hander failed! errno:%d, %s", signum, errno, strerror(errno));
    }
    return ret;
}

static void ProcOSHandlerNomal(int signum, int handlerFlag, struct sigaction *actToOS, struct sigaction *actToKnet)
{
    /*
    * 对用户注册不同的信号处理函数(SIG_DFL,SIG_IGN,其他处理函数other_handler):
    * Knet会根据不同的信号，对应向内核注册不同的函数(toOS),和保存不同的后续处理函数(toKnet)
    * 触发信号时,内核首先会执行toOS的函数,如果toOS的函数是KnetSigHandler,会在这个函数里再调用toKnet的函数
    --------------------------------------+---------------------+---------------+-------------------
          用户注册的信号函数类型:           |      SIG_DFL        |    SIG_IGN    |    other_handler
    --------------------------------------+---------------------+---------------+-------------------
    SINGINT,SIGQUIT,SIGTERM    | toOS	  |  KnetSigHandler     |    SIG_IGN    |   KnetSigHandler
                               | toKnet   |  DefaultExitHandler |    SIG_IGN    |    other_handler
    ---------------------------+----------+---------------------+---------------+-------------------
    SIGCHLD,SIGPOLL,SIGWINCH   | toOS	  |      SIG_DFL        |    SIG_IGN    |   KnetSigHandler
    ...                        | toKnet   |      SIG_DFL        |    SIG_IGN    |    other_handler
    ---------------------------+----------+---------------------+---------------+-------------------
    other signals	           | toOS	  |  KnetSigHandler     |    SIG_IGN    |   KnetSigHandler
                               | toKnet   |  DefaultOtherHandler|    SIG_IGN    |    other_handler
    ---------------------------+----------+---------------------+---------------+-------------------
    */
    if (handlerFlag == KNET_SIGIGN) {
        return;
    }

    switch (signum) {
        case SIGINT:
        case SIGQUIT:
        case SIGTERM:
            if (handlerFlag == KNET_SIGDFL) {
                actToOS->sa_handler = KnetSigHandler;
                actToKnet->sa_handler = DefaultExitHandler;
            } else if (handlerFlag == KNET_SIGOTHER) {
                actToOS->sa_handler = KnetSigHandler;
            }
            break;
        case SIGCHLD:
        case SIGPOLL:
        case SIGWINCH:
        case SIGURG:
        case SIGCONT:
        case SIGSTOP:
        case SIGTSTP:
        case SIGTTOU:
        case SIGTTIN:
            if (handlerFlag == KNET_SIGOTHER) {
                actToOS->sa_handler = KnetSigHandler;
            }
            break;
        default:
            if (handlerFlag == KNET_SIGDFL) {
                /* 添加SA_NODEFER防止递归调用被阻塞,SA_RESETHAND标记让其再次触发行为为默认行为 */
                actToOS->sa_flags |= SA_NODEFER | SA_RESETHAND;
                actToOS->sa_handler = KnetSigHandler;
                actToKnet->sa_handler = DefaultOtherHandler;
            } else if (handlerFlag == KNET_SIGOTHER) {
                actToOS->sa_handler = KnetSigHandler;
            }
            break;
    }
}

static void ProcOSHandlerSigInfo(int signum, int handlerFlag, struct sigaction *actToOS, struct sigaction *actToKnet)
{
    if (handlerFlag == KNET_SIGDFL) {
        return;
    }

    switch (signum) {
        case SIGCHLD:
        case SIGPOLL:
        case SIGWINCH:
        case SIGURG:
        case SIGCONT:
        case SIGSTOP:
        case SIGTSTP:
        case SIGTTOU:
        case SIGTTIN:
            /* 这些信号默认不会退出进程,无法处理,直接后续向内核注册用户的回调 */
            break;
        default:
            /* 这些信号行为会有约束,见函数KnetSigHandlerSigInfo */
            actToOS->sa_flags |= SA_NODEFER | SA_RESETHAND;
            actToOS->sa_sigaction = KnetSigHandlerSigInfo;
            break;
    }

    return;
}

static int SaveSignalHandler(int signum, bool isSignal, const struct sigaction *usrAct, struct sigaction *oldAct)
{
    if (usrAct == NULL) {
        KNET_ERR("Signum %d save handler failed.", signum);
        return -1;
    }

    int ret = 0;
    int handlerFlag = 0;
    struct sigaction actToKnet = *usrAct;
    struct sigaction actToOS = *usrAct;

    /* 带不带SA_SIGINFO标记的信号处理是两套流程 */
    if ((uint32_t)usrAct->sa_flags & SA_SIGINFO) {
        if (usrAct->sa_sigaction == NULL) {
            handlerFlag = KNET_SIGDFL;
        } else {
            handlerFlag = KNET_SIGOTHER;
        }
        ProcOSHandlerSigInfo(signum, handlerFlag, &actToOS, &actToKnet);
    } else {
        if (usrAct->sa_handler == SIG_DFL) {
            handlerFlag = KNET_SIGDFL;
        } else if (usrAct->sa_handler == SIG_IGN) {
            handlerFlag = KNET_SIGIGN;
        } else {
            handlerFlag = KNET_SIGOTHER;
        }
        ProcOSHandlerNomal(signum, handlerFlag, &actToOS, &actToKnet);
    }

    KNET_DEBUG("%s registered signum %d, handler flag %d.", isSignal? "Signal":"Sigaction",
        signum, handlerFlag);

    /* 可能存在用户比Knet更快注册的情况,如果用户注册了该信号则在knet初始化时不再注册 */
    if (g_knetInitSignalRegistering) {
        if (g_userSignalHandler[signum].sa_handler != NULL) {
            return ret;
        }
    }

    /* 向内核注册信号处理函数 */
    ret = RegSigHandlerToOS(signum, &actToOS, isSignal);
    if (ret != 0) {
        return ret;
    }

    if (oldAct != NULL) {
        *oldAct = g_userSignalHandler[signum];
    }
 
    if (!g_knetInitSignalRegistering) {
        /* 返回给用户的原版的旧信号处理函数 */
        g_userSignalHandler[signum] = *usrAct;
    }
    /* 保存用户注册的信号处理函数,在knet捕获信号后再调用 */
    g_knetSignalHandler[signum] = actToKnet;
    return ret;
}

/**
 * @brief tcp sigaction函数入口
 */
int KNET_DpSignalDoSigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
    int ret = 0;
    if (signum < 1 || signum >= _NSIG) {
        errno = EINVAL;
        return -1;
    }
    if (act == NULL) {
        if (oldact != NULL) {
            *oldact = g_userSignalHandler[signum];
        }
        return ret;
    }

    /* 保存用户注册的信号处理函数,并根据情况向内核注册对应的信号处理函数 */
    ret = SaveSignalHandler(signum, false, act, oldact);
    return ret;
}

/**
 * @brief tcp signal函数入口
 */
sighandler_t KNET_DpSignalDoSignal(int signum, sighandler_t handler)
{
    if (signum < 1 || signum >= _NSIG) {
        errno = EINVAL;
        return SIG_ERR;
    }
    int ret = 0;

    /* 以act.sa_flags为例,当signal和sigaction互相注册时,
        signal函数会将flags的值变得不可参考,这里直接置成0覆盖 */
    sighandler_t oldHandler = NULL;
    struct sigaction act = { 0 };
    struct sigaction oldact = { 0 };
    act.sa_handler = handler;
    sigemptyset(&act.sa_mask);

    /* 保存用户注册的信号处理函数,并根据情况向内核注册对应的信号处理函数 */
    ret = SaveSignalHandler(signum, true, &act, &oldact);
    if (ret != 0) {
        return SIG_ERR;
    }

    oldHandler = oldact.sa_handler;
    return oldHandler;
}

/**
 * @brief 收到信号时执行用户注册的信号回调函数(仅限退出信号)
 */
void KNET_DpSigProcUserSigHandler(void)
{
    /* 执行用户注册的信号回调 */
    if (g_knetDpSignalFlags.inExitUserHandler) {
        return;
    }

    /* 收到退出信号,执行用户信号回调函数 */
    int curSignum = g_knetDpSignalFlags.curExitSig;
    if (curSignum != SIGINT && curSignum != SIGTERM && curSignum != SIGQUIT) {
        /* 正常不会走到这里,不过还是拦截一下 */
        KNET_ERR("Received stop signal %d which should not be catched!");
        return;
    }
    if (g_knetSignalHandler[curSignum].sa_handler == NULL) {
        /* 正常不会走到这里,不过还是拦截一下 */
        KNET_ERR("Received stop signal %d but user handler is NULL!");
        return;
    }
    g_knetDpSignalFlags.inExitUserHandler = true;
    g_knetSignalHandler[curSignum].sa_handler(curSignum);
    g_knetDpSignalFlags.inExitUserHandler = false;

    return;
}

/**
 * @brief 信号延迟处理流程中，是否收到中断信号需要退出
 */
bool KNET_DpSignalGetSigDelayCurSig(void)
{
    int signum = g_knetDpSignalFlags.sigDelayCurSig;
    /* 调用一次后将sig清零 */
    g_knetDpSignalFlags.sigDelayCurSig = 0;
    return signum;
}

/**
 * @brief 信号延迟处理流程中，清除信号标志位
 */
void KNET_DpSignalClearSigDelayCurSig(void)
{
    g_knetDpSignalFlags.sigDelayCurSig = 0;
    return;
}

/**
 * @brief 是否收到中断信号需要退出
 */
bool KNET_DpSignalGetCurSig(void)
{
    int signum = g_knetDpSignalFlags.curSig;
    /* 调用一次后将sig清零 */
    g_knetDpSignalFlags.curSig = 0;
    return signum;
}

/**
 * @brief 清除信号标志位
 */
void KNET_DpSignalClearCurSig(void)
{
    g_knetDpSignalFlags.curSig = 0;
    return;
}

/**
 * @brief 是否在信号流程内
 */
bool KNET_DpSignalIsInSigHandler(void)
{
    return g_knetDpSignalFlags.inSigHandler;
}

/**
 * @brief 主线程是否在等待其他线程退出DP流程
 */
bool KNET_DpSignalGetWaitExit(void)
{
    return g_tcpWaitExit;
}

/**
 * @brief 设置主线程等待其他线程退出标志
 */
void KNET_DpSignalSetWaitExit(void)
{
    g_tcpWaitExit = true;
    return;
}

/**
 * @brief k-net初始化时注册信号回调
 */
void KNET_DpSignalRegAll(void)
{
    struct sigaction act = {0};
    sigemptyset(&act.sa_mask);
    act.sa_handler = SIG_DFL;
    act.sa_flags = 0;

    /* 注册所有信号默认处理 */
    g_knetInitSignalRegistering = true;
    for (int i = 1; i < _NSIG; i++) {
        /* 这几种信号内核不允许注册,跳过这几种信号的注册,避免打印ERR_LOG */
        if (i == SIGKILL || i == SIGSTOP || i == SIGUNKNOWN1 || i == SIGUNKNOWN2) {
            continue;
        }
        sigaction(i, &act, NULL);
    }
    g_knetInitSignalRegistering = false;
    return;
}
