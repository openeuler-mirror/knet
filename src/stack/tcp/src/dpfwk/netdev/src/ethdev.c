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

#include "dp_ethernet.h"

#include "utils_log.h"
#include "pmgr.h"
#include "pbuf.h"

#include "dev.h"
#include "basedev.h"

static void RcvEth(NetdevQue_t* rxQue)
{
    Pbuf_t** pbufs = (Pbuf_t**)rxQue->flash;

    int rxCnt = BaseDevRcv(rxQue, pbufs, rxQue->flashSize);
    for (int i = 0; i < rxCnt; i++) {
        if (UTILS_UNLIKELY(pbufs[i]->ref != 1)) {
            DP_ADD_ABN_STAT(DP_PBUF_REF_ERR);
        }
        PBUF_SET_ENTRY(pbufs[i], PMGR_ENTRY_ETH_IN);
        DP_PBUF_SET_WID(pbufs[i], (uint8_t)rxQue->wid); // wid不会超过255，强转无风险
        PBUF_SET_QUE_ID(pbufs[i], (uint8_t)rxQue->queid); // queid不会超过255，强转无风险
        PBUF_SET_DEV(pbufs[i], rxQue->dev);

        NET_DEV_ADD_RX_PKTS(rxQue, 1);
        NET_DEV_ADD_RX_BYTES(rxQue, PBUF_GET_SEG_LEN(pbufs[i]));

        PMGR_Dispatch(pbufs[i]);
    }
}

static int CtrlEthDrv(Netdev_t* dev, int cmd, void* val)
{
    BaseDev_t* baseDev = (BaseDev_t*)(dev->ctx);
    switch (cmd) {
        case DEV_CTL_GET_PRIVATE:
            *(void**)val = baseDev->ctx;
            break;
        default:
            DP_LOG_ERR("CtrlEthDrv failed by invalid cmd, cmd = %d.", cmd);
            return -1;
    }

    return 0;
}

static int InitEthDev(Netdev_t* dev, DP_NetdevCfg_t* devCfg)
{
    BaseDev_t* baseDev = dev->ctx;

    if (devCfg->rxQueCnt <= 0 || devCfg->txCachedDeep <= 0) {
        DP_LOG_ERR("Init ethdev failed, invalid cfg!");
        return -1;
    }

    // rx缓存，用户可以使用DP_PutPkts接口直接放到缓存队列，所以允许rxBurst为空、但是rxCachedDeep不为空的场景
    if (devCfg->ops->txBurst == NULL) {
        DP_LOG_ERR("Init ethdev failed, devCfg->ops->txBurst NULL!");
        return -1;
    }

    if (strlen(devCfg->ifname) == 0) {
        if (strcpy_s(dev->name, DP_IF_NAMESIZE, "eth") != 0) {
            DP_LOG_ERR("Init ethdev failed by strcpy_s eth.");
            return -1;
        }
    } else {
        if (strcpy_s(dev->name, DP_IF_NAMESIZE, devCfg->ifname) != 0) {
            DP_LOG_ERR("Init ethdev failed by strcpy_s ifname.");
            return -1;
        }
    }

    baseDev->ctx = devCfg->ctx;
    baseDev->ctrl    = devCfg->ops->ctrl;
    baseDev->rxHash  = devCfg->ops->rxHash;
    baseDev->rxBurst = devCfg->ops->rxBurst;
    baseDev->txBurst = devCfg->ops->txBurst;

    dev->maxMtu     = DEFAULT_MAX_MTU;
    dev->minMtu     = DEFAULT_MIN_MTU;
    dev->ifflags    = 0;
    dev->linkHdrLen = sizeof(DP_EthHdr_t);
    dev->mtu        = dev->maxMtu - dev->linkHdrLen;
    dev->dstEntry   = PMGR_ENTRY_ETH_OUT;
    dev->in.ndEntry = PMGR_ENTRY_ND_OUT;
    dev->in6.ndEntry = PMGR_ENTRY_ND_OUT;

    return 0;
}

DevOps_t g_ethDevOps = {
    .privateLen = sizeof(BaseDev_t),
    .init       = InitEthDev,
    .deinit     = NULL,
    .ctrl       = CtrlEthDrv,
    .doRcv      = RcvEth,
    .doXmit     = BaseDevXmit,
    .rxHash     = BaseDevRxHash,
};
