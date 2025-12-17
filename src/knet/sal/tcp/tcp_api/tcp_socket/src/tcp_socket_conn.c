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
#include "knet_config.h"
#include "knet_osapi.h"
#include "knet_dpdk_init.h"
#include "knet_tcp_api_init.h"
#include "tcp_fd.h"
#include "tcp_os.h"
#include "knet_signal_tcp.h"
#include "knet_init.h"
#include "knet_cothread_inner.h"
#include "tcp_socket.h"

#define ADDRLEN_NULL_VALUE 0xAAAA // 用作addrlen为NULL时的打印值
#define DP_SO_USERDATA 10000000
#define LO_IP "127.0.0.1"

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
            KNET_ERR("DP_PosixSetsockopt send buf size failed, ret %d, errno %d, %s, dpFd %d, bufLen %d",
                ret, errno, strerror(errno), dpFd, bufLen);
        }
        BEFORE_DPFUNC();
        ret = DP_PosixSetsockopt(dpFd, SOL_SOCKET, SO_RCVBUF, &bufLen, sizeof(bufLen));
        AFTER_DPFUNC();
        if (ret < 0) {
            KNET_ERR("DP_PosixSetsockopt recv buf failed, ret %d, errno %d, %s, dpFd %d, bufLen %d",
                ret, errno, strerror(errno), dpFd, bufLen);
        }
    }
}

static int SocketCreateDpFd(int domain, int type, int protocol)
{
    BEFORE_DPFUNC();
    int dpFd = DP_PosixSocket(domain, type, protocol);
    AFTER_DPFUNC();
    if (dpFd < 0) {
        KNET_ERR("DP_PosixSocket ret %d, errno %d, %s, domain %d, type %d, protocol %d",
            dpFd, errno, strerror(errno), domain, type, protocol);
        return KNET_INVALID_FD;
    }

    return dpFd;
}

static int SocketCreateOsFd(int domain, int type, int protocol, int dpFd)
{
    KNET_CHECK_AND_GET_OS_API(g_origOsApi.socket, KNET_INVALID_FD);
    int osFd = g_origOsApi.socket(domain, type, protocol);
    if (!KNET_IsFdValid(osFd)) {
        KNET_ERR("OS socket ret %d, errno %d, %s", osFd, errno, strerror(errno));
        BEFORE_DPFUNC();
        DP_PosixClose(dpFd);
        AFTER_DPFUNC();
        return KNET_INVALID_FD;
    }

    UdpBufSizeSet(domain, type, dpFd);

    KNET_SetFdSocketState(KNET_FD_STATE_HIJACK, osFd, dpFd);
    KNET_INFO("OSFd %d, dpFd %d, domain %d, type %d, protocol %d",
        osFd, dpFd, domain, type, protocol);
    
    return osFd;
}

static inline int UserDataSet(int dpFd, int osFd)
{
    void *userData = (void *)(intptr_t)osFd;
    int ret = DP_PosixSetsockopt(dpFd, SOL_SOCKET, DP_SO_USERDATA, &userData, sizeof(void*));
    if (ret < 0) {
        KNET_ERR("DP_PosixSetsockopt set user data failed, ret %d, errno %d, %s, osFd %d, dpFd %d",
            ret, errno, strerror(errno), osFd, dpFd);
        return KNET_INVALID_FD;
    }
    return ret;
}

