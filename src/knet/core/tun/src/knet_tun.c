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
 
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <limits.h> /* PATH_MAX */

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <linux/if_arp.h>
#include <linux/if_tun.h>
#include <linux/if.h>

#include "knet_config.h"
#include "knet_log.h"
#include "knet_capability.h"
#include "knet_tun.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SFE_TUN_DEV "/dev/net/tun"
 
static int32_t TapAlloc(const char* ifName)
{
    struct ifreq ifr = { 0 };
    int32_t fd, ret, flags;
    KNET_GetCap(KNET_CAP_NET_ADMIN);
    fd = open(SFE_TUN_DEV, O_RDWR);
    KNET_ClearCap(KNET_CAP_NET_ADMIN);
    if (fd < 0) {
        KNET_ERR("TapAlloc: open is failed, fd %d", fd);
        return fd;
    }
    /* Flags: IFF_TUN - TUN device (no Ethernet headers)
     * IFF_TAP - TAP device
     * IFF_NO_PI - Do not provide packet information
     * IFF_MULTI_QUEUE - Multiqueue device
     */
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI | IFF_MULTI_QUEUE;
    ret = strcpy_s(ifr.ifr_name, sizeof(ifr.ifr_name), ifName);
    if (ret != 0) {
        KNET_ERR("Strcpy ifName failed, ret %d", ret);
        goto err;
    }
    KNET_GetCap(KNET_CAP_NET_ADMIN);
    ret = ioctl(fd, TUNSETIFF, (void *) &ifr);
    KNET_ClearCap(KNET_CAP_NET_ADMIN);
    if (ret < 0) {
        KNET_ERR("OS ioctl TUNSETIFF is failed, fd %d, ret %d", fd, ret);
        goto err;
    }
    // 设置不启用该队列，如果启用队列，会和协议栈fd抢数据，协议栈可能收不到部分控制报文，实际只需要打开拿到ifindex，无需使用fd
    ifr.ifr_flags = IFF_DETACH_QUEUE;
    ret = ioctl(fd, TUNSETQUEUE, (void *)&ifr);
    if (ret < 0) {
        KNET_ERR("Detach multiqueue failed, fd %d, ret %d", fd, ret);
        goto err;
    }
    // 设置非阻塞
    flags = (int32_t)fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        KNET_ERR("OS fcntl getfl is failed, fd %d, flags %d", fd, flags);
        goto err;
    }
    flags = (int32_t)((uint32_t)flags | O_NONBLOCK);
    ret = fcntl(fd, F_SETFL, flags);
    if (ret < 0) {
        KNET_ERR("OS fcntl setfl is falied, fd %d, ret %d", fd, ret);
        goto err;
    }
    /* 进程退出 tap口删除 设置为1时持久保存 */
    ret = ioctl(fd, TUNSETPERSIST, 0);
    if (ret < 0) {
        KNET_ERR("OS ioctl TUNSETPERSIST is falied, fd %d, ret %d", fd, ret);
        goto err;
    }
    return fd;
err:
    close(fd);
    return -1;
}

