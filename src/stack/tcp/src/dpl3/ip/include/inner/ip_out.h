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
#ifndef IP_OUT_H
#define IP_OUT_H

#include "dp_ip.h"

#include "pbuf.h"
#include "netdev.h"
#include "inet_sk.h"
#include "utils_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t mtu;
} IpTxCb_t; // 仅用于IP层传递信息

typedef struct {
    uint32_t ipid;
} ALIGNED_TO(CACHE_LINE) IpId_t; // 独占一个cacheline，避免多线程场景下伪共享

extern IpId_t* g_ipIdTbl;

static inline uint16_t IpGetGlobalId(void)
{
    static uint32_t ipid = 0xdeadbeef;
    return (ATOMIC32_Inc(&ipid)) & 0xFFFF;
}

static inline uint16_t IpGetId(uint8_t wid)
{
    return (g_ipIdTbl[wid].ipid++) & 0xFFFF;
}

#define IpTxCb(pbuf) PBUF_GET_CB((pbuf), IpTxCb_t*)

static inline uint16_t IpCalcCksum(Pbuf_t* pbuf, Netdev_t* dev, DP_IpHdr_t* ipHdr, uint8_t hdrLen)
{
    if ((NETDEV_TX_IPV4_CKSUM_ENABLED(dev)) &&
        (PBUF_GET_PKT_FLAGS(pbuf) & PBUF_PKTFLAGS_FRAGMENTED) == 0 && ipHdr->type != DP_IPPROTO_ICMP) {
        DP_PBUF_SET_OLFLAGS_BIT(pbuf, DP_PBUF_OLFLAGS_TX_IP_CKSUM);
        return 0;
    }

    return UTILS_CksumSwap(UTILS_Cksum(0, (uint8_t*)ipHdr, hdrLen));
}

void IpFillHdr(Pbuf_t* pbuf, const INET_FlowInfo_t* flow);

#ifdef __cplusplus
}
#endif
#endif
