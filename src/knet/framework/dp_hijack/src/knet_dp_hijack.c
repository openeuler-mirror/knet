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
#define _GNU_SOURCE
#include <poll.h>
#include <dlfcn.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <syslog.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <malloc.h>
#include <sys/timeb.h>
#include <sys/ioctl.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <errno.h>
#include <netinet/tcp.h>

#include "dp_posix_socket_api.h"
#include "dp_posix_poll_api.h"
#include "dp_posix_epoll_api.h"
#include "dp_debug_api.h"
#include "knet_types.h"
#include "knet_log.h"
#include "knet_dp_fd.h"
#include "knet_atomic.h"
#include "knet_lock.h"
#include "knet_config.h"
#include "os_api.h"
#include "init.h"
#include "knet_symbols.h"
#include "knet_dp_hijack_inner.h"
#include "knet_dp_hijack.h"

/* 调用dp接口前处理 */
#define BEFORE_DPFUNC()                                          \
    do {                                                         \
        g_signalFlags.sigDelay = true;                           \
    } while (0)

/* 调用dp接口后处理 */
#define AFTER_DPFUNC()                                           \
    do {                                                         \
        g_signalFlags.sigDelay = false;                          \
        if (KNET_UNLIKELY(g_signalFlags.sigExitTriggered)) {     \
            g_signalFlags.sigExitTriggered = false;              \
            ProcUserSigHandler();                                \
        }                                                        \
    } while (0)

bool g_dpInited = false; // true: dp协议栈初始化完成

struct OsApi g_origOsApi = {0};

/* 信号处理用到的全局变量 */
static __thread struct SignalFlags g_signalFlags = {0};
static bool g_knetInitSignalRegistering = false; // knet初始化时注册信号标记
static struct sigaction g_userSignalHandler[_NSIG + 1] = { 0 }; // 回给用户注册的回调
static struct sigaction g_knetSignalHandler[_NSIG + 1] = { 0 }; // knet实际会调用的回调
#ifdef KNET_TEST
bool g_dpWaitExit = false;
#else
static bool g_dpWaitExit = false;
#endif

static struct SignalTriggerTimes g_signalTriggerTimes = {0};

struct SignalTriggerTimes* KNET_SignalTriggerTimesGet(void)
{
    return &g_signalTriggerTimes;
}

/**
 * @brief 是否收到中断信号需要退出
 */
bool KNET_IsSignalTriggered(void)
{
    int signum = g_signalFlags.curSig;
    /* 调用一次后将sig清零 */
    g_signalFlags.curSig = 0;
    return signum;
}

void KNET_CleanSignalTriggered(void)
{
    g_signalFlags.curSig = 0;
    return;
}

/**
 * @brief 是否在信号流程内
 */
bool KNET_IsInSignal(void)
{
    return g_signalFlags.inSigHandler;
}

/**
 * @brief 主进程是否在等待其他线程退出DP流程
 */
bool KNET_IsDpWaitingExit(void)
{
    return g_dpWaitExit;
}

#ifdef KNET_TEST
bool g_isForkedParent = true;
#else
static bool g_isForkedParent = true;
#endif

bool KNET_IsForkedParent(void)
{
    return g_isForkedParent;
}

static void GetOrigFunc(void)
{
    static KNET_SpinLock lock = {
        .value = KNET_SPIN_UNLOCKED_VALUE,
    };

    KNET_SpinlockLock(&lock);
    OsGetOrigFunc(&g_origOsApi); // 加入函数
    KNET_SpinlockUnlock(&lock);
}

void SetDpInited(void)
{
    KNET_INFO("Dp init success");
    g_dpInited = true;
    KnetFdInit();
}

/**
 * @brief k-net初始化时注册信号回调
 */
void KNET_SigactionReg(void)
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

/**
 * @brief 执行用户注册的信号回调
 */
static void ProcUserSigHandler(void)
{
    if (g_signalFlags.inExitUserHandler) {
        return;
    }

    /* 收到退出信号,执行用户信号回调函数 */
    int curSignum = g_signalFlags.curExitSig;
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
    g_signalFlags.inExitUserHandler = true;
    g_knetSignalHandler[curSignum].sa_handler(curSignum);
    g_signalFlags.inExitUserHandler = false;

    return;
}

/**
 * @brief 不带SA_SIGINFO标记触发信号后实际会调用到该函数
 */
