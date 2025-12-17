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
#include "utils_log.h"
#include "utils_cfg.h"
#include "dp_sem_api.h"

static DP_SemHooks_S g_semFns = {0};

/* 信号量操作接口注册函数 */
uint32_t DP_SemHookReg(const DP_SemHooks_S *pHooks)
{
    if (UTILS_IsCfgInited() != 0) {
        DP_LOG_ERR("SemHook reg failed, init already!");
        return 1;
    }
    if (UTILS_GetBaseFunc()->semFns != NULL) {
        DP_LOG_ERR("SemHook reg failed, reg already!");
        return 1;
    }
    if ((pHooks == NULL) || (pHooks->initHook == NULL) || (pHooks->deinitHook == NULL) ||
        (pHooks->postHook == NULL) || (pHooks->timeWaitHook == NULL)) {
        DP_LOG_ERR("SemHook reg failed, invalid pHooks!");
        return 1;
    }

    if (pHooks->semSize == 0) {
        DP_LOG_ERR("SemHook reg failed, invalid size!");
        return 1;
    }

    g_semFns.initHook = pHooks->initHook;
    g_semFns.deinitHook = pHooks->deinitHook;
    g_semFns.postHook = pHooks->postHook;
    g_semFns.timeWaitHook = pHooks->timeWaitHook;
    g_semFns.semSize = pHooks->semSize;

    UTILS_GetBaseFunc()->semFns = &g_semFns;

    return 0;
}
