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

#ifndef TBM_H
#define TBM_H

#include "dp_in_api.h"

#include "netdev.h"
#include "ns.h"
#include "utils_base.h"
#include "utils_spinlock.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DP_RTMGRP_NEIGH 4

typedef struct TBM_RtKey {
    DP_InAddr_t addr;
    DP_InAddr_t mask;
} TBM_RtKey_t;

typedef struct TBM_RtItem {
    LIST_ENTRY(TBM_RtItem) node;

    TBM_RtKey_t   key;
    DP_InAddr_t nxtHop;

    int        flags;
    int        valid;
    atomic32_t ref;

    NETDEV_IfAddr_t* ifaddr;
} TBM_RtItem_t;

typedef LIST_HEAD(, TBM_RtItem) RtItemList;

typedef struct TBM_RtTbl {
    int            cnt;
    int            maxCnt;
    uint32_t       updCnt;
    TBM_RtItem_t*  dftRt;
    void *rtItemPool; /* 使用定长内存池存储rt表项 */
    void *fibTbl; /* 使用外部注册的fib表作为查找表，存储路由key与索引的关系，如果未创建则默认使用遍历itempool的方式查找 */
    int fibTblUsed;
    atomic32_t     ref;
} TBM_RtTbl_t;

typedef struct TBM_NdItem {
    DP_InAddr_t  dst;
    DP_EthAddr_t mac;
    uint16_t       state;
    uint8_t        flags; // 预留字段
    uint8_t        type; // 预留字段
    int            valid;
    atomic32_t     ref;
    Netdev_t*      dev;
    uint32_t       insertTime;      /* 表项插入时间，当表项存在时间超过老化时间则删除 */
    uint32_t       updateTime;      /* 表项更新时间 */
} TBM_NdItem_t;

typedef struct TBM_NdFakeItem {
    TBM_NdItem_t item;
    int          cachedCnt;
    int          maxCachedCnt;
    Pbuf_t**     cached;
    Spinlock_t   lock;
} TBM_NdFakeItem_t;

typedef struct TBM_NdTbl {
    int            cnt;
    int            maxCnt;
    uint32_t       updTime;
    atomic32_t     ref;
    void *ndItemPool; /* 使用定长内存池存储nd表项 */
    void *ndLookUpTbl; /* 使用HASH表作为查找表，存储dst与索引的关系，如果未创建则默认使用遍历itempool的方式查找 */
    int LookUpTblUsed;
    int            fakeCnt;
    int            maxFakeCnt;
    TBM_NdFakeItem_t** fakeItems;
    Spinlock_t     lock;
} TBM_NdTbl_t;

#define TBM_ND_IS_VALID(item) ((item)->state & (TBM_ND_STATE_REACHABLE | TBM_ND_STATE_PERMANENT))

int TBM_Init(int slave);
void TBM_Deinit(void);

TBM_RtItem_t* TBM_GetRtItem(NS_Net_t* net, int vrfId, DP_InAddr_t addr);

void TBM_PutRtItem(TBM_RtItem_t* rt);

// 暂时通过路由 key 判断是否为子网广播路由
static inline int TBM_IsBroadcastRt(TBM_RtItem_t* rt)
{
    if (rt->key.mask == DP_INADDR_BROADCAST) {
        return 0;
    }

    return (((~(rt->key.mask)) & (rt->key.addr)) | rt->key.mask) == DP_INADDR_BROADCAST;
}

// 通过路由下一跳为 0 或本地接口地址判断是否直连路由
static inline int TBM_IsDirectRt(TBM_RtItem_t* rt)
{
    return rt->nxtHop == DP_INADDR_ANY || rt->nxtHop == rt->ifaddr->local;
}

TBM_NdItem_t* TBM_GetNdItem(NS_Net_t* net, int vrfId, DP_InAddr_t addr);

void TBM_PutNdItem(TBM_NdItem_t* nd);

// notify group 和 rtmgrp定义保持一致
enum {
    TBM_NOTIFY_TYPE_ND = DP_RTMGRP_NEIGH, // ND通知
};

typedef struct TBM_Notify {
    LIST_ENTRY(TBM_Notify) node;

    int      pid;
    int      protocol;
    uint32_t groups; // 组播位，复用定义的组播位

    /**
     * @brief
     * type: TBM_NOTIFY_TYPE_*
     * op: 各自对象的op
     * item: 对象，使用者自行保存相关信息
     */
    void (*cb)(struct TBM_Notify* tn, int type, int op, void* item);
    void* ctx;

    Spinlock_t lock;
} TBM_Notify_t;

int  TBM_AddNotify(NS_Net_t* net, TBM_Notify_t* tn);
void TBM_DelNotify(NS_Net_t* net, TBM_Notify_t* tn);
void TBM_Notify(NS_Net_t* net, int type, int op, void* item);

TBM_NdFakeItem_t* TBM_InsertFakeNd(Netdev_t* dev, DP_InAddr_t dst);

void TBM_UpdateFakeNdItem(Netdev_t* dev, Pbuf_t* pbuf);

TBM_NdFakeItem_t* TBM_GetFakeNdItem(NS_Net_t* net, int vrfId, DP_InAddr_t addr);
 
void TBM_PutFakeNdItem(TBM_NdFakeItem_t* nd);

int TBM_GetNdCnt(NS_Net_t* net);

#ifdef __cplusplus
}
#endif
#endif