static void KnetSigHandler(int signum)
{
    g_signalFlags.inSigHandler = true;
    g_signalFlags.curSig = signum;

    KNET_SpinlockLock(&g_signalTriggerTimes.lock);
    ++g_signalTriggerTimes.knetSignalEnterCnt;
    KNET_SpinlockUnlock(&g_signalTriggerTimes.lock);

    KnetLogLevel origLogLevel = KNET_LogLevelGet();
    KNET_LogLevelSet(KNET_LOG_EMERG); // 信号处理函数流程不打印日志

    switch (signum) {
        case SIGINT:
        case SIGQUIT:
        case SIGTERM:
            /* 在dp流程中,置标志位延后处理 */
            if (g_signalFlags.sigDelay) {
                g_signalFlags.curExitSig = signum;
                g_signalFlags.sigExitTriggered = true;
            } else {
                /* 如果带了SA_RESTART标记,说明希望继续阻塞,将sig置0后续判断不需要退出阻塞流程 */
                if ((uint32_t)g_knetSignalHandler[signum].sa_flags & SA_RESTART) {
                    g_signalFlags.curSig = 0;
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
                g_signalFlags.curSig = 0;
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

    g_signalFlags.inSigHandler = false;
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

    KnetLogLevel origLogLevel = KNET_LogLevelGet();
    KNET_LogLevelSet(KNET_LOG_EMERG); // 信号处理函数流程不打印日志

    if (KNET_IsForkedParent()) {
        KNET_SetDpdkAndStackThreadStop();
        usleep(KNET_SIGQUIT_WAIT); // 等待10ms
        (void)KNET_JoinDpdkAndStackThread();
    }
    if (g_knetSignalHandler[signum].sa_sigaction != NULL) {
        g_knetSignalHandler[signum].sa_sigaction(signum, info, secret);
    }
    /* 防止用户回调函数里没有退出的想法,这里强行让程序退出 */
    (void)raise(signum);

    KNET_LogLevelSet(origLogLevel);

    KNET_SpinlockLock(&g_signalTriggerTimes.lock);
    ++g_signalTriggerTimes.knetSignalExitCnt;
    KNET_SpinlockUnlock(&g_signalTriggerTimes.lock);

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
                actToKnet->sa_handler = KNET_DefaultExitHandler;
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
                actToKnet->sa_handler = KNET_DefaultOtherHandler;
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

KNET_API int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
    CHECK_AND_GET_OS_API(g_origOsApi.sigaction, INVALID_FD);

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

KNET_API sighandler_t signal(int signum, sighandler_t handler)
{
    if (g_origOsApi.signal == NULL) {
        GetOrigFunc();
    }
    if (g_origOsApi.signal == NULL) {
        KNET_ERR("Load system symbol failed.");
        return SIG_ERR;
    }
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
 * @note 协议栈目前UDP默认发送缓冲区9K，接收缓冲区40K，与内核不一致，需要KNET侧额外设置
*/
static void UdpBufSizeSet(int domain, int type, int dpFd)
{
    if (domain == AF_INET && type == SOCK_DGRAM) {
        int bufLen = 212992; // 内核UDP socket发送/接收缓冲区大小默认值212922=208KB
        BEFORE_DPFUNC();
        int ret = DP_PosixSetsockopt(dpFd, SOL_SOCKET, SO_SNDBUF, &bufLen, sizeof(bufLen));
        AFTER_DPFUNC();
        if (ret < 0) {
            KNET_ERR("DP_PosixSetsockopt send buf size failed, ret %d, errno %d, %s, bufLen %d",
                ret, errno, strerror(errno), bufLen);
        }
        BEFORE_DPFUNC();
        ret = DP_PosixSetsockopt(dpFd, SOL_SOCKET, SO_RCVBUF, &bufLen, sizeof(bufLen));
        AFTER_DPFUNC();
        if (ret < 0) {
            KNET_ERR("DP_PosixSetsockopt recv buf failed, ret %d, errno %d, %s, bufLen %d",
                ret, errno, strerror(errno), bufLen);
        }
    }
}

/**
 * @note 协议栈目前不支持tcp在建连阶段自动根据网卡设备的mtu设置mss，需要KNET侧额外设置
*/
static void TcpMssSet(int sockfd)
{
    int mss = KNET_GetCfg(CONF_INTERFACE_MTU).intValue - 40; // 40字节是IP头+tcp头的长度
    size_t len = sizeof(mss);
    int ret = DP_PosixSetsockopt(OsFdToDpFd(sockfd), IPPROTO_TCP, TCP_MAXSEG, &mss, (socklen_t)len);
    if (ret < 0) {
        KNET_ERR("DP_PosixSetsockopt ret %d, errno %d, %s", ret, errno, strerror(errno));
    }
}

KNET_API int socket(int domain, int type, int protocol)
{
    /* 在协议栈使用socket前，完成对打流相关资源的初始化
     * 目前协议栈仅支持TCP和UDP，若后续有增补须同步修改 */
    int ret;
    if (domain == AF_INET && (type == SOCK_STREAM || type == SOCK_DGRAM)) {
        ret = KNET_TrafficResourcesInit();
        if (ret != 0) {
            errno = ENAVAIL;
            KNET_ERR("Traffic resources init failed, errno %d, %s", errno, strerror(errno));
            return INVALID_FD;
        }
        KNET_INFO("Traffic domain %d, type %d, protocol %d", domain, type, protocol);
    }

    /* 在主线程等待退出的时候,走内核的创建 */
    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.socket, INVALID_FD);
        int osFd = g_origOsApi.socket(domain, type, protocol);
        return osFd;
    }
    /* 信号退出流程中直接退出 */
    if (g_dpWaitExit) {
        errno = EPERM;
        KNET_WARN("Function socket was not allowed to be called in signal exiting process, errno %d, %s",
            errno, strerror(errno));
        return -1;
    }
    if (domain == AF_UNIX || domain == AF_LOCAL || domain == AF_NETLINK) {
        CHECK_AND_GET_OS_API(g_origOsApi.socket, INVALID_FD);
        int osFd = g_origOsApi.socket(domain, type, protocol);
        KNET_INFO("OSFd %d IPC domain %d type %d protocol %d go os", osFd, domain, type, protocol);
        return osFd;
    }

    BEFORE_DPFUNC();
    int dpFd = DP_PosixSocket(domain, type, protocol);
    AFTER_DPFUNC();

    if (dpFd < 0) {
        KNET_ERR("DP_PosixSocket ret %d, errno %d, %s, domain %d, type %d, protocol %d",
            dpFd, errno, strerror(errno), domain, type, protocol);
        return INVALID_FD;
    }

    CHECK_AND_GET_OS_API(g_origOsApi.socket, INVALID_FD);
    int osFd = g_origOsApi.socket(domain, type, protocol);
    if (!IsFdValid(osFd)) {
        KNET_ERR("OS socket ret %d, errno %d, %s", osFd, errno, strerror(errno));
        DP_PosixClose(dpFd);
        return INVALID_FD;
    }

    UdpBufSizeSet(domain, type, dpFd);

    SetFdSocketState(FD_STATE_HIJACK, osFd, dpFd);
    KNET_INFO("OSFd %d, dpFd %d, domain %d, type %d, protocol %d",
        osFd, dpFd, domain, type, protocol);

    return osFd;
}

KNET_API int listen(int sockfd, int backlog)
{
    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.listen, INVALID_FD);
        return g_origOsApi.listen(sockfd, backlog);
    }

    if (!IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d listen is not hijacked, backlog %d", sockfd, backlog);
        CHECK_AND_GET_OS_API(g_origOsApi.listen, INVALID_FD);
        return g_origOsApi.listen(sockfd, backlog);
    }

    TcpMssSet(sockfd);
    BEFORE_DPFUNC();
    int ret = DP_PosixListen(OsFdToDpFd(sockfd), backlog);
    AFTER_DPFUNC();
    if (ret < 0) {
        KNET_ERR("DP_PosixListen ret %d, errno %d, %s", ret, errno, strerror(errno));
    }
    KNET_DEBUG("Listen success, listenfd %d, dpListenfd %d, backlog %d", sockfd, OsFdToDpFd(sockfd), backlog);
    return ret;
}

KNET_API int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.bind, INVALID_FD);
        return g_origOsApi.bind(sockfd, addr, addrlen);
    }

    if (!IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d bind is not hijacked", sockfd);
        CHECK_AND_GET_OS_API(g_origOsApi.bind, INVALID_FD);
        return g_origOsApi.bind(sockfd, addr, addrlen);
    }
    BEFORE_DPFUNC();
    int ret = DP_PosixBind(OsFdToDpFd(sockfd), addr, addrlen);
    AFTER_DPFUNC();
    if (ret < 0) {
        KNET_ERR("DP_PosixBind ret %d, osFd %d, dpFd %d, errno %d, %s",
            ret, sockfd, OsFdToDpFd(sockfd), errno, strerror(errno));
        return ret;
    }
    KNET_DEBUG("Bind success, osFd %d, dpFd %d, ip 0x%x, port 0x%x",
        sockfd, OsFdToDpFd(sockfd),
        ((struct sockaddr_in *)addr)->sin_addr.s_addr, ((struct sockaddr_in *)addr)->sin_port);
    return ret;
}

KNET_API int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.connect, INVALID_FD);
        return g_origOsApi.connect(sockfd, addr, addrlen);
    }

    if (!IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d connect is not hijacked", sockfd);
        CHECK_AND_GET_OS_API(g_origOsApi.connect, INVALID_FD);
        return g_origOsApi.connect(sockfd, addr, addrlen);
    }
    BEFORE_DPFUNC();
    int ret = DP_PosixConnect(OsFdToDpFd(sockfd), addr, addrlen);
    AFTER_DPFUNC();
    if (ret < 0) {
        if (errno == EINPROGRESS) {
            KNET_INFO("DP_PosixConnect ret %d, osFd %d, dpFd %d, errno %d, %s",
                ret, sockfd, OsFdToDpFd(sockfd), errno, strerror(errno));
        } else {
            KNET_ERR("DP_PosixConnect ret %d, osFd %d, dpFd %d, errno %d, %s",
                ret, sockfd, OsFdToDpFd(sockfd), errno, strerror(errno));
        }
    }

    return ret;
}

KNET_API int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.getpeername, INVALID_FD);
        return g_origOsApi.getpeername(sockfd, addr, addrlen);
    }

    if (!IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d getpeername is not hijacked", sockfd);
        CHECK_AND_GET_OS_API(g_origOsApi.getpeername, INVALID_FD);
        return g_origOsApi.getpeername(sockfd, addr, addrlen);
    }
    BEFORE_DPFUNC();
    int ret = DP_PosixGetpeername(OsFdToDpFd(sockfd), addr, addrlen);
    AFTER_DPFUNC();
    if (ret < 0) {
        KNET_ERR("DP_PosixGetpeername ret %d, errno %d, %s, osFd %d, dpFd %d, addrlen %u",
            ret, errno, strerror(errno), sockfd, OsFdToDpFd(sockfd),
            (addrlen != NULL) ? *addrlen : ADDRLEN_NULL_VALUE);
    }
    return ret;
}

KNET_API int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.getsockname, INVALID_FD);
        return g_origOsApi.getsockname(sockfd, addr, addrlen);
    }

    if (!IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d getsockname is not hijacked", sockfd);
        CHECK_AND_GET_OS_API(g_origOsApi.getsockname, INVALID_FD);
        return g_origOsApi.getsockname(sockfd, addr, addrlen);
    }
    BEFORE_DPFUNC();
    int ret = DP_PosixGetsockname(OsFdToDpFd(sockfd), addr, addrlen);
    AFTER_DPFUNC();
    if (ret < 0) {
        KNET_ERR("DP_PosixGetsockname ret %d, errno %d, %s, osFd %d, dpFd %d, addrlen %u",
            ret, errno, strerror(errno), sockfd, OsFdToDpFd(sockfd),
            (addrlen != NULL) ? *addrlen : ADDRLEN_NULL_VALUE);
    }
    return ret;
}

KNET_API ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.send, INVALID_FD);
        return g_origOsApi.send(sockfd, buf, len, flags);
    }

    if (!IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d send is not hijacked", sockfd);
        CHECK_AND_GET_OS_API(g_origOsApi.send, INVALID_FD);
        return g_origOsApi.send(sockfd, buf, len, flags);
    }
    BEFORE_DPFUNC();
    ssize_t ret = DP_PosixSend(OsFdToDpFd(sockfd), buf, len, flags);
    AFTER_DPFUNC();
    if (ret < 0 && errno != EAGAIN) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "osFd %d dpFd %d DP_PosixSend ret %zd, errno %d, %s, len %zu, flags %d",
            sockfd, OsFdToDpFd(sockfd), ret, errno, strerror(errno), len, flags);
    }
    return ret;
}

KNET_API ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr,
    socklen_t addrlen)
{
    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.sendto, INVALID_FD);
        return g_origOsApi.sendto(sockfd, buf, len, flags, dest_addr, addrlen);
    }

    if (!IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d sendto is not hijacked", sockfd);
        CHECK_AND_GET_OS_API(g_origOsApi.sendto, INVALID_FD);
        return g_origOsApi.sendto(sockfd, buf, len, flags, dest_addr, addrlen);
    }
    BEFORE_DPFUNC();
    ssize_t ret = DP_PosixSendto(OsFdToDpFd(sockfd), buf, len, flags, dest_addr, addrlen);
    AFTER_DPFUNC();
    if (ret < 0 && errno != EAGAIN) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR,
            "osFd %d dpFd %d DP_PosixSendto ret %zd, errno %d, %s, len %zu, flags %d, addrlen %u",
            sockfd, OsFdToDpFd(sockfd), ret, errno, strerror(errno), len, flags, addrlen);
    }
    return ret;
}

