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
#include <securec.h>

#include "dp_ioctl_defs_api.h"
#include "dp_netdev_api.h"

#include "dp_arp.h"
#include "dp_ethernet.h"
#include "utils_log.h"

#include "devtbl.h"
#include "dev.h"

static int DumpStats(Netdev_t* dev, DP_DevStats_t* stats)
{
    (void)memset_s(stats, sizeof(*stats), 0, sizeof(*stats));

    return strcpy_s(stats->ifname, sizeof(stats->ifname), dev->name);
}

static int DumpDevStats(NS_Net_t* net, int ifindex, DP_DevStats_t* stats)
{
    int       ret = -EINVAL;
    Netdev_t* dev;

    NS_Lock(net);

    dev = NETDEV_GetDev(net, ifindex);
    if (dev != NULL) {
        ret = DumpStats(dev, stats);
    }

    NS_Unlock(net);

    return ret;
}

int DP_DumpDevStats(int ifindex, DP_DevStats_t* stats)
{
    NS_Net_t* net = NS_GetDftNet();
    if (stats == NULL) {
        DP_LOG_ERR("Get dev ifname failed, stats is NULL!");
        return -EINVAL;
    }
    return DumpDevStats(net, ifindex, stats);
}

typedef struct {
    DP_DevStats_t* stats;
    int              max;
    int              cnt;
} DevStats_t;

static int DoDumpStats(Netdev_t* dev, void* p)
{
    DevStats_t* stats = (DevStats_t*)p;

    if (stats->stats == NULL) {
        stats->cnt++;
        return 0;
    }

    if (stats->cnt >= stats->max) {
        return -1;
    }

    DumpStats(dev, &stats->stats[stats->cnt]);
    stats->cnt++;

    return 0;
}

static int DumpAllDevStats(NS_Net_t* net, DP_DevStats_t* stats, int cnt)
{
    DevStats_t devStats;

    devStats.stats = stats;
    devStats.max   = cnt;
    devStats.cnt   = 0;

    NS_Lock(net);

    WalkAllDevs(NS_GET_DEV_TBL(net), DoDumpStats, &devStats);

    NS_Unlock(net);

    return devStats.cnt;
}

int DP_DumpAllDevStats(DP_DevStats_t* stats, int cnt)
{
    NS_Net_t* net = NS_GetDftNet();
    if ((stats == NULL) || (cnt == 0)) {
        DP_LOG_ERR("Get all dev ifname failed, param invalid!");
        return -EINVAL;
    }
    return DumpAllDevStats(net, stats, cnt);
}

typedef int (*DoIoctl_t)(Netdev_t* dev, void* p);

static int GetIfIndex(Netdev_t* dev, struct DP_Ifreq* ifreq)
{
    ifreq->ifr_ifindex = dev->ifindex;
    return 0;
}

static int GetIfFlags(Netdev_t* dev, struct DP_Ifreq* ifreq)
{
    ifreq->ifr_flags = (int16_t)dev->ifflags; // 当前支撑ifflags的类型不会超过int16_t范围，不会出现整型截断
    return 0;
}

static int SetIfFlags(Netdev_t* dev, struct DP_Ifreq* ifreq)
{
    uint16_t sets = ((uint16_t)(ifreq->ifr_flags)) ^ dev->ifflags;

    if ((sets & (~(DP_IFF_UP | DP_IFF_DEBUG | DP_IFF_RUNNING))) != 0) {
        DP_LOG_ERR("SetIfFlags failed unsupport flags, ifr_flags = %u.", sets);
        return -EINVAL;
    }

    // 与内核保持一致，入参IFF_UP置位表示配置设备UP，置空表示配置设备DOWN
    if ((((uint16_t)(ifreq->ifr_flags)) & DP_IFF_UP) == DP_IFF_UP) {
        if ((dev->ifflags & DP_IFF_UP) != DP_IFF_UP) {
            DevStart(dev);
        }
    } else if ((dev->ifflags & DP_IFF_UP) == DP_IFF_UP) {
        DevStop(dev);
    }

    return 0;
}

static int GetIfAddr(Netdev_t* dev, struct DP_Ifreq* ifreq)
{
    struct DP_SockaddrIn* addrIn = (struct DP_SockaddrIn*)(&ifreq->ifr_addr);

    if (dev->in.ifAddr == NULL) {
        DP_LOG_ERR("GetIfAddr failed by dev->in.ifAddr NULL.");
        return -EINVAL;
    }

    addrIn->sin_family      = DP_AF_INET;
    addrIn->sin_addr.s_addr = dev->in.ifAddr->local;

    return 0;
}

