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
#include <arpa/inet.h>
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

static uint32_t g_tapId = 0;

static int32_t IoctlTap(int32_t fd, struct ifreq* ifr)
{
    KNET_GetCap(KNET_CAP_NET_ADMIN);
    int ret = ioctl(fd, TUNSETIFF, (void *) ifr);
    KNET_ClearCap(KNET_CAP_NET_ADMIN);
    if (ret < 0) {
        KNET_ERR("OS ioctl TUNSETIFF failed, fd %d, ret %d", fd, ret);
        return -1;
    }
    // 设置不启用该队列，如果启用队列，会和协议栈fd抢数据，协议栈可能收不到部分控制报文，实际只需要打开拿到ifindex，无需使用fd
    ifr->ifr_flags = IFF_DETACH_QUEUE;
    ret = ioctl(fd, TUNSETQUEUE, (void *)ifr);
    if (ret < 0) {
        KNET_ERR("Detach multiqueue failed, fd %d, ret %d", fd, ret);
        return -1;
    }
    
    KNET_GetCap(KNET_CAP_NET_ADMIN);
    ret = ioctl(fd, TUNSETNOCSUM, 0);
    KNET_ClearCap(KNET_CAP_NET_ADMIN);
    if (ret != 0) {
        KNET_ERR("Tap fd %d set no csum failed, ret %d, errno %d", fd, ret, errno);
        return -1;
    }
 
    unsigned long oflags = 0;
    if (KNET_GetCfg(CONF_HW_TSO)->intValue > 0 && KNET_GetCfg(CONF_HW_TCP_CHECKSUM)->intValue > 0) {
        oflags |= TUN_F_CSUM | TUN_F_TSO4;
    } else if (KNET_GetCfg(CONF_HW_TCP_CHECKSUM)->intValue > 0) {
        oflags |= TUN_F_CSUM;
    }
    KNET_GetCap(KNET_CAP_NET_ADMIN);
    ret = ioctl(fd, TUNSETOFFLOAD, oflags);
    KNET_ClearCap(KNET_CAP_NET_ADMIN);
    if (ret != 0) {
        KNET_ERR("Tap fd %d set tso failed, ret %d, errno %d", fd, ret, errno);
        return -1;
    }
    return 0;
}
 
