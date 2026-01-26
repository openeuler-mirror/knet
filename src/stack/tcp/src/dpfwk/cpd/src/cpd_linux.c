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
#include <fcntl.h>
#include <dlfcn.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <sys/ioctl.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/if_tun.h>

#include <securec.h>

#include "dp_socket_types_api.h"
#include "dp_tbm.h"

#include "dp_ip.h"
#include "dp_icmp.h"
#include "dp_ethernet.h"

#include "utils_base.h"
#include "utils_log.h"
#include "shm.h"
#include "netdev.h"
#include "cpd_core.h"
#include "cpd_linux.h"

#define CPD_CP_NETLINK_MAX_RECV_BUF 65535
#define SYS_LIB_NAME "/usr/lib64/libc.so.6"
#define MAX_NETLINK_PKT_CNT 128 // 一次最大接收netlink消息最大值

static CpdSysHandle g_cpdSysHandle = { 0 };
int g_netlinkfd;

#define DP_TUN_DEV "/dev/net/tun"
#define INIT_FUNCTION(func, ret) do { \
        g_cpdSysHandle.real_##func = dlsym(g_cpdSysHandle.sysHandle, #func); \
        if (g_cpdSysHandle.real_##func == NULL) { \
            (ret) = -1; \
        } \
} while (0)

CpdSysHandle* GetCpdSysHandle(void)
{
    return &g_cpdSysHandle;
}

/* 表项消息属性解析 */
static void CpdParseRtAttr(struct rtattr *rtAttr[], int maxAttrCount, struct rtattr *origRtAttr, int attrLen)
{
    (void)memset_s(rtAttr, sizeof(char *) * maxAttrCount, 0, sizeof(char *) * maxAttrCount);
    for (; RTA_OK(origRtAttr, attrLen); origRtAttr = RTA_NEXT(origRtAttr, attrLen)) {
        if (origRtAttr->rta_type < maxAttrCount) {
            rtAttr[origRtAttr->rta_type] = origRtAttr;
        }
    }
}

static void CpdAnalysisNdInfo(struct nlmsghdr *nlMsgHdr, struct rtattr *rtAttr[], struct ndmsg *ndMsg)
{
    uint32_t dstLen;
    uint8_t tempType;

    if (nlMsgHdr->nlmsg_type == RTM_NEWNEIGH) {
        tempType = DP_NEW_ND;
    } else if (nlMsgHdr->nlmsg_type == RTM_DELNEIGH) {
        tempType = DP_DEL_ND;
    } else {
        DP_LOG_INFO("Nlmsg analysis skipped with unknown nlmsg_type.");
        return;
    }

    CpdNdOpList_t *ndOpItem = SHM_MALLOC(sizeof(CpdNdOpList_t), MOD_CPD, DP_MEM_FREE);
    if (ndOpItem == NULL) {
        DP_LOG_ERR("Malloc memory failed for ndOpItem.");
        return;
    }
    (void)memset_s(ndOpItem, sizeof(CpdNdOpList_t), 0, sizeof(CpdNdOpList_t));

    ndOpItem->type = tempType;
    ndOpItem->ifindex = (uint32_t)ndMsg->ndm_ifindex;
    ndOpItem->state = ndMsg->ndm_state;

    dstLen = (uint32_t)(RTA_PAYLOAD(rtAttr[NDA_DST]));
    if ((ndMsg->ndm_family == AF_INET) && (dstLen == sizeof(uint32_t))) {
        ndOpItem->family =  DP_AF_INET;
        ndOpItem->ip.ipv4 = *DP_TBM_ATTR_GET_VAL(rtAttr[NDA_DST], DP_InAddr_t);
        if (rtAttr[NDA_LLADDR] != NULL) {
            DP_MAC_COPY(&ndOpItem->mac, DP_TBM_ATTR_GET_VAL(rtAttr[NDA_LLADDR], DP_EthAddr_t));
        }
        LIST_INSERT_TAIL(&g_cpdNdOpList, ndOpItem, node);
        return;
    }
    SHM_FREE(ndOpItem, DP_MEM_FREE);
    DP_LOG_INFO("Ndmsg analysis skipped with unknown ndm_family or bad len.");
}

