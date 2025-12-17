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
#include "knet_tcp_symbols.h"
#include "knet_log.h"
#include "knet_telemetry.h"

void (*g_dPShowStatistics)(DP_StatType_t type, int workerId, uint32_t flag);
int (*g_dPSocketCountGet)(int type);
int (*g_dPGetSocketState)(int fd, DP_SocketState_t* state);
int (*g_dPGetSocketDetails)(int fd, DP_SockDetails_t* info);
int (*g_dPProcIfreq)(DP_Netdev_t* dev, int request, struct DP_Ifreq* ifreq);
DP_Netdev_t* (*g_dPCreateNetdev)(DP_NetdevCfg_t* cfg);
int (*g_dPRtCfg)(DP_RtOpt_t op, DP_RtInfo_t* msg, DP_TbmAttr_t* attrs[], int attrCnt);
int (*g_dPInit)(int slave);
int (*g_dPCpdInit)(void);
int (*g_dPCpdRunOnce)(int cpdId);
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
void* (*g_dPZcopyAlloc)(size_t size);
void (*g_dPZcopyFree)(void *addr);
ssize_t (*g_dPZWritev)(int fd, const struct DP_ZIovec *iov, int iovcnt);
ssize_t (*g_dPZReadv)(int fd, const struct DP_ZIovec *iov, int iovcnt);
uint32_t (*g_dPDebugShowHookReg)(DP_DebugShowHook hook);
int (*g_dPAddrHooksReg)(DP_AddrHooks_t* addrHooks);
int (*g_dPBindAddrHooksReg)(DP_AddrBindHooks_t* addrBindHooks);
uint32_t (*g_dPMemHookReg)(DP_MemHooks_S *memHooks);
uint32_t (*g_dPMempoolHookReg)(DP_MempoolHooks_S* pHooks);
uint32_t (*g_dPRandIntHookReg)(DP_RandomHooks_S *randHook);
int (*g_dPRegGetSelfWorkerIdHook)(DP_WorkerGetSelfIdHook getSelf);
uint32_t (*g_dPClockReg)(DP_ClockGetTimeHook timeHook);
int (*g_dPHashTblHooksReg)(DP_HashTblHooks_t *hashTblHooks);
int (*g_dPFib4TblHooksReg)(DP_Fib4TblHooks_t *fib4TblHooks);
uint32_t (*g_dPLogHookReg)(DP_LogHook fnHook);
uint32_t(*g_dPSemHookReg)(const DP_SemHooks_S *pHooks);
void (*g_dPLogLevelSet)(DP_LogLevel_E logLevel);
int (*g_dPSemHookGetSockCnt)(int type);
int (*g_dPCfg)(DP_CfgKv_t* kv, int cnt);
int (*g_dpCpdQueHooksReg)(void* queOps);
int (*g_dpGetNetdevQueMap)(int32_t wid, int32_t ifIndex, uint32_t* queMap, int32_t mapCnt);

