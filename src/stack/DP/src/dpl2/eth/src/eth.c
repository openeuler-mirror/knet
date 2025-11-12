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

#include "dp_tbm.h"

#include "dp_ethernet.h"
#include "utils_log.h"
#include "utils_statistic.h"
#include "pbuf.h"
#include "pmgr.h"
#include "tbm.h"
#include "netdev.h"

static Pbuf_t* EthInput(Pbuf_t* pbuf)
{
    DP_INC_PKT_STAT(pbuf->wid, DP_PKT_ETH_IN);
    DP_EthHdr_t*   ethHdr = PBUF_MTOD(pbuf, DP_EthHdr_t*);
    uint16_t       pktLen = PBUF_GET_SEG_LEN(pbuf);
    Netdev_t*      dev;
    uint16_t       hdrLen = sizeof(DP_EthHdr_t);

    if (pktLen <= sizeof(*ethHdr)) {
        goto drop;
    }

    PBUF_CUT_HEAD(pbuf, hdrLen);

    dev = PBUF_GET_DEV(pbuf);
    PBUF_SET_PKT_TYPE(pbuf, PBUF_PKTTYPE_HOST);
    // 排除源mac是本接口的报文
    if (DP_MAC_IS_EQUAL(&ethHdr->src, &dev->hwAddr.mac)) {
        goto drop;
    }
    // 非arp报文需要校验dmac，如果是vlan的话，则需要在此校验前判断
    if (!(DP_MAC_IS_EQUAL(&ethHdr->dst, &dev->hwAddr.mac))) {
        if (DP_MAC_IS_BROADCAST(&ethHdr->dst)) {
            PBUF_SET_PKT_TYPE(pbuf, PBUF_PKTTYPE_BROADCAST);
        } else {
            goto drop;
        }
    }

    switch (ethHdr->type) {
        case UTILS_HTONS(DP_ETH_P_IP):
            PBUF_SET_ENTRY(pbuf, PMGR_ENTRY_IP_IN);
            return pbuf;
        case UTILS_HTONS(DP_ETH_P_ARP):
            DP_INC_PKT_STAT(pbuf->wid, DP_PKT_ARP_DELIVER);
            PBUF_SET_ENTRY(pbuf, PMGR_ENTRY_ARP_IN);
            return pbuf;
        default:
            PBUF_SET_ENTRY(pbuf, PMGR_ENTRY_NONE);
            return pbuf;
    }

drop:
    PBUF_Free(pbuf);

    return NULL;
}

static Pbuf_t* EthOutput(Pbuf_t* pbuf)
{
    DP_INC_PKT_STAT(pbuf->wid, DP_PKT_ETH_OUT);
    NETDEV_XmitPbuf(pbuf);

    return NULL;
}

static Pbuf_t* NdOutput(Pbuf_t* pbuf)
{
    Netdev_t*             dev = PBUF_GET_DEV(pbuf);
    TBM_NdItem_t*         nd  = PBUF_GET_ND(pbuf);
    DP_EthHdr_t* hdr;
    if (PBUF_GET_HEADROOM(pbuf) < sizeof(DP_EthHdr_t)) {
        PBUF_Free(pbuf);
        return NULL;
    }

    PBUF_PUT_HEAD(pbuf, sizeof(DP_EthHdr_t));
    hdr = PBUF_MTOD(pbuf, DP_EthHdr_t*);

    hdr->type = PBUF_GET_L3_TYPE(pbuf);
    DP_MAC_COPY(&hdr->src, &dev->hwAddr.mac);
    if (nd != NULL) {
        DP_MAC_COPY(&hdr->dst, (DP_EthAddr_t*)&nd->mac);
    } else if (PBUF_GET_PKT_TYPE(pbuf) == PBUF_PKTTYPE_BROADCAST) {
        DP_MAC_SET_BROADCAST(&hdr->dst);
    } else {
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_ARP_SEARCH_IN);
        nd = TBM_GetNdItem(dev->net, dev->vrfId, PBUF_GET_DST_ADDR(pbuf));
        // 真表中不存在表项或表项不可用时，查找假表
        if (nd == NULL || (nd->state != DP_ND_STATE_REACHABLE && nd->state != DP_ND_STATE_PERMANENT)) {
            PBUF_CUT_HEAD(pbuf, sizeof(DP_EthHdr_t));
            TBM_UpdateFakeNdItem(dev, pbuf);
            PBUF_Free(pbuf);

            if (nd != NULL) {
                TBM_PutNdItem(nd);
            }
            return NULL;
        }
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_ARP_HAVE_NORMAL_ARP);
        DP_MAC_COPY(&hdr->dst, (DP_EthAddr_t*)&nd->mac);

        TBM_PutNdItem(nd);
    }

    PBUF_SET_ENTRY(pbuf, dev->dstEntry);

    return pbuf;
}

int ETH_Init(int slave)
{
    PMGR_AddEntry(PMGR_ENTRY_ETH_IN, EthInput);
    PMGR_AddEntry(PMGR_ENTRY_ETH_OUT, EthOutput);
    PMGR_AddEntry(PMGR_ENTRY_ND_OUT, NdOutput);

    (void)slave;
    return 0;
}

void ETH_Deinit(int slave)
{
    (void)slave;
}
