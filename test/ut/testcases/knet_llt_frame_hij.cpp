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
#include <sys/uio.h>
#include <dlfcn.h>

#include "dp_posix_socket_api.h"
#include "knet_types.h"
#include "knet_log.h"
#include "tcp_fd.h"
#include "knet_atomic.h"
#include "knet_tcp_symbols.h"
#include "knet_osapi.h"
#include "knet_socket_bridge.h"
#include "tcp_event.h"
#include "tcp_socket.h"

#include "securec.h"
#include "knet_lock.h"
#include "knet_init.h"

#include "common.h"
#include "mock.h"

extern  "C" {
extern bool g_tcpInited;
extern bool g_isForkedParent;
extern void AssignDlsym(void **ptr, const char *name);
extern bool KNET_DpIsForkedParent(void);
extern void KNET_SetDpInited(void);
extern int AcceptNoBlock(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
extern int DP_Init(int slave);
extern void DP_Deinit(int slave);

}

static bool FuncRetTrue(void)
{
    return true;
}

static bool FuncAccept(void)
{
    errno = EINVAL;
    return 0;
}

static int FunSocketEacces(void)
{
    errno = EACCES;
    return -1;
}

static int FunSocketEnobufs(void)
{
    errno = ENOBUFS;
    return -1;
}

static int FunCloseEio(void)
{
    errno = EIO;
    return -1;
}

static int FunCloseEintr(void)
{
    errno = EINTR;
    return -1;
}

static int Init()
{
    int ret = 0;
    ret = KNET_FdInit();
    return ret;
}

static int Deinit()
{
    KNET_FdDeinit();
    return 0;
}
void KNET_AssignDlsym(void **ptr, const char *name)
{
    *ptr = NULL;
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_CREATE_ELIBBAD, Init, Deinit)
{
    int domain = AF_UNIX;
    int ret = 0;
    int type = SOCK_RAW;
    int protocol = 0;
    int sockfd = 1;
    int backlog = 1;
    void *buf = 0;
    size_t len = 0;
    size_t count = 0;
    int flags = 0;
    struct sockaddr *addr = { 0 };
    socklen_t addrlen = 0;
    struct iovec *iov = { 0 };
    int iovcnt = 0;
    struct msghdr *msg = { 0 };
    int level = 0;
    int optname = 0;
    void *optval = { 0 };
    socklen_t optlen = { 0 };
    unsigned long request = 0;
    errno = 0;


    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetNegative(1));
    Mock->Create(AssignDlsym, KNET_AssignDlsym);
    g_tcpInited = false;
    struct OsApi temp = {0};
    struct OsApi origin = {0};
    origin = g_origOsApi;
    g_origOsApi = temp;
    ret = socket(domain, type, protocol);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = listen(sockfd, backlog);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = connect(sockfd, addr, addrlen);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = bind(sockfd, addr, addrlen);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = getpeername(sockfd, addr, &addrlen);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = getsockname(sockfd, addr, &addrlen);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = send(sockfd, buf, len, flags);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = sendto(sockfd, buf, len, flags, addr, addrlen);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = writev(sockfd, iov, iovcnt);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = write(sockfd, buf, count);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = sendmsg(sockfd, msg, flags);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = recv(sockfd, buf, len, flags);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = recvmsg(sockfd, msg, flags);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = recvfrom(sockfd, buf, len, flags, addr, &addrlen);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = readv(sockfd, iov, iovcnt);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = read(sockfd, buf, count);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = getsockopt(sockfd, level, optname, optval, optlen);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = setsockopt(sockfd, level, optname, optval, &optlen);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = accept(sockfd, addr, &addrlen);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = accept4(sockfd, addr, &addrlen, flags);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = shutdown(sockfd, 1);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = fork();
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = ioctl(sockfd, request);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    struct timespec timespec = { 0 };
    sigset_t mask = { 0 };
    ret = pselect(0, NULL, NULL, NULL, &timespec, &mask);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = select(0, NULL, NULL, NULL, (timeval *)&timespec);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    struct pollfd *fds = { 0 };
    nfds_t nfds = 1;
    const struct timespec *timeoutTs = NULL;
    const sigset_t *sigmask = { 0 };
    ret = ppoll(fds, nfds, timeoutTs, sigmask);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = poll(fds, nfds, 1);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    int cmd = 0;
    ret = fcntl(sockfd, cmd);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = fcntl64(sockfd, cmd);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = epoll_pwait(0, NULL, 0, 0, &mask);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    int epfd = -1;
    struct epoll_event *events = { 0 };
    int maxevents = 0;
    int timeout = 0;
    ret = epoll_wait(epfd, events, maxevents, timeout);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = epoll_ctl(epfd, 0, sockfd, NULL);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = close(sockfd);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);
    
    errno = 0;
    ret = sigaction(SIGINT, NULL, NULL);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    Mock->Delete(KNET_TrafficResourcesInit);
    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));

    errno = 0;
    ret = epoll_create(1);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    errno = 0;
    ret = epoll_create1(1);
    DT_ASSERT_EQUAL(errno, ELIBBAD);
    DT_ASSERT_EQUAL(-1, ret);

    g_tcpInited = true;
    Mock->Delete(KNET_TrafficResourcesInit);
    Mock->Delete(AssignDlsym);
    g_origOsApi = origin;
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_IS_FORKED_PARENT, Init, Deinit)
{
    bool ret = KNET_DpIsForkedParent();
    DT_ASSERT_EQUAL(ret, true);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_SET_TCP_INITED, Init, Deinit)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KNET_FdInit, TEST_GetFuncRetPositive(0));
    KNET_SetDpInited();
    DT_ASSERT_EQUAL(g_tcpInited, true);
    g_tcpInited = false;
    Mock->Delete(KNET_FdInit);
    DeleteMock(Mock);
}


DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_SOCKET_NORMAL, Init, Deinit)
{
    int domain = 0;
    int ret = 0;
    int type = 0;
    int protocol = 0;
    
    ret = KnetInitDpSymbols();
    DT_ASSERT_EQUAL(ret, 0);

    ret = socket(domain, type, protocol);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(DP_PosixSocket, TEST_GetFuncRetNegative(1));
    ret = socket(domain, type, protocol);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    Mock->Create(DP_PosixSocket, TEST_GetFuncRetPositive(1));
    ret = socket(domain, type, protocol);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = false;
    Mock->Delete(DP_PosixSocket);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_SOCKET_DOMAIN, Init, Deinit)
{
    int domain = AF_UNIX;
    int ret = 0;
    int type = SOCK_RAW;
    int protocol = 0;

    g_tcpInited = true;
    ret = socket(domain, type, protocol);
    DT_ASSERT_NOT_EQUAL(ret, INVALID_FD);

    close(ret);
    g_tcpInited = false;
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_LISTEN_NORMAL, Init, Deinit)
{
    int sockfd = -1;
    int ret = 0;
    int backlog = 0;

    ret = listen(sockfd, backlog);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    ret = listen(sockfd, backlog);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixListen, TEST_GetFuncRetNegative(1));
    Mock->Create(listen, TEST_GetFuncRetNegative(1));
    ret = listen(sockfd, backlog);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    Mock->Create(DP_PosixListen, TEST_GetFuncRetPositive(0));
    Mock->Create(listen, TEST_GetFuncRetPositive(0));
    ret = listen(sockfd, backlog);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = false;
    Mock->Delete(listen);
    Mock->Delete(DP_PosixListen);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_CONNET_NORMAL, Init, Deinit)
{
    int sockfd = -1;
    int ret = 0;
    struct sockaddr *addr = { 0 };
    socklen_t addrlen = 0;

    ret = connect(sockfd, addr, addrlen);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    ret = connect(sockfd, addr, addrlen);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixConnect, TEST_GetFuncRetNegative(1));
    Mock->Create(connect, TEST_GetFuncRetNegative(1));
    ret = connect(sockfd, addr, addrlen);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    Mock->Create(DP_PosixConnect, TEST_GetFuncRetPositive(0));
    Mock->Create(connect, TEST_GetFuncRetPositive(0));
    ret = connect(sockfd, addr, addrlen);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = false;
    Mock->Delete(DP_PosixConnect);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_IsFdHijack);
    Mock->Delete(connect);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_BIND_NORMAL, Init, Deinit)
{
    int sockfd = -1;
    int ret = 0;
    struct sockaddr addr = { 0 };
    socklen_t addrlen = 0;

    ret = bind(sockfd, &addr, addrlen);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    ret = bind(sockfd, &addr, addrlen);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixBind, TEST_GetFuncRetNegative(1));
    Mock->Create(bind, TEST_GetFuncRetNegative(1));
    ret = bind(sockfd, &addr, addrlen);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    Mock->Create(DP_PosixBind, TEST_GetFuncRetPositive(0));
    Mock->Create(bind, TEST_GetFuncRetPositive(0));
    ret = bind(sockfd, &addr, addrlen);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = false;
    Mock->Delete(DP_PosixBind);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_IsFdHijack);
    Mock->Delete(bind);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_GET_PEER_NAME_NORMAL, Init, Deinit)
{
    int sockfd = -1;
    int ret = 0;
    struct sockaddr *addr = { 0 };
    socklen_t *addrlen = { 0 };

    ret = getpeername(sockfd, addr, addrlen);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    ret = getpeername(sockfd, addr, addrlen);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixGetpeername, TEST_GetFuncRetNegative(1));
    ret = getpeername(sockfd, addr, addrlen);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    Mock->Create(DP_PosixGetpeername, TEST_GetFuncRetPositive(0));
    ret = getpeername(sockfd, addr, addrlen);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = false;
    Mock->Delete(DP_PosixGetpeername);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_GET_SOCK_NAME_NORMAL, Init, Deinit)
{
    int sockfd = -1;
    int ret = 0;
    struct sockaddr addr = {0};
    socklen_t addrlen = {0};

    ret = getsockname(sockfd, &addr, &addrlen);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    ret = getsockname(sockfd, &addr, &addrlen);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixGetsockname, TEST_GetFuncRetNegative(1));
    ret = getsockname(sockfd, &addr, &addrlen);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    Mock->Create(DP_PosixGetsockname, TEST_GetFuncRetPositive(0));
    ret = getsockname(sockfd, &addr, &addrlen);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = false;
    Mock->Delete(DP_PosixGetsockname);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_SEND_NORMAL, Init, Deinit)
{
    int sockfd = -1;
    int ret = 0;
    void *buf = 0;
    size_t len = 0;
    int flags = 0;

    ret = send(sockfd, buf, len, flags);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    ret = send(sockfd, buf, len, flags);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixSend, TEST_GetFuncRetNegative(1));
    ret = send(sockfd, buf, len, flags);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    Mock->Create(DP_PosixSend, TEST_GetFuncRetPositive(0));
    ret = send(sockfd, buf, len, flags);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = false;
    Mock->Delete(DP_PosixSend);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_SEND_TO_NORMAL, Init, Deinit)
{
    int sockfd = -1;
    int ret = 0;
    void *buf = 0;
    size_t len = 0;
    int flags = 0;
    struct sockaddr *destAddr = { 0 };
    socklen_t addrlen = 0;

    ret = sendto(sockfd, buf, len, flags, destAddr, addrlen);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    ret = sendto(sockfd, buf, len, flags, destAddr, addrlen);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixSendto, TEST_GetFuncRetNegative(1));
    ret = sendto(sockfd, buf, len, flags, destAddr, addrlen);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    Mock->Create(DP_PosixSendto, TEST_GetFuncRetPositive(0));
    ret = sendto(sockfd, buf, len, flags, destAddr, addrlen);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = false;
    Mock->Delete(DP_PosixSendto);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_WRITEV_NORMAL, Init, Deinit)
{
    int sockfd = -1;
    int ret = 0;
    struct iovec *iov = { 0 };
    int iovcnt = 0;

    ret = writev(sockfd, iov, iovcnt);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    ret = writev(sockfd, iov, iovcnt);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixWritev, TEST_GetFuncRetNegative(1));
    ret = writev(sockfd, iov, iovcnt);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    Mock->Create(DP_PosixWritev, TEST_GetFuncRetPositive(0));
    ret = writev(sockfd, iov, iovcnt);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = false;
    Mock->Delete(DP_PosixWritev);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_SEND_MSG_NORMAL, Init, Deinit)
{
    int sockfd = -1;
    int ret = 0;
    struct msghdr *msg = { 0 };
    int flags = 0;

    ret = sendmsg(sockfd, msg, flags);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    ret = sendmsg(sockfd, msg, flags);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixSendmsg, TEST_GetFuncRetNegative(1));
    ret = sendmsg(sockfd, msg, flags);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    Mock->Create(DP_PosixSendmsg, TEST_GetFuncRetPositive(0));
    ret = sendmsg(sockfd, msg, flags);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = false;
    Mock->Delete(DP_PosixSendmsg);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_RECV_NORMAL, Init, Deinit)
{
    int sockfd = -1;
    int ret = 0;
    size_t len = 0;
    void *buf = { 0 };
    int flags = 0;

    ret = recv(sockfd, buf, len, flags);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    ret = recv(sockfd, buf, len, flags);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    buf = malloc(sizeof(char));
    DT_ASSERT_NOT_EQUAL(buf, NULL);
    len = 1;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixRecv, TEST_GetFuncRetNegative(1));
    ret = recv(sockfd, buf, len, flags);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    Mock->Create(DP_PosixRecv, TEST_GetFuncRetPositive(0));
    ret = recv(sockfd, buf, len, flags);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = false;
    Mock->Delete(DP_PosixRecv);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
    free(buf);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_RECV_FROM_NORMAL, Init, Deinit)
{
    int sockfd = -1;
    int ret = 0;
    size_t len = 0;
    void *buf = { 0 };
    struct sockaddr *srcAddr = { 0 };
    socklen_t *addrlen = 0;
    int flags = 0;

    ret = recvfrom(sockfd, buf, len, flags, srcAddr, addrlen);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    ret = recvfrom(sockfd, buf, len, flags, srcAddr, addrlen);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixRecvfrom, TEST_GetFuncRetNegative(1));
    ret = recvfrom(sockfd, buf, len, flags, srcAddr, addrlen);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    Mock->Create(DP_PosixRecvfrom, TEST_GetFuncRetPositive(0));
    ret = recvfrom(sockfd, buf, len, flags, srcAddr, addrlen);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = false;
    Mock->Delete(DP_PosixRecvfrom);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_RECV_MSG_NORMAL, Init, Deinit)
{
    int sockfd = -1;
    int ret = 0;
    struct msghdr *msg = { 0 };
    int flags = 0;

    ret = recvmsg(sockfd, msg, flags);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    ret = recvmsg(sockfd, msg, flags);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixRecvmsg, TEST_GetFuncRetNegative(1));
    ret = recvmsg(sockfd, msg, flags);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    Mock->Create(DP_PosixRecvmsg, TEST_GetFuncRetPositive(0));
    ret = recvmsg(sockfd, msg, flags);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = false;
    Mock->Delete(DP_PosixRecvmsg);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_READV_NORMAL, Init, Deinit)
{
    int sockfd = -1;
    int ret = 0;
    struct iovec *iov = { 0 };
    int iovcnt = 0;

    ret = readv(sockfd, iov, iovcnt);
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    printf("***\n");

    g_tcpInited = true;
    ret = readv(sockfd, iov, iovcnt);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixReadv, TEST_GetFuncRetNegative(1));
    ret = readv(sockfd, iov, iovcnt);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    Mock->Create(DP_PosixReadv, TEST_GetFuncRetPositive(0));
    ret = readv(sockfd, iov, iovcnt);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = false;
    Mock->Delete(DP_PosixReadv);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_GET_SOCK_NORMAL, Init, Deinit)
{
    int sockfd = -1;
    int ret = 0;
    int level = 0;
    int optname = 0;
    void *optval = { 0 };
    socklen_t *optlen = { 0 };

    ret = getsockopt(sockfd, level, optname, optval, optlen);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    ret = getsockopt(sockfd, level, optname, optval, optlen);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixGetsockopt, TEST_GetFuncRetNegative(1));
    ret = getsockopt(sockfd, level, optname, optval, optlen);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    Mock->Create(DP_PosixGetsockopt, TEST_GetFuncRetPositive(0));
    ret = getsockopt(sockfd, level, optname, optval, optlen);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = false;
    Mock->Delete(DP_PosixGetsockopt);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_SET_SOCK_NORMAL, Init, Deinit)
{
    int sockfd = -1;
    int ret = 0;
    int level = 0;
    int optname = 0;
    void *optval = { 0 };
    socklen_t optlen = 0;

    ret = setsockopt(sockfd, level, optname, optval, optlen);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    ret = setsockopt(sockfd, level, optname, optval, optlen);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixSetsockopt, TEST_GetFuncRetNegative(1));
    ret = setsockopt(sockfd, level, optname, optval, optlen);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    Mock->Create(DP_PosixSetsockopt, TEST_GetFuncRetPositive(0));
    Mock->Create(setsockopt, TEST_GetFuncRetPositive(0));
    ret = setsockopt(sockfd, level, optname, optval, optlen);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = false;
    Mock->Delete(DP_PosixSetsockopt);
    Mock->Delete(setsockopt);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_ACCEPT_NORMAL, Init, Deinit)
{
    int sockfd = -1;
    int ret = 0;
    struct sockaddr *addr = { 0 };
    socklen_t *addrlen = { 0 };

    ret = accept(sockfd, addr, addrlen);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    ret = accept(sockfd, addr, addrlen);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(AcceptNoBlock, FuncAccept);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_GetFdType, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixAccept, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixFcntl, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_Fcntl, TEST_GetFuncRetPositive(0));
    
    ret = accept(sockfd, addr, addrlen);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Create(KNET_IsFdValid, FuncRetTrue);
    Mock->Create(KNET_SetFdSocketState, FuncRetTrue);
    ret = accept(sockfd, addr, addrlen);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = false;

    Mock->Delete(AcceptNoBlock);
    Mock->Delete(KNET_Fcntl);
    Mock->Delete(DP_PosixFcntl);
    Mock->Delete(KNET_GetFdType);
    Mock->Delete(DP_PosixAccept);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_IsFdHijack);
    Mock->Delete(KNET_IsFdValid);
    Mock->Delete(KNET_SetFdSocketState);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_ACCEPT4_NORMAL, Init, Deinit)
{
    int sockfd = -1;
    int ret = 0;
    struct sockaddr *addr = { 0 };
    socklen_t *addrlen = { 0 };
    int flags = 0;

    ret = accept4(sockfd, addr, addrlen, flags);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    ret = accept4(sockfd, addr, addrlen, SOCK_PACKET);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(1)); // 不加会在KNET_OsFdToDpFd函数越界
    Mock->Create(AcceptNoBlock, FuncAccept);
    Mock->Create(KNET_GetFdType, TEST_GetFuncRetPositive(0));
    ret = accept4(sockfd, addr, addrlen, SOCK_PACKET);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    Mock->Create(DP_PosixFcntl, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_DpAccept, TEST_GetFuncRetPositive(0));
    Mock->Create(accept, TEST_GetFuncRetPositive(0));
    Mock->Create(fcntl, TEST_GetFuncRetPositive(0));
    ret = accept4(sockfd, addr, addrlen, SOCK_NONBLOCK);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_DpAccept);
    Mock->Delete(accept);
    Mock->Delete(fcntl);
    Mock->Delete(DP_PosixFcntl);

    Mock->Create(DP_PosixFcntl, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_DpAccept, TEST_GetFuncRetNegative(1));
    Mock->Create(accept, TEST_GetFuncRetNegative(1));
    ret = accept4(sockfd, addr, addrlen, SOCK_NONBLOCK);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_DpAccept);
    Mock->Delete(accept);
    Mock->Delete(DP_PosixFcntl);

    Mock->Create(DP_PosixFcntl, TEST_GetFuncRetNegative(1));
    ret = accept4(sockfd, addr, addrlen, SOCK_NONBLOCK);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(DP_PosixFcntl);

    Mock->Delete(AcceptNoBlock);
    Mock->Delete(KNET_GetFdType);
    Mock->Delete(KNET_IsFdHijack);
    Mock->Delete(KNET_OsFdToDpFd);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_CLOSE_NORMAL, Init, Deinit)
{
    int sockfd = -1;
    int ret = 0;

    ret = close(sockfd);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    ret = close(sockfd);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_GetFdType, TEST_GetFuncRetNegative(1));
    Mock->Create(KNET_ResetFdState, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixClose, TEST_GetFuncRetNegative(1));
    ret = close(sockfd);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    Mock->Create(DP_PosixClose, TEST_GetFuncRetPositive(0));
    ret = close(sockfd);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = false;
    Mock->Delete(DP_PosixClose);
    Mock->Delete(KNET_ResetFdState);
    Mock->Delete(KNET_GetFdType);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_CLOSE_ERROR, Init, Deinit)
{
    int sockfd = -1;
    int ret = 0;
    g_tcpInited = true;
    int (*osClose)(int fd) = dlsym((void *) -1L, "close");

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_GetFdType, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_ResetFdState, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixClose, TEST_GetFuncRetPositive(0));

    Mock->Create(osClose, FunCloseEio);
    errno = 0;
    ret = close(sockfd);
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    DT_ASSERT_EQUAL(errno, EIO);
    Mock->Delete(osClose);

    Mock->Create(osClose, FunCloseEintr);
    errno = 0;
    ret = close(sockfd);
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    DT_ASSERT_EQUAL(errno, EINTR);
    Mock->Delete(osClose);

    g_tcpInited = false;
    Mock->Delete(DP_PosixClose);
    Mock->Delete(KNET_ResetFdState);
    Mock->Delete(KNET_GetFdType);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_SHUTDOWN_NORMAL, Init, Deinit)
{
    int sockfd = -1;
    int how = 0;
    int ret = 0;

    ret = shutdown(sockfd, how);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    ret = shutdown(sockfd, how);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixShutdown, TEST_GetFuncRetNegative(1));
    ret = shutdown(sockfd, how);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    Mock->Create(DP_PosixShutdown, TEST_GetFuncRetPositive(0));
    ret = shutdown(sockfd, how);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = false;
    Mock->Delete(DP_PosixShutdown);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_READ_NORMAL, Init, Deinit)
{
    int sockfd = -1;
    void *buf = 0;
    int ret = 0;
    size_t count = 0;

    ret = read(sockfd, buf, count);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    ret = read(sockfd, buf, count);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixRead, TEST_GetFuncRetPositive(0));
    ret = read(sockfd, buf, count);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = false;
    Mock->Delete(DP_PosixRead);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_WRITE_NORMAL, Init, Deinit)
{
    int sockfd = -1;
    void *buf = 0;
    int ret = 0;
    size_t count = 0;

    ret = write(sockfd, buf, count);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    ret = write(sockfd, buf, count);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixWrite, TEST_GetFuncRetPositive(0));
    ret = write(sockfd, buf, count);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = false;
    Mock->Delete(DP_PosixWrite);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}

