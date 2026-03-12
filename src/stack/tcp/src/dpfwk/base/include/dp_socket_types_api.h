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
/**
 * @file dp_socket_types_api.h
 * @brief socket相关基础信息定义
 */

#ifndef DP_SOCKET_TYPES_API_H
#define DP_SOCKET_TYPES_API_H

#include <stddef.h>
#include <stdint.h>

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
#define DP_SOCK_CLOEXEC  02000000
#define DP_FD_CLOEXEC    1
#define DP_MSG_PEEK      0x02
#define DP_MSG_DONTROUTE 0x4
#define DP_MSG_DONTWAIT  0x40
#define DP_MSG_ZEROCOPY  0x40000000
#define DP_FIONBIO       0x5421
#define DP_FIONREAD      0x541B
#define DP_MSG_NOSIGNAL  0x4000
#define DP_MSG_MORE      0x8000

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
 * @brief socket SO_TYPE选项
 */
#define DP_SO_TYPE    3

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
 * @brief socket priority选项
 */
#define DP_SO_PRIORITY    12

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
 * @brief socket rcvbuf选项
 */
#define DP_SO_RCVLOWAT     18

/**
 * @ingroup sock_types
 * @brief socket SO_SNDLOWAT选项
 */
#define DP_SO_SNDLOWAT     19

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
 * @brief socket SO_TIMESTAMP选项
 */
#define DP_SO_TIMESTAMP    29

/**
 * @ingroup sock_types
 * @brief socket SO_ACCEPTCONN选项
 */
#define DP_SO_ACCEPTCONN    30

/**
 * @ingroup sock_types
 * @brief socket SO_RCVBUFFORCE选项
 */
#define DP_SO_RCVBUFFORCE    33

/**
 * @ingroup sock_types
 * @brief socket protocol选项
 */
#define DP_SO_PROTOCOL 38

/**
 * @ingroup sock_types
 * @brief socket userdata选项
 */
#define DP_SO_USERDATA 10000000

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
 * @brief fcntl命令 GETFD标识
 */
#define DP_F_GETFD 1

/**
 * @ingroup sock_types
 * @brief fcntl命令 SETFD标识
 */
#define DP_F_SETFD 2

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
 * @brief tcp nodelay选项
 */
#define DP_TCP_NODELAY        1

/**
 * @ingroup sock_types
 * @brief tcp mss选项
 */
#define DP_TCP_MAXSEG         2

/**
 * @ingroup sock_types
 * @brief tcp cork选项
 */
#define DP_TCP_CORK           3

/**
 * @ingroup sock_types
 * @brief tcp keepidle选项，Start keeplives after this period
 */
#define DP_TCP_KEEPIDLE       4

/**
 * @ingroup sock_types
 * @brief tcp keepintvl选项，Interval between keepalives
 */
#define DP_TCP_KEEPINTVL      5

/**
 * @ingroup sock_types
 * @brief tcp keepcnt选项，Number of keepalives before death
 */
#define DP_TCP_KEEPCNT        6

/**
 * @ingroup sock_types
 * @brief tcp syncnt选项，Number of SYN retransmits
 */
#define DP_TCP_SYNCNT         7

/**
 * @ingroup sock_types
 * @brief tcp defer accept选项
 */
#define DP_TCP_DEFER_ACCEPT   9

/**
 * @ingroup sock_types
 * @brief tcp info选项
 */
#define DP_TCP_INFO           11

/**
 * @ingroup sock_types
 * @brief tcp quick ack选项
 */
#define DP_TCP_QUICKACKNUM      12

/**
 * @ingroup sock_types
 * @brief tcp congestion选项
 */
#define DP_TCP_CONGESTION     13

/**
 * @ingroup sock_types
 * @brief tcp user timeout选项
 */
#define DP_TCP_USER_TIMEOUT   18

/**
 * @ingroup sock_types
 * @brief tcp keepidle limit选项，用于限制连续keepidle超时的次数
 */
#define DP_TCP_KEEPIDLE_LIMIT   20000000


/**
 * @ingroup sock_types
 * @brief tcp bbr选项，设置拥塞控制相关参数
 */
#define DP_TCP_BBR_CONGESTION_PARAM 20000003

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
 * 释放回调类型
 */
typedef void (*DP_ZIovFreeHook_t)(void* addr, void* param);

/**
 * @ingroup sock_types
 * zero copy iovec类型
 */
