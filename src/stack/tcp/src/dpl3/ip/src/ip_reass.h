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

#ifndef IP_REASS_H
#define IP_REASS_H

#include "pbuf.h"
#include "dp_ip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IP_DF_FRAG UTILS_HTONS(DP_IP_FRAG_DF) // 不分片

#define IP_REASS_TIME_INTERVAL (100) // 每个一秒扫描一次超时

#define IsFragPkt(ipHdr) (((ipHdr)->off & (~IP_DF_FRAG)) != 0)

DP_Pbuf_t* IpReass(DP_Pbuf_t* pbuf, DP_IpHdr_t* ipHdr, uint8_t hdrLen);

void IpReassTimer(int wid, uint32_t tickNow);

int IpReassInit(void);

void IpReassDeinit(void);

#ifdef __cplusplus
}
#endif
#endif
