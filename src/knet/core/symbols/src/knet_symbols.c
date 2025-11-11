/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 用于实现查找软件函数符号
 */
#include "knet_symbols.h"
#include "knet_log.h"

void (*g_dPShowStatistics)(DP_StatType_t type, int workerId, uint32_t flag);
int (*g_dPProcIfreq)(DP_Netdev_t* dev, int request, struct DP_Ifreq* ifreq);
DP_Netdev_t* (*g_dPCreateNetdev)(DP_NetdevCfg_t* cfg);
int (*g_dPRtCfg)(DP_RtOpt_t op, DP_RtInfo_t* msg, DP_TbmAttr_t* attrs[], int attrCnt);
int (*g_dPInit)(int slave);
int (*g_dPCpdInit)(void);
int (*g_dPCpdRunOnce)(void);
void (*g_dPRunWorkerOnce)(int wid);
int (*g_dPPosixSocket)(int domain, int type, int protocol);
int (*g_dPPosixClose)(int fd);
int (*g_dPPosixListen)(int sockfd, int backlog);
int (*g_dPPosixBind)(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int (*g_dPPosixConnect)(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int (*g_dPPosixGetpeername)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int (*g_dPPosixGetsockname)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int (*g_dPPosixSend)(int sockfd, const void* buf, size_t len, int flags);
ssize_t (*g_dPPosixSendto)(int sockfd, const void *buf, size_t len, int flags,
    const struct sockaddr *destAddr, socklen_t addrlen);
ssize_t (*g_dPPosixWritev)(int fd, const struct iovec *iov, int iovcnt);
ssize_t (*g_dPPosixSendmsg)(int sockfd, const struct msghdr *msg, int flags);
ssize_t (*g_dPPosixRecv)(int sockfd, void* buf, size_t len, int flags);
ssize_t (*g_dPPosixRecvfrom)(int sockfd, void *buf, size_t len, int flags, struct sockaddr *srcAddr,
    socklen_t *addrlen);
ssize_t (*g_dPPosixRecvmsg)(int sockfd, struct msghdr *msg, int flags);
ssize_t (*g_dPPosixReadv)(int fd, const struct iovec *iov, int iovcnt);
int (*g_dPPosixGetsockopt)(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
int (*g_dPPosixSetsockopt)(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
int (*g_dPPosixAccept)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int (*g_dPPosixShutdown)(int sockfd, int how);
ssize_t (*g_dPPosixRead)(int sockfd, void *buf, size_t count);
ssize_t (*g_dPPosixWrite)(int sockfd, const void *buf, size_t count);
int (*g_dPEpollCreateNotify)(int size, DP_EpollNotify_t* callback);
int (*g_dPPosixEpollCtl)(int epfd, int op, int fd, struct epoll_event *event);
int (*g_dPPosixEpollWait)(int epfd, struct epoll_event *events, int maxevents, int timeout);
int (*g_dPPosixFcntl)(int fd, int cmd, int val);
int (*g_dPPosixPoll)(struct pollfd *fds, nfds_t nfds, int timeout);
int (*g_dPPosixIoctl)(int fd, int request, void* arg);
uint32_t (*g_nPDKDebugShowHookReg)(DP_DebugShowHook hook);
int (*g_dPAddrHooksReg)(DP_AddrHooks_t* addrHooks);
uint32_t (*g_nPDKMemHookReg)(DP_MemHooks_S *memHooks);
uint32_t (*g_nPDKMempoolHookReg)(DP_MempoolHooks_S* pHooks);
uint32_t (*g_nPDKRandIntHookReg)(DP_RandomHooks_S *randHook);
int (*g_dPRegGetSelfWorkerIdHook)(DP_WorkerGetSelfIdHook getSelf);
uint32_t (*g_nPDKClockReg)(DP_ClockGetTimeHook timeHook);
int (*g_dPHashTblHooksReg)(DP_HashTblHooks_t *hashTblHooks);
int (*g_dPFib4TblHooksReg)(DP_Fib4TblHooks_t *fib4TblHooks);
uint32_t (*g_nPDKLogHookReg)(DP_LogHook fnHook);
uint32_t(*g_nPDKSemHookReg)(const DP_SemHooks_S *pHooks);
void (*g_nPDKLogLevelSet)(DP_LogLevel_E logLevel);
int (*g_nPDKSemHookGetSockCnt)(int type);
int (*g_dPCfg)(DP_CfgKv_t* kv, int cnt);

// hisackdp.so
static struct KnetSymbolsInfo g_dp[] = {
    KNET_ADD_SYMBOL(DP_ShowStatistics, dPShowStatistics),
    KNET_ADD_SYMBOL(DP_ProcIfreq, dPProcIfreq),
    KNET_ADD_SYMBOL(DP_CreateNetdev, dPCreateNetdev),
    KNET_ADD_SYMBOL(DP_RtCfg, dPRtCfg),
    KNET_ADD_SYMBOL(DP_Init, dPInit),
    KNET_ADD_SYMBOL(DP_CpdInit, dPCpdInit),
    KNET_ADD_SYMBOL(DP_CpdRunOnce, dPCpdRunOnce),
    KNET_ADD_SYMBOL(DP_RunWorkerOnce, dPRunWorkerOnce),
    KNET_ADD_SYMBOL(DP_PosixSetsockopt, dPPosixSetsockopt),
    KNET_ADD_SYMBOL(DP_PosixFcntl, dPPosixFcntl),
    KNET_ADD_SYMBOL(DP_PosixSocket, dPPosixSocket),
    KNET_ADD_SYMBOL(DP_PosixClose, dPPosixClose),
    KNET_ADD_SYMBOL(DP_PosixListen, dPPosixListen),
    KNET_ADD_SYMBOL(DP_PosixBind, dPPosixBind),
    KNET_ADD_SYMBOL(DP_PosixConnect, dPPosixConnect),
    KNET_ADD_SYMBOL(DP_PosixGetpeername, dPPosixGetpeername),
    KNET_ADD_SYMBOL(DP_PosixGetsockname, dPPosixGetsockname),
    KNET_ADD_SYMBOL(DP_PosixSend, dPPosixSend),
    KNET_ADD_SYMBOL(DP_PosixSendto, dPPosixSendto),
    KNET_ADD_SYMBOL(DP_PosixWritev, dPPosixWritev),
    KNET_ADD_SYMBOL(DP_PosixSendmsg, dPPosixSendmsg),
    KNET_ADD_SYMBOL(DP_PosixRecv, dPPosixRecv),
    KNET_ADD_SYMBOL(DP_PosixRecvfrom, dPPosixRecvfrom),
    KNET_ADD_SYMBOL(DP_PosixRecvmsg, dPPosixRecvmsg),
    KNET_ADD_SYMBOL(DP_PosixReadv, dPPosixReadv),
    KNET_ADD_SYMBOL(DP_PosixGetsockopt, dPPosixGetsockopt),
    KNET_ADD_SYMBOL(DP_PosixAccept, dPPosixAccept),
    KNET_ADD_SYMBOL(DP_PosixShutdown, dPPosixShutdown),
    KNET_ADD_SYMBOL(DP_PosixRead, dPPosixRead),
    KNET_ADD_SYMBOL(DP_PosixWrite, dPPosixWrite),
    KNET_ADD_SYMBOL(DP_EpollCreateNotify, dPEpollCreateNotify),
    KNET_ADD_SYMBOL(DP_PosixEpollCtl, dPPosixEpollCtl),
    KNET_ADD_SYMBOL(DP_PosixEpollWait, dPPosixEpollWait),
    KNET_ADD_SYMBOL(DP_PosixPoll, dPPosixPoll),
    KNET_ADD_SYMBOL(DP_PosixIoctl, dPPosixIoctl),
    KNET_ADD_SYMBOL(DP_DebugShowHookReg, nPDKDebugShowHookReg),
    KNET_ADD_SYMBOL(DP_AddrHooksReg, dPAddrHooksReg),
    KNET_ADD_SYMBOL(DP_MemHookReg, nPDKMemHookReg),
    KNET_ADD_SYMBOL(DP_MempoolHookReg, nPDKMempoolHookReg),
    KNET_ADD_SYMBOL(DP_RandIntHookReg, nPDKRandIntHookReg),
    KNET_ADD_SYMBOL(DP_RegGetSelfWorkerIdHook, dPRegGetSelfWorkerIdHook),
    KNET_ADD_SYMBOL(DP_ClockReg, nPDKClockReg),
    KNET_ADD_SYMBOL(DP_HashTblHooksReg, dPHashTblHooksReg),
    KNET_ADD_SYMBOL(DP_Fib4TblHooksReg, dPFib4TblHooksReg),
    KNET_ADD_SYMBOL(DP_LogHookReg, nPDKLogHookReg),
    KNET_ADD_SYMBOL(DP_LogLevelSet, nPDKLogLevelSet),
    KNET_ADD_SYMBOL(DP_SemHookReg, nPDKSemHookReg),
    KNET_ADD_SYMBOL(DP_SocketCountGet, nPDKSemHookGetSockCnt),
    KNET_ADD_SYMBOL(DP_Cfg, dPCfg),
};

void DP_ShowStatistics(DP_StatType_t type, int workerId, uint32_t flag)
{
    g_dPShowStatistics(type, workerId, flag);
}

int DP_ProcIfreq(DP_Netdev_t* dev, int request, struct DP_Ifreq* ifreq)
{
    return g_dPProcIfreq(dev, request, ifreq);
}

DP_Netdev_t* DP_CreateNetdev(DP_NetdevCfg_t* cfg)
{
    return g_dPCreateNetdev(cfg);
}

int DP_RtCfg(DP_RtOpt_t op, DP_RtInfo_t* msg, DP_TbmAttr_t* attrs[], int attrCnt)
{
    return g_dPRtCfg(op, msg, attrs, attrCnt);
}

int DP_Init(int slave)
{
    return g_dPInit(slave);
}

int DP_CpdInit(void)
{
    return g_dPCpdInit();
}

int DP_CpdRunOnce(void)
{
    return g_dPCpdRunOnce();
}

void DP_RunWorkerOnce(int wid)
{
    g_dPRunWorkerOnce(wid);
}

int DP_PosixSocket(int domain, int type, int protocol)
{
    return g_dPPosixSocket(domain, type, protocol);
}

int DP_PosixClose(int fd)
{
    return g_dPPosixClose(fd);
}

int DP_PosixListen(int sockfd, int backlog)
{
    return g_dPPosixListen(sockfd, backlog);
}

int DP_PosixBind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    return g_dPPosixBind(sockfd, addr, addrlen);
}

int DP_PosixConnect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    return g_dPPosixConnect(sockfd, addr, addrlen);
}

int DP_PosixGetpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    return g_dPPosixGetpeername(sockfd, addr, addrlen);
}

int DP_PosixGetsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    return g_dPPosixGetsockname(sockfd, addr, addrlen);
}