struct DP_ZIovec {
    void*             iov_base;
    size_t            iov_len;
    void*             cb;
    DP_ZIovFreeHook_t freeCb;
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
 * zero copy版本的msg头部信息
 */
struct DP_ZMsghdr {
    void*             msg_name;
    DP_Socklen_t      msg_namelen;
    struct DP_ZIovec* msg_iov;
    size_t            msg_iovlen;
    void*             msg_control;
    size_t            msg_controllen;
    int               msg_flags;
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

/**
 * @ingroup sock_types
 * BBR拥塞控制参数信息
 */
struct DP_TcpBbrParam {
    uint8_t probeRttCwnd;      /**< PROBERTT阶段拥塞控制窗口，单位MSS个数，范围【4-255】 */
    uint8_t probeRttTimeOut;   /**< 其它阶段进入PROBERTT阶段最大超时时间，单位为s，范围【10-255】 */
    uint8_t probeRttCycle;     /**< PROBERTT阶段探测超时时间，单位为ms，范围【10-250】 */
    uint8_t ignoreRecovery;    /**< BBR算法特殊优化点，是否跳过RECOVERY阶段降窗操作，缺省不跳过 */
    uint8_t dismissLT;         /**< BBR算法特殊优化点，是否关闭LT探测，缺省不关闭 */
    uint8_t incrFactor;        /**< 增益斜率设置为 33%   提高代码增速 */
    uint8_t delProbeRtt;       /**< 优化BBR算法，裁剪PROBERTT阶段 */
    uint8_t reserve;
};

/**
 * @ingroup sock_types
 * IpRecvErr错误栈
 */
struct DP_SockExtendedErr {
    int32_t	 ee_errno;
    uint8_t	 ee_origin;
    uint8_t	 ee_type;
    uint8_t	 ee_code;
    uint8_t	 ee_pad;
    uint32_t ee_info;
    uint32_t ee_data;
};

/**
 * @ingroup sock_types
 * IpRecvErr错误栈origin
 */
#define DP_SO_EE_ORIGIN_NONE	0
#define DP_SO_EE_ORIGIN_LOCAL	1
#define DP_SO_EE_ORIGIN_ICMP	2
#define DP_SO_EE_ORIGIN_ICMP6	3
#define DP_SO_EE_ORIGIN_TXSTATUS	4
#define DP_SO_EE_ORIGIN_TIMESTAMPING DP_SO_EE_ORIGIN_TXSTATUS

/**
 * @ingroup sock_types
 * cmsg操作宏
 */
#define DP_CMSG_NXTHDR1(ctl, len, cmsg) DP_CmsgNxtHdr1((ctl), (len), (cmsg))
#define DP_CMSG_NXTHDR(mhdr, cmsg) DP_CmsgNxtHdr((mhdr), (cmsg))
#define DP_CMSG_ALIGN(len) (((len) + sizeof(long) - 1) & ~(sizeof(long) - 1))
#define DP_CMSG_DATA(cmsg)	((void *)((char *)(cmsg) + DP_CMSG_ALIGN(sizeof(struct DP_Cmsghdr))))
#define DP_CMSG_SPACE(len) (DP_CMSG_ALIGN(sizeof(struct DP_Cmsghdr)) + DP_CMSG_ALIGN(len))
#define DP_CMSG_LEN(len) (DP_CMSG_ALIGN(sizeof(struct DP_Cmsghdr)) + (len))
#define DP_CMSG_FIRSTHDR1(ctl, len) ((len) >= sizeof(struct DP_Cmsghdr) ? \
				  (struct DP_Cmsghdr *)(ctl) : \
				  (struct DP_Cmsghdr *)NULL)
#define DP_CMSG_FIRSTHDR(msg)	DP_CMSG_FIRSTHDR1((msg)->msg_control, (msg)->msg_controllen)
#define DP_CMSG_OK(mhdr, cmsg) ((cmsg)->cmsg_len >= sizeof(struct DP_Cmsghdr) && \
			      (cmsg)->cmsg_len <= (unsigned long) \
                ((mhdr)->msg_controllen - \
			       ((char *)(cmsg) - (char *)(mhdr)->msg_control)))

static inline struct DP_Cmsghdr* DP_CmsgNxtHdr1(void* ctl, size_t size, struct DP_Cmsghdr* cmsg)
{
    struct DP_Cmsghdr* ptr;
    ptr = (struct DP_Cmsghdr *)(((unsigned char *)cmsg) + DP_CMSG_ALIGN(cmsg->cmsg_len));
    if ((unsigned long)((char*)(ptr + 1) - (char*)ctl) > size) {
        return (struct DP_Cmsghdr *)0;
    }
    return ptr;
}

static inline struct DP_Cmsghdr* DP_CmsgNxtHdr(struct DP_Msghdr *msg, struct DP_Cmsghdr *cmsg)
{
    return DP_CmsgNxtHdr1(msg->msg_control, msg->msg_controllen, cmsg);
}

#ifdef __cplusplus
}
#endif
#endif
