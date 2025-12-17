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

#include "knet_log.h"
#include "knet_lock.h"
#include "knet_rpc.h"

#define SOCKET_PATH "/etc/knet/run/knet_mp.sock"
#define MAX_EVENTS 16
#define MAX_RETRY_COUNT 32
#define RPC_CONNECT_MAX 1024
#define RPC_MAGIC_NUMBER 0x12348765
#define OVER_MAX_RETRY_CNT (-3)
#define RPC_EXCEPTION (-2)
#define RPC_DISCONNECT (-1)
#define RPC_SUCCESS (0)

struct RpcPkgInfo {
    int mod;
    int ret;
    size_t dataLen;
    enum RpcMsgDataType dataType;
    char padding[4];
};

struct RpcPkgHdr {
    int magicNumber;
    int reserved;
    struct RpcPkgInfo pkgInfo;
};

static RpcHandler g_handlerDic[KNET_RPC_EVENT_MAX][KNET_RPC_MOD_MAX] = { 0 };

static KNET_SpinLock g_rpcLock = {
    .value = KNET_SPIN_UNLOCKED_VALUE,
};

KNET_STATIC int RpcSend(int fd, const void *sendBuf, size_t len)
{
    size_t sendLen = len;
    int ret = 0;
    int index = 0;
    while (sendLen > 0) {
        ret = send(fd, sendBuf + index, sendLen, MSG_NOSIGNAL);
        if (ret < 0) {
            KNET_ERR("K-NET rpc send rpcPkg failed, ret %d, dataLen %zu", ret, len);
            return RPC_EXCEPTION;
        } else if (ret == 0) {
            KNET_ERR("K-NET rpc send rpcPkg failed, ret %d, dataLen %zu", ret, len);
            return RPC_DISCONNECT;
        }
        index = index + ret;
        sendLen = sendLen - (size_t)ret;
    }
    return RPC_SUCCESS;
}

KNET_STATIC int RpcMsgSend(int fd, enum KNET_RpcModType mod, struct KNET_RpcMessage *request)
{
    struct RpcPkgHdr hdr = { 0 };
    hdr.magicNumber = RPC_MAGIC_NUMBER;
    hdr.reserved = 0;
    hdr.pkgInfo.mod = mod;
    hdr.pkgInfo.ret = request->ret;
    hdr.pkgInfo.dataType = request->dataType;
    hdr.pkgInfo.dataLen = request->dataLen;

    size_t len = sizeof(hdr);
    int ret = RpcSend(fd, &hdr, len);
    if (ret != 0) {
        KNET_ERR("K-NET rpc send rpcHdr failed");
        return ret;
    }
    
    if (request->dataType == RPC_MSG_DATA_TYPE_FIXED_LEN) {
        return RpcSend(fd, request->fixedLenData, request->dataLen);
    } else if (request->dataType == RPC_MSG_DATA_TYPE_VARIABLE_LEN) {
        return RpcSend(fd, request->variableLenData, request->dataLen);
    }

    return RPC_EXCEPTION;
}

KNET_STATIC int RpcHandleRecvBytes(ssize_t bytesRead)
{
    if (bytesRead < 0) {
        return RPC_EXCEPTION;
    } else if (bytesRead == 0) {
        return RPC_DISCONNECT;
    } else {
        return RPC_SUCCESS;
    }
}

KNET_STATIC int RpcRecvVarLenData(int fd, struct RpcPkgInfo *pkgInfo, struct KNET_RpcMessage *request)
{
    if (pkgInfo->dataLen <= 0 || pkgInfo->dataLen > RPC_MESSAGE_SIZE_MAX) {
        KNET_ERR("K-NET rpc recv error dataLen, len %zu", pkgInfo->dataLen);
        return RPC_EXCEPTION;
    }
    /**
    主进程接收到从进程请求并且请求中的数据为可变长度时会进行calloc,正常场景下free操作在RpcHandleTask
    接口内进行;从进程接收到主进程传来的结果并且请求中的数据为可变长度时会进行calloc,正常场景下free操作在
    从进程调用KNET_RpcCall并且使用完结果之后进行.
    */
    request->variableLenData = calloc(1, pkgInfo->dataLen);
    if (request->variableLenData == NULL) {
        KNET_ERR("K-NET rpc calloc failed, errno %d", errno);
        return RPC_EXCEPTION;
    }
    ssize_t bytesRead = recv(fd, request->variableLenData, pkgInfo->dataLen, MSG_WAITALL);
    if (bytesRead <= 0) {
        free(request->variableLenData);
        request->variableLenData = NULL;
        KNET_ERR("K-NET rpc recv variableLenData failed, bytesRead %zd", bytesRead);
        return RpcHandleRecvBytes(bytesRead);
    }

    request->ret = pkgInfo->ret;
    request->dataType = pkgInfo->dataType;
    request->dataLen = pkgInfo->dataLen;

    return RPC_SUCCESS;
}

