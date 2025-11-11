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

#ifndef RT_H
#define RT_H

#include "tbm.h"
#include "utils_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TBM_RtKey  RtKey_t;
typedef struct TBM_RtItem RtItem_t;
typedef struct TBM_RtTbl  RtTbl_t;
typedef struct TBM_Cache  RtCache_t;

#define RTTBL_HASH_MASK ((CFG_GET_VAL(DP_CFG_RT_MAX)) - 1)

void* AllocRtTbl(void);

void FreeRtTbl(void* tbl);

RtItem_t* AllocRtItem(void);

// insert之后不允许使用此接口释放
void FreeRtItem(RtItem_t* rtItem);

int InsertRt(RtTbl_t* tbl, RtItem_t* rtItem);

int RemoveRt(RtTbl_t* tbl, RtKey_t* rtKey);

RtItem_t* LookupRt(RtTbl_t* tbl, RtKey_t* rtKey);

RtItem_t* GetRt(RtTbl_t* tbl, DP_InAddr_t dst);

void PutRt(RtItem_t* item);


#ifdef __cplusplus
}
#endif
#endif