static int32_t TapAlloc(const char* ifName)
{
    KNET_GetCap(KNET_CAP_NET_ADMIN);
    int32_t fd = open(SFE_TUN_DEV, O_RDWR);
    KNET_ClearCap(KNET_CAP_NET_ADMIN);
    if (fd < 0) {
        KNET_ERR("TapAlloc open failed, fd %d", fd);
        return fd;
    }
    /* Flags: IFF_TUN - TUN device (no Ethernet headers)
     * IFF_TAP - TAP device
     * IFF_NO_PI - Do not provide packet information
     * IFF_MULTI_QUEUE - Multiqueue device
     */
    struct ifreq ifr = { 0 };
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI | IFF_MULTI_QUEUE;
    int32_t ret = strcpy_s(ifr.ifr_name, sizeof(ifr.ifr_name), ifName);
    if (ret != 0) {
        KNET_ERR("Strcpy ifName failed, ret %d", ret);
        goto err;
    }
    if (IoctlTap(fd, &ifr) != 0) {
        goto err;
    }
    // 设置非阻塞
    int32_t flags = (int32_t)fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        KNET_ERR("OS fcntl getfl failed, fd %d, flags %d", fd, flags);
        goto err;
    }
    flags = (int32_t)((uint32_t)flags | O_NONBLOCK);
    ret = fcntl(fd, F_SETFL, flags);
    if (ret < 0) {
        KNET_ERR("OS fcntl setfl falied, fd %d, ret %d", fd, ret);
        goto err;
    }
    /* 进程退出 tap口删除 设置为1时持久保存 */
    ret = ioctl(fd, TUNSETPERSIST, 0);
    if (ret < 0) {
        KNET_ERR("OS ioctl TUNSETPERSIST falied, fd %d, ret %d", fd, ret);
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
    KNET_DEBUG("OS ioctl set macAddr, ifName %s, macAddr %02x:%02x:%02x:%02x:%02x:%02x", ifr->ifr_name,
        (unsigned char)ifr->ifr_hwaddr.sa_data[0], (unsigned char)ifr->ifr_hwaddr.sa_data[1], // 打印第0，1位mac地址，无需校验
        (unsigned char)ifr->ifr_hwaddr.sa_data[2], (unsigned char)ifr->ifr_hwaddr.sa_data[3], // 打印第2，3位mac地址，无需校验
        (unsigned char)ifr->ifr_hwaddr.sa_data[4], (unsigned char)ifr->ifr_hwaddr.sa_data[5]); // 打印第4，5位mac地址，无需校验
    if (ret != 0) {
        KNET_ERR("OS ioctl set macAddr failed, sockFd %d, ret %d", sockFd, ret);
        return -1;
    }
    // 设置网卡UP
    ret = ioctl(sockFd, SIOCGIFFLAGS, (void *)ifr);
    if (ret < 0) {
        KNET_ERR("OS ioctl get ifflags failed, sockFd %d, ret %d", sockFd, ret);
        return -1;
    }
    ifr->ifr_flags = (int16_t)((uint16_t)ifr->ifr_flags | IFF_UP);
    KNET_GetCap(KNET_CAP_NET_ADMIN);
    ret = ioctl(sockFd, SIOCSIFFLAGS, ifr);
    KNET_ClearCap(KNET_CAP_NET_ADMIN);
    if (ret != 0) {
        KNET_ERR("OS ioctl set ifflags failed, sockFd %d, ret %d", sockFd, ret);
        return -1;
    }

    return 0;
}

static int32_t TapSetIp(int32_t sockFd, struct ifreq *ifr)
{
    uint32_t ipAddr = (uint32_t)KNET_GetCfg(CONF_INTERFACE_IP)->intValue;
    struct sockaddr_in sin = { 0 };
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = ipAddr;
    int32_t ret = memcpy_s(&ifr->ifr_addr, sizeof(ifr->ifr_addr), &sin, sizeof(struct sockaddr));
    if (ret != 0) {
        KNET_ERR("Memcpy ip failed, ret %d", ret);
        return -1;
    }

    KNET_GetCap(KNET_CAP_NET_ADMIN);
    ret = ioctl(sockFd, SIOCSIFADDR, ifr);
    KNET_ClearCap(KNET_CAP_NET_ADMIN);
    
    char ip_str[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &(sin.sin_addr), ip_str, INET_ADDRSTRLEN);
    KNET_DEBUG("OS ioctl set ipAddr, ifName %s, ipAddr %s", ifr->ifr_name, ip_str);

    if (ret != 0) {
        KNET_ERR("OS ioctl set ipAddr failed, sockFd %d, ret %d", sockFd, ret);
        return -1;
    }
    return 0;
}

static int32_t TapSetNetmask(int32_t sockFd, struct ifreq *ifr)
{
    uint32_t netmask = (uint32_t)KNET_GetCfg(CONF_INTERFACE_NETMASK)->intValue;
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

    char netmask_str[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &(sin.sin_addr), netmask_str, INET_ADDRSTRLEN);
    KNET_DEBUG("OS ioctl set netmask, ifName %s, netmask %s", ifr->ifr_name, netmask_str);

    if (ret != 0) {
        KNET_ERR("OS ioctl set netmask failed, sockFd %d, ret %d", sockFd, ret);
        return -1;
    }
    return 0;
}

