/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 内存相关操作
 */

#include <rte_malloc.h>
#include "knet_types.h"
#include "knet_config.h"
#include "knet_mem.h"

static __thread int8_t g_runMode = KNET_RUN_MODE_INVALID;

__thread bool g_isInSignalQuit = false;

#ifdef KNET_TEST
void KNET_SetRunMode(int8_t mode)
{
    g_runMode = mode;
}
#endif

void *KNET_MemAlloc(size_t size)
{
    if (KNET_UNLIKELY(g_runMode == KNET_RUN_MODE_INVALID)) {
        g_runMode = KNET_GetCfg(CONF_COMMON_MODE)->intValue;
    }
    if (g_runMode == KNET_RUN_MODE_SINGLE) {
        return rte_malloc(NULL, size, RTE_CACHE_LINE_SIZE);
    }
    return malloc(size);
}

void KNET_MemFree(void *ptr)
{
    if (KNET_UNLIKELY(g_runMode == KNET_RUN_MODE_INVALID)) {
        g_runMode = KNET_GetCfg(CONF_COMMON_MODE)->intValue;
    }
    if (g_runMode == KNET_RUN_MODE_SINGLE) {
        rte_free(ptr);
        return;
    }
    /* 在信号流程中退出,free有死锁风险,这里不再free,直接让内核去回收资源 */
    if (KNET_UNLIKELY(g_isInSignalQuit)) {
        return;
    }
    free(ptr);
}

void KNET_MemSetFlagInSignalQuiting(void)
{
    g_isInSignalQuit = true;
    return;
}