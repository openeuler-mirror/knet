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
#include <securec.h>

#include "utils.h"
#include "utils_dft_fns.h"
#include "utils_mem_pool.h"
// #include "dp_sem.h"
// #include "dp_clock.h"
// #include "dp_mem.h"

DP_Hooks_t g_baseFunc = { .randFn = NULL };

int UTILS_FuncInit(void)
{
    if (g_baseFunc.randFn == NULL) { // 随机数接口不提供默认配置接口
        return -1;
    }

    return 0;
}

void UTILS_FuncDeInit(void)
{
    (void)memset_s(&g_baseFunc, sizeof(g_baseFunc), 0, sizeof(g_baseFunc));
    // DP 默认的信号量和 DP 一开始实现的默认信号量处理不一样
    // DP_SemRegDefault();
    // DP_ClockRegDefault();
    // DP_MemRegDefault();
    MempoolHookClr();
}

DP_Hooks_t* UTILS_GetBaseFunc(void)
{
    return &g_baseFunc;
}
