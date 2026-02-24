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

#ifndef K_NET_SAL_INNER_H
#define K_NET_SAL_INNER_H

#ifdef __cplusplus
extern "C" {
#endif

uint16_t KnetGetFdirQid(unsigned __int128 queMap, uint16_t *qid);

int KNET_ACC_TxBurst(void *ctx, uint16_t queueId, void **buf, int cnt);

int KNET_ACC_RxBurst(void *ctx, uint16_t queueId, void **buf, int cnt);

int KNET_ACC_TxHash(void* ctx, const struct DP_Sockaddr* srcAddr, DP_Socklen_t srcAddrLen,
    const struct DP_Sockaddr *dstAddr, DP_Socklen_t dstAddrLen);

int KnetSetDpCfg(void);

int32_t KnetRegWorkderId(void);

int32_t KNET_ACC_WorkerGetSelfId(void);

#ifdef __cplusplus
}
#endif
#endif // K_NET_SAL_INNER_H