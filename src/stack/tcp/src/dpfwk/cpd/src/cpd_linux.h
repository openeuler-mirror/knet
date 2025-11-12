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

#ifndef CPD_LINUX_H
#define CPD_LINUX_H

#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include "utils_base.h"
#include "cpd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CPD_NDA_RTA(r) ((struct rtattr *)(((char *)(r)) + NLMSG_ALIGN(sizeof(struct ndmsg))))

typedef struct {
    void *sysHandle;
    int (*real_socket)(int domain, int type, int protocol);
    int (*real_close)(int fd);
    int (*real_bind)(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
    int (*real_setsockopt)(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
    ssize_t (*real_fcntl)(int fd, int cmd, ...);
    int (*real_ioctl)(int sockfd, int request, void *p);
    ssize_t (*real_sendmsg)(int sockfd, const struct msghdr *pstMsg, int flags);
    ssize_t (*real_recvmsg)(int sockfd, struct msghdr *msg, int flags);
    ssize_t (*real_write)(int fd, const void *buf, size_t count);
    ssize_t (*real_read)(int fd, void *buf, size_t count);
    ssize_t (*real_sendto)(int sockfd, const void *buf, size_t len,
        int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
} CpdSysHandle;

/* netlink请求消息 */
typedef struct {
    struct nlmsghdr nlMsgHdr;
    union {
        struct ndmsg arpMsg; /* arp请求消息头 */
        struct rtmsg routeMsg; /* route请求消息头 */
        struct ifinfomsg ifInfoMsg; /* 接口请求消息头 */
    };
} NetlinkReqMsg_t;

typedef struct CpdArpOpNode {
    LIST_ENTRY(CpdArpOpNode) node;
    uint32_t ifindex;
    uint32_t ip;
    DP_EthAddr_t mac;
    uint16_t state;
    uint8_t type;
    uint8_t resv;
} CpdArpOpList_t;

typedef LIST_HEAD(, CpdArpOpNode) CpdArpOpNodeHead;

extern CpdArpOpNodeHead g_cpdArpOpList;

CpdSysHandle* GetCpdSysHandle(void);

int SysCallInit(void);

void CloseNetlinkFd(void);

int CpdCpInit(void);

int CPD_SyncTable(SycnTableEntry *entryList, uint32_t* entryNum);

int CPD_SendPkt(uint32_t ifindex, const void* buf, uint32_t len);

int CPD_RcvPkt(uint32_t ifindex, void* buf, uint32_t len);

int CPD_TblMissHandle(int type, void* srcAddr, void* dstAddr);

int CPD_TapAlloc(DP_Netdev_t *dev);

int CPD_TapFree(int tapFd);


#ifdef __cplusplus
}
#endif

#endif
