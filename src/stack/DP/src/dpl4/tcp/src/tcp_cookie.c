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

#include "tcp_cookie.h"

#include <securec.h>

#include "worker.h"
#include "utils_sha256.h"
#include "tcp_types.h"
#include "tcp_timer.h"
#include "tcp_sack.h"

#define COOKIE_DATA_LEN 256
#define HASH_DIGEST_LEN 32      // sha256 HASH算法摘要长度

uint16_t g_TcpMssTable[TCP_MAX_MSSID_INDEX] = {
    TCP_MIN_MSS,
    TCP_BSD_MSS,
    TCP_DEF4_MSS,
    TCP_BSD_DOUBLE_MSS,
    TCP_DEF6_MSS,
    TCP_ETH_IPV6_MSS,
    TCP_ETH_IPV4_MSS,
    TCP_PMTUD_MSS,
    TCP_JUMBO_MSS,
};

int TcpCookieGetMssIndex(uint16_t mss)
{
    int mssIndex;
    for (mssIndex = TCP_MAX_MSSID_INDEX - 1; mssIndex > 0; mssIndex--) {
        if (mss >= g_TcpMssTable[mssIndex]) {
            break;
        }
    }

    return mssIndex;
}

static uint32_t TcpCookieGetTimestamp(void)
{
    // 监听tcp没有核，使用0核定时器获取时间
    return WORKER_GetTime() / TCP_COOKIE_INC_TICK;
}

static uint32_t TcpCookieInetHash(TcpCookieInetHashInfo_t* info, uint32_t count)
{
    uint32_t cookieData[COOKIE_DATA_LEN] = {0};
    uint32_t offset = 0;
    uint32_t digestBuffer[HASH_DIGEST_LEN] = {0};

    if (COOKIE_DATA_LEN >= sizeof(TcpCookieInetHashInfo_t)) {
        (void)memcpy_s((cookieData + offset), sizeof(uint16_t), (uint8_t *)&(info->dport), sizeof(uint16_t));
        offset += (uint32_t)sizeof(uint16_t);

        (void)memcpy_s((cookieData + offset), sizeof(uint16_t), (uint8_t *)&(info->sport), sizeof(uint16_t));
        offset += (uint32_t)sizeof(uint16_t);

        (void)memcpy_s((cookieData + offset), sizeof(uint32_t), (uint8_t *)&(info->paddr), sizeof(uint32_t));
        offset += (uint32_t)sizeof(uint32_t);

        (void)memcpy_s((cookieData + offset), sizeof(uint32_t), (uint8_t *)&(info->laddr), sizeof(uint32_t));
        offset += (uint32_t)sizeof(uint32_t);
    }

    if (COOKIE_DATA_LEN - offset >= sizeof(uint32_t)) {
        (void)memcpy_s((cookieData + offset), sizeof(uint32_t), (uint8_t *)&count, sizeof(uint32_t));
        offset += (uint32_t)sizeof(uint32_t);
    }

    SHA256GenHash((const uint8_t *)cookieData, offset,
        (uint8_t *)digestBuffer, HASH_DIGEST_LEN);

    return digestBuffer[1];
}

void TcpCookieCalcInetIss(TcpPktInfo_t* pi, TcpCookieInetHashInfo_t* info, uint16_t mss)
{
    uint32_t hash1;
    uint32_t hash2;
    uint32_t mssIndex = (uint32_t)TcpCookieGetMssIndex(mss);
    uint32_t timeSec = TcpCookieGetTimestamp();

    hash1 = TcpCookieInetHash(info, 0);
    hash2 = (TcpCookieInetHash(info, (timeSec & 0xFF)) + mssIndex) & TCP_COOKIE_MASK;

    info->iss = hash1 + hash2 + pi->seq + (timeSec << TCP_COOKIE_BITS);
}

bool TcpCookieVerifyInetIss(TcpPktInfo_t* pi, TcpCookieInetHashInfo_t* info, uint16_t* mss)
{
    uint32_t hash1;
    uint32_t hash2;
    uint32_t mssIndex;

    hash1 = TcpCookieInetHash(info, 0);
    mssIndex = pi->ack - hash1 - pi->seq;

    hash2 = TcpCookieInetHash(info, (mssIndex >> TCP_COOKIE_BITS));
    mssIndex = (mssIndex - hash2) & TCP_COOKIE_MASK;

    if (mssIndex >= TCP_MAX_MSSID_INDEX) {
        return false;
    }
    *mss = g_TcpMssTable[mssIndex];
    return true;
}

uint32_t TcpCookieGenTsval(TcpSynOpts_t* synOpts, uint8_t hasOpt)
{
    // 监听tcp没有核，使用0核定时器获取时间
    uint32_t tsVal = g_tcpTimers[0]->fastTimer.twTick;
    // 后6位分别存 ECN(0/1)、SACK(0/1)、WScale(0~14)的信息
    // 更新本报文时间戳，保证后6位修改后，后续报文不会比当前时间小
    tsVal -= (TCP_COOKIE_OPT_MASK + 1);
    tsVal &= ~TCP_COOKIE_OPT_MASK;

    // 拥塞标识ECN当前不支持
    if ((hasOpt & synOpts->rcvSynOpt & TCP_SYN_OPT_SACK_PERMITTED) != 0) {
        tsVal |= TCP_COOKIE_OPT_SACK;
    }
    if ((hasOpt & synOpts->rcvSynOpt & TCP_SYN_OPT_WINDOW) != 0) {
        tsVal |= ((synOpts->ws << 2) & TCP_COOKIE_OPT_WS);  // 2: 填入0x3c位置,左移2位
    }
    return tsVal;
}

void TcpCookieSetOpts(TcpSk_t* tcp, TcpSynOpts_t* opts)
{
    // 能够协商选项时表示带有时间戳选项
    tcp->negOpt |= TCP_SYN_OPT_TIMESTAMP;
    tcp->tsEcho = opts->tsVal;
    tcp->tsVal  = TcpGetRttTick(tcp);

    // mss已在cookiesyn流程中协商
    // ws协商，如果对端不支持这个选项，则本端的sndWs设置为0
    if ((opts->tsEcho & TCP_COOKIE_OPT_WS) != 0) {
        tcp->sndWs = (uint8_t)(opts->tsEcho & TCP_COOKIE_OPT_WS) >> 2;    // 2 : WScale在时间戳中占3-6位
        tcp->negOpt |= TCP_SYN_OPT_WINDOW;
    } else {
        tcp->rcvWs = 0;
        tcp->sndWs = 0;
    }

    // sack协商选项在写入时间戳时已协商
    if ((opts->tsEcho & TCP_COOKIE_OPT_SACK) != 0) {
        tcp->negOpt |= TCP_SYN_OPT_SACK_PERMITTED;
        TcpInitSackInfo(&tcp->sackInfo);
    } else {
        tcp->negOpt &= ~TCP_SYN_OPT_SACK_PERMITTED;
    }
}
