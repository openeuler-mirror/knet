/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 主从进程通信
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

struct RpcPkgHdr {
    int mod;
    int ret;
    size_t len;
};

struct RpcPkg {
    int magicNumber;
    int reserved;
    struct RpcPkgHdr hdr;
    uint8_t data[RPC_MESSAGE_SIZE];
};

static RpcHandler g_handlerDic[KNET_CONNECT_EVENT_MAX][KNET_MOD_MAX] = { 0 };

static KNET_SpinLock g_rpcLock = {
    .value = KNET_SPIN_UNLOCKED_VALUE,
};

static int RpcSendTo(int fd, enum KnetModType mod, struct KnetRpcMessage *request)
{
    int ret = 0;
    struct RpcPkgHdr hdr = { 0 };
    struct RpcPkg rpcPkg = { 0 };
    hdr.mod = mod;
    hdr.ret = request->ret;
    hdr.len = request->len;
    rpcPkg.magicNumber = RPC_MAGIC_NUMBER;
    rpcPkg.hdr = hdr;

    ret = memcpy_s(rpcPkg.data, RPC_MESSAGE_SIZE, request->data, request->len);
    if (ret != 0) {
        KNET_ERR("K-NET rpc memcpy data failed, ret: %d, dataLen: %zu", ret, request->len);
        return RPC_DISCONNECT;
    }

    size_t len = sizeof(rpcPkg) - RPC_MESSAGE_SIZE + hdr.len;
    int index = 0;
    while (len > 0) {
        ret = send(fd, &rpcPkg + index, len, MSG_NOSIGNAL);
        if (ret < 0) {
            KNET_ERR("K-NET rpc send rpcPkg failed, ret: %d, dataLen: %zu", ret, len);
            return RPC_EXCEPTION;
        } else if (ret == 0) {
            KNET_ERR("K-NET rpc send rpcPkg failed, ret: %d, dataLen: %zu", ret, len);
            return RPC_DISCONNECT;
        }
        index = index + ret;
        len = len - (size_t)ret;
    }
    return RPC_SUCCESS;
}

static int HandleRecvBytes(ssize_t bytesRead)
{
    if (bytesRead < 0) {
        return RPC_EXCEPTION;
    } else if (bytesRead == 0) {
        return RPC_DISCONNECT;
    } else {
        return RPC_SUCCESS;
    }
}

static int RpcRecvFrom(int fd, enum KnetModType *mod, struct KnetRpcMessage *request)
{
    struct RpcPkgHdr hdr = { 0 };
    uint32_t magicNumber = 0;
    ssize_t bytesRead = 0;
    int count = 0;

    while (magicNumber != RPC_MAGIC_NUMBER) {
        count = count + 1;
        if (count > MAX_RETRY_COUNT) {
            KNET_ERR("Rpc: The number of MAX_RETRY_COUNT is exceeded");
            return OVER_MAX_RETRY_CNT;
        }
        bytesRead = recv(fd, &magicNumber, sizeof(magicNumber), MSG_WAITALL);
        if (bytesRead == 0) {
            return RPC_DISCONNECT;
        } else if (bytesRead < 0) {
            KNET_ERR("K-NET rpc recv mn failed, bytesRead: %zd", bytesRead);
            return RPC_EXCEPTION;
        }
    }
    int reserved = 0;
    bytesRead = recv(fd, &reserved, sizeof(reserved), MSG_WAITALL);
    if (bytesRead <= 0) {
        KNET_ERR("K-NET rpc recv reserve failed, bytesRead: %zd", bytesRead);
        return HandleRecvBytes(bytesRead);
    }
    bytesRead = recv(fd, &hdr, sizeof(hdr), MSG_WAITALL);
    if (bytesRead <= 0) {
        KNET_ERR("K-NET rpc recv hdr failed, bytesRead: %zd", bytesRead);
        return HandleRecvBytes(bytesRead);
    }

    if (hdr.len > 0 && hdr.len <= RPC_MESSAGE_SIZE) {
        bytesRead = recv(fd, request->data, hdr.len, MSG_WAITALL);
        if (bytesRead <= 0) {
            KNET_ERR("K-NET rpc recv data failed, bytesRead: %zd", bytesRead);
            return HandleRecvBytes(bytesRead);
        }
    } else {
        KNET_ERR("K-NET rpc recv data failed, len: %zu", hdr.len);
        return RPC_DISCONNECT;
    }

    request->len = hdr.len;
    request->ret = hdr.ret;
    *mod = hdr.mod;

    return RPC_SUCCESS;
}

int KNET_RegServer(enum ConnectEvent event, enum KnetModType mod, RpcHandler handler)
{
    if (event >= KNET_CONNECT_EVENT_MAX || event < 0 || mod >= KNET_MOD_MAX || mod < 0 || handler == NULL) {
        KNET_ERR("K-NET rpc register handler failed, event: %d, mod: %d", event, mod);
        return -1;
    }
    if (g_handlerDic[event][mod] != NULL) {
        KNET_ERR("Rpc: event%dmod%d has been registered", event, mod);
        return -1;
    }
    g_handlerDic[event][mod] = handler;
    return 0;
}

void KNET_DesServer(enum ConnectEvent event, enum KnetModType mod)
{
    if (event >= KNET_CONNECT_EVENT_MAX || event < 0 || mod >= KNET_MOD_MAX || mod < 0) {
        KNET_ERR("K-NET rpc destroy handler failed, event: %d, mod: %d", event, mod);
        return;
    }
    g_handlerDic[event][mod] = NULL;
}

