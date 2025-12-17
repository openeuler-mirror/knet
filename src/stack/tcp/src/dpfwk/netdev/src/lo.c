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

#include "pbuf.h"
#include "pmgr.h"
#include "worker.h"
#include "utils_ring.h"
#include "utils_spinlock.h"
#include "utils_log.h"

#include "dev.h"

static void LoDoRcv(NetdevQue_t* que)
{
    NetdevQue_t* rxQue = (NetdevQue_t*)&que->dev->rxQues[0]; // LO设备约定从读队列0缓存收包
    Pbuf_t** pbufs = (Pbuf_t**)rxQue->flash;
    uint32_t cnt = 0;
    // 环回口的报文都在缓存队列里
    if (RING_IsEmpty(&rxQue->cached) != 0) {
        return;
    }
    SPINLOCK_Lock(&rxQue->lock);
    cnt = RING_PopBurst(&rxQue->cached, (void**)pbufs, rxQue->flashSize);
    SPINLOCK_Unlock(&rxQue->lock);

    for (uint32_t i = 0; i < cnt; i++) {
        PBUF_SET_ENTRY(pbufs[i], PMGR_ENTRY_IP_IN);
        PBUF_SET_QUE_ID(pbufs[i], (uint8_t)rxQue->queid);
        DP_PBUF_SET_WID(pbufs[i], (uint8_t)rxQue->wid); // wid不会超过255，强转无风险
        PBUF_SET_DEV(pbufs[i], rxQue->dev);

        NET_DEV_ADD_RX_PKTS(rxQue, 1);
        NET_DEV_ADD_RX_BYTES(rxQue, PBUF_GET_SEG_LEN(pbufs[i]));

        // 报文分发
        PMGR_Dispatch(pbufs[i]);
    }
}

static void LoXmit(NetdevQue_t* que, Pbuf_t** pbuf, int cnt)
{
    NetdevQue_t* rxQue = (NetdevQue_t*)&que->dev->rxQues[0];  // LO设备约定从读队列0缓存发包
    Pbuf_t*      clone;

    NET_DEV_ADD_TX_PKTS(que, (uint64_t)cnt);
    for (uint16_t i = 0; i < cnt; i++) {
        clone = PBUF_Clone(pbuf[i]);
        DP_PbufFree(pbuf[i]);

        if (clone == NULL) {
            continue;
        }

        SPINLOCK_Lock(&rxQue->lock);

        NET_DEV_ADD_TX_BYTES(que, PBUF_GET_SEG_LEN(clone));

        if (RING_Push(&rxQue->cached, clone) != 0) {
            DP_PbufFree(clone);
        }

        SPINLOCK_Unlock(&rxQue->lock);
    }

    DP_WakeupWorker(rxQue->wid);
}

static int LoInit(Netdev_t* dev, DP_NetdevCfg_t* devCfg)
{
    if (devCfg->rxQueCnt != 1 || devCfg->txQueCnt != 1) {
        DP_LOG_ERR("Init lodev failed, invalid cfg!");
        return -1;
    }

    if (strlen(devCfg->ifname) == 0) {
        if (strcpy_s(dev->name, DP_IF_NAMESIZE, "lo") != 0) {
            DP_LOG_ERR("Init lodev failed by strcpy_s lo.");
            return -1;
        }
    } else {
        if (strcpy_s(dev->name, DP_IF_NAMESIZE, devCfg->ifname) != 0) {
            DP_LOG_ERR("Init lodev failed by strcpy_s ifname.");
            return -1;
        }
    }

    dev->maxMtu  = UINT16_MAX;
    dev->minMtu  = 0;
    dev->mtu     = UINT16_MAX;
    dev->ifflags = DP_IFF_LOOPBACK;
    dev->dstEntry = PMGR_ENTRY_BUTT;

    return 0;
}

static int LoRxHash(Netdev_t* dev, const struct DP_Sockaddr* srcAddr, DP_Socklen_t srcAddrLen,
    const struct DP_Sockaddr *dstAddr, DP_Socklen_t dstAddrLen)
{
    (void)dev;
    (void)srcAddr;
    (void)srcAddrLen;
    (void)dstAddr;
    (void)dstAddrLen;
    return 0;
}

DevOps_t g_loOps = {
    .privateLen = 0,
    .init       = LoInit,
    .deinit     = NULL,
    .ctrl       = NULL,
    .doRcv      = LoDoRcv,
    .doXmit     = LoXmit,
    .rxHash     = LoRxHash,
};
