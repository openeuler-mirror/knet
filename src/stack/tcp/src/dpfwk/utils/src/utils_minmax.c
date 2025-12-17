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
#include "utils_base.h"
#include "utils_minmax.h"

static uint64_t MinmaxUpdateValue(Minmax_t *m, uint64_t xWin, const MinMaxSample_t *sampleVal)
{
    uint64_t dx = sampleVal->time - m->samples[0U].time;

    if (UTILS_UNLIKELY(dx > xWin)) {
        MinmaxShiftLeft(m, sampleVal);
        if (UTILS_UNLIKELY(sampleVal->time - m->samples[0U].time > xWin)) {
            MinmaxShiftLeft(m, sampleVal);
        }
    } else if (UTILS_UNLIKELY(m->samples[1U].time == m->samples[0U].time) && dx > xWin / 4U) {
        MinmaxUpdateTop2(m, sampleVal);
    } else if (UTILS_UNLIKELY(m->samples[2U].time == m->samples[1U].time) && dx > xWin / 2U) {
        MinmaxUpdateTop1(m, sampleVal);
    }

    return m->samples[0U].value;
}

uint64_t MinmaxUpdateMaxValue(Minmax_t *m, uint64_t xWin, uint64_t time, uint64_t value)
{
    MinMaxSample_t sampleVal;

    sampleVal.time = time;
    sampleVal.value = value;

    if (UTILS_UNLIKELY(sampleVal.value >= m->samples[0U].value) ||
        UTILS_UNLIKELY(sampleVal.time - m->samples[2U].time > xWin)) {
        return MinmaxResetValue(m, time, value);
    }

    if (UTILS_UNLIKELY(sampleVal.value >= m->samples[1U].value)) {
        MinmaxUpdateTop2(m, &sampleVal);
    } else if (UTILS_UNLIKELY(sampleVal.value >= m->samples[2U].value)) {
        MinmaxUpdateTop1(m, &sampleVal);
    }

    return MinmaxUpdateValue(m, xWin, &sampleVal);
}

uint64_t MinmaxUpdateMinValue(Minmax_t *m, uint64_t xWin, uint64_t time, uint64_t value)
{
    MinMaxSample_t sampleVal;

    sampleVal.time = time;
    sampleVal.value = value;

    if (UTILS_UNLIKELY(sampleVal.value <= m->samples[0U].value) ||
        UTILS_UNLIKELY(sampleVal.time - m->samples[2U].time > xWin)) {
        return MinmaxResetValue(m, time, value);
    }

    if (UTILS_UNLIKELY(sampleVal.value <= m->samples[1U].value)) {
        MinmaxUpdateTop2(m, &sampleVal);
    } else if (UTILS_UNLIKELY(sampleVal.value <= m->samples[2U].value)) {
        MinmaxUpdateTop1(m, &sampleVal);
    }

    return MinmaxUpdateValue(m, xWin, &sampleVal);
}

