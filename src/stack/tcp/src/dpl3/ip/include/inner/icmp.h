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

#ifndef ICMP_H
#define ICMP_H

#include "pbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ICMP_NET_UNREACHABLE = 0,
    ICMP_HOST_UNREACHABLE,
    ICMP_PROTOCOL_UNREACHABLE,
    ICMP_PORT_UNREACHABLE,
    ICMP_NEED_FRAGMENTATION, // fragmentation needed and DF set
    ICMP_SOURCE_ROUTE_FAILED, // source route failed
} ICMP_UnreachableCode_t;

/**
 * @brief 生成ICMP不可达消息
 *
 * @param orig
 * @param code
 * @return
 */
Pbuf_t* ICMP_GenUnreachableMsg(Pbuf_t* orig, ICMP_UnreachableCode_t code);

#ifdef __cplusplus
}
#endif
#endif
