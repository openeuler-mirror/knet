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

#ifndef DP_INET_H
#define DP_INET_H

#include "dp_in_api.h"

#include "dp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief int类型主机序转换为网络序
 *
 * @param hostlong
 * @return
 */
uint32_t DP_Htonl(uint32_t hostlong);

/**
 * @brief short类型主机序转换为网络序
 *
 * @param hostshort
 * @return
 */
uint16_t DP_Htons(uint16_t hostshort);

/**
 * @brief int类型网络序转换为主机序
 *
 * @param netlong
 * @return
 */
uint32_t DP_Ntohl(uint32_t netlong);

/**
 * @brief short类型网络序转换为主机序
 *
 * @param netshort
 * @return
 */
uint16_t DP_Ntohs(uint16_t netshort);

DP_InAddr_t DP_InetAddr(const char* cp);

const char* DP_InetNtoa(struct DP_InAddr inaddr);

const char* DP_InetNtop(int af, const void* src, char* dst, int size);

int DP_InetPton(int af, const char* src, void* dst);

#if DP_BYTE_ORDER == DP_LITTLE_ENDIAN
#define DP_MAKE_IP_ADDR(aa, bb, cc, dd) ((uint32_t)(dd) << 24 | (uint32_t)(cc) << 16 | (uint32_t)(bb) << 8 | (aa))
#else
#define DP_MAKE_IP_ADDR(aa, bb, cc, dd) ((uint32_t)(aa) << 24 | (uint32_t)(bb) << 16 | (uint32_t)(cc) << 8 | (dd))
#endif

#define DP_INET_MASK_LEN 32
static inline uint32_t DP_MakeNetmask(uint32_t n)
{
    return DP_Htonl((0xFFFFFFFFULL << (DP_INET_MASK_LEN - n)) & 0xFFFFFFFF);
}

static inline int DP_NetmaskLen(uint32_t mask)
{
    int len = 0;
    uint32_t hostMask = DP_Ntohl(mask);

    while ((hostMask != 0) && ((hostMask & 0x80000000) != 0)) { // 0x80000000: uint32 最高位为 1
        len++;
        hostMask <<= 1;
    }

    if (hostMask != 0) {
        return -1; // 255: 掩码不连续时返回 255
    }

    return len;
}

#define DP_INADDR_LOOPBACK DP_MAKE_IP_ADDR(127, 0, 0, 1)

#ifdef __cplusplus
}
#endif
#endif
