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
#ifndef UTILS_CKSUM_H
#define UTILS_CKSUM_H

#include "dp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t UTILS_Cksum(uint32_t cksum, uint8_t* data, uint32_t len);

// 该函数主要为计算多个Pbuf场景下校验和，该场景下len仅会传入偶数
uint32_t UTILS_CkMultiSum(uint32_t cksum, uint8_t* data, uint32_t len);

uint16_t UTILS_CksumAdd(uint32_t cksum);

uint16_t UTILS_CksumSwap(uint32_t cksum);

#ifdef __cplusplus
}
#endif
#endif