static void CpdAnalysisNetLinkMsg(uint8_t *recvBuf, uint32_t msgLen)
{
    struct nlmsghdr *nlMsgHdr = (struct nlmsghdr *)recvBuf;
    struct ndmsg *ndMsg;
    struct rtattr *rtAttr[NDA_MAX + 1] = {0};

    for (; NLMSG_OK(nlMsgHdr, msgLen); nlMsgHdr = NLMSG_NEXT(nlMsgHdr, msgLen)) {
        ndMsg = (struct ndmsg *)NLMSG_DATA(nlMsgHdr);
        CpdParseRtAttr(rtAttr, (NDA_MAX + 1), CPD_NDA_RTA(ndMsg),
                       (int)(nlMsgHdr->nlmsg_len - NLMSG_LENGTH(sizeof(*ndMsg))));
        if (rtAttr[NDA_DST] == NULL) {
            DP_LOG_INFO("NetLinkmsg analysis skipped with dst ip attr null.");
            return;
        }
        CpdAnalysisNdInfo(nlMsgHdr, rtAttr, ndMsg);
    }
}

// recvmsg 到中转表
static void CpdProcNetlinkMsg(int nlFd)
{
    struct iovec vec;
    struct msghdr msgHdr = { 0 };
    struct sockaddr_nl sockAddrNl = { 0 };
    uint8_t* recvBuf = NULL;
    int msgLen = 0;

    /* 在recvmsg中填入数据，无需初始化 */
    recvBuf = SHM_MALLOC(CPD_CP_NETLINK_MAX_RECV_BUF, MOD_CPD, DP_MEM_FREE);
    if (recvBuf == NULL) {
        DP_LOG_ERR("Malloc memory failed for cpd recvBuf.");
        return;
    }

    vec.iov_base = (void*)recvBuf;
    vec.iov_len = CPD_CP_NETLINK_MAX_RECV_BUF;

    msgHdr.msg_name = &sockAddrNl;
    msgHdr.msg_namelen = (uint32_t)sizeof(sockAddrNl);
    msgHdr.msg_iov = &vec;
    msgHdr.msg_iovlen = 1;

    int cnt = 0;
    do {
        msgLen = (int)g_cpdSysHandle.real_recvmsg(nlFd, &msgHdr, 0);
        if (msgLen > 0) {
            CpdAnalysisNetLinkMsg(recvBuf, (uint32_t)msgLen);
        }
        if (msgLen < 0) {
            DP_ADD_ABN_STAT(DP_CPD_SYNC_TABLE_RECV_ERR);
        }
        cnt++;
    } while (msgLen > 0 && cnt < MAX_NETLINK_PKT_CNT);
    errno = 0;      // errno会被设置为EAGAIN。这里清零以免影响用户判断
    SHM_FREE(recvBuf, DP_MEM_FREE);
}

static int GetFirstNetlinkMsg(int nlFd)
{
    NetlinkReqMsg_t netlinkInfo;
    struct iovec vec;
    struct msghdr msgHdrInfo = {0};
    struct sockaddr_nl sockAddrNl = {0};

    (void)memset_s(&netlinkInfo, sizeof(NetlinkReqMsg_t), 0, sizeof(NetlinkReqMsg_t));
    netlinkInfo.nlMsgHdr.nlmsg_len = (uint32_t)(NLMSG_LENGTH(sizeof(struct ndmsg)));
    netlinkInfo.nlMsgHdr.nlmsg_type = RTM_GETNEIGH;
    netlinkInfo.nlMsgHdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;

    netlinkInfo.arpMsg.ndm_family = AF_UNSPEC;      // AF_UNSPEC表示未指定地址族
    netlinkInfo.arpMsg.ndm_type = 0xFF & ~NUD_NOARP;

    vec.iov_base = (void*)&netlinkInfo;
    vec.iov_len = netlinkInfo.nlMsgHdr.nlmsg_len;

    sockAddrNl.nl_family = AF_NETLINK;
    sockAddrNl.nl_groups = RTMGRP_NEIGH;

    msgHdrInfo.msg_iov = &vec;
    msgHdrInfo.msg_iovlen = 1;
    msgHdrInfo.msg_name = &sockAddrNl;
    msgHdrInfo.msg_namelen = (uint32_t)sizeof(sockAddrNl);

    if (g_cpdSysHandle.real_sendmsg(nlFd, &msgHdrInfo, 0) < 0) {
        DP_ADD_ABN_STAT(DP_CPD_SYNC_TABLE_SEND_ERR);
        return -1;
    }
    CpdProcNetlinkMsg(nlFd);
    return 0;
}

