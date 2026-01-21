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


#include <regex.h>

#include "knet_log.h"
#include "knet_utils.h"
#include "knet_dtoe_config_setter.h"

#define LOG_LEVEL_MAX_LEN 10

static int JsonChecker(cJSON *jsonValue, union KNET_CfgValue *value, union KnetCfgValidateParam *param, int type)
{
    if (jsonValue == NULL || param == NULL || value == NULL) {
        KNET_ERR("Json get failed, invalid param");
        return -1;
    }
    if (jsonValue->type != type) {
        KNET_ERR("Json get failed, invalid type, expect %d , actually %d", type, jsonValue->type);
        return -1;
    }

    switch (type) {
        case cJSON_String: {
            if (jsonValue->valuestring == NULL) {
                KNET_ERR("Json get failed, nullptr string");
                return -1;
            }
            break;
        }
        default:
            break;
    }
    return 0;
}

int IntSetter(cJSON *jsonValue, union KNET_CfgValue *value, union KnetCfgValidateParam *param)
{
    if (JsonChecker(jsonValue, value, param, cJSON_Number) != 0) {
        return -1;
    }

    // 直接采用int或者long long强制类型转换时，面对特别大的整数会出现数据截断，因此需要先进行范围判断
    if (jsonValue->valuedouble > INT32_MAX || jsonValue->valuedouble < INT32_MIN) {
        KNET_ERR("The value is out of the int32 range");
        return -1;
    }

    if (jsonValue->valuedouble != (int)jsonValue->valuedouble) { // 判断是否为整数
        KNET_ERR("The num is not an integer");
        return -1;
    }

    if (jsonValue->valueint < param->intValue.min || jsonValue->valueint > param->intValue.max) {
        KNET_ERR("Json int get failed, invalid value and out of [%lld, %lld]",
            param->intValue.min,
            param->intValue.max);
        return -1;
    }
    value->intValue = jsonValue->valueint;
    return 0;
}

int LogLevelSetter(cJSON *jsonValue, union KNET_CfgValue *value, union KnetCfgValidateParam *param)
{
    if (JsonChecker(jsonValue, value, param, cJSON_String) != 0) {
        return -1;
    }

    char confLevel[LOG_LEVEL_MAX_LEN] = {0};
    
    for (int i = 0; i < (LOG_LEVEL_MAX_LEN - 1) && jsonValue->valuestring[i] != '\0'; i++) {
        confLevel[i] = toupper((unsigned char)jsonValue->valuestring[i]);
    }

    const char *pattern = "^(ERROR|WARNING|DEBUG|INFO)$";
    bool matched = KNET_RegMatch(pattern, confLevel);
    if (matched == false) {
        return -1;
    }

    size_t strLen = strlen(confLevel) + 1;
    int memcpyRet = memcpy_s(value->strValue, sizeof(value->strValue), confLevel, strLen);
    if (memcpyRet != 0) {
        KNET_ERR("String set failed by memcpy: %d", memcpyRet);
        return -1;
    }

    return 0;
}