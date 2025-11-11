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
#include <netinet/in.h>
#include <arpa/inet.h>

#include "knet_log.h"
#include "knet_queue_id.h"
#include "knet_config_setter.h"

#define LOG_LEVEL_MAX_LEN 10
#define REG_ERROR_MSG_LEN 100

static inline uint32_t ReverseBits(uint32_t num)
{
    uint32_t maxBits = (uint32_t)(sizeof(num) * 8 - 1);
    uint32_t reverseNum = 0;
    for (uint32_t i = 0; i <= maxBits; i++) {
        if ((num & (((uint32_t)1) << i))) {
            reverseNum |= ((uint32_t)1) << (maxBits - i);
        }
    }
    return reverseNum;
}

static int JsonChecker(cJSON *jsonValue, union CfgValue *value, union KnetCfgValidateParam *param, int type)
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

static bool RegMatch(const char *pattern, const char *string)
{
    if (pattern == NULL || string == NULL) {
        KNET_ERR("Invalid reg pattern and string");
        return false;
    }

    regex_t regex;

    // 编译正则表达式
    int ret = regcomp(&regex, pattern, REG_EXTENDED);
    if (ret != 0) {
        KNET_ERR("Could not compile regex, error code:%d", ret);
        return false;
    }

    // 执行正则表达式匹配
    bool matched = false;
    ret = regexec(&regex, string, 0, NULL, 0);
    if (ret == 0) {
        matched = true;
    } else if (ret == REG_NOMATCH) {
        matched = false;
        KNET_ERR("The string is invalid");
    } else {
        char msgbuf[REG_ERROR_MSG_LEN] = {0};
        regerror(ret, &regex, msgbuf, sizeof(msgbuf));
        matched = false;
        KNET_ERR("Regex match failed: %s", msgbuf);
    }

    // 释放正则表达式
    regfree(&regex);

    return matched;
}

int IntSetter(cJSON *jsonValue, union CfgValue *value, union KnetCfgValidateParam *param)
{
    if (JsonChecker(jsonValue, value, param, cJSON_Number) != 0) {
        return -1;
    }

    // 校验参数是否为整数
    if (jsonValue->valuedouble != (int)jsonValue->valuedouble) {
        KNET_ERR("The num is not an integer");
        return -1;
    }

    if (jsonValue->valueint < param->intValue.min || jsonValue->valueint > param->intValue.max) {
        KNET_ERR("Json int get failed, invalid value and outof [%lld, %lld]",
            param->intValue.min,
            param->intValue.max);
        return -1;
    }
    value->intValue = jsonValue->valueint;
    return 0;
}

int StringSetter(cJSON *jsonValue, union CfgValue *value, union KnetCfgValidateParam *param)
{
    if (JsonChecker(jsonValue, value, param, cJSON_String) != 0) {
        return -1;
    }

    bool matched = RegMatch(param->pattern, jsonValue->valuestring);
    if (matched == false) {
        return -1;
    }

    size_t jsonStrLen = strlen(jsonValue->valuestring) + 1;
    int memcpyRet = memcpy_s(value->strValue, sizeof(value->strValue), jsonValue->valuestring, jsonStrLen);
    if (memcpyRet != 0) {
        KNET_ERR("String set failed by memcpy: %d", memcpyRet);
        return -1;
    }

    return 0;
}

int IpSetter(cJSON *jsonValue, union CfgValue *value, union KnetCfgValidateParam *param)
{
    if (JsonChecker(jsonValue, value, param, cJSON_String) != 0) {
        return -1;
    }

    uint32_t ipValue = inet_addr(jsonValue->valuestring);
    if (ipValue == INADDR_NONE) {
        KNET_ERR("Json ip get failed, invalid address");
        return -1;
    }

    value->intValue = (int32_t)ipValue;
    return 0;
}

int NetMaskSetter(cJSON *jsonValue, union CfgValue *value, union KnetCfgValidateParam *param)
{
    if (JsonChecker(jsonValue, value, param, cJSON_String) != 0) {
        return -1;
    }

    uint32_t netmaskValue = inet_addr(jsonValue->valuestring);
    if (netmaskValue == INADDR_NONE) {
        KNET_ERR("Json netmask get failed, invalid address");
        return -1;
    }

    uint32_t tempValue = ReverseBits(ntohl(netmaskValue));
    // 检查合法性
    if ((tempValue & (tempValue + 1)) != 0) {
        KNET_ERR("Json netmask get failed, invalid netmask");
        return -1;
    }
    value->intValue = (int32_t)netmaskValue;

    return 0; // 合法子网掩码
}

int MacSetter(cJSON *jsonValue, union CfgValue *value, union KnetCfgValidateParam *param)
{
    if (JsonChecker(jsonValue, value, param, cJSON_String) != 0) {
        return -1;
    }

    (void)memset_s(value->strValue, sizeof(value->strValue), 0, sizeof(value->strValue));
    int ret = KNET_ParseMac(jsonValue->valuestring, (uint8_t *)value->strValue);
    if (ret != 0) {
        KNET_ERR("Json mac get failed, invalid address");
        return -1;
    }

    return 0;
}

int CoreListGlobalSetter(cJSON *jsonValue, union CfgValue *value, union KnetCfgValidateParam *param)
{
    if (JsonChecker(jsonValue, value, param, cJSON_String) != 0) {
        return -1;
    }

    int ret = KNET_CoreListInit(jsonValue->valuestring);
    if (ret != 0) {
        KNET_ERR("Core list init failed, ret %d", ret);
        return -1;
    }
    size_t jsonStrLen = strlen(jsonValue->valuestring) + 1;
    int memcpyRet = memcpy_s(value->strValue, sizeof(value->strValue), jsonValue->valuestring, jsonStrLen);
    if (memcpyRet != 0) {
        KNET_ERR("Core list set failed by memcpy: %d", memcpyRet);
        return -1;
    }

    return 0;
}

int LogLevelSetter(cJSON *jsonValue, union CfgValue *value, union KnetCfgValidateParam *param)
{
    if (JsonChecker(jsonValue, value, param, cJSON_String) != 0) {
        return -1;
    }

    char confLevel[LOG_LEVEL_MAX_LEN] = {0};
    
    for (int i = 0; i < LOG_LEVEL_MAX_LEN && jsonValue->valuestring[i] != '\0'; i++) {
        confLevel[i] = toupper((unsigned char)jsonValue->valuestring[i]);
    }

    const char *pattern = "^(ERROR|WARNING|DEBUG|INFO)$";
    bool matched = RegMatch(pattern, confLevel);
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