KNET_API ssize_t writev(int sockfd, const struct iovec *iov, int iovcnt)
{
    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.writev, INVALID_FD);
        return g_origOsApi.writev(sockfd, iov, iovcnt);
    }

    if (!IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d writev is not hijacked", sockfd);
        CHECK_AND_GET_OS_API(g_origOsApi.writev, INVALID_FD);
        return g_origOsApi.writev(sockfd, iov, iovcnt);
    }
    BEFORE_DPFUNC();
    ssize_t ret = DP_PosixWritev(OsFdToDpFd(sockfd), iov, iovcnt);
    AFTER_DPFUNC();
    if (ret < 0 && errno != EAGAIN) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "osFd %d dpFd %d DP_PosixWritev ret %d, errno %d, %s, iovcnt %d",
            sockfd, OsFdToDpFd(sockfd), ret, errno, strerror(errno), iovcnt);
    }
    return ret;
}

KNET_API ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.sendmsg, INVALID_FD);
        return g_origOsApi.sendmsg(sockfd, msg, flags);
    }

    if (!IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d sendmsg is not hijacked", sockfd);
        CHECK_AND_GET_OS_API(g_origOsApi.sendmsg, INVALID_FD);
        return g_origOsApi.sendmsg(sockfd, msg, flags);
    }
    BEFORE_DPFUNC();
    ssize_t ret = DP_PosixSendmsg(OsFdToDpFd(sockfd), msg, flags);
    AFTER_DPFUNC();
    if (ret < 0 && errno != EAGAIN) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "osFd %d dpFd %d DP_PosixSendmsg ret %zd, errno %d, %s, flags %d",
            sockfd, OsFdToDpFd(sockfd), ret, errno, strerror(errno), flags);
    }
    return ret;
}

KNET_API ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.recv, INVALID_FD);
        return g_origOsApi.recv(sockfd, buf, len, flags);
    }

    if (!IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d recv is not hijacked", sockfd);
        CHECK_AND_GET_OS_API(g_origOsApi.recv, INVALID_FD);
        return g_origOsApi.recv(sockfd, buf, len, flags);
    }
    BEFORE_DPFUNC();
    ssize_t ret = DP_PosixRecv(OsFdToDpFd(sockfd), buf, len, flags);
    AFTER_DPFUNC();
    if (ret < 0 && errno != EAGAIN) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "osFd %d dpFd %d DP_PosixRecv ret %zd, errno %d, %s, len %zu, flags %d",
            sockfd, OsFdToDpFd(sockfd), ret, errno, strerror(errno), len, flags);
    }
    return ret;
}

KNET_API ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr,
    socklen_t *addrlen)
{
    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.recvfrom, INVALID_FD);
        return g_origOsApi.recvfrom(sockfd, buf, len, flags, src_addr, addrlen);
    }

    if (!IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d recvfrom is not hijacked", sockfd);
        CHECK_AND_GET_OS_API(g_origOsApi.recvfrom, INVALID_FD);
        return g_origOsApi.recvfrom(sockfd, buf, len, flags, src_addr, addrlen);
    }
    BEFORE_DPFUNC();
    ssize_t ret = DP_PosixRecvfrom(OsFdToDpFd(sockfd), buf, len, flags, src_addr, addrlen);
    AFTER_DPFUNC();
    if (ret < 0 && errno != EAGAIN) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "osFd %d dpFd %d DP_PosixRecvfrom ret %zd, errno %d, %s, len %zu, flags %d",
            sockfd, OsFdToDpFd(sockfd), ret, errno, strerror(errno), len, flags);
    }
    return ret;
}

KNET_API ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)
{
    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.recvmsg, INVALID_FD);
        return g_origOsApi.recvmsg(sockfd, msg, flags);
    }

    if (!IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d recvmsg is not hijacked", sockfd);
        CHECK_AND_GET_OS_API(g_origOsApi.recvmsg, INVALID_FD);
        return g_origOsApi.recvmsg(sockfd, msg, flags);
    }
    BEFORE_DPFUNC();
    ssize_t ret = DP_PosixRecvmsg(OsFdToDpFd(sockfd), msg, flags);
    AFTER_DPFUNC();
    if (ret < 0 && errno != EAGAIN) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "osFd %d dpFd %d DP_PosixRecvmsg ret %zd, errno %d, %s, flags %d",
            sockfd, OsFdToDpFd(sockfd), ret, errno, strerror(errno), flags);
    }
    return ret;
}

KNET_API ssize_t readv(int sockfd, const struct iovec *iov, int iovcnt)
{
    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.readv, INVALID_FD);
        return g_origOsApi.readv(sockfd, iov, iovcnt);
    }

    if (!IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d readv is not hijacked", sockfd);
        CHECK_AND_GET_OS_API(g_origOsApi.readv, INVALID_FD);
        return g_origOsApi.readv(sockfd, iov, iovcnt);
    }
    BEFORE_DPFUNC();
    ssize_t ret = DP_PosixReadv(OsFdToDpFd(sockfd), iov, iovcnt);
    AFTER_DPFUNC();
    if (ret < 0 && errno != EAGAIN) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "osFd %d dpFd %d DP_PosixReadv ret %zd, errno %d, %s, iovcnt %d",
            sockfd, OsFdToDpFd(sockfd), ret, errno, strerror(errno), iovcnt);
    }
    return ret;
}

KNET_API int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen)
{
    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.getsockopt, INVALID_FD);
        return g_origOsApi.getsockopt(sockfd, level, optname, optval, optlen);
    }

    if (!IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d getsockopt is not hijacked", sockfd);
        CHECK_AND_GET_OS_API(g_origOsApi.getsockopt, INVALID_FD);
        return g_origOsApi.getsockopt(sockfd, level, optname, optval, optlen);
    }
    BEFORE_DPFUNC();
    int ret = DP_PosixGetsockopt(OsFdToDpFd(sockfd), level, optname, optval, optlen);
    AFTER_DPFUNC();
    if (ret < 0) {
        KNET_ERR("DP_PosixGetsockopt ret %d, errno %d, %s", ret, errno, strerror(errno));
    }
    return ret;
}

KNET_API int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.setsockopt, INVALID_FD);
        return g_origOsApi.setsockopt(sockfd, level, optname, optval, optlen);
    }

    if (!IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d setsockopt is not hijacked", sockfd);
        CHECK_AND_GET_OS_API(g_origOsApi.setsockopt, INVALID_FD);
        return g_origOsApi.setsockopt(sockfd, level, optname, optval, optlen);
    }
    BEFORE_DPFUNC();
    int ret = DP_PosixSetsockopt(OsFdToDpFd(sockfd), level, optname, optval, optlen);
    AFTER_DPFUNC();
    if (ret < 0) {
        KNET_ERR("DP_PosixSetsockopt ret %d, errno %d, %s, level %d, optname %d, optlen %u",
            ret, errno, strerror(errno), level, optname, optlen);
    }
    return ret;
}

KNET_API int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    CHECK_AND_GET_OS_API(g_origOsApi.accept, INVALID_FD);
    if (!g_dpInited) {
        return g_origOsApi.accept(sockfd, addr, addrlen);
    }

    if (!IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d accept is not hijacked", sockfd);
        return g_origOsApi.accept(sockfd, addr, addrlen);
    }
    /* 信号退出流程中直接退出 */
    if (g_dpWaitExit) {
        errno = EPERM;
        KNET_WARN("Function accept was not allowed to be called in signal exiting process, errno %d, %s",
            errno, strerror(errno));
        return -1;
    }
    BEFORE_DPFUNC();
    int acceptDpFd = DP_PosixAccept(OsFdToDpFd(sockfd), addr, addrlen);
    AFTER_DPFUNC();
    if (acceptDpFd < 0) {
        if (errno != EAGAIN) {
            KNET_ERR("DP_PosixAccept acceptDpFd %d, sockfd %d, dpFd %d, errno %d, %s, addrlen %u",
                acceptDpFd, sockfd, OsFdToDpFd(sockfd), errno, strerror(errno),
                (addrlen != NULL) ? *addrlen : ADDRLEN_NULL_VALUE);
        } else {
            KNET_DEBUG("DP_PosixAccept acceptDpFd %d, sockfd %d, dpFd %d, errno %d, %s, addrlen %u",
                acceptDpFd, sockfd, OsFdToDpFd(sockfd), errno, strerror(errno),
                (addrlen != NULL) ? *addrlen : ADDRLEN_NULL_VALUE);
        }
        return -1;
    }

    /* 限制：os socket的domain、type、protocol必须同acceptDpFd的一致，目前协议栈accept只会返回tcp类型，所以socket入参写死
     * 限制原因：TcpSoLingerSet函数是通过osFd的类型去判断dpFd的类型 */
    int acceptOsFd = g_origOsApi.socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (!IsFdValid(acceptOsFd)) {
        KNET_ERR("OS socket ret %d, errno %d, %s", acceptOsFd, errno, strerror(errno));
        BEFORE_DPFUNC();
        DP_PosixClose(acceptDpFd);
        AFTER_DPFUNC();
        return INVALID_FD;
    }

    SetFdSocketState(FD_STATE_HIJACK, acceptOsFd, acceptDpFd);

    KNET_DEBUG("Accept success, listenFd %d, acceptOsFd %d, acceptDpFd %d", sockfd, acceptOsFd, acceptDpFd);

    return acceptOsFd;
}

