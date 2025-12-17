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
#ifndef ND_H
#define ND_H

#include "tbm.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NDTBL_HASH_MASK ((CFG_GET_VAL(DP_CFG_ARP_MAX)) - 1)

typedef struct TBM_NdItem NdItem_t;
typedef struct TBM_NdFakeItem NdFakeItem_t;
typedef struct TBM_NdTbl NdTbl_t;

void* AllocNdTbl(void);

void FreeNdTbl(void* tbl);

NdItem_t* AllocNdItem(void);

void FreeNdItem(NdItem_t* ndItem);

int InsertNd(NdTbl_t* tbl, NdItem_t* item);

int RemoveNd(NdTbl_t* tbl, TBM_IpAddr_t dst, Netdev_t* dev);

int GetNdCnt(NdTbl_t* tbl);

NdItem_t* GetNd(NdTbl_t* tbl, TBM_IpAddr_t dst);

void PutNd(NdItem_t* item);

NdFakeItem_t* AllocNdFakeItem(void);

void FreeFakeNdItem(NdFakeItem_t* fakeItem);

void PutFakeNd(NdFakeItem_t* fakeItem);

NdFakeItem_t* InsertFakeNd(NdTbl_t* tbl, TBM_IpAddr_t dst, Netdev_t* dev);

bool IsNeedNotify(NdFakeItem_t* fakeItem);

NdFakeItem_t* GetFakeNd(NdTbl_t* tbl, TBM_IpAddr_t dst);

int PushNdMissCache(NdFakeItem_t* fakeItem, Pbuf_t* pbuf);

/**
 * @brief 删除假表项并且发送假表项缓存的报文，在控制面更新表项和删除老化表项时调用
 */
void RemoveFakeNdItem(NdTbl_t* tbl, TBM_IpAddr_t dst);

void ClearFakeNd(NdTbl_t* tbl);

#ifdef __cplusplus
}
#endif
#endif