// 进行dp socket资源创建
int DpSocketResCreate(int domain, int type, int protocol)
{
    int dpFd = SocketCreateDpFd(domain, type, protocol);
    if (dpFd < 0) {
        KNET_ERR("SocketCreateDpFd ret %d, errno %d, %s, domain %d, type %d, protocol %d",
            dpFd, errno, strerror(errno), domain, type, protocol);
        return KNET_INVALID_FD;
    }

    int osFd = SocketCreateOsFd(domain, type, protocol, dpFd);
    if (osFd < 0) {
        KNET_ERR("SocketCreateOsFd ret %d, errno %d, %s, domain %d, type %d, protocol %d",
            osFd, errno, strerror(errno), domain, type, protocol);
        return KNET_INVALID_FD;
    }

    int ret = UserDataSet(dpFd, osFd);
    if (ret < 0) {
        KNET_ERR("UserDataSet set user data failed, ret %d, osFd %d, dpFd %d, errno %d, %s",
            ret, osFd, dpFd, errno, strerror(errno));
        BEFORE_DPFUNC();
        DP_PosixClose(dpFd);
        AFTER_DPFUNC();
        close(osFd);
        return KNET_INVALID_FD;
    }

    return osFd;
}

int KNET_DpSocket(int domain, int type, int protocol)
{
    /* 在协议栈使用socket前，完成对打流相关资源的初始化
     * 目前协议栈仅支持TCP和UDP，若后续有增补须同步修改 */
    int ret;
    if (domain == AF_INET && (((uint32_t)type & (SOCK_STREAM | SOCK_DGRAM)) != 0)) {
        ret = KNET_TrafficResourcesInit();
        if (ret != 0) {
            errno = ENAVAIL;
            KNET_ERR("Traffic resources init failed, errno %d, %s", errno, strerror(errno));
            return KNET_INVALID_FD;
        }
        KNET_INFO("Traffic domain %d, type %d, protocol %d", domain, type, protocol);
    }

    /* 在主线程等待退出的时候,走内核的创建 */
    if (!g_tcpInited|| KNET_IsCothreadGoKernel()) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.socket, KNET_INVALID_FD);
        int osFd = g_origOsApi.socket(domain, type, protocol);
        return osFd;
    }
    /* 信号退出流程中直接退出 */
    if (KNET_DpSignalGetWaitExit()) {
        errno = EPERM;
        KNET_WARN("Function socket was not allowed to be called in signal exiting process, errno %d, %s",
            errno, strerror(errno));
        return -1;
    }
    if (domain == AF_UNIX || domain == AF_LOCAL || domain == AF_NETLINK) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.socket, KNET_INVALID_FD);
        int osFd = g_origOsApi.socket(domain, type, protocol);
        KNET_INFO("OSFd %d IPC domain %d type %d protocol %d go os", osFd, domain, type, protocol);
        return osFd;
    }

    return DpSocketResCreate(domain, type, protocol);
}
/**
 * @note 协议栈目前不支持tcp在建连阶段自动根据网卡设备的mtu设置mss，需要KNET侧额外设置
*/
static void TcpMssSet(int sockfd)
{
    int mss = KNET_GetCfg(CONF_INTERFACE_MTU)->intValue - 40; // 40字节是IP头+tcp头的长度
    size_t len = sizeof(mss);
    int ret = DP_PosixSetsockopt(KNET_OsFdToDpFd(sockfd), IPPROTO_TCP, TCP_MAXSEG, &mss, (socklen_t)len);
    if (ret < 0) {
        KNET_ERR("DP_PosixSetsockopt set mss failed, ret %d, errno %d, %s, osFd %d, dpFd %d",
            ret, errno, strerror(errno), sockfd, KNET_OsFdToDpFd(sockfd));
    }
}

KNET_STATIC void ConstructAddr(const struct DP_Sockaddr* addr, struct sockaddr_in* addrIn)
{
    addrIn->sin_family = AF_INET;
    addrIn->sin_addr.s_addr = ((struct DP_SockaddrIn *)addr)->sin_addr.s_addr;
    addrIn->sin_port = ((struct DP_SockaddrIn *)addr)->sin_port;
}

