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
#include "dp_rand_api.h"

uint32_t DP_RandIntHookReg(DP_RandomHooks_S *randHook)
{
    if (UTILS_IsCfgInited() != 0) {
        DP_LOG_ERR("Randint hookreg failed, init already!");
        return 1;
    }
    if (UTILS_GetBaseFunc()->randFn != NULL) {
        DP_LOG_ERR("Randint hookreg failed, reg already!");
        return 1;
    }
    if ((randHook == NULL) || (randHook->randInt == NULL)) {
        DP_LOG_ERR("Randint hookreg failed, invalid randHook!");
        return 1;
    }

    UTILS_GetBaseFunc()->randFn = randHook->randInt;

    return 0;
}