DTEST_CASE_F(frameHijack, testFrameSelectNormal, Init, Deinit)
{
    struct timeval timespec = {};
    int ret = select(0, NULL, NULL, NULL, &timespec);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = true;

    ret = select(-1, NULL, NULL, NULL, &timespec);
    DT_ASSERT_EQUAL(errno, EINVAL);
    DT_ASSERT_EQUAL(ret, -1);

    /* select正常工作 */
    int pipefd[2] = {0};
    ret = pipe(pipefd);
    if (ret == -1) {
        perror("pipe failed");
    }
    DT_ASSERT_EQUAL(ret, 0);

    fd_set readFds;
    fd_set writeFds;
    fd_set exceptionFds;
    FD_ZERO(&readFds);
    FD_ZERO(&writeFds);
    FD_ZERO(&exceptionFds);

    FD_SET(pipefd[0], &readFds);
    FD_SET(pipefd[1], &writeFds);
    FD_SET(pipefd[0], &exceptionFds);
    FD_SET(pipefd[1], &exceptionFds);

    ret = select(FD_SETSIZE, &readFds, &writeFds, &exceptionFds, &timespec);
    DT_ASSERT_NOT_EQUAL(ret, -1);

    g_tcpInited = false;
}

DTEST_CASE_F(frameHijack, testFramePselectNormal, Init, Deinit)
{
    struct timespec timespec = {};
    sigset_t mask = {};
    int ret = pselect(0, NULL, NULL, NULL, &timespec, &mask);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = true;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(select, TEST_GetFuncRetPositive(0));

    ret = pselect(0, NULL, NULL, NULL, &timespec, &mask);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(select);
    g_tcpInited = false;
}

DTEST_CASE_F(frameHijack, testFrameEpollpwaitNormal, Init, Deinit)
{
    sigset_t mask = { 0 };
    int ret = epoll_pwait(0, NULL, 0, 0, &mask);
    DT_ASSERT_EQUAL(ret, -1);

    g_tcpInited = true;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_DpEpollWait, TEST_GetFuncRetPositive(0));

    ret = epoll_pwait(0, NULL, 0, 0, &mask);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KNET_DpEpollWait);
    g_tcpInited = false;
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_EPOLL_CREATE_NORMAL, Init, Deinit)
{
    int ret = 0;
    int size = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_EpollCreateNotify, TEST_GetFuncRetNegative(1));

    ret = epoll_create(size);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    DP_Init(0);
    g_tcpInited = true;
    ret = epoll_create(size);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    ret = epoll_create(size);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    size = 1;
    Mock->Create(eventfd, TEST_GetFuncRetPositive(0));
    ret = epoll_create(size);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(eventfd);
    Mock->Delete(KNET_TrafficResourcesInit);
    Mock->Delete(DP_EpollCreateNotify);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_EPOLL_CREATE1_NORMAL, Init, Deinit)
{
    int ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));

    ret = epoll_create1(-1);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    ret = epoll_create1(-1);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    Mock->Create(KNET_DpEpollCreate, TEST_GetFuncRetPositive(0));
    ret = epoll_create1(0);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(KNET_DpEpollCreate);

    Mock->Create(KNET_DpEpollCreate, TEST_GetFuncRetPositive(0));
    ret = epoll_create1(EPOLL_CLOEXEC);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(KNET_DpEpollCreate);

    g_tcpInited = false;
    Mock->Delete(KNET_TrafficResourcesInit);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_EPOLL_CTL_NORMAL, Init, Deinit)
{
    int ret = 0;
    int epfd = -1;
    int op = 0;
    int sockfd = -1;
    struct epoll_event event = { 0 };
    event.data.u64 = 0;

    ret = epoll_ctl(epfd, op, sockfd, NULL);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    ret = epoll_ctl(epfd, op, sockfd, NULL);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));

    event.data.u64 = 1;
    Mock->Create(DP_PosixEpollCtl, TEST_GetFuncRetNegative(1));
    ret = epoll_ctl(epfd, op, sockfd, &event);
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    Mock->Delete(DP_PosixEpollCtl);

    Mock->Create(DP_PosixEpollCtl, TEST_GetFuncRetPositive(0));
    ret = epoll_ctl(epfd, op, sockfd, &event);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = false;
    Mock->Delete(DP_PosixEpollCtl);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_EPOLL_WAIT_ABNORMAL, Init, Deinit)
{
    int ret = 0;
    int epfd = -1;
    struct epoll_event *events = { 0 };
    int maxevents = 0;
    int timeout = 0;

    ret = epoll_wait(epfd, events, maxevents, timeout);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    ret = epoll_wait(epfd, events, maxevents, timeout);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    ret = epoll_wait(epfd, NULL, maxevents, timeout);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    maxevents = 1;
    ret = epoll_wait(epfd, events, maxevents, timeout);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = false;
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_FCNTL_NORMAL, Init, Deinit)
{
    int ret = 0;
    int sockfd = -1;
    int cmd = 0;

    ret = fcntl(sockfd, cmd);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    ret = fcntl(sockfd, cmd);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixFcntl, TEST_GetFuncRetNegative(1));
    Mock->Create(fcntl, TEST_GetFuncRetNegative(1));
    ret = fcntl(sockfd, cmd);
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    Mock->Delete(DP_PosixFcntl);

    cmd = 1;
    Mock->Create(DP_PosixFcntl, TEST_GetFuncRetPositive(0));
    Mock->Create(fcntl, TEST_GetFuncRetPositive(0));
    ret = fcntl(sockfd, cmd, F_GETFL);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = false;
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_IsFdHijack);
    Mock->Delete(DP_PosixFcntl);
    Mock->Delete(fcntl);
    DeleteMock(Mock);
}

