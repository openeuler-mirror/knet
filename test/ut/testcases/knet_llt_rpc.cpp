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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdarg.h>
#include <inttypes.h>
#include <sys/queue.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <netinet/in.h>
#include "knet_types.h"
#include "knet_log.h"
#include "knet_rpc.h"

#include "securec.h"
#include "common.h"
#include "mock.h"

#define READY_EVENTS_NUM (2)
#define MAGIC_NUM 0x12348765
#define RPC_EXCEPTION (-2)
static int g_epollWaitCount = 0;
static int g_serverFd = 0;
static int g_recvCount = 0;

extern "C" {
int RpcRecvVarLenData(int fd, struct RpcPkgInfo *pkgInfo, struct KNET_RpcMessage *request);
int RpcHandleRecvBytes(ssize_t bytesRead);
void RpcHandleDisconnect(int epollFd, struct epoll_event *event);
int RpcMsgSendRecv(int fd, enum KNET_RpcModType mod, struct KNET_RpcMessage* request,
                               struct KNET_RpcMessage* response);
}

DTEST_CASE_F(RPC, TEST_KNET_RPC_Client_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(socket, TEST_GetFuncRetNegative(1));

    int mode = 0;
    struct KNET_RpcMessage knetRpcRequest;
    knetRpcRequest.dataLen = 1;
    knetRpcRequest.dataType = RPC_MSG_DATA_TYPE_FIXED_LEN;
    struct KNET_RpcMessage knetRpcResponse;
    int ret = KNET_RpcCall(mode, &knetRpcRequest, &knetRpcResponse);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Create(socket, TEST_GetFuncRetPositive(0));
    Mock->Create(strncpy_s, TEST_GetFuncRetNegative(1));
    ret = KNET_RpcCall(mode, &knetRpcRequest, &knetRpcResponse);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(strncpy_s);

    Mock->Create(strncpy_s, TEST_GetFuncRetPositive(0));
    Mock->Create(connect, TEST_GetFuncRetPositive(0));
    Mock->Create(send, TEST_GetFuncRetPositive(1));
    Mock->Create(recv, TEST_GetFuncRetPositive(0));
    Mock->Create(close, TEST_GetFuncRetPositive(0));
    ret = KNET_RpcCall(mode, &knetRpcRequest, &knetRpcResponse);
    DT_ASSERT_EQUAL(ret, -1);

    knetRpcRequest.dataType = RPC_MSG_DATA_TYPE_VARIABLE_LEN;
    knetRpcRequest.dataLen = 0;
    ret = KNET_RpcCall(mode, &knetRpcRequest, &knetRpcResponse);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(socket);
    Mock->Delete(connect);
    Mock->Delete(send);
    Mock->Delete(recv);
    Mock->Delete(strncpy_s);
    Mock->Delete(close);
    DeleteMock(Mock);
    g_recvCount = 0;
}

int Handler(int id, struct KNET_RpcMessage *knetRpcRequest, struct KNET_RpcMessage *knetRpcResponse)
{
    return 0;
}

DTEST_CASE_F(RPC, TEST_KNET_RPC_REG_NORMAL, NULL, NULL)
{
    enum KNET_RpcEventType event = 0;
    enum KNET_RpcModType mode = 0;

    int ret = KNET_RpcRegServer(event, mode, NULL);
    DT_ASSERT_EQUAL(ret, -1);

    ret = KNET_RpcRegServer(event, mode, &Handler);
    DT_ASSERT_EQUAL(ret, 0);
}

DTEST_CASE_F(RPC, TEST_KNET_RPC_DES_NORMAL, NULL, NULL)
{
    enum KNET_RpcEventType event = 0;
    enum KNET_RpcModType mode = 0;

    KNET_RpcDesServer(-1, mode);
    KNET_RpcDesServer(event, mode);
}

struct RpcPkg {
    int mod;
    int ret;
    size_t dataLen;
    enum RpcMsgDataType dataType;
    char padding[4];
};

static int Function1(int sockfd, void *buf, size_t len, int flags)
{
    if (g_recvCount == 0 || g_recvCount == 1) {
        uint32_t num = MAGIC_NUM;
        int ret = memcpy_s(buf, sizeof(num), &num, sizeof(num));
        if (ret != 0) {
            return -1;
        }
        g_recvCount = g_recvCount + 1;
        return sizeof(num);
    } else {
        struct RpcPkg pkg = {0};
        pkg.mod = 0;
        pkg.dataLen = 1;
        pkg.dataType = RPC_MSG_DATA_TYPE_FIXED_LEN;
        int ret = memcpy_s(buf, sizeof(pkg), &pkg, sizeof(pkg));
        if (ret != 0) {
            return -1;
        }
        g_recvCount = g_recvCount + 1;
        return sizeof(pkg);
    }
}