ssize_t DP_PosixSend(int sockfd, const void* buf, size_t len, int flags)
{
    return g_dPPosixSend(sockfd, buf, len, flags);
}

ssize_t DP_PosixSendto(int sockfd, const void *buf, size_t len, int flags,
                       const struct sockaddr *destAddr, socklen_t addrlen)
{
    return g_dPPosixSendto(sockfd, buf, len, flags, destAddr, addrlen);
}

ssize_t DP_PosixWritev(int fd, const struct iovec *iov, int iovcnt)
{
    return g_dPPosixWritev(fd, iov, iovcnt);
}

ssize_t DP_PosixSendmsg(int sockfd, const struct msghdr *msg, int flags)
{
    return g_dPPosixSendmsg(sockfd, msg, flags);
}

ssize_t DP_PosixRecv(int sockfd, void* buf, size_t len, int flags)
{
    return g_dPPosixRecv(sockfd, buf, len, flags);
}

ssize_t DP_PosixRecvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *srcAddr, socklen_t *addrlen)
{
    return g_dPPosixRecvfrom(sockfd, buf, len, flags, srcAddr, addrlen);
}

ssize_t DP_PosixRecvmsg(int sockfd, struct msghdr *msg, int flags)
{
    return g_dPPosixRecvmsg(sockfd, msg, flags);
}

