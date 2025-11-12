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

#include "ethdev.h"

#include "pbuf.h"
#include "pmgr.h"
#include "utils_ring.h"
#include "utils_spinlock.h"

#include "dev.h"

#define MAX_ETH_RECV_NUM_ONCE 16u // 单次收包最大规格数量

typedef struct {
    void* ctx;
    int (*ctrl)(void* ctx, int opt, void* arg, uint32_t argLen);
    int (*rxBurst)(void* ctx, uint16_t queid, void** bufs, int cnt);
    int (*txBurst)(void* ctx, uint16_t queid, void** bufs, int cnt);
} EthDev_t;

static void ClearInvalidPkt(Pbuf_t **pbufs, int32_t index, int32_t maxCnt)
{
    int idx = index;
    while (idx < maxCnt) {
        if (pbufs[idx] != NULL) {
            PBUF_Free(pbufs[idx]);
        }
        idx++;
    }
}

void RcvEth(NetdevQue_t* que)
{
    NetdevQue_t* rxQue  = (NetdevQue_t*)que;
    EthDev_t*    ethDev = (EthDev_t*)(rxQue->dev->ctx);
    Pbuf_t*      pbufs[MAX_ETH_RECV_NUM_ONCE];
    int32_t      cnt = 0;

    // 先处理缓存
    if (RING_IsEmpty(&rxQue->cached) == 0) {
        SPINLOCK_Lock(&rxQue->lock);
        cnt = (int32_t)RING_PopBurst(&rxQue->cached, (void**)pbufs, sizeof(pbufs) / sizeof(pbufs[0]));
        SPINLOCK_Unlock(&rxQue->lock);
    }

    if (cnt == MAX_ETH_RECV_NUM_ONCE) {
        goto dispatch;
    }

    if (ethDev->rxBurst != NULL) {
        int rxCnt = ethDev->rxBurst(ethDev->ctx, rxQue->queid, (void**)&pbufs[cnt], MAX_ETH_RECV_NUM_ONCE - (int)cnt);
        for (int i = 0; i < rxCnt; i++, cnt++) {
            if (pbufs[cnt] == NULL) {
                ClearInvalidPkt(pbufs, cnt, rxCnt);
                break;
            }
        }
    }

dispatch:
    for (int32_t i = 0; i < cnt; i++) {
        PBUF_SET_ENTRY(pbufs[i], PMGR_ENTRY_ETH_IN);
        PBUF_SET_WID(pbufs[i], (uint8_t)rxQue->wid); // wid不会超过255，强转无风险
        PBUF_SET_QUE_ID(pbufs[i], (uint8_t)que->queid); // queid不会超过255，强转无风险
        PBUF_SET_DEV(pbufs[i], rxQue->dev);

        // 报文分发
        PMGR_Dispatch(pbufs[i]);
    }
}

static void XmitPkt(NetdevQue_t* que, Pbuf_t** pbuf, uint16_t cnt)
{
    EthDev_t*  ethDev = (EthDev_t*)(que->dev->ctx);
    ethDev->txBurst(ethDev->ctx, que->queid, (void**)pbuf, cnt);
}

static int CtrlEthDrv(Netdev_t* dev, int cmd, void* val)
{
    EthDev_t* ethDev = (EthDev_t*)(dev->ctx);
    switch (cmd) {
        case DEV_CTL_GET_PRIVATE:
            *(void**)val = ethDev->ctx;
            break;
        default:
            return -1;
    }

    return 0;
}

static int InitEthDev(Netdev_t* dev, DP_NetdevCfg_t* devCfg)
{
    EthDev_t* ethDev = dev->ctx;

    if (devCfg->rxQueCnt <= 0 || devCfg->txCachedDeep <= 0) {
        return -1;
    }

    // rx缓存，用户可以使用DP_PutPkts接口直接放到缓存队列，所以允许rxBurst为空、但是rxCachedDeep不为空的场景
    if (devCfg->ops->txBurst == NULL) {
        return -1;
    }

    if (strlen(devCfg->ifname) == 0) {
        if (strcpy_s(dev->name, DP_IF_NAMESIZE, "eth") != 0) {
            return -1;
        }
    } else {
        if (strcpy_s(dev->name, DP_IF_NAMESIZE, devCfg->ifname) != 0) {
            return -1;
        }
    }

    ethDev->ctx = devCfg->ctx;
    ethDev->ctrl    = devCfg->ops->ctrl;
    ethDev->rxBurst = devCfg->ops->rxBurst;
    ethDev->txBurst = devCfg->ops->txBurst;

    dev->maxMtu     = DEFAULT_MAX_MTU;
    dev->minMtu     = DEFAULT_MIN_MTU;
    dev->ifflags    = 0;
    dev->linkHdrLen = sizeof(DP_EthHdr_t);
    dev->mtu        = dev->maxMtu - dev->linkHdrLen;
    dev->dstEntry   = PMGR_ENTRY_ETH_OUT;
    dev->in.ndEntry = PMGR_ENTRY_ND_OUT;

    return 0;
}

static void DeInitEthDev(Netdev_t* dev)
{
    (void)dev;
}

DevOps_t g_ethDevOps = {
    .privateLen = sizeof(EthDev_t),
    .init       = InitEthDev,
    .deinit     = DeInitEthDev,
    .ctrl       = CtrlEthDrv,
    .doRcv      = RcvEth,
    .doXmit     = XmitPkt,
};
