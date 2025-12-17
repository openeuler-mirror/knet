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

#ifndef CPD_ADPT_H
#define CPD_ADPT_H

#include "dp_cpd_api.h"
#include "dp_types.h"
#include "tbm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CPD_NEW_NEIGH,
    CPD_DEL_NEIGH,
} Cpd_NeighOpt_t;

void CpdTblMissHandle(void* tn, int type, int op, uint8_t family, void* item);

int CpdPktTranfer(uint32_t ifindex, void* pbuf, uint32_t dataLen, int cpdQueueId);

void CpdTblSync(void);

void CpdPktHandle(int cpdQueueId);

#ifdef __cplusplus
}
#endif

#endif
