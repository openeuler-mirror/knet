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
#include "utils_cksum.h"

uint32_t UTILS_Cksum(uint32_t cksum, uint8_t* data, uint32_t len)
{
    uint8_t temp[2];
    uint8_t* p = data;
    uint32_t  ret = cksum;
    uint32_t dataLen = len;

    while (dataLen > 1) {
        temp[0] = *p;
        temp[1] = *(p + 1);
        p += 2; // 这里用2个uint8_t拼成一个uin16_t
        dataLen -= 2; // 这里用2个uint8_t拼成一个uin16_t
        ret += *(uint16_t*)temp;
    }

    if (dataLen > 0) {
        temp[0] = *(uint8_t*)p;
        temp[1] = 0;

        ret += *(uint16_t*)temp;
    }

    return ret;
}

// 该函数主要为计算多个Pbuf场景下校验和，该场景下len仅会传入偶数
uint32_t UTILS_CkMultiSum(uint32_t cksum, uint8_t* data, uint32_t len)
{
    uint8_t  temp[2];
    uint32_t res = cksum;
    // 这里用两个uint8_t拼成一个uin16_t所以要+2
    for (uint32_t i = 0; i < len; i += 2) {
        temp[0] = data[i];
        temp[1] = data[i + 1];
        res += *(uint16_t*)temp;
    }

    return res;
}

uint16_t UTILS_CksumAdd(uint32_t cksum)
{
    uint32_t ret = cksum;
    do {
        ret = (ret & 0xffff) + (ret >> 16); // 右移16位，以此实现高16位和低16位的数值相加
    } while ((ret >> 16) != 0); // 右移16位，非0则继续

    return (uint16_t)ret;
}

uint16_t UTILS_CksumSwap(uint32_t cksum)
{
    return ~UTILS_CksumAdd(cksum);
}