KNET_STATIC int RpcRecvFixedLenData(int fd, struct RpcPkgInfo *pkgInfo, struct KNET_RpcMessage *request)
{
    if (pkgInfo->dataLen < 0 || pkgInfo->dataLen > RPC_MESSAGE_SIZE) {
        KNET_ERR("K-NET rpc recv error dataLen, len %zu", pkgInfo->dataLen);
        return RPC_EXCEPTION;
    }

    if (pkgInfo->dataLen != 0) {
        ssize_t bytesRead = recv(fd, request->fixedLenData, pkgInfo->dataLen, MSG_WAITALL);
        if (bytesRead <= 0) {
            KNET_ERR("K-NET rpc recv fixedLenData failed, bytesRead %zd", bytesRead);
            return RpcHandleRecvBytes(bytesRead);
        }
    }

    request->ret = pkgInfo->ret;
    request->dataType = pkgInfo->dataType;
    request->dataLen = pkgInfo->dataLen;

    return RPC_SUCCESS;
}

KNET_STATIC int RpcMsgRecv(int fd, enum KNET_RpcModType *mod, struct KNET_RpcMessage *request)
{
    uint32_t magicNumber = 0;
    ssize_t bytesRead = 0;
    int count = 0;

    while (magicNumber != RPC_MAGIC_NUMBER) {
        count = count + 1;
        if (count > MAX_RETRY_COUNT) {
            KNET_ERR("The maximum retry count is exceeded");
            return OVER_MAX_RETRY_CNT;
        }
        bytesRead = recv(fd, &magicNumber, sizeof(magicNumber), MSG_WAITALL);
        if (bytesRead == 0) {
            return RPC_DISCONNECT;
        } else if (bytesRead < 0) {
            KNET_ERR("K-NET rpc recv mn failed, bytesRead %zd", bytesRead);
            return RPC_EXCEPTION;
        }
    }
    int reserved = 0;
    bytesRead = recv(fd, &reserved, sizeof(reserved), MSG_WAITALL);
    if (bytesRead <= 0) {
        KNET_ERR("K-NET rpc recv reserve failed, bytesRead %zd", bytesRead);
        return RpcHandleRecvBytes(bytesRead);
    }
    struct RpcPkgInfo pkgInfo = { 0 };
    bytesRead = recv(fd, &pkgInfo, sizeof(pkgInfo), MSG_WAITALL);
    if (bytesRead <= 0) {
        KNET_ERR("K-NET rpc recv pkgInfo failed, bytesRead %zd", bytesRead);
        return RpcHandleRecvBytes(bytesRead);
    }

    if (pkgInfo.dataType == RPC_MSG_DATA_TYPE_FIXED_LEN) {
        *mod = pkgInfo.mod;
        return RpcRecvFixedLenData(fd, &pkgInfo, request);
    } else if (pkgInfo.dataType == RPC_MSG_DATA_TYPE_VARIABLE_LEN) {
        *mod = pkgInfo.mod;
        return RpcRecvVarLenData(fd, &pkgInfo, request);
    }

    KNET_ERR("K-NET rpc recv data failed, invalid dataType %d", pkgInfo.dataType);
    return RPC_EXCEPTION;
}

int KNET_RpcRegServer(enum KNET_RpcEventType event, enum KNET_RpcModType mod, RpcHandler handler)
{
    if (event >= KNET_RPC_EVENT_MAX || event < 0 || mod >= KNET_RPC_MOD_MAX || mod < 0 || handler == NULL) {
        KNET_ERR("K-NET rpc register handler failed, event %d, mod %d", event, mod);
        return -1;
    }
    if (g_handlerDic[event][mod] != NULL) {
        KNET_ERR("RpcHandler of event %d mod %d has been registered", event, mod);
        return -1;
    }
    g_handlerDic[event][mod] = handler;
    return 0;
}

void KNET_RpcDesServer(enum KNET_RpcEventType event, enum KNET_RpcModType mod)
{
    if (event >= KNET_RPC_EVENT_MAX || event < 0 || mod >= KNET_RPC_MOD_MAX || mod < 0) {
        KNET_ERR("K-NET rpc destroy handler failed, event %d, mod %d", event, mod);
        return;
    }
    g_handlerDic[event][mod] = NULL;
}