int KNET_PreBind(void* userData, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen)
{
    if (addr == NULL) {
        KNET_ERR("PreBind addr is NULL");
        return -1;
    }
 
    if ((int)addrlen < (int)sizeof(struct DP_Sockaddr)) {
        KNET_ERR("PreBind addrLen %u is invalid", addrlen);
        return -1;
    }
 
    int osFd = (uintptr_t)userData;
    if (osFd <= 0) {
        KNET_ERR("Bind osFd failed , osFd %d invalid", osFd);
        return -1;
    }
 
    struct sockaddr_in addrIn = {0};
    ConstructAddr(addr, &addrIn);
 
    KNET_CHECK_AND_GET_OS_API(g_origOsApi.bind, KNET_INVALID_FD);
    int ret = g_origOsApi.bind(osFd, (struct sockaddr *)&addrIn, sizeof(addrIn));
    if (ret != 0) {
        KNET_ERR("OS bind ip 0x%x, port %d failed ret %d, errno %d, %s",
            addrIn.sin_addr.s_addr, addrIn.sin_port, ret, errno, strerror(errno));
        return ret;
    }

    KNET_DEBUG("OS bind ip 0x%x port %d success", addrIn.sin_addr.s_addr, addrIn.sin_port);
 
    return ret;
}

int KNET_DpBind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.bind, KNET_INVALID_FD);
        return g_origOsApi.bind(sockfd, addr, addrlen);
    }

    if (!KNET_IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d bind is not hijacked", sockfd);
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.bind, KNET_INVALID_FD);
        return g_origOsApi.bind(sockfd, addr, addrlen);
    }

    BEFORE_DPFUNC();
    int ret = DP_PosixBind(KNET_OsFdToDpFd(sockfd), addr, addrlen);
    AFTER_DPFUNC();
    if (ret < 0) {
        KNET_ERR("DP_PosixBind ret %d, osFd %d, dpFd %d, ip 0x%x, port 0x%x, errno %d, %s",
            ret, sockfd, KNET_OsFdToDpFd(sockfd), ((struct sockaddr_in *)addr)->sin_addr.s_addr,
            ((struct sockaddr_in *)addr)->sin_port, errno, strerror(errno));
        return ret;
    }
    KNET_DEBUG("Bind success, osFd %d, dpFd %d, ip 0x%x, port 0x%x",
        sockfd, KNET_OsFdToDpFd(sockfd),
        ((struct sockaddr_in *)addr)->sin_addr.s_addr, ((struct sockaddr_in *)addr)->sin_port);
    return ret;
}

int KNET_DpListen(int sockfd, int backlog)
{
    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.listen, KNET_INVALID_FD);
        return g_origOsApi.listen(sockfd, backlog);
    }

    if (!KNET_IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d listen is not hijacked, backlog %d", sockfd, backlog);
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.listen, KNET_INVALID_FD);
        return g_origOsApi.listen(sockfd, backlog);
    }

    TcpMssSet(sockfd);
    BEFORE_DPFUNC();
    int ret = DP_PosixListen(KNET_OsFdToDpFd(sockfd), backlog);
    AFTER_DPFUNC();
    if (ret < 0) {
        KNET_ERR("DP_PosixListen ret %d, errno %d, %s, dpfd %d, backlog %d", ret, errno, strerror(errno),
            KNET_OsFdToDpFd(sockfd), backlog);
        return ret;
    }

    KNET_CHECK_AND_GET_OS_API(g_origOsApi.listen, KNET_INVALID_FD);
    ret = g_origOsApi.listen(sockfd, backlog);
    if (ret < 0) {
        KNET_ERR("Os listen ret %d, errno %d, %s, sockfd %d, backlog %d", ret, errno, strerror(errno), sockfd,
            backlog);
    }

    KNET_DEBUG("Listen success, listenfd %d, dpListenfd %d, backlog %d", sockfd, KNET_OsFdToDpFd(sockfd), backlog);
    return ret;
}

