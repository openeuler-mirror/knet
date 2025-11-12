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

/**
 * @file dp_types.h
 * @brief NRTP基础类型定义
 */

#ifndef DP_TYPES_H
#define DP_TYPES_H

#include <stdint.h>

/**! 计数类型定义 */
#ifndef DP_STAT_CTR_TYPE
typedef uint64_t DP_Ctr_t;
#define DP_STAT_CTR_TYPE
#else
typedef DP_STAT_CTR_TYPE DP_Ctr_t;
#endif

#define DP_PACKED      __attribute__((packed))
#define DP_ALIAGNED(n) __attribute__((aligned(n)))

#define DP_LITTLE_ENDIAN 0x4321
#define DP_BIG_ENDIAN    0x1234

#ifndef DP_BYTE_ORDER
#error "DP_BYTE_ORDER must be set to DP_LITTLE_ENDIAN or DP_BIG_ENDIAN"
#endif

#ifndef DP_MULTI_PROCESS
#define DP_MULTI_PROCESS 1 // 支持共享内存
#endif

#endif
