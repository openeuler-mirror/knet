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
#ifndef __KNET_SOCKETEXT_INIT_H__
#define __KNET_SOCKETEXT_INIT_H__
#include "knet_telemetry.h"

/**
 * @brief 显示sock相关统计
 * @param telemetryInfo 遥测信息
 * @param queId 队列id
 * @return int 0：成功 -1：失败
 */
void ShowDpStats(KNET_TelemetryInfo *telemetryInfo, int queId);

/**
 * @brief 持久化刷新当前进程所有dp状态
 * @param telemetryInfo 遥测信息
 */
void PrepareAllDpStates(KNET_TelemetryPersistInfo *telemetryInfo);
#endif  // __KNET_SOCKETEXT_INIT_H__