static int ConnectOsFd(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    KNET_CHECK_AND_GET_OS_API(g_origOsApi.connect, KNET_INVALID_FD);
    KNET_SetFdSocketState(KNET_FD_STATE_INVALID, sockfd, KNET_OsFdToDpFd(sockfd));
    int ret =  g_origOsApi.connect(sockfd, addr, addrlen);
    if (ret < 0) {
        if (errno == EINPROGRESS) {
            KNET_INFO("Internal exchange osFd %d dpFd %d ip 0x%x port %hu connect failed, ret %d, errno %d, %s", sockfd,
                KNET_OsFdToDpFd(sockfd), ((struct sockaddr_in*)addr)->sin_addr.s_addr,
                ((struct sockaddr_in*)addr)->sin_port, ret, errno, strerror(errno));
        } else {
            KNET_ERR("Internal exchange osFd %d dpFd %d ip 0x%x port %hu connect failed, ret %d, errno %d, %s", sockfd,
                KNET_OsFdToDpFd(sockfd), ((struct sockaddr_in*)addr)->sin_addr.s_addr,
                ((struct sockaddr_in*)addr)->sin_port, ret, errno, strerror(errno));
        }
        return ret;
    }

    KNET_DEBUG("Internal exchange osFd %d dpFd %d ip 0x%x port %hu connect success", sockfd, KNET_OsFdToDpFd(sockfd),
               ((struct sockaddr_in*)addr)->sin_addr.s_addr, ((struct sockaddr_in*)addr)->sin_port);
    return ret;
}

int KNET_DpConnect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.connect, KNET_INVALID_FD);
        return g_origOsApi.connect(sockfd, addr, addrlen);
    }

    if (!KNET_IsFdHijack(sockfd)) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.connect, KNET_INVALID_FD);
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d connect is not hijacked", sockfd);
        return g_origOsApi.connect(sockfd, addr, addrlen);
    }

    if (KNET_UNLIKELY(addr == NULL)) {
        KNET_ERR("osFd %d connect failed, addr is NULL", sockfd);
        errno = EFAULT;
        return -1;
    }

    // 当connect ip属于内交换，则恢复为内核fd，调用OS接口
    uint32_t connectAddr = ((struct sockaddr_in *)addr)->sin_addr.s_addr;
    if (connectAddr == (uint32_t)KNET_GetCfg(CONF_INTERFACE_IP)->intValue ||
        connectAddr == INADDR_ANY || connectAddr == inet_addr(LO_IP)) {
        return ConnectOsFd(sockfd, addr, addrlen);
    }

    BEFORE_DPFUNC();
    int ret = DP_PosixConnect(KNET_OsFdToDpFd(sockfd), addr, addrlen);
    AFTER_DPFUNC();
    if (ret < 0) {
        if (errno == EINPROGRESS) {
            // 非阻塞connect，需要设置连接位，否则epoll会一直读到内核的可写事件
            KNET_SetEstablishedFdState(sockfd);
            KNET_INFO("DP_PosixConnect ret %d, osFd %d, dpFd %d, ip 0x%x, port %hu, errno %d, %s",
                ret, sockfd, KNET_OsFdToDpFd(sockfd), ((struct sockaddr_in *)addr)->sin_addr.s_addr,
                ((struct sockaddr_in *)addr)->sin_port, errno, strerror(errno));
        } else {
            KNET_ERR("DP_PosixConnect ret %d, osFd %d, dpFd %d, ip 0x%x, port %hu, errno %d, %s",
                ret, sockfd, KNET_OsFdToDpFd(sockfd), ((struct sockaddr_in *)addr)->sin_addr.s_addr,
                ((struct sockaddr_in *)addr)->sin_port, errno, strerror(errno));
        }
        return ret;
    }

    KNET_SetEstablishedFdState(sockfd);

    KNET_DEBUG("Connect success, osFd %d, dpFd %d, ip 0x%x, port %hu",
        sockfd, KNET_OsFdToDpFd(sockfd), ((struct sockaddr_in *)addr)->sin_addr.s_addr,
        ((struct sockaddr_in *)addr)->sin_port);

    return ret;
}

