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
#ifndef UTILS_RING_H
#define UTILS_RING_H

#include <stdlib.h>

#include "dp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void**   elms;
    uint32_t start;
    uint32_t end;
    uint32_t cnt;
    uint32_t deep;
} Ring_t;

static inline int RING_Init(Ring_t* ring, void** elms, uint32_t size)
{
    if (ring == NULL || size == 0U) {
        return -1;
    }

    ring->start = 0;
    ring->end   = 0;
    ring->cnt   = 0;
    ring->deep  = size;
    ring->elms  = elms;
    return 0;
}

static inline int RING_Push(Ring_t* ring, void* elm)
{
    if (ring->cnt >= ring->deep) {
        return -1;
    }

    ring->elms[ring->end] = elm;
    ring->cnt++;
    ring->end++;
    ring->end = (ring->end == ring->deep) ? 0 : ring->end;

    return 0;
}

static inline void* RING_Pop(Ring_t* ring)
{
    void* elm;

    if (ring->cnt == 0) {
        return NULL;
    }

    elm = ring->elms[ring->start];
    ring->cnt--;
    ring->start++;
    ring->start = (ring->start == ring->deep) ? 0 : ring->start;

    return elm;
}

static inline uint32_t RING_PopBurst(Ring_t* ring, void** elms, uint32_t cnt)
{
    uint32_t ret;

    ret = (ring->cnt > cnt) ? cnt : ring->cnt;

    for (uint32_t i = 0; i < ret; i++) {
        elms[i]     = ring->elms[ring->start++];
        ring->start = (ring->start == ring->deep) ? 0 : ring->start;
    }

    ring->cnt -= ret;

    return ret;
}

static inline int RING_IsEmpty(Ring_t* ring)
{
    return (ring->cnt == 0) ? 1 : 0;
}

#ifdef __cplusplus
}
#endif
#endif