static int Function2(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    if (g_epollWaitCount == 0) {
        events[0].data.fd = g_serverFd;
        events[1].data.fd = g_serverFd + 1;
        events[1].events = EPOLLIN;
        g_epollWaitCount = g_epollWaitCount + 1;
        return READY_EVENTS_NUM;
    } else if (g_epollWaitCount == 1) {
        errno = EINTR;
        g_epollWaitCount = g_epollWaitCount + 1;
        return -1;
    } else {
        errno = ENOMEM;
        g_epollWaitCount = g_epollWaitCount + 1;
        return -1;
    }
}

DTEST_CASE_F(RPC, TEST_KNET_RPC_RUN_NORMAL, NULL, NULL)
{
    KNET_RpcRegServer(1, 0, &Handler);
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(socket, TEST_GetFuncRetNegative(1));
    int ret = KNET_RpcRun();
    DT_ASSERT_EQUAL(ret, -1);
    
    Mock->Create(socket, TEST_GetFuncRetPositive(0));
    Mock->Create(strncpy_s, TEST_GetFuncRetNegative(1));
    ret = KNET_RpcRun();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(strncpy_s);

    Mock->Create(strncpy_s, TEST_GetFuncRetPositive(0));
    Mock->Create(unlink, TEST_GetFuncRetPositive(0));
    Mock->Create(bind, TEST_GetFuncRetPositive(0));
    Mock->Create(chmod, TEST_GetFuncRetPositive(0));
    Mock->Create(listen, TEST_GetFuncRetPositive(0));
    Mock->Create(epoll_create, TEST_GetFuncRetPositive(0));
    Mock->Create(epoll_ctl, TEST_GetFuncRetPositive(0));
    Mock->Create(epoll_wait, Function2);
    Mock->Create(accept, TEST_GetFuncRetPositive(0));
    Mock->Create(send, TEST_GetFuncRetPositive(1));
    Mock->Create(recv, Function1);
    Mock->Create(close, TEST_GetFuncRetPositive(0));
    ret = KNET_RpcRun();
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(socket);
    Mock->Delete(unlink);
    Mock->Delete(bind);
    Mock->Delete(chmod);
    Mock->Delete(listen);
    Mock->Delete(epoll_create);
    Mock->Delete(epoll_ctl);
    Mock->Delete(epoll_wait);
    Mock->Delete(send);
    Mock->Delete(recv);
    Mock->Delete(strncpy_s);
    Mock->Delete(close);
    Mock->Delete(accept);
    DeleteMock(Mock);
    g_recvCount = 0;
}

struct RpcPkgInfo {
    int mod;
    int ret;
    size_t dataLen;
    enum RpcMsgDataType dataType;
    char padding[4];
};

DTEST_CASE_F(RPC, TEST_KNET_RPC_RECV_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(recv, TEST_GetFuncRetPositive(1));

    struct KNET_RpcMessage request = { 0 };
    struct RpcPkgInfo pkgInfo = { 0 };
    pkgInfo.dataLen = -1;
    int ret = RpcRecvVarLenData(1, &pkgInfo, &request);
    DT_ASSERT_EQUAL(ret, RPC_EXCEPTION);

    pkgInfo.dataLen = 1;
    ret = RpcRecvVarLenData(1, &pkgInfo, &request);
    free(request.variableLenData);
    request.variableLenData = NULL;

    ret = RpcHandleRecvBytes(1);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(recv);
    DeleteMock(Mock);
}

DTEST_CASE_F(RPC, TEST_KNET_RPC_HANDLE_DISCONNECT_NORMAL, NULL, NULL)
{
    KNET_RpcRegServer(KNET_RPC_EVENT_DISCONNECT, 0, &Handler);
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(epoll_ctl, TEST_GetFuncRetNegative(1));

    struct epoll_event event = { 0 };
    RpcHandleDisconnect(0, &event);

    Mock->Delete(epoll_ctl);
    DeleteMock(Mock);
}

DTEST_CASE_F(RPC, TEST_KNET_RPC_MSG_SEND_RECV_NORMAL, NULL, NULL)
{
    struct KNET_RpcMessage request = { 0 };
    struct KNET_RpcMessage response = { 0 };
    int ret = RpcMsgSendRecv(0, 0, &request, &response);
    DT_ASSERT_EQUAL(ret, RPC_EXCEPTION);
}