int KNET_DpGetpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.getpeername, KNET_INVALID_FD);
        return g_origOsApi.getpeername(sockfd, addr, addrlen);
    }

    if (!KNET_IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d getpeername is not hijacked", sockfd);
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.getpeername, KNET_INVALID_FD);
        return g_origOsApi.getpeername(sockfd, addr, addrlen);
    }
    BEFORE_DPFUNC();
    int ret = DP_PosixGetpeername(KNET_OsFdToDpFd(sockfd), addr, addrlen);
    AFTER_DPFUNC();
    if (ret < 0) {
        KNET_ERR("DP_PosixGetpeername ret %d, errno %d, %s, osFd %d, dpFd %d, addrlen %u",
            ret, errno, strerror(errno), sockfd, KNET_OsFdToDpFd(sockfd),
            (addrlen != NULL) ? *addrlen : ADDRLEN_NULL_VALUE);
    }
    return ret;
}

int KNET_DpGetsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.getsockname, KNET_INVALID_FD);
        return g_origOsApi.getsockname(sockfd, addr, addrlen);
    }

    if (!KNET_IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d getsockname is not hijacked", sockfd);
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.getsockname, KNET_INVALID_FD);
        return g_origOsApi.getsockname(sockfd, addr, addrlen);
    }
    BEFORE_DPFUNC();
    int ret = DP_PosixGetsockname(KNET_OsFdToDpFd(sockfd), addr, addrlen);
    AFTER_DPFUNC();
    if (ret < 0) {
        KNET_ERR("DP_PosixGetsockname ret %d, errno %d, %s, osFd %d, dpFd %d, addrlen %u",
            ret, errno, strerror(errno), sockfd, KNET_OsFdToDpFd(sockfd),
            (addrlen != NULL) ? *addrlen : ADDRLEN_NULL_VALUE);
    }
    return ret;
}

static int AcceptCreateOsFd(int acceptDpFd)
{
    int acceptOsFd = g_origOsApi.socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (!KNET_IsFdValid(acceptOsFd)) {
        KNET_ERR("OS socket ret %d, errno %d, %s", acceptOsFd, errno, strerror(errno));
        BEFORE_DPFUNC();
        DP_PosixClose(acceptDpFd);
        AFTER_DPFUNC();
        return INVALID_FD;
    }

    KNET_SetFdSocketState(KNET_FD_STATE_HIJACK, acceptOsFd, acceptDpFd);
    KNET_SetEstablishedFdState(acceptOsFd);
    return acceptOsFd;
}

KNET_STATIC int AcceptNoBlock(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    KNET_CHECK_AND_GET_OS_API(g_origOsApi.accept, KNET_INVALID_FD);
    int kernelAcceptFd = g_origOsApi.accept(sockfd, addr, addrlen);
    if (kernelAcceptFd > 0) {
        KNET_DEBUG("kernelAccept: sockfd %d, kernelAcceptFd %d", sockfd, kernelAcceptFd);
        if (addr != NULL && addrlen != NULL && *addrlen >= sizeof(struct sockaddr_in)) {
            KNET_DEBUG("kernelAccept: client ip 0x%x, port %hu", ((struct sockaddr_in *)addr)->sin_addr.s_addr,
                ((struct sockaddr_in *)addr)->sin_port);
        }
        return kernelAcceptFd;
    }

    BEFORE_DPFUNC();
    int acceptDpFd = DP_PosixAccept(KNET_OsFdToDpFd(sockfd), addr, addrlen);
    AFTER_DPFUNC();
    if (acceptDpFd < 0) {
        if (errno != EAGAIN) {
            KNET_ERR("DP_PosixAccept acceptDpFd %d, sockfd %d, dpFd %d, errno %d, %s, addrlen %u",
                acceptDpFd, sockfd, KNET_OsFdToDpFd(sockfd), errno, strerror(errno),
                (addrlen != NULL) ? *addrlen : ADDRLEN_NULL_VALUE);
        } else {
            KNET_DEBUG("DP_PosixAccept acceptDpFd %d, sockfd %d, dpFd %d, errno %d, %s, addrlen %u",
                acceptDpFd, sockfd, KNET_OsFdToDpFd(sockfd), errno, strerror(errno),
                (addrlen != NULL) ? *addrlen : ADDRLEN_NULL_VALUE);
        }
        return -1;
    }

    KNET_DEBUG("dpAccept: sockfd %d, acceptDpFd %d", sockfd, acceptDpFd);
    if (addr != NULL && addrlen != NULL && *addrlen >= sizeof(struct sockaddr_in)) {
        KNET_DEBUG("dpAccept: client ip 0x%x, port %hu", ((struct sockaddr_in *)addr)->sin_addr.s_addr,
            ((struct sockaddr_in *)addr)->sin_port);
    }

    /* 限制：os socket的domain、type、protocol必须同acceptDpFd的一致，目前协议栈accept只会返回tcp类型，所以socket入参写死
     * 限制原因：TcpSoLingerSet函数是通过osFd的类型去判断dpFd的类型 */
    int acceptOsFd = AcceptCreateOsFd(acceptDpFd);
    if (acceptOsFd < 0) {
        KNET_ERR("AcceptCreateOsFd acceptOsFd %d, errno %d, %s", acceptOsFd, errno, strerror(errno));
        return INVALID_FD;
    }

    return acceptOsFd;
}

