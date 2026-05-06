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

#ifndef UTILS_MEM_POOL_H
#define UTILS_MEM_POOL_H

#include "dp_mp_api.h"

#define DP_MAX_MEM_POOL_INFO_NUM 32     /* 内存池最大数量 */
#define DP_MAX_MEM_POOL_NAME_LEN 32     /* 内存池命名最大长度 */

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

DP_MempoolHooks_S *UTILS_GetMpFunc(void);

/* 内存池钩子函数调用接口 */
int32_t DP_MempoolCreate(const DP_MempoolCfg_S* cfg, const DP_MempoolAttr_S* attr, DP_Mempool* handler);
void* DP_MempoolAlloc(DP_Mempool mp);
void DP_MempoolFree(DP_Mempool mp, void* ptr);
void DP_MempoolDestory(DP_Mempool mp);
void* DP_MempoolConstruct(DP_Mempool mp, void* addr, uint64_t offset, uint16_t len);
void* DP_EbufGetNextPbuf(void* ebuf, uint32_t len, uint16_t idx);
uint16_t DP_EbufRefCntUpdate(void* ptr, int16_t value);
void DP_EbufCallback(void* ptr);
void DP_EbufSetRefCnt(void* ptr, uint16_t cnt);

void MempoolHookClr(void);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

#endif
