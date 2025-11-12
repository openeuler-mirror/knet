/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 定长内存管理算法
 */
#ifndef __KNET_FMM_H__
#define __KNET_FMM_H__

#include "knet_types.h"
#include "knet_config.h"

#define KNET_FMM_POOL_MAX_NUM       256
#define KNET_FMM_POOL_NAME_LEN      64
#define KNET_FMM_ERROR              1

typedef void (*KnetFmmObjInitCb)(void *obj);

typedef struct {
    char name[KNET_FMM_POOL_NAME_LEN]; /* pool的名字，必须带字符串结束符 */
    uint32_t eltNum;         /* pool的内存单元个数 */
    uint32_t eltSize;          /* 每个内存单元的大小 */
    uint32_t cacheSize;     /* 每个cache容纳的内存单元数目 */
    uint32_t socketId;       /* numa id */
    KnetFmmObjInitCb objInit; /* 内存单元初始化钩子函数 */
} KNET_FmmPoolCfg;

uint32_t KNET_FmmCreatePool(KNET_FmmPoolCfg *cfg, uint32_t *poolId);
uint32_t KNET_FmmDestroyPool(uint32_t poolId);
int KNET_InitFmm(enum KnetProcType procType);
int KNET_UnInitFmm(enum KnetProcType procType);

#endif