static void SyncCpdNdTable(SycnTableEntry *entry, uint32_t entryNum, uint32_t* currentCnt)
{
    CpdNdOpList_t *ndOpItem = LIST_FIRST(&g_cpdNdOpList);
    uint32_t temp = *currentCnt;
    while (ndOpItem != NULL && temp < entryNum) {
        entry[temp].type = ndOpItem->type;
        entry[temp].family = ndOpItem->family;
        entry[temp].ifindex = ndOpItem->ifindex;
        entry[temp].state = ndOpItem->state;
        TBM_IPADDR_COPY(&entry[temp].ndEntry.dst, &ndOpItem->ip);
        DP_MAC_COPY(&entry[temp].ndEntry.mac, &ndOpItem->mac);

        LIST_REMOVE(&g_cpdNdOpList, ndOpItem, node);
        SHM_FREE(ndOpItem, DP_MEM_FREE);
        ndOpItem = LIST_FIRST(&g_cpdNdOpList);
        temp++;
    }
    *currentCnt = temp;
}

static void CpdCreateIcmpPkt(uint8_t *data, uint32_t dataLen, const uint32_t src, const uint32_t dst)
{
    DP_IpHdr_t *ipHdr = (DP_IpHdr_t *)data;
    DP_IcmpHdr_t *icmpHdr = (DP_IcmpHdr_t *)(data + sizeof(DP_IpHdr_t));

    ipHdr->version = DP_IP_VERSION_IPV4;
    ipHdr->hdrlen  = sizeof(DP_IpHdr_t) >> 2;    // 2： 首部长度记录为 4字节 数
    ipHdr->tos    = 0;
    ipHdr->totlen = (uint16_t)UTILS_NTOHS(dataLen);
    ipHdr->ipid   = (uint16_t)UTILS_NTOHS((RAND_GEN() & 0xFFFF));
    ipHdr->off    = UTILS_HTONS(DP_IP_FRAG_DF);
    ipHdr->ttl    = DP_ICMP_TTL;
    ipHdr->type   = DP_IPPROTO_ICMP;
    ipHdr->chksum = 0;
    ipHdr->dst    = dst;
    ipHdr->src    = src;

    icmpHdr->type  = DP_ICMP_TYPE_ECHO;
    icmpHdr->code  = 0;
    icmpHdr->cksum = 0;
    icmpHdr->resv  = 0;
}

static int CpdSendIcmpToKernel(const uint8_t *data, uint32_t dataLen, const uint32_t dst)
{
    struct sockaddr_in sockAddrIn = {0};
    struct sockaddr sockAddr = {0};
    int sockOpt = 1;
    int ret = 0;

    int fd = g_cpdSysHandle.real_socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (fd < 0) {
        return -1;
    }

    if (g_cpdSysHandle.real_setsockopt(fd, IPPROTO_IP, IP_HDRINCL, &sockOpt, sizeof(sockOpt)) < 0) {
        goto err_exit;
    }

    sockAddrIn.sin_family = AF_INET;
    sockAddrIn.sin_addr.s_addr = dst;

    ret = memcpy_s(&sockAddr, sizeof(struct sockaddr), &sockAddrIn, sizeof(struct sockaddr_in));
    if (ret != EOK) {
        goto err_exit;
    }

    if (g_cpdSysHandle.real_sendto(fd, data, dataLen, 0, &sockAddr, sizeof(struct sockaddr)) < 0) {
        goto err_exit;
    }

    g_cpdSysHandle.real_close(fd);
    return 0;

err_exit:
    g_cpdSysHandle.real_close(fd);
    return -1;
}

static int CpdSendIcmpPkt(DP_InAddr_t* srcAddr, DP_InAddr_t* dstAddr)
{
    int ret;
    uint8_t* data;
    uint32_t dataLen = sizeof(DP_IpHdr_t) + sizeof(DP_IcmpHdr_t);
    /* 在CpdCreateIcmpPkt中全部赋值，无需初始化 */
    data = SHM_MALLOC(dataLen, MOD_CPD, DP_MEM_FREE);
    if (data == NULL) {
        DP_LOG_ERR("Malloc memory failed for icmpHdr.");
        return -1;
    }
    CpdCreateIcmpPkt(data, dataLen, *srcAddr, *dstAddr);
    ret = CpdSendIcmpToKernel(data, dataLen, *dstAddr);
    SHM_FREE(data, DP_MEM_FREE);
    if (ret != 0) {
        DP_LOG_DBG("CpdSendIcmpToKernel failed.");
        DP_ADD_ABN_STAT(DP_CPD_SEND_ICMP_ERR);
        return -1;
    }
    return 0;
}

void CloseNetlinkFd(void)
{
    (void)g_cpdSysHandle.real_close(g_netlinkfd);
}

