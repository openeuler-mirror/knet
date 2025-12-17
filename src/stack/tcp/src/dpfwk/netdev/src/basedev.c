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
#include "basedev.h"

static void ClearInvalidPkt(Pbuf_t **pbufs, int index, int maxCnt)
{
    int idx = index;
    while (idx < maxCnt) {
        if (pbufs[idx] != NULL) {
            PBUF_Free(pbufs[idx]);
        }
        idx++;
    }
}

int BaseDevRcv(NetdevQue_t* rxQue, Pbuf_t** pbufs, int cnt)
{
    BaseDev_t* dev = (BaseDev_t*)(rxQue->dev->ctx);
    int        rxCnt = 0;
    int        rxBurstCnt;

    if (RING_IsEmpty(&rxQue->cached) == 0) {
        SPINLOCK_Lock(&rxQue->lock);
        rxCnt = (int)RING_PopBurst(&rxQue->cached, (void**)pbufs, (uint32_t)cnt);
        SPINLOCK_Unlock(&rxQue->lock);
    }
    if (rxCnt == cnt || dev->rxBurst == NULL) {
        return rxCnt;
    }

    rxBurstCnt = dev->rxBurst(dev->ctx, rxQue->queid, (void**)&pbufs[rxCnt], cnt - rxCnt);
    for (int i = 0; i < rxBurstCnt; i++, rxCnt++) {
        if (pbufs[rxCnt] == NULL) {
            ClearInvalidPkt(pbufs, rxCnt, rxBurstCnt);
            break;
        }
    }
    return rxCnt;
}

void BaseDevXmit(NetdevQue_t* txQue, Pbuf_t** pbufs, int cnt)
{
    BaseDev_t* dev = (BaseDev_t*)(txQue->dev->ctx);

    NET_DEV_ADD_TX_PKTS(txQue, (uint64_t)cnt);
    for (int i = 0; i < cnt; i++) {
        NET_DEV_ADD_TX_BYTES(txQue, PBUF_GET_SEG_LEN(pbufs[i]));
    }

    int ret = dev->txBurst(dev->ctx, txQue->queid, (void**)pbufs, cnt);
    if (ret != 0) {
        NET_DEV_ADD_TX_DROP(txQue, (uint64_t)cnt);
    }
}

int BaseDevRxHash(Netdev_t* dev, const struct DP_Sockaddr* rAddr, DP_Socklen_t rAddrLen,
    const struct DP_Sockaddr *lAddr, DP_Socklen_t lAddrLen)
{
    BaseDev_t* baseDev = (BaseDev_t*)(dev->ctx);
    if (baseDev->rxHash == NULL) {
        return 0;
    }
    return baseDev->rxHash(baseDev->ctx, rAddr, rAddrLen, lAddr, lAddrLen);
}
