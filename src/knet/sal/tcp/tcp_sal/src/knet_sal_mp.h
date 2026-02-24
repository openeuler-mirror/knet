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
#ifndef K_NET_SAL_MP_H
#define K_NET_SAL_MP_H

#include "dp_mp_api.h"
#include "knet_types.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t KnetHandleInit(void);
int32_t KNET_ACC_CreateMbufMemPool(const DP_MempoolCfg_S *cfg, const DP_MempoolAttr_S *attr, DP_Mempool *handler);
void KNET_ACC_DestroyMbufMemPool(DP_Mempool mp);
void KNET_ACC_MbufMemPoolFree(DP_Mempool mp, void *ptr);
void *KNET_ACC_MbufMemPoolAlloc(DP_Mempool mp);
void *KNET_ACC_MbufConstruct(DP_Mempool mp, void *addr, uint64_t offset, uint16_t len);

#ifdef __cplusplus
}
#endif
#endif // K_NET_SAL_MP_H