int TestHijackEpollWaitMock(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    events[0].events = EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLPRI | EPOLLRDHUP | EPOLLRDNORM | EPOLLRDBAND |
        EPOLLWRNORM | EPOLLWRBAND;

    return 1;
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_POLL_NORMAL, Init, Deinit)
{
    struct pollfd fds = { 0 };
    nfds_t nfds = 1;
    int timeout = 0;
    int ret = 0;

    fds.events = POLLIN | POLLOUT | POLLERR | POLLHUP | POLLPRI | POLLRDNORM | POLLRDBAND | POLLWRNORM | POLLWRBAND;
    fds.events |= 0x2000; // POLLRDHUP

    ret = poll(&fds, nfds, timeout);
    DT_ASSERT_EQUAL(ret, 1);

    g_tcpInited = true;
    ret = poll(NULL, nfds, timeout);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_DpEpollCreate, TEST_GetFuncRetNegative(1));
    ret = poll(&fds, nfds, timeout);
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    Mock->Delete(KNET_DpEpollCreate);

    Mock->Create(KNET_DpEpollCreate, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_DpEpollCtl, TEST_GetFuncRetNegative(1));
    ret = poll(&fds, nfds, timeout);
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    Mock->Delete(KNET_DpEpollCtl);

    Mock->Create(KNET_DpEpollCtl, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_DpEpollWait, TEST_GetFuncRetNegative(1));
    ret = poll(&fds, nfds, timeout);
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    Mock->Delete(KNET_DpEpollWait);

    Mock->Create(KNET_DpEpollWait, TestHijackEpollWaitMock);
    ret = poll(&fds, nfds, timeout);
    DT_ASSERT_EQUAL(ret, 1);

    g_tcpInited = false;
    Mock->Delete(KNET_DpEpollCreate);
    Mock->Delete(KNET_DpEpollCtl);
    Mock->Delete(KNET_DpEpollWait);
    DeleteMock(Mock);
}

int EpollCreateEmfileMock(int size)
{
    errno = EMFILE;
    return -1;
}

int EpollCreateEnomemMock(int size)
{
    errno = ENOMEM;
    return -1;
}
    
int EventFdEinvalMock(int n)
{
    errno = EINVAL;
    return -1;
}
    
int EpollCtlEnospcMock(int epfd, int op, int sockfd, struct epoll_event *event)
{
    errno = ENOSPC;
    return -1;
}

void *CallocEnomemMock(size_t n, size_t size)
{
    return NULL;
}

void *MallocEnonmemMock(size_t size)
{
    return NULL;
}

// 内部调用内核的 epoll_create，进程打开的 file 数量达到限制
DTEST_CASE_F(FREAM_HIJ, TEST_FRAME_POLL_EMFILE, Init, Deinit)
{
    struct pollfd fds = { 0 };
    nfds_t nfds = 1;
    int timeout = 0;
    int ret = 0;
    fds.events = POLLIN;

    g_tcpInited = true;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    DP_Init(0);

    int (*osEpollCreate)(int size) = dlsym((void *) -1L, "epoll_create");

    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(osEpollCreate, EpollCreateEmfileMock);
    errno = 0;
    ret = poll(&fds, nfds, timeout);
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    DT_ASSERT_EQUAL(errno, EMFILE);

    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(osEpollCreate);
    Mock->Delete(KNET_TrafficResourcesInit);
    DeleteMock(Mock);
}

// 内部调用 epoll_create，epoll_create 中调用的内核的 eventfd，在linux2.6.26或更早版本中，当flags不为0报错
DTEST_CASE_F(FREAM_HIJ, TEST_FRAME_POLL_EVENTFD_EINVAL, Init, Deinit)
{
    struct pollfd fds = { 0 };
    nfds_t nfds = 1;
    int timeout = 0;
    int ret = 0;
    fds.events = POLLIN;

    g_tcpInited = true;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    DP_Init(0);

    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(eventfd, EventFdEinvalMock);
    errno = 0;
    ret = poll(&fds, nfds, timeout);
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    DT_ASSERT_EQUAL(errno, EINVAL);

    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(eventfd);
    Mock->Delete(KNET_TrafficResourcesInit);
    DeleteMock(Mock);
}

// 内部调用 epoll_create, epoll_create 中调用内核的 epoll_ctl，其内部调用epoll_ctl系统接口，
// 尝试在主机上注册（EPOLL_CTL_ADD）新文件描述符时遇到了/ proc / sys / fs / epoll / max_user_watches施加的限制
DTEST_CASE_F(FREAM_HIJ, TEST_FRAME_POLL_ENOSPC, Init, Deinit)
{
    struct pollfd fds = { 0 };
    nfds_t nfds = 1;
    int timeout = 0;
    int ret = 0;
    fds.events = POLLIN;

    g_tcpInited = true;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    DP_Init(0);

    int (*osEpollCtl)(int size) = dlsym((void *) -1L, "epoll_ctl");

    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(osEpollCtl, EpollCtlEnospcMock);
    errno = 0;
    ret = poll(&fds, nfds, timeout);
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    DT_ASSERT_EQUAL(errno, ENOSPC);

    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(osEpollCtl);
    Mock->Delete(KNET_TrafficResourcesInit);
    DeleteMock(Mock);
}

// 内部调用内核的 epoll_create，内存不足
DTEST_CASE_F(FREAM_HIJ, TEST_FRAME_POLL_EPOLL_CREATE_ENOMEM, Init, Deinit)
{
    struct pollfd fds = { 0 };
    nfds_t nfds = 1;
    int timeout = 0;
    int ret = 0;
    fds.events = POLLIN;

    g_tcpInited = true;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    DP_Init(0);

    int (*osEpollCreate)(int size) = dlsym((void *) -1L, "epoll_create");

    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(osEpollCreate, EpollCreateEnomemMock);
    errno = 0;
    ret = poll(&fds, nfds, timeout);
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    DT_ASSERT_EQUAL(errno, ENOMEM);

    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(osEpollCreate);
    Mock->Delete(KNET_TrafficResourcesInit);
    DeleteMock(Mock);
}

// 内部调用 calloc 失败
DTEST_CASE_F(FREAM_HIJ, TEST_FRAME_POLL_CALLOC_ENOMEM, Init, Deinit)
{
    struct pollfd fds = { 0 };
    nfds_t nfds = 1;
    int timeout = 0;
    int ret = 0;
    fds.events = POLLIN;

    g_tcpInited = true;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    DP_Init(0);

    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_DpEpollCreate, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_DpEpollCtl, TEST_GetFuncRetPositive(0));
    Mock->Create(close, TEST_GetFuncRetPositive(0));
    Mock->Create(calloc, CallocEnomemMock);
    errno = 0;
    ret = poll(&fds, nfds, timeout);
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    DT_ASSERT_EQUAL(errno, ENOMEM);

    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(calloc);
    Mock->Delete(close);
    Mock->Delete(KNET_DpEpollCtl);
    Mock->Delete(KNET_DpEpollCreate);
    Mock->Delete(KNET_TrafficResourcesInit);
    DeleteMock(Mock);
}

// 内部调用 epoll_wait，epoll_wait 内部调用 malloc，内存分配失败
DTEST_CASE_F(FREAM_HIJ, TEST_FRAME_POLL_EPOLL_WAIT_MALLOC_ENOMEM, Init, Deinit)
{
    struct pollfd fds[1024] = {};
    nfds_t nfds = 1024;
    int timeout = 0;
    int ret = 0;
    
    for (int i = 0; i < nfds; ++i) {
        fds[i].events = POLLIN;
    }

    g_tcpInited = true;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    DP_Init(0);

    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_DpEpollCreate, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_DpEpollCtl, TEST_GetFuncRetPositive(0));
    Mock->Create(close, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_Log, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_IsFdHijack, TEST_GetFuncRetPositive(1));
    Mock->Create(malloc, MallocEnonmemMock);
    errno = 0;
    ret = poll(fds, nfds, timeout);
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    DT_ASSERT_EQUAL(errno, ENOMEM);

    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(malloc);
    Mock->Delete(KNET_IsFdHijack);
    Mock->Delete(KNET_Log);
    Mock->Delete(close);
    Mock->Delete(KNET_DpEpollCtl);
    Mock->Delete(KNET_DpEpollCreate);
    Mock->Delete(KNET_TrafficResourcesInit);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_PPOLL_NORMAL, Init, Deinit)
{
    struct pollfd *fds = { 0 };
    nfds_t nfds = 1;
    const struct timespec *timeoutTs = NULL;
    const sigset_t *sigmask = { 0 };
    int ret = 0;

    ret = ppoll(fds, nfds, timeoutTs, sigmask);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_DpPoll, TEST_GetFuncRetNegative(1));
    ret = ppoll(fds, nfds, timeoutTs, sigmask);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    Mock->Delete(KNET_DpPoll);
    Mock->Create(KNET_DpPoll, TEST_GetFuncRetPositive(0));
    ret = ppoll(fds, nfds, timeoutTs, sigmask);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = false;
    Mock->Delete(KNET_DpPoll);
    DeleteMock(Mock);
}

