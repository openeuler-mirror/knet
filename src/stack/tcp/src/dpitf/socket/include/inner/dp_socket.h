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
#ifndef DP_SOCKET_H
#define DP_SOCKET_H

#include <stdio.h>

#include "dp_socket_types_api.h"

#ifdef __cplusplus
extern "C" {
#endif

int DP_Socket(int domain, int type, int protocol);

int DP_Connect(int sockfd, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen);

ssize_t DP_Send(int sockfd, const void* buf, size_t len, int flags);

ssize_t DP_Sendto(
    int sockfd, const void* buf, size_t len, int flags, const struct DP_Sockaddr* dstAddr, DP_Socklen_t addrlen);

ssize_t DP_Sendmsg(int sockfd, const struct DP_Msghdr* msg, int flags);

ssize_t DP_ZSendmsg(int sockfd, const struct DP_ZMsghdr* msg, int flags, ssize_t totalLen);

ssize_t DP_Write(int sockfd, const void *buf, size_t count);

ssize_t DP_Writev(int sockfd, const struct DP_Iovec *iov, int iovcnt);

ssize_t DP_Recv(int sockfd, void* buf, size_t len, int flags);

ssize_t DP_Recvfrom(
    int sockfd, void* buf, size_t len, int flags, struct DP_Sockaddr* srcAddr, DP_Socklen_t* addrlen);

ssize_t DP_Recvmsg(int sockfd, struct DP_Msghdr* msg, int flags);

ssize_t DP_ZRecvmsg(int sockfd, struct DP_ZMsghdr* msg, int flags);

ssize_t DP_Read(int sockfd, void *buf, size_t count);

ssize_t DP_Readv(int sockfd, const struct DP_Iovec *iov, int iovcnt);

int DP_Close(int fd);

int DP_Shutdown(int sockfd, int how);

int DP_Bind(int sockfd, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen);

int DP_Listen(int sockfd, int backlog);

int DP_Accept(int sockfd, struct DP_Sockaddr* addr, DP_Socklen_t* addrlen);

int DP_Getpeername(int sockfd, struct DP_Sockaddr* addr, DP_Socklen_t* addrlen);

int DP_Getsockname(int sockfd, struct DP_Sockaddr* addr, DP_Socklen_t* addrlen);

int DP_Ioctl(int fd, int request, void* arg);

int DP_Fcntl(int fd, int cmd, int val);

#ifdef __cplusplus
}
#endif
#endif
