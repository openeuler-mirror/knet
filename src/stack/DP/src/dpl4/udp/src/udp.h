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

#ifndef UDP_H
#define UDP_H

#include "utils_base.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline uint16_t UdpGetRsvPort(uint16_t* minPort, uint16_t* range)
{
    uint16_t port;

    *minPort = 61212; // 61212 默认最新端口
    *range   = 2000; // 2000 随机范围
    port     = RAND_GEN() % (*range);

    return port + *minPort;
}

#ifdef __cplusplus
}
#endif
#endif