static int SetIfAddr(Netdev_t* dev, struct DP_Ifreq* ifreq)
{
    NETDEV_IfAddr_t*        ifAddr = dev->in.ifAddr;
    struct DP_SockaddrIn* addrIn = (struct DP_SockaddrIn*)(&ifreq->ifr_addr);

    if (ifAddr == NULL) {
        ifAddr = NETDEV_AllocIfAddr();
        if (ifAddr == NULL) {
            DP_LOG_ERR("SetIfAddr failed by alloc ifAddr failed.");
            return -ENOMEM;
        }
        ifAddr->broadcast = DP_INADDR_ANY;
        ifAddr->dev       = dev;
    }

    ifAddr->local = addrIn->sin_addr.s_addr;

    dev->in.ifAddr = ifAddr;

    return 0;
}

static int GetIfBrdaddr(Netdev_t* dev, struct DP_Ifreq* ifreq)
{
    struct DP_SockaddrIn* addrIn = (struct DP_SockaddrIn*)(&ifreq->ifr_addr);

    if (dev->in.ifAddr == NULL) {
        DP_LOG_ERR("GetIfBrdaddr failed by dev->in.ifAddr NULL.");
        return -EINVAL;
    }

    addrIn->sin_addr.s_addr = dev->in.ifAddr->broadcast;

    return 0;
}

static int SetIfBrdaddr(Netdev_t* dev, struct DP_Ifreq* ifreq)
{
    struct DP_SockaddrIn* addrIn = (struct DP_SockaddrIn*)(&ifreq->ifr_broadaddr);

    if (dev->in.ifAddr == NULL) {
        DP_LOG_ERR("SetIfBrdaddr failed by dev->in.ifAddr NULL.");
        return -EINVAL;
    }

    dev->in.ifAddr->broadcast = addrIn->sin_addr.s_addr;

    return 0;
}

static int GetIfNetmask(Netdev_t* dev, struct DP_Ifreq* ifreq)
{
    struct DP_SockaddrIn* addrIn = (struct DP_SockaddrIn*)(&ifreq->ifr_addr);

    if (dev->in.ifAddr == NULL) {
        DP_LOG_ERR("GetIfNetmask failed by dev->in.ifAddr NULL.");
        return -EINVAL;
    }

    addrIn->sin_family      = DP_AF_INET;
    addrIn->sin_addr.s_addr = dev->in.ifAddr->mask;

    return 0;
}

static int SetIfNetmask(Netdev_t* dev, struct DP_Ifreq* ifreq)
{
    struct DP_SockaddrIn* addrIn = (struct DP_SockaddrIn*)(&ifreq->ifr_addr);

    if (dev->in.ifAddr == NULL) {
        DP_LOG_ERR("SetIfNetmask failed by dev->in.ifAddr NULL.");
        return -EINVAL;
    }

    dev->in.ifAddr->mask = addrIn->sin_addr.s_addr;

    return 0;
}

static int GetIfMtu(Netdev_t* dev, struct DP_Ifreq* ifreq)
{
    ifreq->ifr_mtu = dev->mtu;
    return 0;
}

static int SetIfMtu(Netdev_t* dev, struct DP_Ifreq* ifreq)
{
    if ((ifreq->ifr_mtu < dev->minMtu) || (ifreq->ifr_mtu > dev->maxMtu)) {
        DP_LOG_ERR("SetIfMtu failed by invalid mtu, ifr_mtu = %d.", ifreq->ifr_mtu);
        return -EINVAL;
    }
    dev->mtu = (uint16_t)ifreq->ifr_mtu; // mtu不会出现小于0的情况，上面已经做了判断，强转无风险
    return 0;
}

static int GetIfHwaddr(Netdev_t* dev, struct DP_Ifreq* ifreq)
{
    ifreq->ifr_hwaddr.sa_family = DP_ARPHDR_HRD_ETHER;
    DP_MAC_COPY((DP_EthAddr_t*)(ifreq->ifr_hwaddr.sa_data), &dev->hwAddr.mac);
    return 0;
}

static int SetIfHwaddr(Netdev_t* dev, struct DP_Ifreq* ifreq)
{
    if (ifreq->ifr_hwaddr.sa_family != DP_ARPHDR_HRD_ETHER) {
        DP_LOG_ERR("SetIfHwaddr failed by invalid hwaddr family, ifr_hwaddr = %d.", ifreq->ifr_hwaddr.sa_family);
        return -EINVAL;
    }
    DP_EthAddr_t* hwAddr = (DP_EthAddr_t*)(ifreq->ifr_hwaddr.sa_data);
    if (DP_MAC_IS_DUMMY(hwAddr)) {
        DP_LOG_ERR("SetIfHwaddr failed by invalid dummy hwaddr data.");
        return -EINVAL;
    }

    DP_EthAddr_t tmpAddr = {0};
    DP_MAC_COPY(&tmpAddr, &dev->hwAddr.mac);
    DP_MAC_COPY(&dev->hwAddr.mac, hwAddr);

    return 0;
}

static int SetIfName(Netdev_t* dev, struct DP_Ifreq* ifreq)
{
    if (strcpy_s(dev->name, DP_IF_NAMESIZE, ifreq->ifr_name) != 0) {
        DP_LOG_ERR("SetIfName failed by strcpy_s ifr_name.");
        return -EINVAL;
    }
    return 0;
}