KNET_API int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.accept4, INVALID_FD);
        return g_origOsApi.accept4(sockfd, addr, addrlen, flags);
    }

    if (!IsFdHijack(sockfd)) {
        CHECK_AND_GET_OS_API(g_origOsApi.accept4, INVALID_FD);
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d accept4 is not hijacked", sockfd);
        return g_origOsApi.accept4(sockfd, addr, addrlen, flags);
    }

    int validFlags = SOCK_CLOEXEC | SOCK_NONBLOCK;

    // 入参flags校验
    if ((flags & ~validFlags) != 0) {
        KNET_ERR("Sockfd %d, accept4 flags %d is invalid", sockfd, flags);
        errno = EINVAL;
        return -1;
    }

    /* 设置 socket 为非阻塞 */
    if ((flags &  SOCK_NONBLOCK) != 0) {
        int sockFlags = fcntl(sockfd, F_GETFL, 0);
        if (sockFlags < 0) {
            KNET_ERR("fcntl F_GETFL sockfd %d failed, ret %d, errno %d, %s", sockfd, sockFlags, errno, strerror(errno));
            return -1;
        }
        int ret = fcntl(sockfd, F_SETFL, sockFlags | O_NONBLOCK);
        if (ret < 0) {
            KNET_ERR("fcntl F_SETFL sockfd %d failed, ret %d, errno %d, %s", sockfd, ret, errno, strerror(errno));
            return -1;
        }
    }
    /* fork()函数之后，不支持子进程调用父进程的fd，默认使能了SOCK_CLOEXEC标志位 */
    int acceptOsFd = accept(sockfd, addr, addrlen);
    if (acceptOsFd < 0) {
        KNET_ERR("ListenFd %d accept failed, ret %d, errno %d, %s", sockfd, acceptOsFd, errno, strerror(errno));
        return -1;
    }

    return acceptOsFd;
}

KNET_API int close(int sockfd)
{
    CHECK_AND_GET_OS_API(g_origOsApi.close, -1);
    if (!g_dpInited) {
        return g_origOsApi.close(sockfd);
    }

    int failed = 0;

    if (!IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d close is not hijacked", sockfd);
        return g_origOsApi.close(sockfd);
    }

    /* 子进程不能DP_PosixClose，redis back ground saving fork会关掉listenfd，从而释放共享的dpdk资源，导致父进程无法正常运行 */
    if (!g_isForkedParent) {
        return 0;
    }

    if (GetFdType(sockfd) == FD_TYPE_SOCKET) {
        KNET_DEBUG("Close socket osFd %d, dpFd %d", sockfd, OsFdToDpFd(sockfd));
    }

    KNET_DEBUG("Close osFd %d, dpFd %d", sockfd, OsFdToDpFd(sockfd));
    BEFORE_DPFUNC();
    int ret = DP_PosixClose(OsFdToDpFd(sockfd));
    AFTER_DPFUNC();
    if (ret < 0) {
        failed = 1;
        KNET_ERR("DP_PosixClose ret %d, errno %d, %s", ret, errno, strerror(errno));
    }

    if (GetFdType(sockfd) == FD_TYPE_EPOLL) {
        ret = g_origOsApi.close(GetFdPrivateData(sockfd)->epollData.data.eventFd);
        if (ret < 0) {
            failed = 1;
            KNET_ERR("OS close eventfd ret %d, errno %d, %s", ret, errno, strerror(errno));
        }
    }

    ResetFdState(sockfd);

    /* 必须先清理fd资源，os close必须放最后。若先os close，会导致多线程情况下其他进程申请了fd，fd资源又被清理掉 */
    ret = g_origOsApi.close(sockfd);
    if (ret < 0) {
        failed = 1;
        KNET_ERR("OS close ret %d, errno %d, %s", ret, errno, strerror(errno));
    }

    return failed == 0 ? 0 : -1;
}

KNET_API int shutdown(int sockfd, int how)
{
    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.shutdown, INVALID_FD);
        return g_origOsApi.shutdown(sockfd, how);
    }

    if (!IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d shutdown is not hijacked", sockfd);
        CHECK_AND_GET_OS_API(g_origOsApi.shutdown, INVALID_FD);
        return g_origOsApi.shutdown(sockfd, how);
    }
    BEFORE_DPFUNC();
    int ret = DP_PosixShutdown(OsFdToDpFd(sockfd), how);
    AFTER_DPFUNC();
    if (ret < 0) {
        KNET_ERR("DP_PosixShutdown ret %d, errno %d, %s", ret, errno, strerror(errno));
    }
    return ret;
}

KNET_API ssize_t read(int sockfd, void *buf, size_t count)
{
    if (!g_dpInited) {
        KNET_DEBUG("Dp is not initialized, fd %d go os", sockfd);
        CHECK_AND_GET_OS_API(g_origOsApi.read, INVALID_FD);
        return g_origOsApi.read(sockfd, buf, count);
    }

    if (!IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d read is not hijacked", sockfd);
        CHECK_AND_GET_OS_API(g_origOsApi.read, INVALID_FD);
        return g_origOsApi.read(sockfd, buf, count);
    }
    BEFORE_DPFUNC();
    ssize_t ret = DP_PosixRead(OsFdToDpFd(sockfd), buf, count);
    AFTER_DPFUNC();
    if (ret < 0 && errno != EAGAIN) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "DP_PosixRead ret %d, osFd %d, dpFd %d, errno %d, %s, count %zu",
            ret, sockfd, OsFdToDpFd(sockfd), errno, strerror(errno), count);
    }

    return ret;
}

KNET_API ssize_t write(int sockfd, const void *buf, size_t count)
{
    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.write, INVALID_FD);
        return g_origOsApi.write(sockfd, buf, count);
    }

    if (!IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d write is not hijacked", sockfd);
        CHECK_AND_GET_OS_API(g_origOsApi.write, INVALID_FD);
        return g_origOsApi.write(sockfd, buf, count);
    }
    BEFORE_DPFUNC();
    ssize_t ret = DP_PosixWrite(OsFdToDpFd(sockfd), buf, count);
    AFTER_DPFUNC();
    if (ret < 0 && errno != EAGAIN) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "osFd %d dpFd %d DP_PosixWrite ret %d, errno %d, %s, count %zu",
            sockfd, OsFdToDpFd(sockfd), ret, errno, strerror(errno), count);
    }

    return ret;
}

void EpollCallback(uint8_t *data)
{
    struct EpollNotifyData *notifyData = (struct EpollNotifyData *)data;
    int eventFd = notifyData->eventFd;
    int ret;

    /* 避免一直调用浪费CPU */
    ret = KNET_HalAtomicTestSet64(&notifyData->active);
    if (ret == 0) {
        return;
    }

    ret = eventfd_write(eventFd, 1);
    if (ret < 0) {
        KNET_ERR("OS event fd write ret %d, errno %d, %s", ret, errno, strerror(errno));
        return;
    }
}

static inline int ResetDpCallbackData(struct EpollNotifyData *notifyData)
{
    uint64_t value = 0;
    int oriErrno = errno;

    (void)eventfd_read(notifyData->eventFd, &value);
    errno = oriErrno;

    KNET_HalAtomicSet64(&notifyData->active, 0);
    return 0;
}

static inline int DPEpollCreateNotify(int osFd, int eventFd)
{
    DP_EpollNotify_t *notify = NULL;
    struct EpollNotifyData *data = NULL;

    notify = &GetFdPrivateData(osFd)->epollData.notify;
    data = &GetFdPrivateData(osFd)->epollData.data;

    data->eventFd = eventFd;
    KNET_HalAtomicSet64(&data->active, 0);

    notify->fn = EpollCallback;
    notify->data = (uint8_t *)data;

    KNET_DEBUG("Event fd %d", eventFd);
    return DP_EpollCreateNotify(1, notify);
}

static inline int RegEpollDepFuncs(void)
{
    // 初始化依赖的接口

    CHECK_AND_GET_OS_API(g_origOsApi.epoll_create, -1);
    CHECK_AND_GET_OS_API(g_origOsApi.epoll_ctl, -1);
    CHECK_AND_GET_OS_API(g_origOsApi.epoll_wait, -1);

    return 0;
}