static int SetFlags(int sockfd, int acceptOsFd, int sockFlag)
{
    int ret = fcntl(sockfd, F_SETFL, sockFlag);
    if (ret != 0) {
        KNET_ERR("Accept listenFd %d fcntl failed ret %d, errno %d, %s", sockfd, ret, errno, strerror(errno));
        close(acceptOsFd);
        return ret;
    }

    ret = fcntl(acceptOsFd, F_SETFL, sockFlag);
    if (ret != 0) {
        KNET_ERR("Accept fd %d fcntl failed ret %d, errno %d, %s", acceptOsFd, ret, errno, strerror(errno));
        close(acceptOsFd);
        return ret;
    }
    return 0;
}

static int AccceptLoop(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int sockFlag)
{
    int acceptOsFd = -1;
    while (1) {
        acceptOsFd = AcceptNoBlock(sockfd, addr, addrlen);
        if (acceptOsFd > 0) {
            // 需要设置回去原来的标志位
            int ret = SetFlags(sockfd, acceptOsFd, sockFlag);
            if (ret < 0) {
                return ret;
            }
            errno = EOK;
            break;
        }

        if (errno != EAGAIN || (((uint32_t)sockFlag) & O_NONBLOCK) != 0) {
            break;
        }

        int curSig = KNET_DpSignalGetCurSig();
        if (KNET_UNLIKELY(curSig)) {
            KNET_INFO("Received signal %d, stop waiting", curSig);
            errno = EINTR;
            return -1;
        }
    }

    return acceptOsFd;
}

int KNET_DpAccept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.accept, KNET_INVALID_FD);
        return g_origOsApi.accept(sockfd, addr, addrlen);
    }

    if (!KNET_IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d accept is not hijacked", sockfd);
        return g_origOsApi.accept(sockfd, addr, addrlen);
    }

    int sockFlag = fcntl(sockfd, F_GETFL, 0);
    if (KNET_UNLIKELY(sockFlag < 0)) {
        KNET_ERR("Accept fcntl F_GETFL sockfd %d failed, ret %d, errno %d, %s",
            sockfd, sockFlag, errno, strerror(errno));
        return -1;
    }
    int ret = fcntl(sockfd, F_SETFL, (uint32_t)sockFlag | O_NONBLOCK);
    if (KNET_UNLIKELY(ret < 0)) {
        KNET_ERR("Accept fcntl F_SETFL sockfd %d failed, ret %d, errno %d, %s", sockfd, ret, errno, strerror(errno));
        return -1;
    }
 
    KNET_DpSignalClearCurSig();
    int acceptOsFd = AccceptLoop(sockfd, addr, addrlen, sockFlag);

    return acceptOsFd;
}