static int32_t TapSetMac(const char *ifName, uint8_t *macAddr, int32_t sockFd, struct ifreq *ifr)
{
    // 设置mac
    int32_t ret = strcpy_s(ifr->ifr_name, sizeof(ifr->ifr_name), ifName);
    if (ret != 0) {
        KNET_ERR("Strcpy ifName failed, ret %d", ret);
        return -1;
    }
    ifr->ifr_hwaddr.sa_family = ARPHRD_ETHER;
    // 内部函数调用，mac长度固定为6，无需校验
    for (int32_t index = 0; index <= 5; index++) { // 拷贝第0~5位
        ifr->ifr_hwaddr.sa_data[index] = (char)macAddr[index];
    }
    KNET_GetCap(KNET_CAP_NET_ADMIN);
    ret = ioctl(sockFd, SIOCSIFHWADDR, ifr);
    KNET_ClearCap(KNET_CAP_NET_ADMIN);
    if (ret != 0) {
        KNET_ERR("OS ioctl set hwaddr is failed, sockFd %d, ret %d", sockFd, ret);
        return -1;
    }
    // 设置网卡UP
    ret = ioctl(sockFd, SIOCGIFFLAGS, (void *)ifr);
    if (ret < 0) {
        KNET_ERR("OS ioctl get ifflags is failed, sockFd %d, ret %d", sockFd, ret);
        return -1;
    }
    ifr->ifr_flags = (int16_t)((uint16_t)ifr->ifr_flags | IFF_UP);
    KNET_GetCap(KNET_CAP_NET_ADMIN);
    ret = ioctl(sockFd, SIOCSIFFLAGS, ifr);
    KNET_ClearCap(KNET_CAP_NET_ADMIN);
    if (ret != 0) {
        KNET_ERR("OS ioctl set ifflags is failed, sockFd %d, ret %d", sockFd, ret);
        return -1;
    }

    return 0;
}

static int32_t TapSetNetmask(int32_t sockFd, struct ifreq *ifr)
{
    uint32_t netmask = (uint32_t)KNET_GetCfg(CONF_INTERFACE_NETMASK).intValue;
    struct sockaddr_in sin = { 0 };
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = netmask;
    int32_t ret = memcpy_s(&ifr->ifr_netmask, sizeof(ifr->ifr_netmask), &sin, sizeof(struct sockaddr));
    if (ret != 0) {
        KNET_ERR("Memcpy ifr netmask failed, ret %d", ret);
        return -1;
    }

    KNET_GetCap(KNET_CAP_NET_ADMIN);
    ret = ioctl(sockFd, SIOCSIFNETMASK, ifr);
    KNET_ClearCap(KNET_CAP_NET_ADMIN);
    if (ret != 0) {
        KNET_ERR("OS ioctl set netmask is failed, sockFd %d, ret %d", sockFd, ret);
        return -1;
    }
    return 0;
}

static int32_t TapSetTapInfo(const char *ifName)
{
    struct ifreq ifr = { 0 };
    struct sockaddr_in sin = { 0 };
 
    int32_t sockFd = socket(PF_INET, SOCK_DGRAM, 0);
    if (sockFd < 0) {
        return -1;
    }
    // 设置mac
    uint8_t *macAddr = (uint8_t *)KNET_GetCfg(CONF_INTERFACE_MAC).strValue;
    int32_t ret = TapSetMac(ifName, macAddr, sockFd, &ifr);
    if (ret != 0) {
        KNET_ERR("Tap set mac failed, ret %d", ret);
        goto err;
    }
 
    // 设置ip
    uint32_t ipAddr = (uint32_t)KNET_GetCfg(CONF_INTERFACE_IP).intValue;
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = ipAddr;
    ret = memcpy_s(&ifr.ifr_addr, sizeof(ifr.ifr_addr), &sin, sizeof(struct sockaddr));
    if (ret != 0) {
        KNET_ERR("Memcpy ip failed, ret %d", ret);
        goto err;
    }

    KNET_GetCap(KNET_CAP_NET_ADMIN);
    ret = ioctl(sockFd, SIOCSIFADDR, &ifr);
    KNET_ClearCap(KNET_CAP_NET_ADMIN);
    if (ret != 0) {
        KNET_ERR("OS ioctl set addr is failed, sockFd %d, ret %d", sockFd, ret);
        goto err;
    }
 
    // 设置mtu
    uint16_t mtu = (uint16_t)KNET_GetCfg(CONF_INTERFACE_MTU).intValue;
    ifr.ifr_mtu = mtu;
    KNET_GetCap(KNET_CAP_NET_ADMIN);
    ret = ioctl(sockFd, SIOCSIFMTU, &ifr);
    KNET_ClearCap(KNET_CAP_NET_ADMIN);
    if (ret != 0) {
        KNET_ERR("OS ioctl set mtu is failed, sockFd %d, ret %d", sockFd, ret);
        goto err;
    }

    // 设置netmask
    ret = TapSetNetmask(sockFd, &ifr);
    if (ret != 0) {
        KNET_ERR("Tap set netmask is failed, sockFd %d, ret %d", sockFd, ret);
        goto err;
    }

    close(sockFd);
    return 0;

err:
    close(sockFd);
    return -1;
}
 
