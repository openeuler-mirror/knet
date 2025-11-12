/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: socket相关基础信息定义
 */

#ifndef DP_SOCKET_TYPES_API_H
#define DP_SOCKET_TYPES_API_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup sock_types Sockets types
 * @ingroup base
 */

#define DP_PF_INET    2
#define DP_PF_INET6   10
#define DP_PF_NETLINK 16
#define DP_PF_PACKET  17

#define DP_AF_INET    DP_PF_INET
#define DP_AF_INET6   DP_PF_INET6
#define DP_AF_NETLINK DP_PF_NETLINK
#define DP_AF_PACKET  DP_PF_PACKET

/**
 * @ingroup sock_types
 * @brief stream socket类型
 */
#define DP_SOCK_STREAM   1

/**
 * @ingroup sock_types
 * @brief dgram socket类型
 */
#define DP_SOCK_DGRAM    2

/**
 * @ingroup sock_types
 * @brief raw socket类型
 */
#define DP_SOCK_RAW      3

/**
 * @ingroup sock_types
 * @brief packet socket类型
 */
#define DP_SOCK_PACKET   10

/**
 * @ingroup sock_types
 * @brief nonblock标识
 */
#define DP_SOCK_NONBLOCK 00004000

#define DP_MSG_PEEK      0x02
#define DP_MSG_DONTROUTE 0x4
#define DP_MSG_DONTWAIT  0x40
#define DP_FIONBIO       0x5421

#define DP_SOL_IP     0

/**
 * @ingroup sock_types
 * @brief get/set sockopt socket选项类型
 */
#define DP_SOL_SOCKET 1
#define DP_SOL_PACKET 263

/**
 * @ingroup sock_types
 * @brief socket reuseaddr选项
 */
#define DP_SO_REUSEADDR    2

/**
 * @ingroup sock_types
 * @brief socket error选项
 */
#define DP_SO_ERROR        4

/**
 * @ingroup sock_types
 * @brief socket broadcast选项
 */
#define DP_SO_BROADCAST    6

/**
 * @ingroup sock_types
 * @brief socket sndbuf选项
 */
#define DP_SO_SNDBUF       7

/**
 * @ingroup sock_types
 * @brief socket rcvbuf选项
 */
#define DP_SO_RCVBUF       8

/**
 * @ingroup sock_types
 * @brief socket keepalive选项
 */
#define DP_SO_KEEPALIVE    9

/**
 * @ingroup sock_types
 * @brief socket linger选项
 */
#define DP_SO_LINGER       13

/**
 * @ingroup sock_types
 * @brief socket reuseport选项
 */
#define DP_SO_REUSEPORT    15

/**
 * @ingroup sock_types
 * @brief socket rcvtimeo选项
 */
#define DP_SO_RCVTIMEO     20

/**
 * @ingroup sock_types
 * @brief socket sndtimeo选项
 */
#define DP_SO_SNDTIMEO     21

/**
 * @ingroup sock_types
 * @brief socket bindtodevice选项
 */
#define DP_SO_BINDTODEVICE 25

/**
 * @ingroup sock_types
 * @brief shutdown标识 半关闭读
 */
#define DP_SHUT_RD 0

/**
 * @ingroup sock_types
 * @brief shutdown标识 半关闭写
 */
#define DP_SHUT_WR 1

/**
 * @ingroup sock_types
 * @brief shutdown标识 读写全关闭
 */
#define DP_SHUT_RDWR 2

/**
 * @ingroup sock_types
 * @brief fcntl命令 GETFL标识
 */
#define DP_F_GETFL 3

/**
 * @ingroup sock_types
 * @brief fcntl命令 SETFL标识
 */
#define DP_F_SETFL 4

/**
 * @ingroup sock_types
 * saFamily类型
 */
typedef unsigned short int DP_SaFamily_t;

/**
 * @ingroup sock_types
 * socklen类型
 */
typedef unsigned int       DP_Socklen_t;

/**
 * @ingroup sock_types
 * sockaddr信息
 */
typedef struct DP_Sockaddr {
    DP_SaFamily_t sa_family;
    char          sa_data[14];
} DP_Sockaddr_t;

/**
 * @ingroup sock_types
 * linger选项信息
 */
struct DP_Linger {
    int l_onoff;
    int l_linger;
};

/**
 * @ingroup sock_types
 * timeval选项信息
 */
struct DP_Timeval {
    long tv_sec;
    long tv_usec;
};

/**
 * @ingroup sock_types
 * iov报文信息
 */
struct DP_Iovec {
    void*  iov_base;
    size_t iov_len;
};

/**
 * @ingroup sock_types
 * msg头部信息
 */
struct DP_Msghdr {
    void*            msg_name;
    DP_Socklen_t     msg_namelen;
    struct DP_Iovec* msg_iov;
    size_t           msg_iovlen;
    void*            msg_control;
    size_t           msg_controllen;
    int              msg_flags;
};

/**
 * @ingroup sock_types
 * msg信息
 */
struct DP_Mmsghdr {
    struct DP_Msghdr msg_hdr;
    unsigned int     msg_len;
};

/**
 * @ingroup sock_types
 * cmsg信息
 */
struct DP_Cmsghdr {
    size_t cmsg_len;
    int    cmsg_level;
    int    cmsg_type;
};

#ifdef __cplusplus
}
#endif
#endif
