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
 * @file dp_udp.h
 * @brief UDP首部及其相关定义
 */

#ifndef DP_UDP_H
#define DP_UDP_H

#include "dp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DP_UdpHdr {
    uint16_t sport;
    uint16_t dport;
    uint16_t len;
    uint16_t chksum;
} DP_PACKED DP_UdpHdr_t;

#define UDP_HLEN 8

#ifdef __cplusplus
}
#endif
#endif