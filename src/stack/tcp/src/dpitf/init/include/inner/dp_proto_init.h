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
#ifndef DP_PROTO_INIT_H
#define DP_PROTO_INIT_H

#include "dp_types.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

#define ATTR_WEAK __attribute__((weak))

#ifndef DPL2_ETH
#define ETH_INIT_ATTR ATTR_WEAK
#else
#define ETH_INIT_ATTR
#endif

#ifndef DPL3_IP
#define IP_INIT_ATTR ATTR_WEAK
#else
#define IP_INIT_ATTR
#endif

#define IP6_INIT_ATTR

#ifndef DPL3_RAW
#define RAW_INIT_ATTR ATTR_WEAK
#else
#define RAW_INIT_ATTR
#endif

#ifndef DPL4_UDP
#define UDP_INIT_ATTR ATTR_WEAK
#else
#define UDP_INIT_ATTR
#endif

#ifndef DPL4_TCP
#define TCP_INIT_ATTR ATTR_WEAK
#else
#define TCP_INIT_ATTR
#endif

// 以下协议初始声明，由各协议实现，在pmgr.c中通过模块宏控进行裁剪
extern int ETH_Init(int slave) ETH_INIT_ATTR;
extern int IP_Init(int slave) IP_INIT_ATTR;
extern int RAW_Init(int slave) RAW_INIT_ATTR;
extern int UDP_Init(int slave) UDP_INIT_ATTR;
extern int TCP_Init(int slave) TCP_INIT_ATTR;

extern void ETH_Deinit(int slave) ETH_INIT_ATTR;
extern void IP_Deinit(int slave) IP_INIT_ATTR;
extern void RAW_Deinit(int slave) RAW_INIT_ATTR;
extern void UDP_Deinit(int slave) UDP_INIT_ATTR;
extern void TCP_Deinit(int slave) TCP_INIT_ATTR;

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

#endif