IfReqOps_t g_reqOps[] = {
    { DP_SIOCGIFINDEX, GetIfIndex },
    { DP_SIOCGIFFLAGS, GetIfFlags },
    { DP_SIOCGIFADDR, GetIfAddr },
    { DP_SIOCGIFBRDADDR, GetIfBrdaddr },
    { DP_SIOCGIFNETMASK, GetIfNetmask },
    { DP_SIOCGIFMTU, GetIfMtu },
    { DP_SIOCGIFHWADDR, GetIfHwaddr },
    // set
    { DP_SIOCSIFFLAGS, SetIfFlags },
    { DP_SIOCSIFADDR, SetIfAddr },
    { DP_SIOCSIFBRDADDR, SetIfBrdaddr },
    { DP_SIOCSIFNETMASK, SetIfNetmask },
    { DP_SIOCSIFMTU, SetIfMtu },
    { DP_SIOCSIFHWADDR, SetIfHwaddr },
    { DP_SIOCSIFNAME, SetIfName },
};

#define GET_REQ_OPS_CNT() (int)(sizeof(g_reqOps) / sizeof(g_reqOps[0]))

int DP_ProcIfreq(Netdev_t* dev, int request, struct DP_Ifreq* ifreq)
{
    int         ret = -EOPNOTSUPP;
    DoIfreqFn_t fn  = NULL;

    if ((dev == NULL) || (ifreq == NULL) || dev->devType >= DP_NETDEV_TYPE_BUTT) {
        DP_LOG_ERR("ProcIfreq failed: dev or ifreq is invalid!");
        return -EINVAL;
    }

    NS_Lock(dev->net);

    for (int i = 0; i < GET_REQ_OPS_CNT(); i++) {
        if (request == g_reqOps[i].reqeust) {
            fn = g_reqOps[i].doIfreq;
            break;
        }
    }

    if (fn != NULL) {
        ret = fn(dev, ifreq);
    } else if (g_devOps[dev->devType]->ctrl != NULL) {
        DP_LOG_INFO("DP_ProcIfreq with unsupport request, use g_devOps->ctrl. request = %d.", request);
        ret = g_devOps[dev->devType]->ctrl(dev, request, ifreq);
    }

    NS_Unlock(dev->net);

    if (ret != 0) {
        DP_LOG_ERR("ProcIfreq failed with ret = %d.", ret);
    }
    return ret;
}

static int FillIfconf(Netdev_t* dev, struct DP_Ifreq* ifreq, int bufSize)
{
    struct DP_SockaddrIn* addrIn;

    if (ifreq == NULL) {
        return sizeof(*ifreq);
    }

    if (bufSize < (int)sizeof(*ifreq)) {
        return 0;
    }

    (void)strcpy_s(ifreq->ifr_name, DP_IF_NAMESIZE, dev->name);
    if (dev->in.ifAddr == NULL) {
        addrIn = (struct DP_SockaddrIn*)(&ifreq->ifr_addr);

        addrIn->sin_family      = DP_AF_INET;
        addrIn->sin_addr.s_addr = DP_INADDR_ANY;
    } else {
        addrIn = (struct DP_SockaddrIn*)(&ifreq->ifr_addr);

        addrIn->sin_family      = DP_AF_INET;
        addrIn->sin_addr.s_addr = dev->in.ifAddr->local;
    }

    return sizeof(*ifreq);
}

typedef struct {
    int                 ifclen;
    struct DP_Ifconf* ifconf;
} GetIfconf_t;

static int DoGetDevIfconf(Netdev_t* dev, void* p)
{
    GetIfconf_t*       devIfconf = (GetIfconf_t*)p;
    struct DP_Ifreq* ifreq;
    int                ifclen;

    ifclen = devIfconf->ifclen;
    if (devIfconf->ifconf->ifc_req == NULL) {
        ifreq = NULL;
    } else {
        ifreq = (struct DP_Ifreq*)(devIfconf->ifconf->ifc_buf + devIfconf->ifconf->ifc_len);
    }

    devIfconf->ifconf->ifc_len += FillIfconf(dev, ifreq, ifclen);

    return 0;
}

static int GetDevIfconf(NS_Net_t* net, struct DP_Ifconf* ifconf)
{
    GetIfconf_t gi;

    NS_Lock(net);

    gi.ifclen          = ifconf->ifc_len;
    gi.ifconf          = ifconf;
    gi.ifconf->ifc_len = 0;

    WalkAllDevs(NS_GET_DEV_TBL(net), DoGetDevIfconf, &gi);

    NS_Unlock(net);

    return 0;
}

int DP_GetIfconf(struct DP_Ifconf* ifconf)
{
    NS_Net_t* net = NS_GetDftNet();

    if (ifconf == NULL) {
        return -EINVAL;
    }

    return GetDevIfconf(net, ifconf);
}
