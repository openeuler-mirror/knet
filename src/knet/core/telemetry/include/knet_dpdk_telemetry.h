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
#ifndef __KNET_DPDK_TELEMETRY_H__
#define __KNET_DPDK_TELEMETRY_H__

#include "rte_telemetry.h"
#include "dp_debug_api.h"
#include "knet_statistics.h"
#include "knet_types.h"

#define TELEMETRY_DEBUG_USLEEP 100000
#define MAX_KEY_NUM 128
#define MAX_OUTPUT_LEN 8192
#define MAX_KEY_LEN 32
#define TIMEOUT_TIMES 10
#define DECIMAL_NUM 10
#define KNET_TELEMETRY_MZ_NAME "knet_telemetry_debug_info_mz"

typedef struct {
    int msgReady[MAX_QUEUE_NUM];  // 从进程判断消息是否需要发送，1表示需要
    DP_StatType_t statType;
    char message[MAX_QUEUE_NUM][MAX_OUTPUT_LEN];
} TelemetryInfo;

int32_t KNET_InitDpdkTelemetry(void);
int32_t KNET_UninitDpdkTelemetry(void);

int KNET_DebugOutputToTelemetry(const char *output, uint32_t len);
DP_StatType_t KNET_GetStatTypeFromString(const char *string);
int KNET_TelemetryStatisticCallback(const char *cmd, const char *params, struct rte_tel_data *data);
int KNET_TelemetryStatisticCallbackMp(const char *cmd, const char *params, struct rte_tel_data *data);

typedef void (*KNET_DpShowStatisticsHook)(DP_StatType_t type, int workerId, uint32_t flag);
int KNET_TelemetryDpShowStatisticsHookReg(KNET_DpShowStatisticsHook hook);

#endif  // __KNET_DPDK_TELEMETRY_H__