// hisackdp.so
static struct KnetSymbolsInfo g_tcp[] = {
    KNET_ADD_SYMBOL(DP_ShowStatistics, dPShowStatistics),
    KNET_ADD_SYMBOL(DP_SocketCountGet, dPSocketCountGet),
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
    KNET_ADD_SYMBOL(DP_ZcopyAlloc, dPZcopyAlloc),
    KNET_ADD_SYMBOL(DP_ZcopyFree, dPZcopyFree),
    KNET_ADD_SYMBOL(DP_ZWritev, dPZWritev),
    KNET_ADD_SYMBOL(DP_ZReadv, dPZReadv),
    KNET_ADD_SYMBOL(DP_DebugShowHookReg, dPDebugShowHookReg),
    KNET_ADD_SYMBOL(DP_AddrHooksReg, dPAddrHooksReg),
    KNET_ADD_SYMBOL(DP_AddrBindHooksReg, dPBindAddrHooksReg),
    KNET_ADD_SYMBOL(DP_MemHookReg, dPMemHookReg),
    KNET_ADD_SYMBOL(DP_MempoolHookReg, dPMempoolHookReg),
    KNET_ADD_SYMBOL(DP_RandIntHookReg, dPRandIntHookReg),
    KNET_ADD_SYMBOL(DP_RegGetSelfWorkerIdHook, dPRegGetSelfWorkerIdHook),
    KNET_ADD_SYMBOL(DP_ClockReg, dPClockReg),
    KNET_ADD_SYMBOL(DP_HashTblHooksReg, dPHashTblHooksReg),
    KNET_ADD_SYMBOL(DP_LogHookReg, dPLogHookReg),
    KNET_ADD_SYMBOL(DP_LogLevelSet, dPLogLevelSet),
    KNET_ADD_SYMBOL(DP_SemHookReg, dPSemHookReg),
    KNET_ADD_SYMBOL(DP_SocketCountGet, dPSemHookGetSockCnt),
    KNET_ADD_SYMBOL(DP_Cfg, dPCfg),
    KNET_ADD_SYMBOL(DP_CpdQueHooksReg, dpCpdQueHooksReg),
    KNET_ADD_SYMBOL(DP_GetNetdevQueMap, dpGetNetdevQueMap),
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

int DP_CpdRunOnce(int cpdId)
{
    return g_dPCpdRunOnce(cpdId);
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

void* DP_ZcopyAlloc(size_t size)
{
    return g_dPZcopyAlloc(size);
}

void DP_ZcopyFree(struct DP_ZIovec *iov)
{
    return g_dPZcopyFree(iov);
}

ssize_t DP_ZWritev(int fd, const struct DP_ZIovec *iov, int iovcnt)
{
    return g_dPZWritev(fd, iov, iovcnt);
}

ssize_t DP_ZReadv(int fd, struct DP_ZIovec *iov, int iovcnt)
{
    return g_dPZReadv(fd, iov, iovcnt);
}

uint32_t DP_DebugShowHookReg(DP_DebugShowHook hook)
{
    return g_dPDebugShowHookReg(hook);
}

int DP_AddrHooksReg(DP_AddrHooks_t* addrHooks)
{
    return g_dPAddrHooksReg(addrHooks);
}

int DP_AddrBindHooksReg(DP_AddrBindHooks_t* addrBindHooks)
{
    return g_dPBindAddrHooksReg(addrBindHooks);
}

uint32_t DP_MemHookReg(DP_MemHooks_S *memHooks)
{
    return g_dPMemHookReg(memHooks);
}

uint32_t DP_MempoolHookReg(DP_MempoolHooks_S* pHooks)
{
    return g_dPMempoolHookReg(pHooks);
}

uint32_t DP_RandIntHookReg(DP_RandomHooks_S *randHook)
{
    return g_dPRandIntHookReg(randHook);
}

int DP_RegGetSelfWorkerIdHook(DP_WorkerGetSelfIdHook getSelf)
{
    return g_dPRegGetSelfWorkerIdHook(getSelf);
}

uint32_t DP_ClockReg(DP_ClockGetTimeHook timeHook)
{
    return g_dPClockReg(timeHook);
}

int DP_HashTblHooksReg(DP_HashTblHooks_t *hashTblHooks)
{
    return g_dPHashTblHooksReg(hashTblHooks);
}

uint32_t DP_LogHookReg(DP_LogHook fnHook)
{
    return g_dPLogHookReg(fnHook);
}

void DP_LogLevelSet(DP_LogLevel_E logLevel)
{
    g_dPLogLevelSet(logLevel);
}

uint32_t DP_SemHookReg(const DP_SemHooks_S *pHooks)
{
    return g_dPSemHookReg(pHooks);
}

int DP_SocketCountGet(int type)
{
    return g_dPSemHookGetSockCnt(type);
}

int DP_Cfg(DP_CfgKv_t* kv, int cnt)
{
    return g_dPCfg(kv, cnt);
}

int DP_CpdQueHooksReg(void* queOps)
{
    return g_dpCpdQueHooksReg(queOps);
}

int32_t DP_GetNetdevQueMap(int32_t wid, int32_t ifIndex, uint32_t* queMap, int32_t mapCnt)
{
    return g_dpGetNetdevQueMap(wid, ifIndex, queMap, mapCnt);
}

static struct KnetSymbolsCfg g_knetSymbolCfgs[] = {
    {NULL, "/usr/lib64/libdpstack.so", g_tcp, ARRAY_SIZE(g_tcp)},
};

static int KnetInitSymbols(struct KnetSymbolsCfg *cfg)
{
    void *handle = NULL;
    void *symAddr = NULL;
    const char* soPath = realpath(cfg->soName, NULL);
    if (soPath == NULL) {
        KNET_ERR("So file is Null.");
        return -1;
    }

    handle = dlopen(soPath, RTLD_NOW);
    if (handle == NULL) {
        free((void*)soPath);
        KNET_ERR("Load So file failed, %s.", dlerror());
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
        KNET_INFO("K-NET deinit tcp symbols success.");
    }
}