ssize_t DP_PosixReadv(int fd, const struct iovec *iov, int iovcnt)
{
    return g_dPPosixReadv(fd, iov, iovcnt);
}

int DP_PosixGetsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen)
{
    return g_dPPosixGetsockopt(sockfd, level, optname, optval, optlen);
}

int DP_PosixSetsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
    return g_dPPosixSetsockopt(sockfd, level, optname, optval, optlen);
}

int DP_PosixAccept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    return g_dPPosixAccept(sockfd, addr, addrlen);
}

int DP_PosixShutdown(int sockfd, int how)
{
    return g_dPPosixShutdown(sockfd, how);
}

ssize_t DP_PosixRead(int sockfd, void *buf, size_t count)
{
    return g_dPPosixRead(sockfd, buf, count);
}

ssize_t DP_PosixWrite(int sockfd, const void *buf, size_t count)
{
    return g_dPPosixWrite(sockfd, buf, count);
}

int DP_EpollCreateNotify(int size, DP_EpollNotify_t* callback)
{
    return g_dPEpollCreateNotify(size, callback);
}

int DP_PosixEpollCtl(int epfd, int op, int fd, struct epoll_event *event)
{
    return g_dPPosixEpollCtl(epfd, op, fd, event);
}

int DP_PosixEpollWait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    return g_dPPosixEpollWait(epfd, events, maxevents, timeout);
}

