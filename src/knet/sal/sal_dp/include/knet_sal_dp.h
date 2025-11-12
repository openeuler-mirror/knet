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

#ifndef K_NET_SAL_DP_H
#define K_NET_SAL_DP_H

#include "knet_transmission.h"
#include "dp_pbuf_api.h"
#include "dp_cfg_api.h"

int KNET_ACC_TxBurst(void *ctx, uint16_t queueId, void **buf, int cnt);
int KNET_ACC_RxBurst(void *ctx, uint16_t queueId, void **buf, int cnt);

int32_t KNET_RegWorkderId(void);

int KNET_SetDpCfg(void);

uint32_t KNET_SAL_Init(void);

#endif // K_NET_SAL_DP_H