static int32_t TapSetTapInfo(const char *ifName)
{
    int32_t sockFd = socket(PF_INET, SOCK_DGRAM, 0);
    if (sockFd < 0) {
        return -1;
    }
    // 设置mac
    uint8_t *macAddr = (uint8_t *)KNET_GetCfg(CONF_INTERFACE_MAC)->strValue;
    struct ifreq ifr = { 0 };
    int32_t ret = TapSetMac(ifName, macAddr, sockFd, &ifr);
    if (ret != 0) {
        KNET_ERR("Tap set mac failed, ret %d", ret);
        goto err;
    }
 
    // 设置ip
    ret = TapSetIp(sockFd, &ifr);
    if (ret != 0) {
        KNET_ERR("Tap set ip failed, sockFd %d, ret %d", sockFd, ret);
        goto err;
    }
 
    // 设置mtu
    uint16_t mtu = (uint16_t)KNET_GetCfg(CONF_INTERFACE_MTU)->intValue;
    ifr.ifr_mtu = mtu;
    KNET_GetCap(KNET_CAP_NET_ADMIN);
    ret = ioctl(sockFd, SIOCSIFMTU, &ifr);
    KNET_ClearCap(KNET_CAP_NET_ADMIN);

    KNET_DEBUG("OS ioctl set mtu, ifName %s, mtu %d", ifr.ifr_name, ifr.ifr_mtu);
    if (ret != 0) {
        KNET_ERR("OS ioctl set mtu failed, sockFd %d, ret %d", sockFd, ret);
        goto err;
    }

    // 设置netmask
    ret = TapSetNetmask(sockFd, &ifr);
    if (ret != 0) {
        KNET_ERR("Tap set netmask failed, sockFd %d, ret %d", sockFd, ret);
        goto err;
    }

    close(sockFd);
    return 0;

err:
    close(sockFd);
    return -1;
}

int32_t KNET_FetchIfIndex(const char *ifName, size_t ifNameLen, int *ifIndex)
{
    int32_t sockFd = socket(PF_INET, SOCK_DGRAM, 0);
    if (sockFd < 0) {
        KNET_ERR("OS socket failed, sockFd %d, errno %d, %s", sockFd, errno, strerror(errno));
        return -1;
    }
    // 获取ifindex
    struct ifreq ifr = { 0 };
    int32_t ret = strcpy_s(ifr.ifr_name, sizeof(ifr.ifr_name), ifName);
    if (ret != 0) {
        KNET_ERR("Strcpy ifName failed, ret %d, ifName %s", ret, ifName);
        close(sockFd);
        return -1;
    }
    KNET_GetCap(KNET_CAP_NET_ADMIN);
    ret = ioctl(sockFd, SIOCGIFINDEX, (void *)&ifr);
    KNET_ClearCap(KNET_CAP_NET_ADMIN);
    if (ret < 0) {
        KNET_ERR("OS ioctl get ifindex from tap failed, sockFd %d, ret %d, errno %d, %s",
            sockFd, ret, errno, strerror(errno));
        close(sockFd);
        return -1;
    }
    *ifIndex = ifr.ifr_ifindex;
 
    close(sockFd);
    return 0;
}

static int32_t TapAllocHelper(const char *ifName, int32_t *fd)
{
    int32_t tapFd = TapAlloc(ifName);
    if (tapFd < 0) {
        KNET_ERR("TapAlloc failed, tapFd %d", tapFd);
        return -1;
    }
    int32_t ret = TapSetTapInfo(ifName);
    if (ret < 0) {
        KNET_ERR("TapSetTapInfo failed, tapFd %d, ret %d", tapFd, ret);
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
    int32_t ret = TapAllocHelper(ifname, fd);
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
int KNET_TAPCreate(int32_t *fd, int *tapIfIndex)
{
    char ifname[IF_NAME_SIZE] = {0};
    int32_t ret = snprintf_truncated_s(ifname, IF_NAME_SIZE, TUN_PRE_NAME "%u", g_tapId);
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

    ret = KNET_FetchIfIndex(ifname, IF_NAME_SIZE, tapIfIndex);
    if (ret != 0) {
        KNET_ERR("K-NET get tap ifindex failed, ret %d", ret);
        KNET_TapFree(*fd);
        *fd = INVALID_FD;
        return -1;
    }

    g_tapId++;
    return 0;
}

#ifdef __cplusplus
}
#endif