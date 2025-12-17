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
#ifndef UTILS_CB_CNT_H
#define UTILS_CB_CNT_H

#include "utils_atomic.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

extern atomic32_t g_tcpCbCnt;
extern atomic32_t g_udpCbCnt;
extern atomic32_t g_epollCbCnt;

// 增加控制块使用计数，如果增加后的值超过上限，则计数减一并返回失败（非0值）；成功时返回0
static inline uint32_t UTILS_IncCbCnt(atomic32_t *cnt, uint32_t max)
{
    uint32_t newCnt = ATOMIC32_Inc(cnt);
    if (newCnt > max) {
        ATOMIC32_Dec(cnt);
        return 1;
    }
    return 0;
}

// 减少控制块使用计数，如果减少后的值小于0，则计数加一并返回失败（非0值）；成功时返回0
static inline uint32_t UTILS_DecCbCnt(atomic32_t *cnt)
{
    int32_t newCnt = (int32_t)ATOMIC32_Dec(cnt);
    if (newCnt < 0) {
        ATOMIC32_Inc(cnt);
        return 1;
    }
    return 0;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

#endif