static int CpdFindTapFd(int ifindex, int cpdQueueId)
{
    for (int i = 0; i < DEV_TBL_SIZE; ++i) {
        if (g_tapInfoList[i][cpdQueueId].ifindex == ifindex) {
            return g_tapInfoList[i][cpdQueueId].fd;
        }
    }
    return -1;
}

int SysCallInit(void)
{
    int ret = 0;
    // 初始化句柄
    g_cpdSysHandle.sysHandle = dlopen(SYS_LIB_NAME, RTLD_NOW | RTLD_GLOBAL);
    if (g_cpdSysHandle.sysHandle == NULL) {
        DP_LOG_ERR("SysCallinit failed by dlopen libc.so.6 failed.");
        return -1;
    }

    INIT_FUNCTION(socket, ret);
    INIT_FUNCTION(close, ret);
    INIT_FUNCTION(bind, ret);
    INIT_FUNCTION(setsockopt, ret);
    INIT_FUNCTION(fcntl, ret);
    INIT_FUNCTION(ioctl, ret);
    INIT_FUNCTION(sendmsg, ret);
    INIT_FUNCTION(recvmsg, ret);
    INIT_FUNCTION(write, ret);
    INIT_FUNCTION(read, ret);
    INIT_FUNCTION(sendto, ret);
    INIT_FUNCTION(writev, ret);
    if (ret != 0) {
        DP_LOG_ERR("SysCallinit failed by dlsym sysHandle failed.");
        dlclose(g_cpdSysHandle.sysHandle);
        return -1;
    }
    return 0;
}

int CpdCpInit(void)
{
    ssize_t nonblockFlag;
    int recvLen = CPD_CP_NETLINK_MAX_RECV_BUF;
    struct sockaddr_nl sockAddrNl = {0};
    int nlFd = g_cpdSysHandle.real_socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (nlFd < 0) {
        DP_LOG_ERR("CpdCpInit failed by netlink socket failed, errno: %d", errno);
        return -1;
    }

    sockAddrNl.nl_family = DP_AF_NETLINK;
    sockAddrNl.nl_pid = 0; // 给内核发消息使用0
    sockAddrNl.nl_groups = RTMGRP_NEIGH;

    if (g_cpdSysHandle.real_bind(nlFd, (struct sockaddr *)&sockAddrNl, sizeof(struct sockaddr)) < 0) {
        DP_LOG_ERR("CpdCpInit failed by netlink socket error 1, errno: %d", errno);
        goto err_exit;
    }

    if (g_cpdSysHandle.real_setsockopt(nlFd, SOL_SOCKET, SO_RCVBUF, (void*)&recvLen, sizeof(recvLen)) < 0) {
        DP_LOG_ERR("CpdCpInit failed by netlink socket error 2, errno: %d", errno);
        goto err_exit;
    }

    nonblockFlag = g_cpdSysHandle.real_fcntl(nlFd, F_GETFL, 0);
    if (nonblockFlag < 0) {
        DP_LOG_ERR("CpdCpInit failed by netlink socket error 3, errno: %d", errno);
        goto err_exit;
    }

    nonblockFlag = (uint32_t)nonblockFlag | O_NONBLOCK;
    if (g_cpdSysHandle.real_fcntl(nlFd, F_SETFL, nonblockFlag) < 0) {
        DP_LOG_ERR("CpdCpInit failed by netlink socket error 4, errno: %d", errno);
        goto err_exit;
    }

    if (GetFirstNetlinkMsg(nlFd) != 0) {
        DP_LOG_ERR("CpdCpInit failed by netlink socket error 5, errno: %d", errno);
        goto err_exit;
    }

    g_netlinkfd = nlFd;
    return 0;

err_exit:
    g_cpdSysHandle.real_close(nlFd);
    return -1;
}

int CPD_SyncTable(SycnTableEntry *entryList, uint32_t* entryNum)
{
    int nlFd = g_netlinkfd;
    uint32_t cnt = 0;
    // 从内核拿信息到中转表
    CpdProcNetlinkMsg(nlFd);
    // 从中转表拿出部分到entryList，记录entryNum
    SyncCpdNdTable(entryList, *entryNum, &cnt);
    *entryNum = cnt;
    return 0;
}

int CPD_SendPkt(uint32_t ifindex, const void* buf, uint32_t len, int cpdQueueId)
{
    int sendLen;
    int fd = CpdFindTapFd((int)ifindex, cpdQueueId);
    if (fd == -1) {
        DP_ADD_ABN_STAT(DP_CPD_FIND_TAP_FAILED);
        return -1;
    }
    sendLen = (int)g_cpdSysHandle.real_write(fd, buf, len);
    if (sendLen != (int)len) {
        DP_ADD_ABN_STAT(DP_CPD_FD_WRITE_FAILED);
        DP_LOG_INFO("Cpd write pkt failed, errno: %d", errno);
        return -1;
    }
    return 0;
}

