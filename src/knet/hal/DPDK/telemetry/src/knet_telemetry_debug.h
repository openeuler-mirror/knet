/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: telemetry遥测函数内部公共接口
 */
#ifndef __KNET_TELEMETRY_DEBUG_H__
#define __KNET_TELEMETRY_DEBUG_H__
 
#include "knet_telemetry.h"

#define MAX_JSON_KEY_NAME_LEN 100
#define INVALID_WORKER_TID "-"
void KnetUpdateSlaveProcessPidInfo(KNET_TelemetryInfo *telemetryInfo);
int KnetHandleTimeout(KNET_TelemetryInfo *telemetryInfo, int i);
int KnetWaitAllSlavePorcessHandle(KNET_TelemetryInfo *telemetryInfo);
int KnetGetQueIdByPid(uint32_t pid, KNET_TelemetryInfo* telemetryInfo);
int KnetGetTidByWorkerId(uint32_t workerId, uint32_t *tid);
int CheckAddContainerToDict(struct rte_tel_data *data, const char *name, struct rte_tel_data *value);
KNET_TelemetryInfo *KnetMultiSetTelemetrySHM();
int ParseTelemetryParams(const char *params, uint32_t *paramsArr, int maxCount);
extern KNET_DpTelemetryHooks g_dpTelemetryHooks;

#endif  // __KNET_TELEMETRY_DEBUG_H__