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

uint32_t DP_ClockReg(DP_ClockGetTimeHook timeHook)
{
    if (timeHook == NULL) {
        DP_LOG_ERR("Clock reg failed, invalid timeHook!");
        return -1;
    }

    if (UTILS_IsCfgInited() != 0) {
        DP_LOG_ERR("Clock reg failed, dp already init!");
        return -1;
    }

    UTILS_GetBaseFunc()->timeFn = timeHook;

    return 0;
}
