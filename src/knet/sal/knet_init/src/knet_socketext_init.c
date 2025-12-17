/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: telemetry初始化
 */
#include <unistd.h>
#include "rte_timer.h"
#include "rte_ethdev.h"
#include "rte_eal_memconfig.h"
#include "rte_malloc.h"
#include "rte_cycles.h"

#include "securec.h"
#include "dp_init_api.h"
#include "dp_netdev_api.h"
#include "dp_worker_api.h"
#include "dp_cfg_api.h"
#include "dp_cpd_api.h"
#include "dp_tbm_api.h"
#include "dp_debug_api.h"
#include "dp_socket_api.h"

#include "knet_config.h"
#include "knet_telemetry.h"
#include "knet_log.h"
#include "knet_lock.h"
#include "knet_utils.h"
#include "tcp_fd.h"
#include "knet_socketext_init.h"

#define TELEMETRY_DEBUG_USLEEP 100000
#define TIMEOUT_TIMES 10
#define MAX_MSG_LEN 8192
#define EMPTY_LEN 6

/**
 * @brief 多进程调用, 每个从进程会通过该接口更新共享内存的数据(维测信息和pid/tid映射关系)
*/
void ShowDpStats(KNET_TelemetryInfo *telemetryInfo, int queId)
{
    /* telemetryInfo 未申请到共享内存，跳过处理，错误日志已经输出，没必要刷屏刷日志 */
    if (telemetryInfo == NULL) {
        return;
    }

    if (telemetryInfo->msgReady[queId] == 1) {
        KNET_TelemetryType telemetryType = telemetryInfo->telemetryType;
        switch (telemetryType) {
            case KNET_TELEMETRY_STATISTIC:
                DP_ShowStatistics(telemetryInfo->statType, -1, KNET_STAT_OUTPUT_TO_TELEMETRY);
                break;
            case KNET_TELEMETRY_UPDATE_QUE_INFO:
                KNET_MaintainQueue2TidPidMp(queId);
                break;
            default:
                KNET_ERR("Telemetry type %d is invalid", telemetryType);
                break;
        }
        
        /* 调用后触发 KNET_ACC_Debug */
        telemetryInfo->msgReady[queId] = 0;
    }
}
