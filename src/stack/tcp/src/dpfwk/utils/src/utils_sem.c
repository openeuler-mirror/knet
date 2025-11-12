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

#include "dp_sem_api.h"

static DP_SemHooks_S g_semFns = {0};

/* 信号量操作接口注册函数 */
uint32_t DP_SemHookReg(const DP_SemHooks_S *pHooks)
{
    if ((pHooks == NULL) || (pHooks->init == NULL) || (pHooks->deinit == NULL) ||
        (pHooks->post == NULL) || (pHooks->timeWait == NULL)) {
        DP_LOG_ERR("SemHook reg failed, invalid pHooks!");
        return 1;
    }

    g_semFns.init = pHooks->init;
    g_semFns.deinit = pHooks->deinit;
    g_semFns.post = pHooks->post;
    g_semFns.timeWait = pHooks->timeWait;
    g_semFns.size = pHooks->size;

    UTILS_GetBaseFunc()->semFns = &g_semFns;

    return 0;
}