// 内部调用内核的 epoll_create，进程打开的 file 数量达到限制
DTEST_CASE_F(FREAM_HIJ, TEST_FRAME_PPOLL_EMFILE, Init, Deinit)
{
    struct pollfd fds = { 0 };
    nfds_t nfds = 1;
    const struct timespec *timeoutTs = NULL;
    const sigset_t *sigmask = { 0 };
    int ret = 0;

    g_tcpInited = true;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    DP_Init(0);

    int (*osEpollCreate)(int size) = dlsym((void *) -1L, "epoll_create");

    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(osEpollCreate, EpollCreateEmfileMock);
    errno = 0;
    ret = ppoll(&fds, nfds, timeoutTs, sigmask);
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    DT_ASSERT_EQUAL(errno, EMFILE);

    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(osEpollCreate);
    Mock->Delete(KNET_TrafficResourcesInit);
    DeleteMock(Mock);
}

DTEST_CASE_F(FREAM_HIJ, TEST_FRAME_PPOLL_EVENTFD_EINVAL, Init, Deinit)
{
    struct pollfd fds = { 0 };
    nfds_t nfds = 1;
    const struct timespec *timeoutTs = NULL;
    const sigset_t *sigmask = { 0 };
    int ret = 0;

    g_tcpInited = true;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    DP_Init(0);

    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(eventfd, EventFdEinvalMock);
    errno = 0;
    ret = ppoll(&fds, nfds, timeoutTs, sigmask);
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    DT_ASSERT_EQUAL(errno, EINVAL);

    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(eventfd);
    Mock->Delete(KNET_TrafficResourcesInit);
    DeleteMock(Mock);
}

DTEST_CASE_F(FREAM_HIJ, TEST_FRAME_PPOLL_ENOSPC, Init, Deinit)
{
    struct pollfd fds = { 0 };
    nfds_t nfds = 1;
    const struct timespec *timeoutTs = NULL;
    const sigset_t *sigmask = { 0 };
    int ret = 0;

    g_tcpInited = true;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    DP_Init(0);

    int (*osEpollCtl)(int size) = dlsym((void *) -1L, "epoll_ctl");

    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(osEpollCtl, EpollCtlEnospcMock);
    errno = 0;
    ret = ppoll(&fds, nfds, timeoutTs, sigmask);
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    DT_ASSERT_EQUAL(errno, ENOSPC);

    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(osEpollCtl);
    Mock->Delete(KNET_TrafficResourcesInit);
    DeleteMock(Mock);
}

DTEST_CASE_F(FREAM_HIJ, TEST_FRAME_PPOLL_CALLOC_ENOMEM, Init, Deinit)
{
    struct pollfd fds = { 0 };
    nfds_t nfds = 1;
    const struct timespec *timeoutTs = NULL;
    const sigset_t *sigmask = { 0 };
    int ret = 0;

    g_tcpInited = true;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    DP_Init(0);

    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_DpEpollCreate, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_DpEpollCtl, TEST_GetFuncRetPositive(0));
    Mock->Create(close, TEST_GetFuncRetPositive(0));
    Mock->Create(calloc, CallocEnomemMock);
    errno = 0;
    ret = ppoll(&fds, nfds, timeoutTs, sigmask);
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    DT_ASSERT_EQUAL(errno, ENOMEM);

    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(calloc);
    Mock->Delete(close);
    Mock->Delete(KNET_DpEpollCtl);
    Mock->Delete(KNET_DpEpollCreate);
    Mock->Delete(KNET_TrafficResourcesInit);
    DeleteMock(Mock);
}

DTEST_CASE_F(FREAM_HIJ, TEST_FRAME_PPOLL_EPOLL_CREATE_ENOMEM, Init, Deinit)
{
    struct pollfd fds = { 0 };
    nfds_t nfds = 1;
    const struct timespec *timeoutTs = NULL;
    const sigset_t *sigmask = { 0 };
    int ret = 0;

    g_tcpInited = true;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    DP_Init(0);

    int (*osEpollCreate)(int size) = dlsym((void *) -1L, "epoll_create");

    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(osEpollCreate, EpollCreateEnomemMock);
    errno = 0;
    ret = ppoll(&fds, nfds, timeoutTs, sigmask);
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    DT_ASSERT_EQUAL(errno, ENOMEM);

    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(osEpollCreate);
    Mock->Delete(KNET_TrafficResourcesInit);
    DeleteMock(Mock);
}

DTEST_CASE_F(FREAM_HIJ, TEST_FRAME_PPOLL_EPOLL_WAIT_MALLOC_ENOMEM, Init, Deinit)
{
    struct pollfd fds[1024] = { 0 };
    nfds_t nfds = 1024;
    const struct timespec *timeoutTs = NULL;
    const sigset_t *sigmask = { 0 };
    int ret = 0;
    g_tcpInited = true;
    
    for (int i = 0; i < nfds; ++i) {
        fds[i].events = POLLIN;
    }

    g_tcpInited = true;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    DP_Init(0);

    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_DpEpollCreate, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_DpEpollCtl, TEST_GetFuncRetPositive(0));
    Mock->Create(close, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_Log, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_IsFdHijack, TEST_GetFuncRetPositive(1));
    Mock->Create(malloc, MallocEnonmemMock);
    errno = 0;
    ret = ppoll(fds, nfds, timeoutTs, sigmask);
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    DT_ASSERT_EQUAL(errno, ENOMEM);

    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(malloc);
    Mock->Delete(KNET_IsFdHijack);
    Mock->Delete(KNET_Log);
    Mock->Delete(close);
    Mock->Delete(KNET_DpEpollCtl);
    Mock->Delete(KNET_DpEpollCreate);
    Mock->Delete(KNET_TrafficResourcesInit);
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_HIJ, TEST_FRAME_IOCTL_NORMAL, Init, Deinit)
{
    int ret = 0;
    int sockfd = -1;
    unsigned long request = 0;

    ret = ioctl(sockfd, request);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    g_tcpInited = true;
    ret = ioctl(sockfd, request);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixIoctl, TEST_GetFuncRetNegative(1));
    ret = ioctl(sockfd, request);
    DT_ASSERT_EQUAL(ret, INVALID_FD);

    Mock->Create(DP_PosixIoctl, TEST_GetFuncRetPositive(0));
    Mock->Create(ioctl, TEST_GetFuncRetPositive(0));
    ret = ioctl(sockfd, request);
    DT_ASSERT_EQUAL(ret, 0);

    g_tcpInited = false;
    Mock->Delete(ioctl);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_IsFdHijack);
    Mock->Delete(DP_PosixIoctl);
    DeleteMock(Mock);
}

DTEST_CASE_F(frameHijack, testForkSuccess, Init, Deinit)
{
    pid_t pid = fork();
    if (pid == 0) {
        exit(0);
    }

    g_tcpInited = true;

    pid = fork();
    if (pid == 0) {
        exit(0);
    }

    g_tcpInited = false;
}

pid_t ForkMockENOMEM()
{
    errno = ENOMEM;
    return -1;
}

DTEST_CASE_F(frameHijack, TEST_FRAME_FORK_ENOMEM, Init, Deinit)
{
    g_tcpInited = true;

    int (*osFork)(int domain, int type, int protocol) = dlsym((void *) -1L, "fork");
    KTestMock *Mock = CreateMock();
    Mock->Create(osFork, ForkMockENOMEM);

    pid_t pid = fork();
    DT_ASSERT_EQUAL(pid, -1);
    DT_ASSERT_EQUAL(errno, ENOMEM);

    g_tcpInited = false;
    Mock->Delete(osFork);
    DeleteMock(Mock);
}

int SocketMockEACESS(int domain, int type, int protocol)
{
    errno = EACCES;
    return -1;
}

int SocketMockENOBUFS(int domain, int type, int protocol)
{
    errno = ENOBUFS;
    return -1;
}

int SocketMockEMFILE(int domain, int type, int protocol)
{
    errno = EMFILE;
    return -1;
}

int SocketMockENFILE(int domain, int type, int protocol)
{
    errno = ENFILE;
    return -1;
}

DTEST_CASE_F(frameHijack, TEST_FRAME_SOCKET_EACESS, Init, Deinit)
{
    g_tcpInited = true;

    int (*osSocket)(int domain, int type, int protocol) = dlsym((void *) -1L, "socket");
    KTestMock *Mock = CreateMock();
    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixSocket, TEST_GetFuncRetPositive(0));
    Mock->Create(osSocket, SocketMockEACESS);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    DT_ASSERT_EQUAL(fd, -1);
    DT_ASSERT_EQUAL(errno, EACCES);

    g_tcpInited = false;
    Mock->Delete(KNET_TrafficResourcesInit);
    Mock->Delete(DP_PosixSocket);
    Mock->Delete(osSocket);
    DeleteMock(Mock);
}

DTEST_CASE_F(frameHijack, TEST_FRAME_SOCKET_ENOBUFS, Init, Deinit)
{
    g_tcpInited = true;

    int (*osSocket)(int domain, int type, int protocol) = dlsym((void *) -1L, "socket");
    KTestMock *Mock = CreateMock();
    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixSocket, TEST_GetFuncRetPositive(0));
    Mock->Create(osSocket, SocketMockENOBUFS);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    DT_ASSERT_EQUAL(fd, -1);
    DT_ASSERT_EQUAL(errno, ENOBUFS);

    g_tcpInited = false;
    Mock->Delete(KNET_TrafficResourcesInit);
    Mock->Delete(DP_PosixSocket);
    Mock->Delete(osSocket);
    DeleteMock(Mock);
}

DTEST_CASE_F(frameHijack, TEST_FRAME_SOCKET_EMFILE, Init, Deinit)
{
    g_tcpInited = true;

    int (*osSocket)(int domain, int type, int protocol) = dlsym((void *) -1L, "socket");
    KTestMock *Mock = CreateMock();
    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixSocket, TEST_GetFuncRetPositive(0));
    Mock->Create(osSocket, SocketMockEMFILE);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    DT_ASSERT_EQUAL(fd, -1);
    DT_ASSERT_EQUAL(errno, EMFILE);

    g_tcpInited = false;
    Mock->Delete(KNET_TrafficResourcesInit);
    Mock->Delete(DP_PosixSocket);
    Mock->Delete(osSocket);
    DeleteMock(Mock);
}