KNET_STATIC void RpcNewConnection(int epollFd, int serverFd)
{
    int clientFd = accept(serverFd, NULL, NULL);
    if (clientFd == -1) {
        KNET_ERR("K-NET rpc server socket accept failed, serverFd %d, error %d", serverFd, errno);
        return;
    }
    KNET_DEBUG("K-NET rpc server accept new client connection, serverFd %d, clientFd %d", serverFd, clientFd);

    struct epoll_event event = { 0 };
    event.events = EPOLLIN;
    event.data.fd = clientFd;
    int ret = epoll_ctl(epollFd, EPOLL_CTL_ADD, clientFd, &event);
    if (ret == -1) {
        KNET_ERR("K-NET rpc server socket EPOLL_CTL_ADD failed, epollFd %d, eventFd %d, error %d", epollFd, clientFd,
                 errno);
        close(clientFd);
        return;
    }

    for (int i = 0; i < KNET_RPC_MOD_MAX; ++i) {
        RpcHandler function = g_handlerDic[KNET_RPC_EVENT_CONNECT][i];
        if (function == NULL) {
            continue;
        }
        function(event.data.fd, NULL, NULL);
    }
}

KNET_STATIC int RpcHandleRequest(int fd, enum KNET_RpcModType mod, struct KNET_RpcMessage* request,
                                 struct KNET_RpcMessage* response)
{
    if (mod >= KNET_RPC_MOD_MAX || mod < 0) {
        KNET_ERR("Invalid mod index %d while request handle", mod);
        return -1;
    }
    RpcHandler function = g_handlerDic[KNET_RPC_EVENT_REQUEST][mod];
    if (function == NULL) {
        KNET_ERR("K-NET rpc HandleRequest failed, handler is NULL");
        return -1;
    }

    int ret = function(fd, request, response);
    if (ret < 0) {
        KNET_ERR("K-NET rpc HandleRequest failed");
    }
    response->ret = ret;
    ret = RpcMsgSend(fd, mod, response);
    /**
    function(注册的handler接口)内部会对response进行赋值,如果data是可变长度场景会在内部进行申请内存,遇到异常
    会自行free,如果正常执行完function,需要在发送完结果后进行free.
    */
    if (response->dataType == RPC_MSG_DATA_TYPE_VARIABLE_LEN && response->variableLenData != NULL) {
        free(response->variableLenData);
        response->variableLenData = NULL;
    }
    return ret;
}

KNET_STATIC void RpcHandleDisconnect(int epollFd, struct epoll_event *event)
{
    for (int i = 0; i < KNET_RPC_MOD_MAX; ++i) {
        RpcHandler function = g_handlerDic[KNET_RPC_EVENT_DISCONNECT][i];
        if (function == NULL) {
            continue;
        }
        function(event->data.fd, NULL, NULL);
    }

    int ret = epoll_ctl(epollFd, EPOLL_CTL_DEL, event->data.fd, NULL);
    if (ret == -1) {
        KNET_ERR("K-NET rpc server socket EPOLL_CTL_DEL failed, epollFd %d, eventFd %d, error %d", epollFd,
                 event->data.fd, errno);
    }
    close(event->data.fd);
    KNET_INFO("K-NET rpc disconnect");
}

KNET_STATIC int SetSocketPermission(void)
{
    int res = chmod(SOCKET_PATH, S_IRUSR | S_IWUSR);
    if (res == -1) {
        KNET_ERR("K-NET rpc chmod socket failed");
        return -1;
    }
    return 0;
}

KNET_STATIC int RpcSocketInit(void)
{
    int serverFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (serverFd == -1) {
        KNET_ERR("K-NET rpc server socket get sockfd failed");
        return -1;
    }

    struct sockaddr_un serverAddr = { 0 };
    serverAddr.sun_family = AF_UNIX;
    if (strncpy_s(serverAddr.sun_path, sizeof(serverAddr.sun_path), SOCKET_PATH, strlen(SOCKET_PATH)) != 0) {
        KNET_ERR("K-NET rpc server socket strncpy_s failed");
        goto closeFd;
    }

    int ret = unlink(SOCKET_PATH);
    if (ret != 0) {
        if (errno != ENOENT) {
            KNET_ERR("K-NET rpc unlink sock failed, error %d", errno);
            goto closeFd;
        }
    }

    ret = bind(serverFd, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    if (ret != 0) {
        KNET_ERR("K-NET rpc server socket bind failed");
        goto closeFd;
    }

    ret = SetSocketPermission();
    if (ret != 0) {
        KNET_ERR("K-NET rpc server set socket permission failed");
        goto closeFd;
    }

    ret = listen(serverFd, RPC_CONNECT_MAX);
    if (ret != 0) {
        KNET_ERR("K-NET rpc server socket listen failed");
        goto closeFd;
    }
    return serverFd;

closeFd:
    close(serverFd);
    return -1;
}

KNET_STATIC int RpcEpollInit(int listenFd)
{
    int epollFd = epoll_create(RPC_CONNECT_MAX);
    if (epollFd == -1) {
        KNET_ERR("K-NET rpc server creat epollFd failed");
        return -1;
    }

    struct epoll_event event = { 0 };
    event.data.fd = listenFd;
    event.events = EPOLLIN;
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, listenFd, &event) != 0) {
        KNET_ERR("K-NET rpc server socket EPOLL_CTL_ADD failed, epollFd %d, listenFd %d, error %d",
            epollFd, listenFd, errno);
        close(epollFd);
        return -1;
    }

    return epollFd;
}