KNET_STATIC int AcceptCheckFlags(int sockfd, int flags)
{
    uint validFlags = SOCK_CLOEXEC | SOCK_NONBLOCK;

    // 入参flags校验
    if (((uint)flags & ~validFlags) != 0) {
        KNET_ERR("Sockfd %d, accept4 flags %d is invalid", sockfd, flags);
        errno = EINVAL;
        return -1;
    }

    return 0;
}

static int Accept4ProcFlags(int sockfd, int flags)
{
    int ret = AcceptCheckFlags(sockfd, flags);
    if (ret < 0) {
        return -1;
    }

    /* 设置 socket 为非阻塞 */
    if (((uint)flags &  SOCK_NONBLOCK) != 0) {
        int sockFlags = KNET_DpFcntl(sockfd, F_GETFL, 0);
        if (sockFlags < 0) {
            KNET_ERR("Fcntl F_GETFL sockfd %d failed, ret %d, errno %d, %s", sockfd, sockFlags, errno, strerror(errno));
            return -1;
        }
        ret = KNET_DpFcntl(sockfd, F_SETFL, (uint)sockFlags | O_NONBLOCK);
        if (ret < 0) {
            KNET_ERR("Fcntl F_SETFL sockfd %d failed, ret %d, errno %d, %s", sockfd, ret, errno, strerror(errno));
            return -1;
        }
    }
    return 0;
}

int KNET_DpAccept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.accept4, KNET_INVALID_FD);
        return g_origOsApi.accept4(sockfd, addr, addrlen, flags);
    }

    if (!KNET_IsFdHijack(sockfd)) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.accept4, KNET_INVALID_FD);
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d accept4 is not hijacked", sockfd);
        return g_origOsApi.accept4(sockfd, addr, addrlen, flags);
    }

    int ret = Accept4ProcFlags(sockfd, flags);
    if (ret < 0) {
        KNET_ERR("Accept4 process flags failed, sockfd %d, ret %d, errno %d, %s, flags %d", sockfd, ret, errno,
            strerror(errno), flags);
        return -1;
    }

    /* fork()函数之后，不支持子进程调用父进程的fd，默认使能了SOCK_CLOEXEC标志位 */
    int acceptOsFd = KNET_DpAccept(sockfd, addr, addrlen);
    if (acceptOsFd < 0) {
        KNET_ERR("ListenFd %d accept failed, ret %d, errno %d, %s", sockfd, acceptOsFd, errno, strerror(errno));
        return -1;
    }

    return acceptOsFd;
}

static int CloseOsFd(int sockfd)
{
    int ret = 0;
    int failed = 0;
    if (KNET_GetFdType(sockfd) == KNET_FD_TYPE_EPOLL) {
        ret = g_origOsApi.close(KNET_GetFdPrivateData(sockfd)->epollData.data.eventFd);
        if (ret < 0) {
            failed = -1;
            KNET_ERR("OS close eventfd ret %d, errno %d, %s, close eventfd sockfd %d", ret, errno, strerror(errno),
                KNET_GetFdPrivateData(sockfd)->epollData.data.eventFd);
        }
    }

    KNET_ResetFdState(sockfd);

    /* 必须先清理fd资源，os close必须放最后。若先os close，会导致多线程情况下其他进程申请了fd，fd资源又被清理掉 */
    ret = g_origOsApi.close(sockfd);
    if (ret < 0) {
        failed = -1;
        KNET_ERR("OS close ret %d, errno %d, %s, close os sockfd %d", ret, errno, strerror(errno), sockfd);
    }
    return failed;
}