DTEST_CASE_F(frameHijack, TEST_FRAME_SOCKET_ENFILE, Init, Deinit)
{
    g_tcpInited = true;

    int (*osSocket)(int domain, int type, int protocol) = dlsym((void *) -1L, "socket");
    KTestMock *Mock = CreateMock();
    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_PosixSocket, TEST_GetFuncRetPositive(0));
    Mock->Create(osSocket, SocketMockENFILE);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    DT_ASSERT_EQUAL(fd, -1);
    DT_ASSERT_EQUAL(errno, ENFILE);

    g_tcpInited = false;
    Mock->Delete(KNET_TrafficResourcesInit);
    Mock->Delete(DP_PosixSocket);
    Mock->Delete(osSocket);
    DeleteMock(Mock);
}

int GetFdTypeTemp(int fd)
{
    if (fd == KNET_FD_TYPE_INVALID) {
        return KNET_FD_TYPE_INVALID;
    } else {
        return KNET_FD_TYPE_SOCKET;
    }
}

int SelectMockEBADF(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
    errno = EBADF;
    return -1;
}

int SelectMockEINTR(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
    errno = EINTR;
    return -1;
}

int SelectMockEINVAL(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
    errno = EINVAL;
    return -1;
}

int SelectMockENOMEM(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
    errno = ENOMEM;
    return -1;
}

int PollMockEFAULT(struct pollfd *fds, nfds_t nfds, int timeout)
{
    errno = EFAULT;
    return -1;
}

int PollMockEINTR(struct pollfd *fds, nfds_t nfds, int timeout)
{
    errno = EINTR;
    return -1;
}

int PollMockEINVAL(struct pollfd *fds, nfds_t nfds, int timeout)
{
    errno = EINVAL;
    return -1;
}

int PollMockENOMEM(struct pollfd *fds, nfds_t nfds, int timeout)
{
    errno = ENOMEM;
    return -1;
}

DTEST_CASE_F(frameHijack, TEST_FRAME_SELECT_EBADF, Init, Deinit)
{
    int ret = 0;

    g_tcpInited = true;

    int (*osSelect)(int domain, int type, int protocol) = dlsym((void *) -1L, "select");
    KTestMock *Mock = CreateMock();
    Mock->Create(osSelect, SelectMockEBADF);

    ret = select(1, NULL, NULL, NULL, NULL);
    DT_ASSERT_EQUAL(ret, -1);
    DT_ASSERT_EQUAL(errno, EBADF);

    g_tcpInited = false;
    Mock->Delete(osSelect);
    DeleteMock(Mock);
}

DTEST_CASE_F(frameHijack, TEST_FRAME_SELECT_EINTR, Init, Deinit)
{
    int ret = 0;
    g_tcpInited = true;

    int (*osSelect)(int domain, int type, int protocol) = dlsym((void *) -1L, "select");
    KTestMock *Mock = CreateMock();
    Mock->Create(osSelect, SelectMockEINTR);

    ret = select(1, NULL, NULL, NULL, NULL);
    DT_ASSERT_EQUAL(ret, -1);
    DT_ASSERT_EQUAL(errno, EINTR);

    g_tcpInited = false;
    Mock->Delete(osSelect);
    DeleteMock(Mock);
}

DTEST_CASE_F(frameHijack, TEST_FRAME_SELECT_EINVAL, Init, Deinit)
{
    int ret = 0;
    g_tcpInited = true;

    int (*osSelect)(int domain, int type, int protocol) = dlsym((void *) -1L, "select");
    KTestMock *Mock = CreateMock();
    Mock->Create(osSelect, SelectMockEINVAL);

    ret = select(1, NULL, NULL, NULL, NULL);
    DT_ASSERT_EQUAL(ret, -1);
    DT_ASSERT_EQUAL(errno, EINVAL);

    g_tcpInited = false;
    Mock->Delete(osSelect);
    DeleteMock(Mock);
}

DTEST_CASE_F(frameHijack, TEST_FRAME_SELECT_ENOMEM, Init, Deinit)
{
    int ret = 0;
    g_tcpInited = true;

    int (*osSelect)(int domain, int type, int protocol) = dlsym((void *) -1L, "select");
    KTestMock *Mock = CreateMock();
    Mock->Create(osSelect, SelectMockENOMEM);

    ret = select(1, NULL, NULL, NULL, NULL);
    DT_ASSERT_EQUAL(ret, -1);
    DT_ASSERT_EQUAL(errno, ENOMEM);

    g_tcpInited = false;
    Mock->Delete(osSelect);
    DeleteMock(Mock);
}

DTEST_CASE_F(frameHijack, TEST_FRAME_SELECT_POLL_EFAULT, Init, Deinit)
{
    int ret = 0;
    g_tcpInited = true;

    int (*osPoll)(int domain, int type, int protocol) = dlsym((void *) -1L, "poll");
    KTestMock *Mock = CreateMock();
    Mock->Create(KNET_GetFdType, GetFdTypeTemp);
    Mock->Create(osPoll, PollMockEFAULT);

    fd_set readFds;
    FD_ZERO(&readFds);
    FD_SET(KNET_FD_TYPE_INVALID, &readFds);
    FD_SET(KNET_FD_TYPE_SOCKET, &readFds);

    ret = select(FD_SETSIZE, &readFds, NULL, NULL, NULL);
    DT_ASSERT_EQUAL(ret, -1);
    DT_ASSERT_EQUAL(errno, EFAULT);

    g_tcpInited = false;
    Mock->Delete(KNET_GetFdType);
    Mock->Delete(osPoll);
    DeleteMock(Mock);
}

DTEST_CASE_F(frameHijack, TEST_FRAME_SELECT_POLL_EINTR, Init, Deinit)
{
    int ret = 0;
    g_tcpInited = true;

    int (*osPoll)(int domain, int type, int protocol) = dlsym((void *) -1L, "poll");
    KTestMock *Mock = CreateMock();
    Mock->Create(KNET_GetFdType, GetFdTypeTemp);
    Mock->Create(osPoll, PollMockEINTR);

    fd_set readFds;
    FD_ZERO(&readFds);
    FD_SET(KNET_FD_TYPE_INVALID, &readFds);
    FD_SET(KNET_FD_TYPE_SOCKET, &readFds);

    ret = select(FD_SETSIZE, &readFds, NULL, NULL, NULL);
    DT_ASSERT_EQUAL(ret, -1);
    DT_ASSERT_EQUAL(errno, EINTR);

    g_tcpInited = false;
    Mock->Delete(KNET_GetFdType);
    Mock->Delete(osPoll);
    DeleteMock(Mock);
}

DTEST_CASE_F(frameHijack, TEST_FRAME_SELECT_POLL_EINVAL, Init, Deinit)
{
    int ret = 0;
    g_tcpInited = true;

    int (*osPoll)(int domain, int type, int protocol) = dlsym((void *) -1L, "poll");
    KTestMock *Mock = CreateMock();
    Mock->Create(KNET_GetFdType, GetFdTypeTemp);
    Mock->Create(osPoll, PollMockEINVAL);

    fd_set readFds;
    FD_ZERO(&readFds);
    FD_SET(KNET_FD_TYPE_INVALID, &readFds);
    FD_SET(KNET_FD_TYPE_SOCKET, &readFds);

    ret = select(FD_SETSIZE, &readFds, NULL, NULL, NULL);
    DT_ASSERT_EQUAL(ret, -1);
    DT_ASSERT_EQUAL(errno, EINVAL);

    g_tcpInited = false;
    Mock->Delete(KNET_GetFdType);
    Mock->Delete(osPoll);
    DeleteMock(Mock);
}

DTEST_CASE_F(frameHijack, TEST_FRAME_SELECT_POLL_ENOMEM, Init, Deinit)
{
    int ret = 0;
    g_tcpInited = true;

    int (*osPoll)(int domain, int type, int protocol) = dlsym((void *) -1L, "poll");
    KTestMock *Mock = CreateMock();
    Mock->Create(KNET_GetFdType, GetFdTypeTemp);
    Mock->Create(osPoll, PollMockENOMEM);

    fd_set readFds;
    FD_ZERO(&readFds);
    FD_SET(KNET_FD_TYPE_INVALID, &readFds);
    FD_SET(KNET_FD_TYPE_SOCKET, &readFds);

    ret = select(FD_SETSIZE, &readFds, NULL, NULL, NULL);
    DT_ASSERT_EQUAL(ret, -1);
    DT_ASSERT_EQUAL(errno, ENOMEM);

    g_tcpInited = false;
    Mock->Delete(KNET_GetFdType);
    Mock->Delete(osPoll);
    DeleteMock(Mock);
}

DTEST_CASE_F(frameHijack, TEST_FRAME_PSELECT_EBADF, Init, Deinit)
{
    int ret = 0;

    g_tcpInited = true;

    int (*osSelect)(int domain, int type, int protocol) = dlsym((void *) -1L, "select");
    KTestMock *Mock = CreateMock();
    Mock->Create(osSelect, SelectMockEBADF);

    ret = pselect(1, NULL, NULL, NULL, NULL, NULL);
    DT_ASSERT_EQUAL(ret, -1);
    DT_ASSERT_EQUAL(errno, EBADF);

    g_tcpInited = false;
    Mock->Delete(osSelect);
    DeleteMock(Mock);
}

