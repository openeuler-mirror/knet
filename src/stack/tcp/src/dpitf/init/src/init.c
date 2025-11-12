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

#include "dp_init_api.h"
#include "dp_deinit.h"

#include "utils.h"
#include "utils_cfg.h"
#include "utils_log.h"
#include "utils_statistic.h"
#include "utils_mem_pool.h"
#include "shm.h"
#include "ns.h"
#include "worker.h"
#include "netdev.h"
#include "tbm.h"
#include "pmgr.h"
#include "sock.h"
#include "pbuf.h"

static int UTILS_ResourceInit(void)
{
    if ((UTILS_GetMpFunc()->mpCreate != NULL) && (PBUF_MpInit() != 0)) {
        DP_LOG_ERR("Dp init pbuf mempool failed with registered mpFunc.");
        return -1;
    }

    if (UTILS_StatInit() != 0) {
        DP_LOG_ERR("Dp init statistic debug module failed.");
        return -1;
    }

    return 0;
}

int DP_Init(int slave)
{
    if (UTILS_IsCfgInited() != 0) {
        DP_LOG_ERR("Dp init failed, config has inited already.");
        return -1;
    }

    if (UTILS_FuncInit() < 0) {
        DP_LOG_ERR("Dp basefunc init failed with NULL rand func reg.");
        return -1;
    }

    if (SHM_MEM_INIT(slave) != 0) {
        DP_LOG_ERR("Dp shm mem init failed.");
        return -1;
    }

    if (NETDEV_Init(slave) != 0) {
        DP_LOG_ERR("Dp init devTasks failed.");
        return -1;
    }

    if (TBM_Init(slave) != 0) {
        DP_LOG_ERR("Dp table manager init failed.");
        return -1;
    }

    if (SOCK_Init(slave) != 0) {
        DP_LOG_ERR("Dp init socket module failed.");
        return -1;
    }

    if (PMGR_Init(slave) != 0) {
        DP_LOG_ERR("Dp init protocol manager failed.");
        return -1;
    }

    if (NS_Init(slave) != 0) {
        DP_LOG_ERR("Dp init namespace failed.");
        return -1;
    }

    // 定时器及任务在worker初始化时处理，涉及到定时器及任务的初始化需要放到这里之前
    if (WORKER_Init() != 0) {
        DP_LOG_ERR("Dp init worker manager failed.");
        return -1;
    }

    if (SHM_INIT(slave) != 0) {
        DP_LOG_ERR("Dp init shm manager failed.");
        return -1;
    }

    if (UTILS_ResourceInit() != 0) {
        return -1;
    }

    UTILS_SetCfgInit(CFG_INITED);
    DP_LOG_INFO("Dp init successfully.");

    return 0;
}

void DP_Deinit(int slave)
{
    if (UTILS_IsCfgInited() != 1) {
        DP_LOG_WARN("Dp deinit without inited config.");
        return;
    }

    UTILS_StatDeinit();
    WORKER_Deinit();
    NS_Deinit(slave);
    PMGR_Deinit(slave);
    SOCK_Deinit(slave);
    NETDEV_Deinit(slave);
    TBM_Deinit();
    PBUF_MpDeinit();
    UTILS_FuncDeInit();
    UTILS_SetCfgInit(CFG_INITIAL);
}
