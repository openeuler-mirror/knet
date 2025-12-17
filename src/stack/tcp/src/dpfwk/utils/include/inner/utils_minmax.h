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
#ifndef UTILS_MINMAX_H
#define UTILS_MINMAX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MINMAX_ARRAY_LEN 3 /* 采样数组长度 */

typedef struct {
    uint64_t time;   /* 采样时刻 */
    uint64_t value;  /* 采样值 */
} MinMaxSample_t;

typedef struct {
    MinMaxSample_t samples[MINMAX_ARRAY_LEN];
} Minmax_t;

static inline uint64_t MinmaxGetValue(const Minmax_t *m)
{
    return m->samples[0U].value;
}

static inline void MinmaxShiftLeft(Minmax_t *m, const MinMaxSample_t *sampleVal)
{
    m->samples[0U] = m->samples[1U];
    m->samples[1U] = m->samples[2U];
    m->samples[2U] = *sampleVal;
}

static inline void MinmaxUpdateTop3(Minmax_t *m, const MinMaxSample_t *sampleVal)
{
    m->samples[0U] = *sampleVal;
    m->samples[1U] = *sampleVal;
    m->samples[2U] = *sampleVal;
}

static inline void MinmaxUpdateTop2(Minmax_t *m, const MinMaxSample_t *sampleVal)
{
    m->samples[1U] = *sampleVal;
    m->samples[2U] = *sampleVal;
}

static inline void MinmaxUpdateTop1(Minmax_t *m, const MinMaxSample_t *sampleVal)
{
    m->samples[2U] = *sampleVal;
}

static inline uint64_t MinmaxResetValue(Minmax_t *m, uint64_t time, uint64_t value)
{
    MinMaxSample_t sampleVal;

    sampleVal.time = time;
    sampleVal.value = value;

    MinmaxUpdateTop3(m, &sampleVal);

    return m->samples[0U].value;
}

uint64_t MinmaxUpdateMaxValue(Minmax_t *m, uint64_t xWin, uint64_t time, uint64_t value);

uint64_t MinmaxUpdateMinValue(Minmax_t *m, uint64_t xWin, uint64_t time, uint64_t value);

#ifdef __cplusplus
}
#endif
#endif