DTEST_CASE_F(frameHijack, TEST_FRAME_PSELECT_EINTR, Init, Deinit)
{
    int ret = 0;
    g_tcpInited = true;

    int (*osSelect)(int domain, int type, int protocol) = dlsym((void *) -1L, "select");
    KTestMock *Mock = CreateMock();
    Mock->Create(osSelect, SelectMockEINTR);

    ret = pselect(1, NULL, NULL, NULL, NULL, NULL);
    DT_ASSERT_EQUAL(ret, -1);
    DT_ASSERT_EQUAL(errno, EINTR);

    g_tcpInited = false;
    Mock->Delete(osSelect);
    DeleteMock(Mock);
}

DTEST_CASE_F(frameHijack, TEST_FRAME_PSELECT_EINVAL, Init, Deinit)
{
    int ret = 0;
    g_tcpInited = true;

    int (*osSelect)(int domain, int type, int protocol) = dlsym((void *) -1L, "select");
    KTestMock *Mock = CreateMock();
    Mock->Create(osSelect, SelectMockEINVAL);

    ret = pselect(1, NULL, NULL, NULL, NULL, NULL);
    DT_ASSERT_EQUAL(ret, -1);
    DT_ASSERT_EQUAL(errno, EINVAL);

    g_tcpInited = false;
    Mock->Delete(osSelect);
    DeleteMock(Mock);
}

DTEST_CASE_F(frameHijack, TEST_FRAME_PSELECT_ENOMEM, Init, Deinit)
{
    int ret = 0;
    g_tcpInited = true;

    int (*osSelect)(int domain, int type, int protocol) = dlsym((void *) -1L, "select");
    KTestMock *Mock = CreateMock();
    Mock->Create(osSelect, SelectMockENOMEM);

    ret = pselect(1, NULL, NULL, NULL, NULL, NULL);
    DT_ASSERT_EQUAL(ret, -1);
    DT_ASSERT_EQUAL(errno, ENOMEM);

    g_tcpInited = false;
    Mock->Delete(osSelect);
    DeleteMock(Mock);
}

DTEST_CASE_F(frameHijack, TEST_FRAME_PSELECT_POLL_EFAULT, Init, Deinit)
{
    int ret = 0;
    g_tcpInited = true;

    int (*osPoll)(int domain, int type, int protocol) = dlsym((void *) -1L, "poll");
    KTestMock *Mock = CreateMock();
    Mock->Create(KNET_GetFdType, GetFdTypeTemp);
    Mock->Create(osPoll, PollMockEFAULT);

    fd_set readFds;
    FD_ZERO(&readFds);
    FD_SET(KNET_FD_TYPE_INVALID, &readFds);
    FD_SET(KNET_FD_TYPE_SOCKET, &readFds);

    ret = pselect(FD_SETSIZE, &readFds, NULL, NULL, NULL, NULL);
    DT_ASSERT_EQUAL(ret, -1);
    DT_ASSERT_EQUAL(errno, EFAULT);

    g_tcpInited = false;
    Mock->Delete(KNET_GetFdType);
    Mock->Delete(osPoll);
    DeleteMock(Mock);
}

DTEST_CASE_F(frameHijack, TEST_FRAME_PSELECT_POLL_EINTR, Init, Deinit)
{
    int ret = 0;
    g_tcpInited = true;

    int (*osPoll)(int domain, int type, int protocol) = dlsym((void *) -1L, "poll");
    KTestMock *Mock = CreateMock();
    Mock->Create(KNET_GetFdType, GetFdTypeTemp);
    Mock->Create(osPoll, PollMockEINTR);

    fd_set readFds;
    FD_ZERO(&readFds);
    FD_SET(KNET_FD_TYPE_INVALID, &readFds);
    FD_SET(KNET_FD_TYPE_SOCKET, &readFds);

    ret = pselect(FD_SETSIZE, &readFds, NULL, NULL, NULL, NULL);
    DT_ASSERT_EQUAL(ret, -1);
    DT_ASSERT_EQUAL(errno, EINTR);

    g_tcpInited = false;
    Mock->Delete(KNET_GetFdType);
    Mock->Delete(osPoll);
    DeleteMock(Mock);
}

DTEST_CASE_F(frameHijack, TEST_FRAME_PSELECT_POLL_EINVAL, Init, Deinit)
{
    int ret = 0;
    g_tcpInited = true;

    int (*osPoll)(int domain, int type, int protocol) = dlsym((void *) -1L, "poll");
    KTestMock *Mock = CreateMock();
    Mock->Create(KNET_GetFdType, GetFdTypeTemp);
    Mock->Create(osPoll, PollMockEINVAL);

    fd_set readFds;
    FD_ZERO(&readFds);
    FD_SET(KNET_FD_TYPE_INVALID, &readFds);
    FD_SET(KNET_FD_TYPE_SOCKET, &readFds);

    ret = pselect(FD_SETSIZE, &readFds, NULL, NULL, NULL, NULL);
    DT_ASSERT_EQUAL(ret, -1);
    DT_ASSERT_EQUAL(errno, EINVAL);

    g_tcpInited = false;
    Mock->Delete(KNET_GetFdType);
    Mock->Delete(osPoll);
    DeleteMock(Mock);
}

DTEST_CASE_F(frameHijack, TEST_FRAME_PSELECT_POLL_ENOMEM, Init, Deinit)
{
    int ret = 0;
    g_tcpInited = true;

    int (*osPoll)(int domain, int type, int protocol) = dlsym((void *) -1L, "poll");
    KTestMock *Mock = CreateMock();
    Mock->Create(KNET_GetFdType, GetFdTypeTemp);
    Mock->Create(osPoll, PollMockENOMEM);

    fd_set readFds;
    FD_ZERO(&readFds);
    FD_SET(KNET_FD_TYPE_INVALID, &readFds);
    FD_SET(KNET_FD_TYPE_SOCKET, &readFds);

    ret = pselect(FD_SETSIZE, &readFds, NULL, NULL, NULL, NULL);
    DT_ASSERT_EQUAL(ret, -1);
    DT_ASSERT_EQUAL(errno, ENOMEM);

    g_tcpInited = false;
    Mock->Delete(KNET_GetFdType);
    Mock->Delete(osPoll);
    DeleteMock(Mock);
}

static int EpollCreateEnfileMock(int size)
{
    errno = ENFILE;
    return -1;
}
 
DTEST_CASE_F(frameHijack, TEST_FRAME_EPOLL_CREATE_ENFILE, Init, Deinit)
{
    int ret = 0;
    int size = 1;
    errno = 0;
 
    int (*osEpollCreate)(int size) = dlsym((void *) -1L, "epoll_create");
 
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(osEpollCreate, EpollCreateEnfileMock);
 
    DP_Init(0);
    g_tcpInited = true;
    ret = epoll_create(size);
 
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    DT_ASSERT_EQUAL(errno, ENFILE);
 
    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(KNET_TrafficResourcesInit);
    Mock->Delete(osEpollCreate);
    DeleteMock(Mock);
}

DTEST_CASE_F(frameHijack, TEST_FRAME_EPOLL_CREATE_ENOMEM, Init, Deinit)
{
    int ret = 0;
    int size = 1;
    errno = 0;
 
    int (*osEpollCreate)(int size) = dlsym((void *) -1L, "epoll_create");
 
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(osEpollCreate, EpollCreateEnomemMock);
 
    DP_Init(0);
    g_tcpInited = true;
    ret = epoll_create(size);
 
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    DT_ASSERT_EQUAL(errno, ENOMEM);
 
    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(KNET_TrafficResourcesInit);
    Mock->Delete(osEpollCreate);
    DeleteMock(Mock);
}
 
static int EpollCreateEinvalMock(int size)
{
    errno = EINVAL;
    return -1;
}
 
DTEST_CASE_F(frameHijack, TEST_FRAME_EPOLL_CREATE_EINVAL, Init, Deinit)
{
    int ret = 0;
    int size = 1;
    errno = 0;
 
    int (*osEventfd)(unsigned int initval, int flags) = dlsym((void *) -1L, "eventfd");
 
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(osEventfd, EpollCreateEinvalMock);
 
    DP_Init(0);
    g_tcpInited = true;
    ret = epoll_create(size);
 
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    DT_ASSERT_EQUAL(errno, EINVAL);
 
    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(KNET_TrafficResourcesInit);
    Mock->Delete(osEventfd);
    DeleteMock(Mock);
}
 
static int EpollCreateEnospcMock(int size)
{
    errno = ENOSPC;
    return -1;
}
 
DTEST_CASE_F(frameHijack, TEST_FRAME_EPOLL_CREATE_ENOSPC, Init, Deinit)
{
    int ret = 0;
    int size = 1;
    errno = 0;
 
    int (*osEpollCtl)(int epfd, int op, int fd, struct epoll_event *event) = dlsym((void *) -1L, "epoll_ctl");
 
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(osEpollCtl, EpollCreateEnospcMock);
 
    DP_Init(0);
    g_tcpInited = true;
    ret = epoll_create(size);
 
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    DT_ASSERT_EQUAL(errno, ENOSPC);
 
    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(KNET_TrafficResourcesInit);
    Mock->Delete(osEpollCtl);
    DeleteMock(Mock);
}
 
static int EpollCreateEioMock(int size)
{
    errno = EIO;
    return -1;
}
 