KNET_STATIC void RpcHandleTask(int epollFd, struct epoll_event *event)
{
    struct KNET_RpcMessage knetRpcRequest = {0};
    struct KNET_RpcMessage knetRpcResponse = {0};
    enum KNET_RpcModType mod;

    int ret = RpcMsgRecv(event->data.fd, &mod, &knetRpcRequest);
    if (ret == RPC_SUCCESS) {
        ret = RpcHandleRequest(event->data.fd, mod, &knetRpcRequest, &knetRpcResponse);
        if (knetRpcRequest.dataType == RPC_MSG_DATA_TYPE_VARIABLE_LEN && knetRpcRequest.variableLenData != NULL) {
            free(knetRpcRequest.variableLenData);
            knetRpcRequest.variableLenData = NULL;
        }
        if (ret < 0) {
            RpcHandleDisconnect(epollFd, event);
        }
    } else {
        RpcHandleDisconnect(epollFd, event);
    }
}

KNET_STATIC int RpcEpollWait(int epollFd, struct epoll_event *events, int maxEvents)
{
    int readyEventsNum = epoll_wait(epollFd, events, maxEvents, -1);
    if (readyEventsNum == -1) {
        if (errno == EINTR) {
            return 0;
        } else {
            return -1;
        }
    }
    return readyEventsNum;
}

KNET_STATIC void RpcEpollTask(int serverFd, int epollFd)
{
    struct epoll_event events[MAX_EVENTS] = { 0 };

    while (1) {
        int readyEventsNum = RpcEpollWait(epollFd, events, MAX_EVENTS);
        if (readyEventsNum == -1) {
            KNET_ERR("K-NET rpc server epoll_wait failed");
            return;
        } else if (readyEventsNum == 0) {
            continue;
        }
        for (int i = 0; i < readyEventsNum; ++i) {
            if (events[i].data.fd == serverFd) {
                RpcNewConnection(epollFd, serverFd);
                continue;
            }
            if (events[i].events & EPOLLIN) {
                RpcHandleTask(epollFd, &events[i]);
            }
        }
    }
}

int KNET_RpcRun(void)
{
    int serverFd = RpcSocketInit();
    if (serverFd < 0) {
        return -1;
    }

    int epollFd = RpcEpollInit(serverFd);
    if (epollFd < 0) {
        close(serverFd);
        return -1;
    }

    (void)RpcEpollTask(serverFd, epollFd);
    close(serverFd);
    close(epollFd);

    return 0;
}

KNET_STATIC int RpcMsgSendRecv(int fd, enum KNET_RpcModType mod, struct KNET_RpcMessage* request,
                               struct KNET_RpcMessage* response)
{
    int ret = RpcMsgSend(fd, mod, request);
    if (ret != RPC_SUCCESS) {
        return ret;
    }

    enum KNET_RpcModType innerMod;
    ret = RpcMsgRecv(fd, &innerMod, response);
    return ret;
}

int KNET_RpcCall(enum KNET_RpcModType mod, struct KNET_RpcMessage *knetRpcRequest,
                 struct KNET_RpcMessage *knetRpcResponse)
{
    static int clientFd = -1;
    int ret = 0;
    KNET_SpinlockLock(&g_rpcLock);
    if (clientFd == -1) {
        clientFd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (clientFd == -1) {
            KNET_ERR("K-NET rpc client socket get sockfd failed, error %d", errno);
            goto abnormal;
        }

        struct sockaddr_un serverAddr = { 0 };
        serverAddr.sun_family = AF_UNIX;
        if (strncpy_s(serverAddr.sun_path, sizeof(serverAddr.sun_path), SOCKET_PATH, strlen(SOCKET_PATH)) != 0) {
            KNET_ERR("K-NET rpc client socket strncpy_s failed");
            close(clientFd);
            goto abnormal;
        }

        ret = connect(clientFd, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
        if (ret < 0) {
            KNET_ERR("K-NET rpc client socket failed to be connected, clientFd %d, error %d", clientFd, errno);
            close(clientFd);
            goto abnormal;
        }
    }

    ret = RpcMsgSendRecv(clientFd, mod, knetRpcRequest, knetRpcResponse);
    if (ret != RPC_SUCCESS) {
        close(clientFd);
        clientFd = -1;
    }
    KNET_SpinlockUnlock(&g_rpcLock);
    return ret;
    
abnormal:
    clientFd = -1;
    KNET_SpinlockUnlock(&g_rpcLock);
    return -1;
}