static int CloseDpFdIfExist(int sockfd)
{
    if (!KNET_IsFdValid(sockfd) || KNET_OsFdToDpFd(sockfd) < 0) {
        return  g_origOsApi.close(sockfd);
    }

    int ret = DP_PosixClose(KNET_OsFdToDpFd(sockfd));
    if (ret < 0) {
        KNET_WARN("DP_PosixClose osFd %d, dpFd %d ret %d, errno %d, %s", sockfd, KNET_OsFdToDpFd(sockfd), ret, errno,
                  strerror(errno));
        // 共线程时，不能释放其他线程的内核fd, 此errno表示共线程时跨线程操作fd, fd无效非法
        if (errno == EBADF && (KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue == 1)) {
            KNET_ERR("DP_PosixClose %d failed, close os fd %d will not run in cothread, maybe close fd in other thread",
                KNET_OsFdToDpFd(sockfd), sockfd);
            return ret;
        }
    }

    KNET_DEBUG("DP close internal exchange osFd %d dpFd %d success", sockfd, KNET_OsFdToDpFd(sockfd));
    KNET_ResetFdState(sockfd);
    return g_origOsApi.close(sockfd);
}

int KNET_DpClose(int sockfd)
{
    KNET_CHECK_AND_GET_OS_API(g_origOsApi.close, KNET_INVALID_FD);
    if (!g_tcpInited) {
        return g_origOsApi.close(sockfd);
    }

    int failed = 0;

    if (!KNET_IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d close is not hijacked", sockfd);
        return CloseDpFdIfExist(sockfd);
    }

    /* 子进程不能DP_PosixClose，redis back ground saving fork会关掉listenfd，从而释放共享的dpdk资源，导致父进程无法正常运行 */
    if (!KNET_DpIsForkedParent()) {
        return 0;
    }

    if (KNET_GetFdType(sockfd) == KNET_FD_TYPE_SOCKET) {
        KNET_DEBUG("Close socket osFd %d, dpFd %d", sockfd, KNET_OsFdToDpFd(sockfd));
    }

    KNET_DEBUG("Close osFd %d, dpFd %d", sockfd, KNET_OsFdToDpFd(sockfd));
    BEFORE_DPFUNC();
    int ret = DP_PosixClose(KNET_OsFdToDpFd(sockfd));
    AFTER_DPFUNC();
    if (ret < 0) {
        failed = -1;
        KNET_ERR("DP_PosixClose ret %d, errno %d, %s, close sockfd %d, dpFd %d",
            ret, errno, strerror(errno), sockfd, KNET_OsFdToDpFd(sockfd));
        // 共线程时，不能释放其他线程的内核fd, 此errno表示共线程时跨线程操作fd, fd无效非法
        if (errno == EBADF && (KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue == 1)) {
            KNET_ERR("DP_PosixClose %d failed, close os fd %d will not run in cothread, maybe close fd in other thread",
                KNET_OsFdToDpFd(sockfd), sockfd);
            return -1;
        }
    }
    ret = CloseOsFd(sockfd);
    if (ret < 0) {
        failed = -1;
        KNET_ERR("CloseOsFd ret %d, errno %d, %s, sockfd %d", ret, errno, strerror(errno), sockfd);
    }

    return failed;
}

int KNET_DpShutdown(int sockfd, int how)
{
    if (!g_tcpInited) {
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.shutdown, KNET_INVALID_FD);
        return g_origOsApi.shutdown(sockfd, how);
    }

    if (!KNET_IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d shutdown is not hijacked, how %d", sockfd, how);
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.shutdown, KNET_INVALID_FD);
        return g_origOsApi.shutdown(sockfd, how);
    }
    BEFORE_DPFUNC();
    int ret = DP_PosixShutdown(KNET_OsFdToDpFd(sockfd), how);
    AFTER_DPFUNC();
    if (ret < 0) {
        KNET_ERR("DP_PosixShutdown ret %d, errno %d, %s, shutdown osFd %d, dpFd %d, how %d", ret, errno,
            strerror(errno), sockfd, KNET_OsFdToDpFd(sockfd), how);
    }

    KNET_DEBUG("Shutdown osFd %d, dpFd %d success", sockfd, KNET_OsFdToDpFd(sockfd));

    return ret;
}