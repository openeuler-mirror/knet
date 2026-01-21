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


#ifndef __KNET_DTOE_CONFIG_SETTER_H__
#define __KNET_DTOE_CONFIG_SETTER_H__

#include "cJSON.h"

#include "knet_dtoe_config.h"

#define REG_PATTERN_LEN 128

union KnetCfgValidateParam {
    char pattern[REG_PATTERN_LEN];
    struct {
        int64_t min;
        int64_t max;
    } intValue;
};

typedef int (*FuncSetter)(cJSON *jsonValue, union KNET_CfgValue *value, union KnetCfgValidateParam *param);

int IntSetter(cJSON *jsonValue, union KNET_CfgValue *value, union KnetCfgValidateParam *param);

int LogLevelSetter(cJSON *jsonValue, union KNET_CfgValue *value, union KnetCfgValidateParam *param);


#endif // __KNET_CONFIG_SETTER_H__