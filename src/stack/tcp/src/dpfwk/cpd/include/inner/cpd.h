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

#ifndef CPD_H
#define CPD_H

#include <sys/uio.h>

#include "tbm.h"

#ifdef __cplusplus
extern "C" {
#endif

// 用于同步表项，从内核拿到信息
typedef struct {
    TBM_NdItem_t ndEntry;
    uint32_t ifindex;
    uint8_t  type; /* 表项类型,取值:Cpd_NeighOpt_t */
    uint8_t  family;
    uint16_t state;
} SycnTableEntry;

typedef struct {
    /* 从控制面获取arp表项数据 */
    int (*syncTable) (SycnTableEntry *entryList, uint32_t* entryNum);
    /* 向控制面写报文 */
    int (*writePkt) (uint32_t ifindex, const void* buf, uint32_t len, int cpdQueueId);
    /* 向控制面写报文 */
    int (*writePktV) (uint32_t ifindex, struct iovec *dataIov, int iovCnt, uint32_t len, int cpdQueueId);
    /* 从控制面读报文 */
    int (*readPkt) (uint32_t ifindex, void* buf, uint32_t len, int cpdQueueId);
    /* 由ADAPTER实现表项miss处理 */
    int (*handleTblMiss) (int type, int ifindex, void* srcAddr, void* dstAddr);
} CPIOCallBack;

void DP_CPD_Deinit(void);

void CpIoHookReg(CPIOCallBack *cb);

#ifdef __cplusplus
}
#endif

#endif