static inline uint64_t GenPrivateEpollData(void)
{
    return 0x0; // attention: 这里不能放0xFFFFFFFFFFFFF，epoll_ctl会报错非法参数
}

static int EpollTrafficResourcesInit(void)
{
    int ret = KNET_TrafficResourcesInit();
    if (ret != 0) {
        errno = ENAVAIL;
        KNET_ERR("Traffic resources init failed, errno %d, %s", errno, strerror(errno));
        return -1;
    }
    return 0;
}

static int EpollCreatePrepare(void)
{
    /* 在协议栈使用epoll_create前，完成对打流相关资源的初始化 */
    int ret = 0;
    ret = EpollTrafficResourcesInit();
    if (ret != 0) {
        return -1;
    }

    ret = RegEpollDepFuncs();
    if (ret != 0) {
        KNET_ERR("Reg epoll funcs failed");
        return -1;
    }
    return 0;
}

KNET_API int epoll_create(int size)
{
    int eventFd, dpFd, osFd, ret;
    struct epoll_event ev = {0};
    if (EpollCreatePrepare() != 0) {
        return -1;
    }

    /* 在主线程等待退出的时候,走内核的创建 */
    if (!g_dpInited) {
        return g_origOsApi.epoll_create(size);
    }
    /* 信号退出流程中直接退出 */
    if (g_dpWaitExit) {
        errno = EPERM;
        KNET_WARN("Function epoll_create was not allowed to be called in signal exiting process, errno %d, %s",
            errno, strerror(errno));
        return -1;
    }

    osFd = g_origOsApi.epoll_create(size);
    if (!IsFdValid(osFd)) {
        KNET_ERR("OS socket ret %d, errno %d, %s", osFd, errno, strerror(errno));
        goto kernel_epoll_create_err;
    }

    eventFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (eventFd == -1) {
        KNET_ERR("Failed alloc eventfd, errno %d, %s", errno, strerror(errno));
        goto eventfd_create_err;
    }

    ev.events |= EPOLLIN;
    ev.data.u64 = GenPrivateEpollData();
    ret = g_origOsApi.epoll_ctl(osFd, EPOLL_CTL_ADD, eventFd, &ev);
    if (ret < 0) {
        KNET_ERR("Epoll ctl ret %d, errno %d, %s", ret, errno, strerror(errno));
        goto epoll_ctl_add_err;
    }

    dpFd = DPEpollCreateNotify(osFd, eventFd);
    if (dpFd < 0) {
        KNET_ERR("DP_EpollCreateNotify ret %d, errno %d, %s", dpFd, errno, strerror(errno));
        goto dp_epoll_create_err;
    }

    SetFdStateAndType(FD_STATE_HIJACK, osFd, dpFd, FD_TYPE_EPOLL);
    KNET_DEBUG("Epoll create success, osFd %d, dpFd %d, eventFd %d", osFd, dpFd, eventFd);

    return osFd;

dp_epoll_create_err:
epoll_ctl_add_err:
    g_origOsApi.close(eventFd);
eventfd_create_err:
    g_origOsApi.close(osFd);
kernel_epoll_create_err:
    return INVALID_FD;
}

KNET_API int epoll_create1(int flags)
{
    /* 在协议栈使用epoll_create1前，完成对打流相关资源的初始化 */
    int ret = EpollTrafficResourcesInit();
    if (ret != 0) {
        return -1;
    }
    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.epoll_create1, INVALID_FD);
        return g_origOsApi.epoll_create1(flags);
    }

    if (flags & ~EPOLL_CLOEXEC) {
        KNET_ERR("Epoll create1 flags invalid");
        errno = EINVAL;
        return -1;
    }
    // 对于0和EPOLL_CLOEXEC标志位均通过knet epoll_create()实现
    return epoll_create(1); // size参数没有使用仅做预留，只要大于0即可
}

KNET_API int epoll_ctl(int epfd, int op, int sockfd, struct epoll_event *event)
{
    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.epoll_ctl, INVALID_FD);
        return g_origOsApi.epoll_ctl(epfd, op, sockfd, event);
    }

    if (!IsFdHijack(epfd) || !IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d/%d epoll_ctl is not hijacked", epfd, sockfd);
        return g_origOsApi.epoll_ctl(epfd, op, sockfd, event);
    }

    /* 内核的epoll ctl事件data也需要保证不能碰撞到private data，否则会丢osFd的event */
    if (event != NULL && event->data.u64 == GenPrivateEpollData()) {
        KNET_ERR("Epoll ctl got unexpect event data, fd:%d, dp fd:%d", epfd, OsFdToDpFd(epfd));
        errno = EINVAL;
        return -1;
    }
    BEFORE_DPFUNC();
    int ret = DP_PosixEpollCtl(OsFdToDpFd(epfd), op, OsFdToDpFd(sockfd), event);
    AFTER_DPFUNC();
    if (ret < 0) {
        if (event != NULL) {
            KNET_ERR("DP_PosixEpollCtl ret %d, errno %d, %s, op %d, events %u, data %x, "
                "osEpfd %d, dpEpfd %d, osFd %d, dpFd %d",
                ret, errno, strerror(errno), op, event->events, event->data.u64,
                epfd, OsFdToDpFd(epfd), sockfd, OsFdToDpFd(sockfd));
        } else {
            KNET_ERR("DP_PosixEpollCtl ret %d, errno %d, %s, op %d, osEpfd %d, dpEpfd %d, osFd %d, dpFd %d",
                ret, errno, strerror(errno), op, epfd, OsFdToDpFd(epfd), sockfd, OsFdToDpFd(sockfd));
        }
        return ret;
    }
    if (event != NULL) {
        KNET_DEBUG("Epoll ctl success, epfd %d, fd %d, epDpFd %d, dpFd %d, op %d, events %x, data %x",
            epfd, sockfd, OsFdToDpFd(epfd), OsFdToDpFd(sockfd), op, event->events, event->data.u64);
    } else {
        KNET_DEBUG("Epoll ctl success, epfd %d, fd %d, epDpFd %d, dpFd %d, op %d",
            epfd, sockfd, OsFdToDpFd(epfd), OsFdToDpFd(sockfd), op);
    }

    return ret;
}

static inline int DetectDpCallbackCalled(struct epoll_event *events, int eventCnt)
{
    int i;
    for (i = 0; i < eventCnt; ++i) {
        if (events[i].data.u64 == GenPrivateEpollData()) {
            return 1;
        }
    }
    return 0;
}

struct EpollCtlBlock {
    int maxEvents;
    int epfd;
    int *kernelEventCnt;
    int *dpEventCnt;
    struct epoll_event *kernelEvents;
    struct epoll_event *dpEvents;
};

static int PollEpollEventImmediately(struct EpollCtlBlock *epollCb)
{
    int dpEventCnt = 0, maxEvents = epollCb->maxEvents, epfd = epollCb->epfd;

    int kernelEventCnt = g_origOsApi.epoll_wait(epfd, epollCb->kernelEvents, maxEvents, 0);
    if (kernelEventCnt < 0) {
        KNET_ERR("Epoll wait failed ret %d, errno %d, %s, epfd %d, maxevents %d",
            kernelEventCnt, errno, strerror(errno), epfd, maxEvents);
        return kernelEventCnt;
    }

    /*
     * 如果这里有eventfd的事件会占掉一个kernelEvents的位置
     * 但eventfd的事件只作通知，不返回给用户，所以这里需要算出真正os epoll事件的个数
     */
    int realKernelEventCnt = 0;
    for (int i = 0; i < kernelEventCnt; ++i) {
        if (epollCb->kernelEvents[i].data.u64 == GenPrivateEpollData()) {
            continue;
        }
        epollCb->dpEvents[realKernelEventCnt].data = epollCb->kernelEvents[i].data;
        epollCb->dpEvents[realKernelEventCnt].events = epollCb->kernelEvents[i].events;

        ++realKernelEventCnt;
    }
    *epollCb->kernelEventCnt = realKernelEventCnt;

    *epollCb->dpEventCnt = 0;
    if (KNET_UNLIKELY(maxEvents == realKernelEventCnt)) {
        return 0;
    }
    BEFORE_DPFUNC();
    dpEventCnt = DP_PosixEpollWait(OsFdToDpFd(epfd), &epollCb->dpEvents[realKernelEventCnt],
        maxEvents - realKernelEventCnt, 0);
    AFTER_DPFUNC();
    if (dpEventCnt < 0) {
        KNET_ERR("DP_PosixEpollWait failed ret %d, errno %d, %s, epfd %d, dpEpfd %d, maxevents %d",
            dpEventCnt, errno, strerror(errno), epfd, OsFdToDpFd(epfd), maxEvents - realKernelEventCnt);
        /* 要返回正常，因为已经从os epoll成功获取了数据，如果返回失败会导致事件丢失 */
        return 0;
    }
    *epollCb->dpEventCnt = dpEventCnt;

    return 0;
}

