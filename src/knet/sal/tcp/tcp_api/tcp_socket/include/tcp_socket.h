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

#ifndef K_NET_TCP_SOCKET_H
#define K_NET_TCP_SOCKET_H

#include <sys/socket.h>

#include "dp_in_api.h"

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief 检查文件描述符是否有效
 *
 * @param [IN] int。osFd 操作系统文件描述符
 * @param [IN] DP_Sockaddr*。addr 地址
 * @param [IN] DP_Socklen_t。addrlen 地址长度
 * @return int 成功返回0，否则返回-1
 */
int KNET_PreBind(void* userData, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen);

/* 接口入参、返回值描述同标准posix接口一致 */
int KNET_DpSocket(int domain, int type, int protocol);
int KNET_DpBind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int KNET_DpListen(int sockfd, int backlog);
int KNET_DpConnect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int KNET_DpGetpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int KNET_DpGetsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
ssize_t KNET_DpSend(int sockfd, const void *buf, size_t len, int flags);
ssize_t KNET_DpSendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr,
    socklen_t addrlen);
ssize_t KNET_DpWritev(int sockfd, const struct iovec *iov, int iovcnt);
ssize_t KNET_DpSendmsg(int sockfd, const struct msghdr *msg, int flags);
ssize_t KNET_DpRecv(int sockfd, void *buf, size_t len, int flags);
ssize_t KNET_DpRecvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr,
    socklen_t *addrlen);
ssize_t KNET_DpRecvmsg(int sockfd, struct msghdr *msg, int flags);
ssize_t KNET_DpReadv(int sockfd, const struct iovec *iov, int iovcnt);
int KNET_DpGetsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
int KNET_DpSetsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
int KNET_DpFcntl(int sockfd, int cmd, ...);
int KNET_DpFcntl64(int sockfd, int cmd, ...);
int KNET_DpAccept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int KNET_DpAccept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags);
int KNET_DpClose(int sockfd);
int KNET_DpShutdown(int sockfd, int how);
ssize_t KNET_DpRead(int sockfd, void *buf, size_t count);
ssize_t KNET_DpWrite(int sockfd, const void *buf, size_t count);
int KNET_DpIoctl(int sockfd, unsigned long request, ...);

#ifdef __cplusplus
}
#endif
#endif // K_NET_TCP_SOCKET_H