DTEST_CASE_F(frameHijack, TEST_FRAME_EPOLL_CREATE_EIO, Init, Deinit)
{
    int ret = 0;
    int size = 1;
    errno = 0;
 
    int (*osEventfd)(unsigned int initval, int flags) = dlsym((void *) -1L, "eventfd");
    int (*osClose)(int fd) = dlsym((void *) -1L, "close");
 
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(osEventfd, EpollCreateEinvalMock);
    Mock->Create(osClose, EpollCreateEioMock);
 
    DP_Init(0);
    g_tcpInited = true;
    ret = epoll_create(size);
 
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    DT_ASSERT_EQUAL(errno, EIO);
 
    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(KNET_TrafficResourcesInit);
    Mock->Delete(osEventfd);
    Mock->Delete(osClose);
    DeleteMock(Mock);
}
 
static int EpollCreateEintrMock(int size)
{
    errno = EINTR;
    return -1;
}
 
DTEST_CASE_F(frameHijack, TEST_FRAME_EPOLL_CREATE_EINTR, Init, Deinit)
{
    int ret = 0;
    int size = 1;
    errno = 0;
 
    int (*osEventfd)(unsigned int initval, int flags) = dlsym((void *) -1L, "eventfd");
    int (*osClose)(int fd) = dlsym((void *) -1L, "close");
 
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(osEventfd, EpollCreateEinvalMock);
    Mock->Create(osClose, EpollCreateEintrMock);
 
    DP_Init(0);
    g_tcpInited = true;
    ret = epoll_create(size);
 
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    DT_ASSERT_EQUAL(errno, EINTR);
 
    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(KNET_TrafficResourcesInit);
    Mock->Delete(osEventfd);
    Mock->Delete(osClose);
    DeleteMock(Mock);
}
 
DTEST_CASE_F(frameHijack, TEST_FRAME_EPOLL_CREATE1_ENFILE, Init, Deinit)
{
    int ret = 0;
    int size = 0;
    errno = 0;
 
    int (*osEpollCreate)(int size) = dlsym((void *) -1L, "epoll_create");
 
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(osEpollCreate, EpollCreateEnfileMock);
 
    DP_Init(0);
    g_tcpInited = true;
    ret = epoll_create1(size);
 
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    DT_ASSERT_EQUAL(errno, ENFILE);
 
    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(KNET_TrafficResourcesInit);
    Mock->Delete(osEpollCreate);
    DeleteMock(Mock);
}
 
DTEST_CASE_F(frameHijack, TEST_FRAME_EPOLL_CREATE1_ENOMEM, Init, Deinit)
{
    int ret = 0;
    int size = 0;
    errno = 0;
 
    int (*osEpollCreate)(int size) = dlsym((void *) -1L, "epoll_create");
 
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(osEpollCreate, EpollCreateEnomemMock);
 
    DP_Init(0);
    g_tcpInited = true;
    ret = epoll_create1(size);
 
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    DT_ASSERT_EQUAL(errno, ENOMEM);
 
    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(KNET_TrafficResourcesInit);
    Mock->Delete(osEpollCreate);
    DeleteMock(Mock);
}
 
DTEST_CASE_F(frameHijack, TEST_FRAME_EPOLL_CREATE1_EINVAL, Init, Deinit)
{
    int ret = 0;
    int size = 0;
    errno = 0;
 
    int (*osEventfd)(unsigned int initval, int flags) = dlsym((void *) -1L, "eventfd");
 
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(osEventfd, EpollCreateEinvalMock);
 
    DP_Init(0);
    g_tcpInited = true;
    ret = epoll_create1(size);
 
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    DT_ASSERT_EQUAL(errno, EINVAL);
 
    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(KNET_TrafficResourcesInit);
    Mock->Delete(osEventfd);
    DeleteMock(Mock);
}
 
DTEST_CASE_F(frameHijack, TEST_FRAME_EPOLL_CREATE1_ENOSPC, Init, Deinit)
{
    int ret = 0;
    int size = 0;
    errno = 0;
 
    int (*osEpollCtl)(int epfd, int op, int fd, struct epoll_event *event) = dlsym((void *) -1L, "epoll_ctl");
 
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(osEpollCtl, EpollCreateEnospcMock);
 
    DP_Init(0);
    g_tcpInited = true;
    ret = epoll_create1(size);
 
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    DT_ASSERT_EQUAL(errno, ENOSPC);
 
    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(KNET_TrafficResourcesInit);
    Mock->Delete(osEpollCtl);
    DeleteMock(Mock);
}
 
DTEST_CASE_F(frameHijack, TEST_FRAME_EPOLL_CREATE1_EIO, Init, Deinit)
{
    int ret = 0;
    int size = 0;
    errno = 0;
 
    int (*osEventfd)(unsigned int initval, int flags) = dlsym((void *) -1L, "eventfd");
    int (*osClose)(int fd) = dlsym((void *) -1L, "close");
 
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(osEventfd, EpollCreateEinvalMock);
    Mock->Create(osClose, EpollCreateEioMock);
 
    DP_Init(0);
    g_tcpInited = true;
    ret = epoll_create1(size);
 
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    DT_ASSERT_EQUAL(errno, EIO);
 
    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(KNET_TrafficResourcesInit);
    Mock->Delete(osEventfd);
    Mock->Delete(osClose);
    DeleteMock(Mock);
}
 
DTEST_CASE_F(frameHijack, TEST_FRAME_EPOLL_CREATE1_EINTR, Init, Deinit)
{
    int ret = 0;
    int size = 0;
    errno = 0;
 
    int (*osEventfd)(unsigned int initval, int flags) = dlsym((void *) -1L, "eventfd");
    int (*osClose)(int fd) = dlsym((void *) -1L, "close");
 
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(osEventfd, EpollCreateEinvalMock);
    Mock->Create(osClose, EpollCreateEintrMock);
 
    DP_Init(0);
    g_tcpInited = true;
    ret = epoll_create1(size);
 
    DT_ASSERT_EQUAL(ret, INVALID_FD);
    DT_ASSERT_EQUAL(errno, EINTR);
 
    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(KNET_TrafficResourcesInit);
    Mock->Delete(osEventfd);
    Mock->Delete(osClose);
    DeleteMock(Mock);
}
 
static int EpollWaitEfaultMock(int size)
{
    errno = EFAULT;
    return -1;
}
 
DTEST_CASE_F(frameHijack, TEST_FRAME_EPOLL_WAIT_EFAULT, Init, Deinit)
{
    int ret = 0;
    errno = 0;
    int epfd = 0 ;
    
    struct epoll_event events = { 0 };
    int maxevents = 10;
    int timeout = 10;
 
    int (*osEpollWait)(int epfd, struct epoll_event events,
                      int maxevents, int timeout) = dlsym((void *) -1L, "epoll_wait");
 
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(osEpollWait, EpollWaitEfaultMock);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
 
    DP_Init(0);
    g_tcpInited = true;
    ret = epoll_wait(epfd, &events, maxevents, -1);
 
    DT_ASSERT_EQUAL(ret, 0);
    DT_ASSERT_EQUAL(errno, EFAULT);
 
    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(KNET_TrafficResourcesInit);
    Mock->Delete(osEpollWait);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}
 
DTEST_CASE_F(frameHijack, TEST_FRAME_EPOLL_PWAIT_EFAULT, Init, Deinit)
{
    int ret = 0;
    errno = 0;
    int epfd = 0;
    sigset_t mask = { 0 };
    
    struct epoll_event events = { 0 };
    int maxevents = 10;
    int timeout = 10;
 
    int (*osEpollWait)(int epfd, struct epoll_event events,
                      int maxevents, int timeout) = dlsym((void *) -1L, "epoll_wait");
 
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(osEpollWait, EpollWaitEfaultMock);
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
 
    DP_Init(0);
    g_tcpInited = true;
    ret = epoll_pwait(epfd, &events, maxevents, -1, &mask);
 
    DT_ASSERT_EQUAL(ret, 0);
    DT_ASSERT_EQUAL(errno, EFAULT);
 
    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(KNET_TrafficResourcesInit);
    Mock->Delete(osEpollWait);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}
 
void *MallocEnomemMock(size_t size)
{
    return NULL;
}
 
DTEST_CASE_F(frameHijack, TEST_FRAME_EPOLL_WAIT_MALLOC_ENOMEM, Init, Deinit)
{
    int ret = 0;
    errno = 0;
    int epfd = 0 ;
    
    struct epoll_event events = { 0 };
    int maxevents = 1024;
    int timeout = 10;
 
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(malloc, MallocEnomemMock);
    
    DP_Init(0);
    g_tcpInited = true;
    ret = epoll_wait(epfd, &events, maxevents, 0);
 
    DT_ASSERT_EQUAL(ret, -1);
    DT_ASSERT_EQUAL(errno, ENOMEM);
 
    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(KNET_TrafficResourcesInit);
    Mock->Delete(KNET_IsFdHijack);
    Mock->Delete(malloc);
    DeleteMock(Mock);
}
 
DTEST_CASE_F(frameHijack, TEST_FRAME_EPOLL_PWAIT_MALLOC_ENOMEM, Init, Deinit)
{
    int ret = 0;
    errno = 0;
    int epfd = 0;
    sigset_t mask = { 0 };
    
    struct epoll_event events = { 0 };
    int maxevents = 1024;
    int timeout = 10;
 
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_IsFdHijack, FuncRetTrue);
    Mock->Create(malloc, MallocEnomemMock);
    
    DP_Init(0);
    g_tcpInited = true;
    ret = epoll_pwait(epfd, &events, maxevents, 0, &mask);
 
    DT_ASSERT_EQUAL(ret, -1);
    DT_ASSERT_EQUAL(errno, ENOMEM);
 
    DP_Deinit(0);
    g_tcpInited = false;
    Mock->Delete(KNET_TrafficResourcesInit);
    Mock->Delete(KNET_IsFdHijack);
    Mock->Delete(malloc);
    DeleteMock(Mock);
}