static void NewConnection(int epollFd, int serverFd)
{
    int clientFd = accept(serverFd, NULL, NULL);
    if (clientFd == -1) {
        KNET_ERR("K-NET rpc server socket accept failed, serverFd: %d, error: %d", serverFd, errno);
        return;
    }

    struct epoll_event event = { 0 };
    event.events = EPOLLIN;
    event.data.fd = clientFd;
    int ret = epoll_ctl(epollFd, EPOLL_CTL_ADD, clientFd, &event);
    if (ret == -1) {
        KNET_ERR("K-NET rpc server socket EPOLL_CTL_ADD failed, epollFd: %d, eventFd: %d, error: %d", epollFd, clientFd,
                 errno);
        close(clientFd);
        return;
    }

    for (int i = 0; i < KNET_MOD_MAX; ++i) {
        RpcHandler function = g_handlerDic[KNET_CONNECT_EVENT_NEW][i];
        if (function == NULL) {
            continue;
        }
        function(event.data.fd, NULL, NULL);
    }
}

static int RpcHandleRequest(int fd, enum KnetModType mod, struct KnetRpcMessage *request,
                            struct KnetRpcMessage *response)
{
    int ret = 0;
    if (mod >= KNET_MOD_MAX || mod < 0) {
        KNET_ERR("invalid mod index %d while request handle", mod);
        return -1;
    }
    RpcHandler function = g_handlerDic[KNET_CONNECT_EVENT_REQUEST][mod];
    if (function == NULL) {
        KNET_ERR("K-NET rpc HandleRequest failed, handler is NULL");
        return -1;
    }

    ret = function(fd, request, response);
    if (ret < 0) {
        KNET_ERR("K-NET rpc HandleRequest failed");
    }
    response->ret = ret;
    ret = RpcSendTo(fd, mod, response);
    return ret;
}

static void RpcHandleDisconnect(int epollFd, struct epoll_event *event)
{
    for (int i = 0; i < KNET_MOD_MAX; ++i) {
        RpcHandler function = g_handlerDic[KNET_CONNECT_EVENT_DISCONNECT][i];
        if (function == NULL) {
            continue;
        }
        function(event->data.fd, NULL, NULL);
    }

    int ret = epoll_ctl(epollFd, EPOLL_CTL_DEL, event->data.fd, NULL);
    if (ret == -1) {
        KNET_ERR("K-NET rpc server socket EPOLL_CTL_DEL failed, epollFd: %d, eventFd: %d, error: %d", epollFd,
                 event->data.fd, errno);
    }
    close(event->data.fd);
    KNET_INFO("K-NET rpc disconnect");
}

static int SetSocketPermission(void)
{
    int res = chmod(SOCKET_PATH, S_IRUSR | S_IWUSR);
    if (res == -1) {
        KNET_ERR("K-NET rpc chmod socket failed");
        return -1;
    }
    return 0;
}

static int RpcSocketInit(void)
{
    int serverFd = 0;
    int ret = 0;

    serverFd = socket(AF_UNIX, SOCK_STREAM, 0);
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

    ret = unlink(SOCKET_PATH);
    if (ret != 0) {
        if (errno != ENOENT) {
            KNET_ERR("K-NET rpc unlink sock failed, error: %d", errno);
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

static int RpcEpollInit(int listenFd)
{
    int epollFd = epoll_create(RPC_CONNECT_MAX);
    struct epoll_event event = { 0 };

    if (epollFd == -1) {
        KNET_ERR("K-NET rpc server creat epollFd failed");
        return -1;
    }

    event.data.fd = listenFd;
    event.events = EPOLLIN;
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, listenFd, &event) != 0) {
        KNET_ERR("K-NET rpc server socket EPOLL_CTL_ADD failed, epollFd: %d, listenFd: %d, error: %d",
            epollFd, listenFd, errno);
        close(epollFd);
        return -1;
    }

    return epollFd;
}

static void RpcHandleTask(int epollFd, struct epoll_event *event)
{
    struct KnetRpcMessage knetRpcRequest = { 0 };
    struct KnetRpcMessage knetRpcResponse = { 0 };
    enum KnetModType mod;
    int ret = 0;

    ret = RpcRecvFrom(event->data.fd, &mod, &knetRpcRequest);
    if (ret == RPC_SUCCESS) {
        knetRpcResponse.len = RPC_MESSAGE_SIZE;
        ret = RpcHandleRequest(event->data.fd, mod, &knetRpcRequest, &knetRpcResponse);
        if (ret < 0) {
            RpcHandleDisconnect(epollFd, event);
        }
    } else {
        RpcHandleDisconnect(epollFd, event);
    }
}

static int RpcEpollWait(int epollFd, struct epoll_event *events, int maxEvents)
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

static void RpcEpollTask(int serverFd, int epollFd)
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
                NewConnection(epollFd, serverFd);
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

static int RpcMsgSendRecv(int fd, enum KnetModType mod, struct KnetRpcMessage *request, struct KnetRpcMessage *response)
{
    int ret = 0;
    enum KnetModType innerMod;

    ret = RpcSendTo(fd, mod, request);
    if (ret != RPC_SUCCESS) {
        return ret;
    }

    ret = RpcRecvFrom(fd, &innerMod, response);
    return ret;
}

int KNET_RpcClient(enum KnetModType mod, struct KnetRpcMessage *knetRpcRequest, struct KnetRpcMessage *knetRpcResponse)
{
    static int clientFd = -1;
    int ret = 0;
    KNET_SpinlockLock(&g_rpcLock);
    if (clientFd == -1) {
        clientFd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (clientFd == -1) {
            KNET_ERR("K-NET rpc client socket get sockfd failed, error: %d", errno);
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
            KNET_ERR("K-NET rpc client socket failed to be connected, clientFd: %d, error: %d", clientFd, errno);
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