int CPD_SendPktV(uint32_t ifindex, struct iovec *dataIov, int iovCnt, uint32_t len, int cpdQueueId)
{
    int sendLen;
    int fd = CpdFindTapFd((int)ifindex, cpdQueueId);
    if (fd == -1) {
        DP_ADD_ABN_STAT(DP_CPD_FIND_TAP_FAILED);
        return -1;
    }
    sendLen = (int)g_cpdSysHandle.real_writev(fd, dataIov, iovCnt);
    if (sendLen != (int)len) {
        DP_ADD_ABN_STAT(DP_CPD_FD_WRITEV_FAILED);
        DP_LOG_INFO("Cpd writev pkt failed, errno: %d", errno);
        return -1;
    }
    return 0;
}

int CPD_RcvPkt(uint32_t ifindex, void* buf, uint32_t len, int cpdQueueId)
{
    int recvLen;
    int fd = CpdFindTapFd((int)ifindex, cpdQueueId);
    if (fd == -1) {
        DP_ADD_ABN_STAT(DP_CPD_FIND_TAP_FAILED);
        return -1;
    }
    recvLen = (int)g_cpdSysHandle.real_read(fd, buf, len);
    if ((recvLen < 0) && (errno != EAGAIN)) {
        DP_LOG_INFO("Cpd recv pkt failed, errno: %d", errno);
        DP_ADD_ABN_STAT(DP_CPD_FD_READ_FAILED);
    }
    errno = 0;  // errno会被设置为EAGAIN。这里清零以免影响用户判断
    return recvLen;
}

int CPD_TblMissHandle(int type, int ifindex, void* srcAddr, void* dstAddr)
{
    if (type == DP_IP_VERSION_IPV4) {
        return CpdSendIcmpPkt((DP_InAddr_t*)srcAddr, (DP_InAddr_t*)dstAddr);
    }
    return 0;
}

int CPD_TapAlloc(DP_Netdev_t* dev)
{
    int ret;
    int fd;
    struct ifreq ifr;
    ssize_t nonblockFlag;
    char ifname[DP_IF_NAMESIZE];

    if (if_indextoname((unsigned int)dev->ifindex, ifname) == NULL) {
        DP_LOG_ERR("Cpd alloc tap failed by if_indextoname failed.");
        return -1;
    }
    fd = open(DP_TUN_DEV, O_RDWR);
    if (fd <= 0) {
        DP_LOG_ERR("Cpd alloc tap failed by open tun dev failed.");
        return fd;
    }

    (void)memset_s(&ifr, sizeof(ifr), 0, sizeof(ifr));
    /* Flags: IFF_TUN - TUN device (no Ethernet headers)
     * IFF_TAP - TAP device
     * IFF_NO_PI - Do not provide packet information
     */
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI | IFF_MULTI_QUEUE;
    if (strcpy_s(ifr.ifr_name, sizeof(ifr.ifr_name), ifname) != EOK) {
        DP_LOG_ERR("Cpd alloc tap failed by tapFd error 1 errno: %d", errno);
        goto err_exit;
    }
    ret = g_cpdSysHandle.real_ioctl(fd, TUNSETIFF, (void*) &ifr);
    if (ret < 0) {
        DP_LOG_ERR("Cpd alloc tap failed by tapFd error 2 errno: %d", errno);
        goto err_exit;
    }
    nonblockFlag = g_cpdSysHandle.real_fcntl(fd, F_GETFL, 0);
    if (nonblockFlag < 0) {
        DP_LOG_ERR("Cpd alloc tap failed by tapFd error 3 errno: %d", errno);
        goto err_exit;
    }
    nonblockFlag = (uint32_t)nonblockFlag | O_NONBLOCK;
    if (g_cpdSysHandle.real_fcntl(fd, F_SETFL, nonblockFlag) < 0) {
        DP_LOG_ERR("Cpd alloc tap failed by tapFd error 4 errno: %d", errno);
        goto err_exit;
    }
    return fd;

err_exit:
    g_cpdSysHandle.real_close(fd);
    return -1;
}

int CPD_TapFree(int tapFd)
{
    if (tapFd == -1) {
        return -1;
    }
    g_cpdSysHandle.real_close(tapFd);
    return 0;
}
