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

#include "dp_in_api.h"

#include "dp_inet.h"
#include "dp_errno.h"

#include "utils_base.h"

#define DP_INET_ADDRSTRLEN 16
#define DP_INET_DOTADDRLEN 3

#define NUMBER_LEN_OVER_HUNDRED 3
#define NUMBER_LEN_OVER_TEN 2
#define HUNDRED_BASE 100
#define TEN_BASE 10

static const char* Inaddr2Str(const struct DP_InAddr* inaddr, char* dst, int size)
{
    uint8_t f[4];

    if ((dst == NULL) || (size < DP_INET_ADDRSTRLEN)) {
        DP_SET_ERRNO(ENOSPC);
        return NULL;
    }

    f[0] = ((const uint8_t*)inaddr)[0];
    f[1] = ((const uint8_t*)inaddr)[1];
    f[2] = ((const uint8_t*)inaddr)[2];
    f[3] = ((const uint8_t*)inaddr)[3];

    for (int i = 0, j = 0; i < (int)(sizeof(DP_InAddr_t)); i++) {
        if (f[i] > 99) { // 大于99 判断3位数
            dst[j++] = '0' + f[i] / 100; // / 100 获取百位，百位转换为字符
            f[i] %= 100; // % 100 获取十位及个位
            dst[j++] = '0' + f[i] / 10; // / 10 获取个位
            f[i] %= 10;
        } else if (f[i] > 9) { // 大于9 判断两位数
            dst[j++] = '0' + f[i] / 10; // / 10获取个位
            f[i] %= 10;
        }
        dst[j++] = '0' + f[i];
        dst[j++] = (i == DP_INET_DOTADDRLEN ? 0 : '.'); // 判断后面一位是否为'.'
    }

    return dst;
}

static int CalcIpAddr(uint8_t index, uint8_t *ipField, uint8_t *field, uint8_t numLen)
{
    int fieldVal = 0;
    if (numLen == NUMBER_LEN_OVER_HUNDRED) {  // 判断数字是否>100
        for (int i = 0; i < numLen; i++) {
            fieldVal = fieldVal * TEN_BASE + field[i];
        }

        if (fieldVal > 0xFF) {
            return -1;
        }
    } else if (numLen == NUMBER_LEN_OVER_TEN) { // 判断数字是否>10 <100
        fieldVal = field[0] * TEN_BASE + field[1];
    } else {
        fieldVal = field[0];
    }

    ipField[index] = (uint8_t)fieldVal;
    return 0;
}

static int Str2Inaddr(const char* src, DP_InAddr_t* dst)
{
    uint8_t     field[3] = {0};
    int         fLen = 0;
    const char* cur  = src;
    uint8_t     ipField[4] = {0};
    uint8_t     ipFieldIndex = 0;
    int         i = 0;

    for (i = 0; i < DP_INET_ADDRSTRLEN; i++) {
        char tmp = cur[i];
        if ((tmp == '.') || (tmp == 0)) {
            if (fLen == 0 || (fLen != 1 && field[0] == 0)) {
                goto err;
            }

            if (CalcIpAddr(ipFieldIndex, ipField, field, (uint8_t)fLen) != 0) {
                goto err;
            }

            if (tmp == 0) { // 已到末尾
                break;
            }

            ipFieldIndex++;
            if (ipFieldIndex >= (uint8_t)sizeof(ipField)) {
                goto err;
            }
            fLen = 0;

            continue;
        }
        if ((tmp < '0') || (tmp > '9') || (fLen >= 3)) { // 异常判断，字符范围是0-9，长度不能超过3位数
            goto err;
        }

        field[fLen++] = (uint8_t)tmp - '0';
    }

    if ((i == DP_INET_ADDRSTRLEN) || (ipFieldIndex != ((uint8_t)sizeof(ipField) - 1))) {
        goto err;
    }

    *dst = DP_MAKE_IP_ADDR(ipField[0], ipField[1], ipField[2], ipField[3]); // 按大小端组合IP地址的0,1,2,3四个点分字段

    return 1;
err:
    DP_SET_ERRNO(EINVAL);

    return 0;
}

DP_InAddr_t DP_InetAddr(const char* cp)
{
    DP_InAddr_t ret;

    if (cp == NULL) {
        DP_SET_ERRNO(EINVAL);
        return DP_INADDR_NONE;
    }

    if (Str2Inaddr(cp, &ret) != 1) {
        return DP_INADDR_NONE;
    }

    return ret;
}

const char* DP_InetNtoa(struct DP_InAddr inaddr)
{
    static char ipStr[DP_INET_ADDRSTRLEN];

    return DP_InetNtop(DP_AF_INET, &inaddr, ipStr, sizeof(ipStr));
}

const char* DP_InetNtop(int af, const void* src, char* dst, int size)
{
    if (src == NULL) {
        DP_SET_ERRNO(EAFNOSUPPORT);
        return NULL;
    }

    if (af == DP_AF_INET) {
        return Inaddr2Str((const struct DP_InAddr*)src, dst, size);
    } else if (af == DP_AF_INET6) {
        DP_SET_ERRNO(EAFNOSUPPORT);
        return NULL;
    }

    DP_SET_ERRNO(EAFNOSUPPORT);
    return NULL;
}

int DP_InetPton(int af, const char* src, void* dst)
{
    if (src == NULL) {
        DP_SET_ERRNO(EAFNOSUPPORT);
        return -1;
    }
    if (dst == NULL) {
        DP_SET_ERRNO(ENOSPC);
        return -1;
    }

    if (af == DP_AF_INET) {
        return Str2Inaddr(src, (DP_InAddr_t*)dst);
    } else if (af == DP_AF_INET6) {
        DP_SET_ERRNO(EAFNOSUPPORT);
        return -1;
    }

    DP_SET_ERRNO(EAFNOSUPPORT);
    return -1;
}

uint32_t DP_Htonl(uint32_t hostlong)
{
    return UTILS_HTONL(hostlong);
}

uint16_t DP_Htons(uint16_t hostshort)
{
    return UTILS_HTONS(hostshort);
}

uint32_t DP_Ntohl(uint32_t netlong)
{
    return UTILS_NTOHL(netlong);
}

uint16_t DP_Ntohs(uint16_t netshort)
{
    return UTILS_NTOHS(netshort);
}
