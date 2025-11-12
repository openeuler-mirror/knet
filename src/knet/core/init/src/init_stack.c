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
#include <linux/if_arp.h>

#include "securec.h"
#include "dp_netdev_api.h"

#include "knet_config.h"
#include "knet_log.h"
#include "init_stack.h"
/**
 * @brief 配置协议栈网络设备
 */
int KNET_ConfigureStackNetdev(DP_Netdev_t *netdev, const char *ifname)
{
    /* 使能网卡队列 */
    struct DP_Ifreq ifReq = {0};
    ifReq.ifr_flags = DP_IFF_UP;
    int ret = strcpy_s(ifReq.ifr_name, DP_IF_NAME_SIZE, ifname);
    if (ret != 0) {
        KNET_ERR("Strcpy ifname failed, ret %d", ret);
        return -1;
    }
    ret = DP_ProcIfreq(netdev, DP_SIOCSIFFLAGS, &ifReq);
    if (ret != 0) {
        KNET_ERR("DP_ProcIfreq DP_SIOCSIFFLAGS failed, ret %d", ret);
        return -1;
    }

    /* 设置本端mac地址 */
    (void)memset_s(&ifReq.ifr_ifru, sizeof(ifReq.ifr_ifru), 0, sizeof(ifReq.ifr_ifru));
    ifReq.ifr_hwaddr.sa_family = ARPHRD_ETHER;
    ret = memcpy_s((void *)ifReq.ifr_hwaddr.sa_data, sizeof(ifReq.ifr_hwaddr.sa_data),
        KNET_GetCfg(CONF_INTERFACE_MAC).strValue, MAC_ADDR_LEN);
    if (ret != 0) {
        KNET_ERR("Memcpy failed, ret %d", ret);
        return -1;
    }

    ret = DP_ProcIfreq(netdev, DP_SIOCSIFHWADDR, &ifReq);
    if (ret != 0) {
        KNET_ERR("DP ProcIfreq set mac failed, ret %d", ret);
        return -1;
    }

    /* 设置本端ip地址 */
    (void)memset_s(&ifReq.ifr_ifru, sizeof(ifReq.ifr_ifru), 0, sizeof(ifReq.ifr_ifru));
    struct DP_SockaddrIn *addr = (struct DP_SockaddrIn *)(void *)&ifReq.ifr_addr;
    addr->sin_family = AF_INET;
    addr->sin_port = 0;
    addr->sin_addr.s_addr = (uint32_t)KNET_GetCfg(CONF_INTERFACE_IP).intValue;
    ret = DP_ProcIfreq(netdev, DP_SIOCSIFADDR, &ifReq);
    if (ret != 0) {
        KNET_ERR("DP ProcIfreq set ip failed, ret %d", ret);
        return -1;
    }

    /* 设置mtu */
    (void)memset_s(&ifReq.ifr_ifru, sizeof(ifReq.ifr_ifru), 0, sizeof(ifReq.ifr_ifru));
    ifReq.ifr_mtu = KNET_GetCfg(CONF_INTERFACE_MTU).intValue;
    ret = DP_ProcIfreq(netdev, DP_SIOCSIFMTU, &ifReq);
    if (ret != 0) {
        KNET_ERR("DP ProcIfreq set mtu failed, ret %d", ret);
        return -1;
    }

    return 0;
}