static int EpollWaitHelperBlock(struct EpollCtlBlock* epollCb, int timeout)
{
    int epfd = epollCb->epfd;
    int maxevents = epollCb->maxEvents;

    /* no events, then call blocked epoll_wait */
    KNET_DEBUG("Epoll fd %d os epoll wait start, timeout %d", epfd, timeout);
    *epollCb->kernelEventCnt = g_origOsApi.epoll_wait(epfd, epollCb->kernelEvents, maxevents, timeout);
    if (*epollCb->kernelEventCnt < 0) {
        KNET_ERR("Epoll fd %d epoll_wait failed ret %d, errno %d, %s",
            epfd, *epollCb->kernelEventCnt, errno, strerror(errno));
        return *epollCb->kernelEventCnt;
    }
    int callbackCalled = DetectDpCallbackCalled(epollCb->kernelEvents, *epollCb->kernelEventCnt);
    int reservedEventCnt = callbackCalled == 0 ? 0 : 1;
    *epollCb->kernelEventCnt = *epollCb->kernelEventCnt - reservedEventCnt;

    KNET_DEBUG("Epoll fd %d dpEpfd %d DP epoll wait start", epfd, OsFdToDpFd(epfd));
    BEFORE_DPFUNC();
    *epollCb->dpEventCnt = DP_PosixEpollWait(OsFdToDpFd(epfd), epollCb->dpEvents,
        maxevents - *epollCb->kernelEventCnt, 0);
    AFTER_DPFUNC();
    if (*epollCb->dpEventCnt < 0) {
        KNET_ERR("DP_PosixEpollWait failed ret %d, errno %d, %s, epfd %d, dpEpfd %d, "
            "dpMaxevents %d, maxevents %d, kernelEventCnt %d",
            *epollCb->dpEventCnt, errno, strerror(errno), epfd, OsFdToDpFd(epfd),
            maxevents - *epollCb->kernelEventCnt, maxevents, *epollCb->kernelEventCnt);
        return *epollCb->dpEventCnt;
    }

    return 0;
}

static int EpollWaitHelper(int epfd, struct epoll_event *events, const int maxevents, int timeout)
{
    int ret = -1;
    struct epoll_event defKernelEvent[DEFAULT_EVENT_NUM];
    struct epoll_event *kernelEvents = defKernelEvent;

    if (maxevents > DEFAULT_EVENT_NUM) {
        kernelEvents = malloc(sizeof(struct epoll_event) * maxevents);
        if (kernelEvents == NULL) {
            KNET_ERR("Malloc kernelEvents failed, epfd %d", epfd);
            errno = ENOMEM;
            return -1;
        }
    }
    int kernelEventCnt = 0, dpEventCnt = 0;

    struct EpollCtlBlock epollCb = {.epfd = epfd, .maxEvents = maxevents, .kernelEventCnt = &kernelEventCnt,
        .dpEventCnt = &dpEventCnt, .kernelEvents = kernelEvents, .dpEvents = events};

    ret = PollEpollEventImmediately(&epollCb);
    if (ret < 0) {
        goto release;
    }

    if (dpEventCnt == 0 && kernelEventCnt == 0 && timeout != 0) {
        ret = EpollWaitHelperBlock(&epollCb, timeout);
        if (ret < 0) {
            goto release;
        }
    }

    // epoll_wait没有事件产生也会调用，是不是可以加条件有一个事件产生了才会执行reset
    ResetDpCallbackData(&GetFdPrivateData(epfd)->epollData.data);

release:
    if (maxevents > DEFAULT_EVENT_NUM) {
        free(kernelEvents);
    }

    return kernelEventCnt + dpEventCnt;
}

KNET_API int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.epoll_wait, INVALID_FD);
        return g_origOsApi.epoll_wait(epfd, events, maxevents, timeout);
    }

    if (!IsFdHijack(epfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Epoll fd %d epoll_wait is not hijacked", epfd);
        return g_origOsApi.epoll_wait(epfd, events, maxevents, timeout);
    }

    /* 后续maxevents作为变长数组的长度，这里必须合法性校验 */
    if (events == NULL || maxevents <= 0 || maxevents > KNET_EPOLL_MAX_NUM) {
        KNET_ERR("Epoll fd %d epoll_wait invalid events, maxevents %d", epfd, maxevents);
        errno = EINVAL;
        return -1;
    }

    return EpollWaitHelper(epfd, events, maxevents, timeout);
}

KNET_API int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout, const sigset_t *sigmask)
{
    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.epoll_pwait, INVALID_FD);
        return g_origOsApi.epoll_pwait(epfd, events, maxevents, timeout, sigmask);
    }

    sigset_t oriMask = {0};
    int ret;
    if (sigmask != NULL) {
        pthread_sigmask(SIG_SETMASK, sigmask, &oriMask);
    }
    ret = epoll_wait(epfd, events, maxevents, timeout);
    if (sigmask != NULL) {
        pthread_sigmask(SIG_SETMASK, &oriMask, NULL);
    }
    return ret;
}

KNET_API int fcntl(int sockfd, int cmd, ...)
{
    va_list va;
    va_start(va, cmd);
    long arg = va_arg(va, long);
    va_end(va);

    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.fcntl, INVALID_FD);
        return g_origOsApi.fcntl(sockfd, cmd, arg);
    }

    if (!IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d fcntl is not hijacked", sockfd);
        CHECK_AND_GET_OS_API(g_origOsApi.fcntl, INVALID_FD);
        return g_origOsApi.fcntl(sockfd, cmd, arg);
    }

    /* dp目前只支持F_GETFL和F_SETFL操作,get操作不带参数arg会给个随机值,这里直接置0 */
    if (cmd == F_GETFL) {
        arg = 0;
    }

    // DP_PosixFcntl函数最后参数为int类型，进行适配
    if (arg > INT32_MAX || arg < 0) {
        KNET_ERR("Fcntl arg is larger then INT32_MAX or smaller then 0 which is not support");
        errno = EINVAL;
        return -1;
    }
    BEFORE_DPFUNC();
    int ret = DP_PosixFcntl(OsFdToDpFd(sockfd), cmd, (int)arg);
    AFTER_DPFUNC();
    if (ret < 0) {
        KNET_ERR("DP_PosixFcntl ret %d, errno %d, %s", ret, errno, strerror(errno));
    }
    return ret;
}

KNET_API int fcntl64(int sockfd, int cmd, ...)
{
    va_list va;
    va_start(va, cmd);
    long arg = va_arg(va, long);
    va_end(va);

    return fcntl(sockfd, cmd, arg);
}

static uint32_t PollEvent2Epoll(short int events)
{
    uint32_t epollEvents = 0;

    if (events & POLLIN) {
        epollEvents |= EPOLLIN;
    }
    if (events & POLLOUT) {
        epollEvents |= EPOLLOUT;
    }
    if (events & POLLERR) {
        epollEvents |= EPOLLERR;
    }
    if (events & POLLHUP) {
        epollEvents |= EPOLLHUP;
    }
    if (events & POLLPRI) {
        // epollEvents |= EPOLLPRI; // DP协议栈不支持
        KNET_DEBUG("EPOLLPRI is not support");
    }
    if (events & POLLRDHUP) {
        epollEvents |= EPOLLRDHUP;
    }
    if (events & POLLRDNORM) {
        epollEvents |= EPOLLRDNORM;
    }
    if (events & POLLRDBAND) {
        epollEvents |= EPOLLRDBAND;
    }
    if (events & POLLWRNORM) {
        epollEvents |= EPOLLWRNORM;
    }
    if (events & POLLWRBAND) {
        epollEvents |= EPOLLWRBAND;
    }

    return epollEvents;
}

static short int EpollEvent2Poll(uint32_t events)
{
    short int pollEvents = 0;
    if (events & EPOLLIN) {
        pollEvents |= POLLIN;
    }
    if (events & EPOLLOUT) {
        pollEvents |= POLLOUT;
    }
    if (events & EPOLLERR) {
        pollEvents |= POLLERR;
    }
    if (events & EPOLLHUP) {
        pollEvents |= POLLHUP;
    }
    if (events & EPOLLPRI) {
        pollEvents |= POLLPRI;
    }
    if (events & EPOLLRDHUP) {
        pollEvents |= POLLRDHUP;
    }
    if (events & EPOLLRDNORM) {
        pollEvents |= POLLRDNORM;
    }
    if (events & EPOLLRDBAND) {
        pollEvents |= POLLRDBAND;
    }
    if (events & EPOLLWRNORM) {
        pollEvents |= POLLWRNORM;
    }
    if (events & EPOLLWRBAND) {
        pollEvents |= POLLWRBAND;
    }

    return pollEvents;
}