int DP_PosixFcntl(int fd, int cmd, int val)
{
    return g_dPPosixFcntl(fd, cmd, val);
}

int DP_PosixPoll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    return g_dPPosixPoll(fds, nfds, timeout);
}

int DP_PosixIoctl(int fd, int request, void* arg)
{
    return g_dPPosixIoctl(fd, request, arg);
}

uint32_t DP_DebugShowHookReg(DP_DebugShowHook hook)
{
    return g_nPDKDebugShowHookReg(hook);
}

int DP_AddrHooksReg(DP_AddrHooks_t* addrHooks)
{
    return g_dPAddrHooksReg(addrHooks);
}

uint32_t DP_MemHookReg(DP_MemHooks_S *memHooks)
{
    return g_nPDKMemHookReg(memHooks);
}

uint32_t DP_MempoolHookReg(DP_MempoolHooks_S* pHooks)
{
    return g_nPDKMempoolHookReg(pHooks);
}

uint32_t DP_RandIntHookReg(DP_RandomHooks_S *randHook)
{
    return g_nPDKRandIntHookReg(randHook);
}

int DP_RegGetSelfWorkerIdHook(DP_WorkerGetSelfIdHook getSelf)
{
    return g_dPRegGetSelfWorkerIdHook(getSelf);
}

uint32_t DP_ClockReg(DP_ClockGetTimeHook timeHook)
{
    return g_nPDKClockReg(timeHook);
}

int DP_HashTblHooksReg(DP_HashTblHooks_t *hashTblHooks)
{
    return g_dPHashTblHooksReg(hashTblHooks);
}

int DP_Fib4TblHooksReg(DP_Fib4TblHooks_t *fib4TblHooks)
{
    return g_dPFib4TblHooksReg(fib4TblHooks);
}

uint32_t DP_LogHookReg(DP_LogHook fnHook)
{
    return g_nPDKLogHookReg(fnHook);
}

void DP_LogLevelSet(DP_LogLevel_E logLevel)
{
    g_nPDKLogLevelSet(logLevel);
}

uint32_t DP_SemHookReg(const DP_SemHooks_S *pHooks)
{
    return g_nPDKSemHookReg(pHooks);
}

int DP_SocketCountGet(int type)
{
    return g_nPDKSemHookGetSockCnt(type);
}

int DP_Cfg(DP_CfgKv_t* kv, int cnt)
{
    return g_dPCfg(kv, cnt);
}

static struct KnetSymbolsCfg g_knetSymbolCfgs[] = {
    {NULL, "/usr/lib64/libdpstack.so", g_dp, ARRAY_SIZE(g_dp)},
};

int KnetInitSymbols(struct KnetSymbolsCfg *cfg)
{
    void *handle = NULL;
    void *symAddr = NULL;
    const char* soPath = realpath(cfg->soName, NULL);
    if (soPath == NULL) {
        KNET_ERR("So file is Null.");
        free((void*)soPath);
        return -1;
    }

    handle = dlopen(soPath, RTLD_NOW);
    if (handle == NULL) {
        free((void*)soPath);
        KNET_ERR("Load So file failed.");
        return -1;
    }
    for (int index = 0; index < cfg->symbolCnt; index++) {
        symAddr = dlsym(handle, cfg->symbols[index].symName);
        if (symAddr == NULL) {
            KNET_ERR("Dlsym load %s failed.", cfg->symbols[index].symName);
            dlclose(handle);
            handle = NULL;
            free((void*)soPath);
            return -1;
        }

        *(cfg->symbols[index].symRef) = symAddr;
    }
    free((void*)soPath);
    cfg->handle = handle;
    return 0;
}

int KnetInitDpSymbols(void)
{
    struct KnetSymbolsCfg *cfg = &g_knetSymbolCfgs[0];
    return KnetInitSymbols(cfg);
}

void KnetDeinitDpSymbols(void)
{
    struct KnetSymbolsCfg *cfg = &g_knetSymbolCfgs[0];
    if (cfg->handle != NULL) {
        dlclose(cfg->handle);
        cfg->handle = NULL;
        KNET_INFO("K-NET deinit dp symbols success.");
    }
}