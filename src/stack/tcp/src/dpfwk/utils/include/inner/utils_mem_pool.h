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

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

DP_MempoolHooks_S *UTILS_GetMpFunc(void);

void MempoolHookClr(void);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

#endif
