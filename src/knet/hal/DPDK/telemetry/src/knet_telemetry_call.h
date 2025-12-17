/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 用于注册的telemetry遥测函数
 */
#ifndef __KNET_TELEMETRY_CALL_H__
#define __KNET_TELEMETRY_CALL_H__
 
#include "rte_telemetry.h"

int KnetTelemetryStatisticCallback(const char *cmd, const char *params, struct rte_tel_data *data);
int KnetTelemetryStatisticCallbackMp(const char *cmd, const char *params, struct rte_tel_data *data);

#endif  // __KNET_TELEMETRY_CALL_H__