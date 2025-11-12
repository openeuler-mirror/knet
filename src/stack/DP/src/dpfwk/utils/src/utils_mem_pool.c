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

#include "utils_log.h"
#include "dp_mp_api.h"
#include "securec.h"

static DP_MempoolHooks_S g_dpMpFns = { 0 };

uint32_t DP_MempoolHookReg(DP_MempoolHooks_S* pHooks)
{
    if ((pHooks == NULL) ||
        (pHooks->mpAlloc == NULL) ||
        (pHooks->mpCreate == NULL) ||
        (pHooks->mpDestroy == NULL) ||
        (pHooks->mpFree == NULL)) {
        DP_LOG_ERR("Mempool hookreg failed, invalid pHooks!");
        return -1;
    }
    g_dpMpFns.mpCreate = pHooks->mpCreate;
    g_dpMpFns.mpAlloc = pHooks->mpAlloc;
    g_dpMpFns.mpFree = pHooks->mpFree;
    g_dpMpFns.mpDestroy = pHooks->mpDestroy;

    return 0;
}

DP_MempoolHooks_S *UTILS_GetMpFunc(void)
{
    return &g_dpMpFns;
}

void MempoolHookClr(void)
{
    memset_s(&g_dpMpFns, sizeof(DP_MempoolHooks_S), 0, sizeof(DP_MempoolHooks_S));
}