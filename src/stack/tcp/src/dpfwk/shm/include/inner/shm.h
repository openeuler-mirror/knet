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

#ifndef SHM_H
#define SHM_H

#include "utils_base.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHM_REG(shmname, val)
#define SHM_INIT(slave)    0
#define SHM_MEM_INIT(slave) 0

#define SHM_REG_INFO(si)
#define SHM_MALLOC(size, mod, type)  DP_MemAlloc((size), (mod), (type))
#define SHM_FREE(ptr, type)          DP_MemFree((ptr), (type))

#ifdef __cplusplus
}
#endif
#endif
