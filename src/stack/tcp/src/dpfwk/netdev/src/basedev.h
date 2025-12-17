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
#ifndef BASEDEV_H
#define BASEDEV_H

#include "netdev.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_MTU (1500u)
#define DEFAULT_MAX_MTU (9614u)
#define DEFAULT_MIN_MTU (46u)

typedef struct {
    void* ctx;
    int (*ctrl)(void* ctx, int opt, void* arg, uint32_t argLen);
    int (*rxHash)(void* ctx, const struct DP_Sockaddr* rAddr, DP_Socklen_t rAddrLen,
        const struct DP_Sockaddr *lAddr, DP_Socklen_t lAddrLen);
    int (*rxBurst)(void* ctx, uint16_t queid, void** bufs, int cnt);
    int (*txBurst)(void* ctx, uint16_t queid, void** bufs, int cnt);
} BaseDev_t;

int BaseDevRcv(NetdevQue_t* rxQue, Pbuf_t** pbufs, int cnt);

void BaseDevXmit(NetdevQue_t* txQue, Pbuf_t** pbufs, int cnt);

int BaseDevRxHash(Netdev_t* dev, const struct DP_Sockaddr* rAddr, DP_Socklen_t rAddrLen,
    const struct DP_Sockaddr *lAddr, DP_Socklen_t lAddrLen);

#ifdef __cplusplus
}
#endif
#endif