static int PollHelper(struct pollfd *fds, const nfds_t nfds, int timeout)
{
    int epfd = epoll_create(nfds);
    if (epfd == -1) {
        KNET_ERR("Poll call epoll_create ret %d, errno %d, %s", epfd, errno, strerror(errno));
        return -1;
    }

    struct epoll_event ev = { 0 };
    for (nfds_t i = 0; i < nfds; i++) {
        ev.events = PollEvent2Epoll(fds[i].events);
        ev.data.fd = fds[i].fd;
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, fds[i].fd, &ev) == -1) {
            KNET_ERR("Poll call epoll_ctl failed, errno %d, %s", errno, strerror(errno));
            close(epfd);
            return -1;
        }
    }

    struct epoll_event *events = calloc(nfds, sizeof(struct epoll_event));
    if (events == NULL) {
        KNET_ERR("Malloc events failed");
        close(epfd);
        errno = ENOMEM;
        return -1;
    }

    int numEvents = epoll_wait(epfd, events, nfds, timeout);
    if (numEvents < 0) {
        KNET_ERR("Poll call epoll_wait ret %d, errno %d, %s", numEvents, errno, strerror(errno));
        free(events);
        close(epfd);
        return -1;
    } else if (numEvents == 0 && timeout == -1) {
        KNET_ERR("Poll get no events, nfds %d, timeout %d", nfds, timeout);
    }

    for (nfds_t i = 0; i < nfds; ++i) {
        fds[i].revents = 0; // 与内核行为一致，用户无需置revents = 0，没有event时内核会将revents置0
        for (int j = 0; j < numEvents; ++j) {
            if (fds[i].fd == events[j].data.fd) {
                fds[i].revents = EpollEvent2Poll(events[j].events);
                break;
            }
        }
    }
    KNET_DEBUG("Poll ret %d, nfds %d, timeout %d", numEvents, nfds, timeout);
    free(events);
    close(epfd);
    return numEvents;
}

KNET_API int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    /* 后续nfds作为变长数组的长度，这里必须合法性校验 */
    if (nfds <= 0 || nfds > KNET_EPOLL_MAX_NUM) {
        KNET_ERR("Invalid events, nfds %u", nfds);
        errno = EINVAL;
        return -1;
    }

    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.poll, INVALID_FD);
        return g_origOsApi.poll(fds, nfds, timeout);
    }

    if (fds == NULL) {
        errno = EFAULT;
        KNET_ERR("Poll invalid param");
        return -1;
    }

    return PollHelper(fds, nfds, timeout);
}

KNET_API int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout_ts, const sigset_t *sigmask)
{
    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.ppoll, INVALID_FD);
        return g_origOsApi.ppoll(fds, nfds, timeout_ts, sigmask);
    }

    sigset_t oriMask;
    int64_t timeout;
    int ret;
    if (timeout_ts == NULL) {
        timeout = -1;
    } else {
        timeout = timeout_ts->tv_sec * SEC_2_M_SEC + timeout_ts->tv_nsec / M_SEC_2_N_SEC;
        if (timeout < 0) {
            errno = EINVAL;
            return -1;
        }
    }
    if (sigmask != NULL) {
        pthread_sigmask(SIG_SETMASK, sigmask, &oriMask);
    }
    ret = poll(fds, nfds, timeout);
    if (sigmask != NULL) {
        pthread_sigmask(SIG_SETMASK, &oriMask, NULL);
    }
    return ret;
}

static int SelectPollingOnce(struct pollfd *osPollFds, nfds_t osPollNfds, struct SelectFdInfo *fdInfo)
{
    if (osPollNfds > 0) { // 性能优化：osPollNfds为0时，不os poll
        fdInfo->osPollRet = g_origOsApi.poll(osPollFds, osPollNfds, 0);
        if (fdInfo->osPollRet < 0) {
            KNET_ERR("OS poll failed, ret %d, errno %d, error %s", fdInfo->osPollRet, errno, strerror(errno));
            return fdInfo->osPollRet;
        }
    }
    BEFORE_DPFUNC();
    fdInfo->dpPollRet = DP_PosixPoll(fdInfo->dpPollFds, fdInfo->dpPollNfds, 0);
    AFTER_DPFUNC();
    if (fdInfo->dpPollRet < 0) {
        KNET_DEBUG("Dp poll failed, ret %d, errno %d, error %s", fdInfo->dpPollRet, errno, strerror(errno));
        if (fdInfo->osPollRet > 0) {
            return fdInfo->osPollRet;
        }
        return fdInfo->dpPollRet;
    }

    return fdInfo->osPollRet + fdInfo->dpPollRet;
}

static inline bool KNET_CounterTimerIsTimeout(int64_t timeBeginMs, int64_t timeoutMs)
{
    struct timeval timeNow = { 0 };
    (void)gettimeofday(&timeNow, NULL);
    int64_t timeNowMs = (int64_t)timeNow.tv_sec * 1000 + timeNow.tv_usec / 1000; // 1000为时间转换倍数。无需考虑溢出，溢出需要2亿年
    if (timeNowMs > timeBeginMs + timeoutMs) {
        return true;
    }

    return false;
}

static int SelectPollingLoops(
    struct pollfd *osPollFds, nfds_t osPollNfds, int64_t timeoutMs, struct SelectFdInfo *fdInfo)
{
    /* 进来先获取一次，如果有事件直接返回，可以减少一次获取时间的开销 */
    int pollRet = SelectPollingOnce(osPollFds, osPollNfds, fdInfo);
    if (pollRet > 0) {
        return pollRet;
    } else if (pollRet < 0) {
        KNET_ERR("select polling failed, ret %d, osPollNfds %d, timeoutMs %lld", pollRet, osPollNfds, timeoutMs);
        return pollRet;
    }

    int pollTimes = 0;
    struct timeval timeBegin = {0};
    (void)gettimeofday(&timeBegin, NULL);
    int64_t timeBeginMs = (int64_t)timeBegin.tv_sec * 1000 + timeBegin.tv_usec / 1000;  // 1000为时间转换倍数。无需考虑溢出，溢出需要2亿年
    do {
        /* 主线程等待其他线程退出 */
        if (KNET_UNLIKELY(KNET_IsDpWaitingExit())) {
            break;
        }
        pollRet += SelectPollingOnce(osPollFds, osPollNfds, fdInfo);
        if (pollRet > 0) {
            return pollRet;
        } else if (pollRet < 0) {
            KNET_ERR("Select polling failed, ret %d, osPollNfds %d, timeoutMs %lld", pollRet, osPollNfds, timeoutMs);
            return pollRet;
        }
        if (pollTimes <= FAST_POLL_TIMES) {
            pollTimes++;
            continue;
        }
        usleep(POLL_INTERVAL);
    } while (timeoutMs < 0 || !KNET_CounterTimerIsTimeout(timeBeginMs, timeoutMs));

    return pollRet;
}

static int SelectPollFdsGet(struct pollfd *osPollFds, int *dpToOsFds, struct SelectFdInfo *fdInfo)
{
    int osPollNfds = 0;
    int dpPollNfds = 0;

    for (int fd = 0; fd < fdInfo->selectNfds; ++fd) {
        bool checkRead = (fdInfo->readfds != NULL) && FD_ISSET(fd, fdInfo->readfds);
        bool checkWrite = (fdInfo->writefds != NULL) && FD_ISSET(fd, fdInfo->writefds);
        bool checkExcept = (fdInfo->exceptfds != NULL) && FD_ISSET(fd, fdInfo->exceptfds);
        if (!checkRead && !checkWrite && !checkExcept) {
            continue;
        }

        struct pollfd *currentPollFd = NULL;

        if (GetFdType(fd) == FD_TYPE_SOCKET) {  // 默认前提：只有hijack fd才能设置为FD_TYPE_SOCKET
            dpToOsFds[dpPollNfds] = fd;
            currentPollFd = &fdInfo->dpPollFds[dpPollNfds++];
            currentPollFd->fd = OsFdToDpFd(fd);
        } else {
            currentPollFd = &osPollFds[osPollNfds++];
            currentPollFd->fd = fd;
        }

        uint8_t events = 0;
        if (checkRead) {
            events |= POLLIN;
            KNET_DEBUG("Fd %d set poll read", fd);
        }
        if (checkWrite) {
            events |= POLLOUT;
            KNET_DEBUG("Fd %d set poll write", fd);
        }
        if (checkExcept) {
            events |= POLLERR;
            KNET_DEBUG("Fd %d set poll err", fd);
        }
        currentPollFd->events = (short)events;

        currentPollFd->revents = 0;
    }

    fdInfo->dpPollNfds = dpPollNfds;

    return osPollNfds;
}

static void SelectOsPollResultGet(
    struct pollfd *osPollFds, int osPollNfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds)
{
    for (int i = 0; i < osPollNfds; i++) {
        if (readfds != NULL && (osPollFds[i].revents & POLLIN) != 0) {
            FD_SET(osPollFds[i].fd, readfds);
        }
        if (writefds != NULL && (osPollFds[i].revents & POLLOUT) != 0) {
            FD_SET(osPollFds[i].fd, writefds);
        }
        if (exceptfds != NULL && (osPollFds[i].revents & POLLERR) != 0) {
            FD_SET(osPollFds[i].fd, exceptfds);
        }
    }
}

