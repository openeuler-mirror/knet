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
/**
 * @file dp_ether_api.h
 * @brief MAC地址结构信息
 */

#ifndef DP_ETHER_API_H
#define DP_ETHER_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup ether Ether
 * @ingroup base
 */

/**
 * @ingroup ether
 * MAC地址
 */
typedef struct {
    uint8_t addr[6];
} __attribute__((packed)) DP_EthAddr_t;

#ifdef __cplusplus
}
#endif

#endif
