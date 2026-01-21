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


#ifndef __KNET_DTOE_CONFIG_H__
#define __KNET_DTOE_CONFIG_H__

#include "knet_types.h"

enum KNET_ConfModule {
    CONF_DTOE,
    CONF_MAX,
};
 
#define MAX_CONF_NUM_PER_INDEX_BITS 20
#define CONF_INDEX_LOWER_MASK ((1 << MAX_CONF_NUM_PER_INDEX_BITS) - 1)

#define MAX_STRVALUE_NUM 64
#define MAX_STRVALUE_COUNT 2
#define MAX_INT_COUNT 8

enum KNET_ConfKey {
    // dtoe配置项起始位置
    CONF_DTOE_MIN = CONF_DTOE << MAX_CONF_NUM_PER_INDEX_BITS,
    CONF_DTOE_LOG_LEVEL = CONF_DTOE_MIN,
    CONF_DTOE_CHANNEL_NUM,
    CONF_DTOE_MAX,
};

union KNET_CfgValue {
    int32_t intValue;
    uint64_t uint64Value;
    int32_t intValueArr[MAX_INT_COUNT];
    char strValue[MAX_STRVALUE_NUM];
    char strValueArr[MAX_STRVALUE_COUNT][MAX_STRVALUE_NUM];
};

/**
 * @brief 配置文件初始化
 * @return int 0表示成功，-1表示失败
 */
int KNET_InitCfg(void);

/**
 * @brief 获取配置项的值，模块内接口，且性能敏感，不检查参数有效性。建议使用前确认参数有效性。
 *
 * @param key [IN] 参数类型 enum ConfKey。指定配置项
 * @return union 配置项值的联合体
 */
const union KNET_CfgValue *KNET_GetCfg(enum KNET_ConfKey key);

#endif