static void SelectDpPollResultGet(
    struct SelectFdInfo *fdInfo, int *dpToOsFds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds)
{
    for (int i = 0; i < fdInfo->dpPollNfds; i++) {
        unsigned short revents = (unsigned short)fdInfo->dpPollFds[i].revents;

        if (readfds != NULL && (revents & POLLIN) != 0) {
            FD_SET(dpToOsFds[i], readfds);
        }
        if (writefds != NULL && (revents & POLLOUT) != 0) {
            FD_SET(dpToOsFds[i], writefds);
        }
        if (exceptfds != NULL && (revents & POLLERR) != 0) {
            FD_SET(dpToOsFds[i], exceptfds);
        }
    }
}

static void SelectFdInfoInit(struct SelectFdInfo *fdInfo, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
    struct pollfd *dpPollFds, int nfds)
{
    fdInfo->readfds = readfds;
    fdInfo->writefds = writefds;
    fdInfo->exceptfds = exceptfds;
    fdInfo->dpPollFds = dpPollFds;
    fdInfo->selectNfds = nfds;
}

static void cleanup(struct pollfd *osPollFds, struct pollfd *dpPollFds, int *dpToOsFds)
{
    if (osPollFds != NULL) {
        free(osPollFds);
    }
    if (dpPollFds != NULL) {
        free(dpPollFds);
    }
    if (dpToOsFds != NULL) {
        free(dpToOsFds);
    }
}

KNET_API int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.select, INVALID_FD);
        return g_origOsApi.select(nfds, readfds, writefds, exceptfds, timeout);
    }

    /* timeoutMs为-1表示select永久阻塞等待 */
    int64_t timeoutMs = (timeout == NULL) ? -1 : timeout->tv_sec * 1000 + timeout->tv_usec / 1000;
    if (nfds < 0 || nfds > FD_SETSIZE || (timeout != NULL && timeoutMs < 0)) {
        KNET_ERR("Select failed, invalid param. n:%d, timeoutMs:%lld", nfds, timeoutMs);
        errno = EINVAL;
        return -1;
    }

    struct pollfd *osPollFds = (struct pollfd *)malloc(nfds * sizeof(struct pollfd));
    struct pollfd *dpPollFds = (struct pollfd *)malloc(nfds * sizeof(struct pollfd));
    int *dpToOsFds = (int *)malloc(nfds * sizeof(int));

    if (osPollFds == NULL || dpPollFds == NULL || dpToOsFds == NULL) {
        KNET_ERR("Memory allocation failed");
        cleanup(osPollFds, dpPollFds, dpToOsFds);
        return -1;
    }

    struct SelectFdInfo fdInfo = {0};
    SelectFdInfoInit(&fdInfo, readfds, writefds, exceptfds, dpPollFds, nfds);

    int osPollNfds = SelectPollFdsGet(osPollFds, dpToOsFds, &fdInfo);
    if (fdInfo.dpPollNfds == 0) {  // 性能优化：无hijackFd，直接走os
        cleanup(osPollFds, dpPollFds, dpToOsFds);
        return g_origOsApi.select(nfds, readfds, writefds, exceptfds, timeout);
    }

    if (readfds != NULL) {
        FD_ZERO(readfds);
    }
    if (writefds != NULL) {
        FD_ZERO(writefds);
    }
    if (exceptfds != NULL) {
        FD_ZERO(exceptfds);
    }

    int pollRet = SelectPollingLoops(osPollFds, osPollNfds, timeoutMs, &fdInfo);
    if (pollRet <= 0) {
        cleanup(osPollFds, dpPollFds, dpToOsFds);
        return pollRet;
    }

    /* 性能优化：仅有os poll结果时才去轮询赋值 */
    if (fdInfo.osPollRet > 0) {
        SelectOsPollResultGet(osPollFds, osPollNfds, readfds, writefds, exceptfds);
    }
    if (fdInfo.dpPollRet > 0) {
        SelectDpPollResultGet(&fdInfo, dpToOsFds, readfds, writefds, exceptfds);
    }

    cleanup(osPollFds, dpPollFds, dpToOsFds);
    return pollRet;
}

KNET_API int pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timespec *timeout,
    const sigset_t *sigmask)
{
    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.pselect, INVALID_FD);
        return g_origOsApi.pselect(nfds, readfds, writefds, exceptfds, timeout, sigmask);
    }

    sigset_t oriMask = {0};
    struct timeval *t = NULL;
    struct timeval selectTime;
    if (timeout != NULL) {
        selectTime.tv_sec = timeout->tv_sec;
        selectTime.tv_usec = timeout->tv_nsec / SEC_2_M_SEC;
        t = &selectTime;
    }
    if (sigmask != NULL) {
        pthread_sigmask(SIG_SETMASK, sigmask, &oriMask);
    }
    int ret = select(nfds, readfds, writefds, exceptfds, t);
    if (sigmask != NULL) {
        pthread_sigmask(SIG_SETMASK, &oriMask, NULL);
    }
    return ret;
}

KNET_API int ioctl(int sockfd, unsigned long request, ...)
{
    va_list va;
    va_start(va, request);
    char *arg = va_arg(va, char *);
    va_end(va);

    if (!g_dpInited) {
        CHECK_AND_GET_OS_API(g_origOsApi.ioctl, INVALID_FD);
        return g_origOsApi.ioctl(sockfd, request, arg);
    }

    if (!IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d ioctl is not hijacked", sockfd);
        CHECK_AND_GET_OS_API(g_origOsApi.ioctl, INVALID_FD);
        return g_origOsApi.ioctl(sockfd, request, arg);
    }
    BEFORE_DPFUNC();
    int ret = DP_PosixIoctl(OsFdToDpFd(sockfd), request, arg);
    AFTER_DPFUNC();
    if (ret < 0) {
        KNET_ERR("DP_PosixIoctl ret %d, errno %d, %s", ret, errno, strerror(errno));
    }
    return ret;
}

KNET_API pid_t fork(void)
{
    CHECK_AND_GET_OS_API(g_origOsApi.fork, INVALID_FD);

    if (!g_dpInited) {
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
        g_dpInited = false; // 子进程直接使用os接口
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

static int TcpSoLingerSet(int osFd)
{
    if (g_origOsApi.getsockopt == NULL) {
        return -1;
    }

    int type = -1;
    size_t optlen = sizeof(type);
    socklen_t optLen = (socklen_t)optlen;
    int ret = g_origOsApi.getsockopt(osFd, SOL_SOCKET, SO_TYPE, &type, &optLen);
    if (ret == 0 && type == SOCK_STREAM) {
        struct linger soLinger = {
            .l_onoff = true,
            .l_linger = 0,
        };
        BEFORE_DPFUNC();
        ret = DP_PosixSetsockopt(OsFdToDpFd(osFd), SOL_SOCKET, SO_LINGER, &soLinger, sizeof(soLinger));
        AFTER_DPFUNC();
        if (ret != 0) {
            KNET_ERR("Set OSFd %d dpFd %d sock opt SO_LINGER failed, ret %d, errno %d, %s",
                osFd, OsFdToDpFd(osFd), ret, errno, strerror(errno));
        }
    }

    return 0;
}

void KNET_AllHijackFdsClose(void)
{
    int fdMax = FdMaxGet();
    for (int osFd = 0; osFd < fdMax; ++osFd) {
        if (IsFdHijack(osFd)) {
            if (TcpSoLingerSet(osFd) != 0) {
                KNET_ERR("OSFd %d solinger set failed", osFd);
            }
            close(osFd);
        }
    }
}

/**
 * @brief 因信号退出时主线程需要等待其他线程可能有的dp流程结束
 */
void KNET_DpExit(void)
{
    if (!g_dpInited) {
        return;
    }

    g_dpWaitExit = true;        // 设置主线程等待标记
    usleep(DP_EXIT_WAIT_SLEEP_TIME);   // 先等待50ms让其他线程都退出来
    KNET_AllHijackFdsClose();   // 关闭所有dp协议栈的fd
    int tryTimes = 0;

    while (1) {
        /* 等待所有tcpfd关闭再退出 */
        int tcpSockCnt = DP_SocketCountGet(0);
        if (tcpSockCnt == 0) {
            break;
        }
        usleep(DP_EXIT_WAIT_SLEEP_TIME); //  每50ms判断一次

        ++tryTimes;
        if (tryTimes > DP_EXIT_WAIT_TRY_TIMES) {
            /* 出现如下打印的两种情况
             * 1.如果业务在此函数前自行关闭fd，就会发送FIN报文，需要等到msl time结束tcp才会关闭
             * 2.有遗漏的业务fd没有关闭 */
            KNET_WARN("%d tcp socket still alive, waiting for msl time or others", tcpSockCnt);
            break;
        }
    }
    return;
}