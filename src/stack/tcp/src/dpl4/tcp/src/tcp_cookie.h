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

#ifndef TCP_COOKIE_H
#define TCP_COOKIE_H

#include "dp_in_api.h"

#include "dp_types.h"

#include "tcp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// COOKIE触发门限，与控制面保持一致，使用 4/5
#define TCP_COOKIE_THRESHOLD(x) (((x) << 2) / 5)

// 在cookie情况下,存储在tsVal中的syn选项，所占位，共占6位
#define TCP_COOKIE_OPT_ECN  0x1
#define TCP_COOKIE_OPT_SACK 0x2
#define TCP_COOKIE_OPT_WS   0x3c
#define TCP_COOKIE_OPT_MASK (TCP_COOKIE_OPT_ECN | TCP_COOKIE_OPT_SACK | TCP_COOKIE_OPT_WS)

#define TCP_COOKIE_BITS 24
#define TCP_COOKIE_MASK (((uint32_t)1 << TCP_COOKIE_BITS) - 1)

#define TCP_MAX_MSSID_INDEX  9

#define TCP_COOKIE_INC_TICK (64 * TCP_SLOW_TIMER_HZ)

extern uint16_t g_TcpMssTable[TCP_MAX_MSSID_INDEX];

#define TCP_MIN_MSS 64
#define TCP_BSD_MSS 512
#define TCP_DEF4_MSS 536
#define TCP_BSD_DOUBLE_MSS 1024
#define TCP_DEF6_MSS 1220
#define TCP_ETH_IPV6_MSS 1440
#define TCP_ETH_IPV4_MSS 1460
#define TCP_PMTUD_MSS 4312
#define TCP_JUMBO_MSS 8960

static inline bool TcpCheckCookie(TcpSk_t* tcpSk)
{
    if (tcpSk->childCnt < tcpSk->backlog &&
        tcpSk->childCnt > (int)TCP_COOKIE_THRESHOLD((uint32_t)tcpSk->backlog)) {
        return true;
    }
    return false;
}

typedef struct {
    DP_InAddr_t laddr;
    DP_InAddr_t paddr;
    uint16_t sport;
    uint16_t dport;
    uint32_t iss;
} DP_PACKED TcpCookieInetHashInfo_t; // 保存TCP五元组信息

int TcpCookieGetMssIndex(uint16_t mss);

void TcpCookieCalcInetIss(TcpPktInfo_t* pi, TcpCookieInetHashInfo_t* info, uint16_t mss);

bool TcpCookieVerifyInetIss(TcpPktInfo_t* pi, TcpCookieInetHashInfo_t* info, uint16_t* mss);

uint32_t TcpCookieGenTsval(TcpSynOpts_t* synOpts, uint8_t hasOpt);

void TcpCookieSetOpts(TcpSk_t* tcp, TcpSynOpts_t* opts);

#ifdef __cplusplus
}
#endif

#endif
