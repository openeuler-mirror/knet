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
#ifndef NS_H
#define NS_H

#include <stddef.h>

#include "utils_atomic.h"
#include "utils_spinlock.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    NS_NET_DEVTBL,
    NS_NET_FIB4,
    NS_NET_FIB6,
    NS_NET_ND,
    NS_NET_NL,
    NS_NET_PACKET,
    NS_NET_RAW,
    NS_NET_TCP,
    NS_NET_UDP,
    NS_NET_BUTT,
};

typedef struct NS_Net {
    void*      tbls[NS_NET_BUTT];
    atomic32_t ref;
    Spinlock_t lock;
    int32_t    id;
    int32_t    isUsed;
} NS_Net_t;

typedef struct {
    void* (*allocTbl)();
    void (*freeTbl)(void* tbl);
} NS_NetOps_t;

extern NS_Net_t* g_defaultNet;

#define NS_NET_MAX (64)
#define NS_INVALID_ID (NS_NET_MAX + 1)
#define NS_DEFAULT_ID (-1)

#define NS_GET_TBL(net, id) (((net) == NULL) ? g_defaultNet->tbls[(id)] : (net)->tbls[(id)])

#define NS_GET_DEV_TBL(net)    NS_GET_TBL((net), NS_NET_DEVTBL)
#define NS_GET_RT_TBL(net)     NS_GET_TBL((net), NS_NET_FIB4)
#define NS_GET_RT6_TBL(net)    NS_GET_TBL((net), NS_NET_FIB6)
#define NS_GET_ND_TBL(net)     NS_GET_TBL((net), NS_NET_ND)
#define NS_GET_RAW_TBL(net)    NS_GET_TBL((net), NS_NET_RAW)
#define NS_GET_TCP_TBL(net)    NS_GET_TBL((net), NS_NET_TCP)
#define NS_GET_UDP_TBL(net)    NS_GET_TBL((net), NS_NET_UDP)
#define NS_GET_PACKET_TBL(net) NS_GET_TBL((net), NS_NET_PACKET)
#define NS_GET_NL_TBL(net)     NS_GET_TBL((net), NS_NET_NL)

void NS_SetNetOps(int id, void *mAlloc, void *mFree);

static inline NS_Net_t* NS_GetDftNet(void)
{
    return g_defaultNet;
}

static inline void* NS_GetDftDevTbl(void)
{
    if (g_defaultNet == NULL) {
        return NULL;
    }
    return g_defaultNet->tbls[NS_NET_DEVTBL];
}

static inline int NS_Lock(NS_Net_t* ns)
{
    return SPINLOCK_Lock(ns == NULL ? &g_defaultNet->lock : &ns->lock);
}

static inline void NS_Unlock(NS_Net_t* ns)
{
    return SPINLOCK_Unlock(ns == NULL ? &g_defaultNet->lock : &ns->lock);
}

static inline void NS_RefNet(NS_Net_t* net)
{
    ATOMIC32_Inc(net != NULL ? &net->ref : &g_defaultNet->ref);
}

static inline void NS_DerefNet(NS_Net_t* net)
{
    ATOMIC32_Dec(net != NULL ? &net->ref : &g_defaultNet->ref);
}

static inline void NS_WaitNetIdle(NS_Net_t* net)
{
    while (ATOMIC32_Load(&net->ref) != 0) { }
}

static inline int NS_GetId(NS_Net_t* net)
{
    return net == NULL ? NS_DEFAULT_ID : net->id;
}

int NS_Create(void);

NS_Net_t* NS_GetNet(int id);

// 必须在PMGR_Init之后调用
int NS_Init(int slave);

void NS_Deinit(int slave);

#ifdef __cplusplus
}
#endif
#endif
