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

#ifndef K_NET_KNET_STATISTIC_H
#define K_NET_KNET_STATISTIC_H

#include "knet_types.h"

/* 统计信息输出打印方式 */
typedef enum {
    KNET_STAT_OUTPUT_TO_LOG = 0,   /* 打印到日志 */
    KNET_STAT_OUTPUT_TO_SCREEN,    /* 打印到屏幕 */
    KNET_STAT_OUTPUT_TO_TELEMETRY, /* 打印到telemetry */
    KNET_STAT_OUTPUT_MAX
} KnetStatOutputType;

#endif  // K_NET_KNET_STATISTIC_H
