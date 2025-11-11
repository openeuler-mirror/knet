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

#include <string.h>
#include <securec.h>

#include "dp_netdev_api.h"
#include "dp_ioctl_defs_api.h"

#include "dp_ethernet.h"

#include "netdev.h"
#include "dev.h"
#include "pmgr.h"

#define DEFAULT_MAX_VID 4096

Netdev_t* NETDEV_FindVlanDev(Netdev_t* dev, uint16_t vlanid)
{
    Netdev_t* sdev;

    ASSERT(dev->devType == DP_NETDEV_TYPE_ETH || dev->devType == DP_NETDEV_TYPE_ETHVLAN);

    sdev = dev->subdevs;
    while (sdev != NULL) {
        if (sdev->vlanid == vlanid) {
            return sdev;
        }

        sdev = sdev->nxtSubNode;
    }

    return NULL;
}

static int InitVlanDev(Netdev_t* dev, DP_NetdevCfg_t* devCfg)
{
    Netdev_t*  parent = DP_GetNetdevByIndex(devCfg->ifindex);
    Netdev_t** tailSubNode;

    if (strlen(devCfg->ifname) == 0) {
        if (strcpy_s(dev->name, DP_IF_NAMESIZE, "vlan") != 0) {
            return -1;
        }
    } else {
        if (strcpy_s(dev->name, DP_IF_NAMESIZE, devCfg->ifname) != 0) {
            return -1;
        }
    }

    if (parent == NULL) {
        return -1;
    }

    tailSubNode = &parent->subdevs;
    while (*tailSubNode != NULL) {
        tailSubNode = &((*tailSubNode)->nxtSubNode);
    }
    *tailSubNode = dev;

    dev->maxMtu     = parent->maxMtu;
    dev->minMtu     = parent->minMtu;
    dev->ifflags    = DP_IFF_BROADCAST;
    dev->linkHdrLen = parent->linkHdrLen + 4; // linkHdrLen 需要+vlan头，长度4字节
    dev->mtu        = dev->maxMtu - dev->linkHdrLen;
    dev->dstEntry   = PMGR_ENTRY_VLAN_OUT;
    dev->in.ndEntry = PMGR_ENTRY_ND_OUT;
    dev->master     = parent;

    DP_MAC_COPY(&dev->hwAddr.mac, &parent->hwAddr.mac);

    return 0;
}

static int CtrlVlan(Netdev_t* dev, int cmd, void* val)
{
    switch (cmd) {
        case DP_SIOCSIFVLAN:
        case DP_SIOCGIFVLAN: {
            struct DP_Ifreq*    ifreq = (struct DP_Ifreq*)val;
            DP_VlanIoctlArgs_t* args  = (DP_VlanIoctlArgs_t*)ifreq->ifr_data;

            if (args == NULL) {
                return -EINVAL;
            }

            if (cmd == DP_SIOCSIFVLAN) {
                if (args->VID > 0 && args->VID < DEFAULT_MAX_VID) {
                    dev->vlanid = (uint16_t)args->VID; // VID在上面做了判断约束，不小于0，此处强转无风险
                } else {
                    return -EINVAL;
                }
            } else {
                args->VID = dev->vlanid;
            }

            return 0;
        }
        default:
            return -EOPNOTSUPP;
    }

    return -EOPNOTSUPP;
}

DevOps_t g_vlanDevOps = {
    .privateLen = 0,
    .init       = InitVlanDev,
    .deinit     = NULL,
    .ctrl       = CtrlVlan,
    .doRcv      = NULL,
    .doXmit     = NULL,
};