static int32_t TapGetIfIndex(const char *ifName, int *tapIfIndex)
{
    int32_t ret, sockFd;
    struct ifreq ifr = { 0 };
 
    sockFd = socket(PF_INET, SOCK_DGRAM, 0);
    if (sockFd < 0) {
        KNET_ERR("OS socket is failed, sockFd %d, errno %d, %s", sockFd, errno, strerror(errno));
        return -1;
    }
    // 获取ifindex
    ret = strcpy_s(ifr.ifr_name, sizeof(ifr.ifr_name), ifName);
    if (ret != 0) {
        KNET_ERR("Strcpy ifName failed, ret %d", ret);
        close(sockFd);
        return -1;
    }
 
    ret = ioctl(sockFd, SIOCGIFINDEX, (void *)&ifr);
    if (ret < 0) {
        KNET_ERR("OS ioctl get ifindex from tap failed, sockFd %d, ret %d, errno %d, %s",
            sockFd, ret, errno, strerror(errno));
        close(sockFd);
        return -1;
    }
    *tapIfIndex = ifr.ifr_ifindex;
 
    close(sockFd);
    return 0;
}

static int32_t KnetTapAlloc(const char *ifName, int32_t *fd)
{
    int32_t ret;
    int32_t tapFd = TapAlloc(ifName);
    if (tapFd < 0) {
        KNET_ERR("TapAlloc is falied, tapFd %d", tapFd);
        return -1;
    }
    ret = TapSetTapInfo(ifName);
    if (ret < 0) {
        KNET_ERR("TapSetTapInfo is failed, tapFd %d, ret %d", tapFd, ret);
        close(tapFd);
        return -1;
    }

    *fd = tapFd;
    return 0;
}

/**
 * @brief 创建并配置TAP口
 */
static int32_t CreateInitTap(const char *ifname, int32_t *fd)
{
    int32_t ret = KnetTapAlloc(ifname, fd);
    if (ret != 0) {
        KNET_ERR("Tap alloc failed, ret %d", ret);
        return -1;
    }

    return 0;
}

int32_t KNET_TapFree(int32_t fd)
{
    if (fd == INVALID_FD) {
        KNET_ERR("Invalid fd %x", fd);
        return -1;
    }

    close(fd);
    return 0;
}

/**
 * @brief 创建并初始化TAP口
 */
int KNET_TAPCreate(const uint32_t tapId, int32_t *fd, int *tapIfIndex)
{
    int32_t ret;
    char ifname[IF_NAME_SIZE] = {0};

    ret = snprintf_truncated_s(ifname, IF_NAME_SIZE, TUN_PRE_NAME "%u", tapId);
    if (ret == -1) {
        KNET_ERR("K-NET truncate name failed, ret %d", ret);
        return -1;
    }

    ret = CreateInitTap(ifname, fd);
    if (ret != 0) {
        KNET_ERR("K-NET create and init tap failed, ret %d", ret);
        *fd = INVALID_FD;
        return -1;
    }

    ret = TapGetIfIndex(ifname, tapIfIndex);
    if (ret != 0) {
        KNET_ERR("K-NET get tap ifindex failed, ret %d", ret);
        KNET_TapFree(*fd);
        *fd = INVALID_FD;
        return -1;
    }

    return 0;
}

#ifdef __cplusplus
}
#endif