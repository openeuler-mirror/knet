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

#ifndef DP_NETLINK_H
#define DP_NETLINK_H

#include <stdint.h>

#include "dp_socket_types_api.h"

// 对应：netlink/rtnetlink的所有信息

#ifdef __cplusplus
extern "C" {
#endif

#define DP_NETLINK_ROUTE 0

struct DP_SockaddrNl {
    DP_SaFamily_t nl_family; /* AF_NETLINK   */
    uint16_t        nl_pad; /* zero         */
    uint32_t        nl_pid; /* port ID      */
    uint32_t        nl_groups; /* multicast groups mask */
};

struct DP_Nlmsghdr {
    uint32_t nlmsg_len; /* Length of message including header */
    uint32_t nlmsg_type; /* Message content */
    uint32_t nlmsg_flags; /* Additional flags */
    uint32_t nlmsg_seq; /* Sequence number */
    uint32_t nlmsg_pid; /* Sending process port ID */
};

struct DP_Nlmsgerr {
    int                  error;
    struct DP_Nlmsghdr msg;
};

#define DP_NLMSG_NOOP    0x1 /* Nothing.             */
#define DP_NLMSG_ERROR   0x2 /* Error                */
#define DP_NLMSG_DONE    0x3 /* End of a dump        */
#define DP_NLMSG_OVERRUN 0x4 /* Data lost            */

#define DP_NLMSG_ALIGNTO     4U
#define DP_NLMSG_ALIGN(len)  (((len) + DP_NLMSG_ALIGNTO - 1) & ~(DP_NLMSG_ALIGNTO - 1))
#define DP_NLMSG_HDRLEN      ((int)DP_NLMSG_ALIGN(sizeof(struct DP_Nlmsghdr)))
#define DP_NLMSG_LENGTH(len) ((len) + DP_NLMSG_HDRLEN)
#define DP_NLMSG_SPACE(len)  DP_NLMSG_ALIGN(DP_NLMSG_LENGTH(len))
#define DP_NLMSG_DATA(nlh)   ((void*)(((char*)(nlh)) + DP_NLMSG_LENGTH(0)))
#define DP_NLMSG_NEXT(nlh, len)                 \
    ((len) -= DP_NLMSG_ALIGN((nlh)->nlmsg_len), \
        (struct DP_Nlmsghdr*)(((char*)(nlh)) + DP_NLMSG_ALIGN((nlh)->nlmsg_len)))
#define DP_NLMSG_OK(nlh, len)                                                                     \
    ((len) >= (int)sizeof(struct DP_Nlmsghdr) && (nlh)->nlmsg_len >= sizeof(struct DP_Nlmsghdr) \
        && (nlh)->nlmsg_len <= (len))
#define DP_NLMSG_PAYLOAD(nlh, len) ((nlh)->nlmsg_len - DP_NLMSG_SPACE((len)))

// rtnetlink

#define DP_RTM_NEWNEIGH 28
#define DP_RTM_DELNEIGH 29
#define DP_RTM_GETNEIGH 30

struct DP_Rtattr {
    unsigned short rta_len;
    unsigned short rta_type;
};

#define DP_RTA_ALIGNTO    4U
#define DP_RTA_ALIGN(len) (((len) + DP_RTA_ALIGNTO - 1) & ~(DP_RTA_ALIGNTO - 1))
#define DP_RTA_OK(rta, len)                                                                 \
    ((len) >= (int)sizeof(struct DP_Rtattr) && (rta)->rta_len >= sizeof(struct DP_Rtattr) \
        && (rta)->rta_len <= (len))
#define DP_RTA_NEXT(rta, attrlen)               \
    ((attrlen) -= DP_RTA_ALIGN((rta)->rta_len), \
        (struct DP_Rtattr*)(((char*)(rta)) + DP_RTA_ALIGN((rta)->rta_len)))
#define DP_RTA_LENGTH(len)  (DP_RTA_ALIGN(sizeof(struct DP_Rtattr)) + (len))
#define DP_RTA_SPACE(len)   DP_RTA_ALIGN(DP_RTA_LENGTH(len))
#define DP_RTA_DATA(rta)    ((void*)(((char*)(rta)) + DP_RTA_LENGTH(0)))
#define DP_RTA_PAYLOAD(rta) ((int)((rta)->rta_len) - DP_RTA_LENGTH(0))

#define DP_RTMGRP_NEIGH 4

// neighbor
struct DP_Ndmsg {
    uint8_t  ndm_family;
    uint8_t  ndm_pad1;
    uint16_t ndm_pad2;
    int32_t  ndm_ifindex;
    uint16_t ndm_state;
    uint8_t  ndm_flags;
    uint8_t  ndm_type;
};

#define DP_NUD_INCOMPLETE 0x01
#define DP_NUD_REACHABLE  0x02
#define DP_NUD_STALE      0x04
#define DP_NUD_DELAY      0x08
#define DP_NUD_PROBE      0x10
#define DP_NUD_FAILED     0x20

#define DP_ND_RTA(x) ((struct DP_Rtattr*)(((char*)(x)) + DP_NLMSG_ALIGN(sizeof(struct DP_Ndmsg))))

#ifdef __cplusplus
}
#endif
#endif
