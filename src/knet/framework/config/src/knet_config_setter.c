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
#include "knet_utils.h"
#include "knet_config_core_queue.h"
#include "knet_config_setter.h"

#define LOG_LEVEL_MAX_LEN 10
#define DECIMAL 10
#define MAX_STRING_LEN 1024
#define MAX_BDF_COUNT 2
#define MAX_CTRL_CPU_COUNT 8
#define MAX_EPOLL_DATA_LEN 32

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

KNET_STATIC int CheckEpollData(char* endptr, const char* str, uint64_t result)
{
    if (endptr == str || *endptr != '\0') {
        KNET_ERR("Invalid integer string format");
        return -1;
    }

    // 检查负号(uint64 不允许负号)
    char convertStr[MAX_EPOLL_DATA_LEN] = {0};
    if (snprintf_s(convertStr, MAX_EPOLL_DATA_LEN, MAX_EPOLL_DATA_LEN - 1, "%llu", result) < 0) {
        KNET_ERR("Snprintf failed to convert epoll data str");
        return -1;
    }
    if (strcmp(convertStr, str)) {
        KNET_ERR("Negative value/Invalid string format not allowed for uint64");
        return -1;
    }
    return 0;
}

int Uint64Setter(cJSON *jsonValue, union KNET_CfgValue *value, union KnetCfgValidateParam *param)
{
    if (JsonChecker(jsonValue, value, param, cJSON_String) != 0) {
        return -1;
    }

    char *endptr = NULL;
    const char *str = jsonValue->valuestring;
    errno = 0; // 重置错误标志
    
    // 尝试转换为 uint64_t
    uint64_t result = strtoull(str, &endptr, 10);
    
    // 检查转换错误和溢出错误
    if (errno == ERANGE) {
        KNET_ERR("Value exceeds uint64 range");
        return -1;
    }
    if (CheckEpollData(endptr, str, result) != 0) {
        KNET_ERR("Invalid epoll data");
        return -1;
    }

    // 无需判断U64范围，因为cJSON解析出来只能到U64范围，因此一定不会超过U64范围

    value->uint64Value = result;
    return 0;
}

int CtrVcpuRingSetter(cJSON *jsonValue, union KNET_CfgValue *value, union KnetCfgValidateParam *param)
{
    if (JsonChecker(jsonValue, value, param, cJSON_Number) != 0) {
        return -1;
    }
    
    // 多进程模式不支持控制线程多队列
    if (KNET_GetCfg(CONF_COMMON_MODE)->intValue == KNET_RUN_MODE_MULTIPLE && jsonValue->valueint != 1) {
        KNET_ERR("Multiple mode does not support multiple ring per ctrl vcpu");
        return -1;
    }

    return IntSetter(jsonValue, value, param);
}

int CtrlVcpuArraySetter(cJSON *jsonValue, union KNET_CfgValue *value, union KnetCfgValidateParam *param)
{
    if (JsonChecker(jsonValue, value, param, cJSON_Array) != 0) {
        return -1;
    }

    int ctrlCpuNums = cJSON_GetArraySize(jsonValue);
    if (KNET_GetCfg(CONF_COMMON_CTRL_VCPU_NUMS)->intValue != ctrlCpuNums) {
        KNET_ERR("Ctrl vcpu ids size is not equal to ctrl cpu nums");
        return -1;
    }

    // 多进程模式不支持多控制线程
    if (KNET_GetCfg(CONF_COMMON_MODE)->intValue == KNET_RUN_MODE_MULTIPLE && ctrlCpuNums != 1) {
        KNET_ERR("Multiple mode does not support multiple ctrl vcpu");
        return -1;
    }

    for (int i = 0; i < ctrlCpuNums; i++) {
        cJSON* ctrlCpu = cJSON_GetArrayItem(jsonValue, i);
        if (ctrlCpu == NULL) {
            KNET_ERR("K-NET conf ctrlVcpuIds[%d] is not found", i);
            return -1;
        }
        // 校验参数是否为整数
        if (ctrlCpu->valuedouble != (int)ctrlCpu->valuedouble) {
            KNET_ERR("The Ctrl vcpu id is not an integer");
            return -1;
        }

        if (ctrlCpu->valueint < param->intValue.min || ctrlCpu->valueint > param->intValue.max) {
            KNET_ERR("Ctrl vcpu get failed, invalid value and out of [%lld, %lld]",
                param->intValue.min,
                param->intValue.max);
            return -1;
        }

        value->intValueArr[i] = ctrlCpu->valueint;
    }

    return 0;
}

int StringSetter(cJSON *jsonValue, union KNET_CfgValue *value, union KnetCfgValidateParam *param)
{
    if (JsonChecker(jsonValue, value, param, cJSON_String) != 0) {
        return -1;
    }

    bool matched = KNET_RegMatch(param->pattern, jsonValue->valuestring);
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

int IpSetter(cJSON *jsonValue, union KNET_CfgValue *value, union KnetCfgValidateParam *param)
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

int NetMaskSetter(cJSON *jsonValue, union KNET_CfgValue *value, union KnetCfgValidateParam *param)
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

int MacSetter(cJSON *jsonValue, union KNET_CfgValue *value, union KnetCfgValidateParam *param)
{
    if (JsonChecker(jsonValue, value, param, cJSON_String) != 0) {
        return -1;
    }

    (void)memset_s(value->strValue, sizeof(value->strValue), 0, sizeof(value->strValue));
    int ret = KNET_ParseMac(jsonValue->valuestring, (uint8_t *)value->strValue);
    if (ret != 0) {
        KNET_ERR("Json mac get failed, invalid mac");
        return -1;
    }

    return 0;
}

static int CheckCoreListNum(char *substr)
{
    uint32_t num = 0;
    if (KNET_TransStrToNum(substr, &num) != 0) {
        KNET_ERR("Check CoreListNum failed, invalid substr");
        return -1;
    }

    if (num >= MAX_CORE_NUM) {
        KNET_ERR("Core number must be in range [0, %d]", MAX_CORE_NUM - 1);
        return -1;
    }

    return (int)num;
}

KNET_STATIC int CheckRangeStr(int leftNum, int rightNum)
{
    if (leftNum < 0 || rightNum < 0) {
        KNET_ERR("LeftNum and rightNum must be positive numbers");
        return -1;
    }

    if (leftNum >= rightNum) {
        KNET_ERR("RightNum must be greater than leftNum");
        return -1;
    }

    if (rightNum >= MAX_CORE_NUM) {
        KNET_ERR("RightNum is too large, max num: %d", MAX_CORE_NUM - 1);
        return -1;
    }

    return 0;
}

KNET_STATIC int PhraseRangeStr(char *substr)
{
    char tempStr[MAX_STRVALUE_NUM] = {0};
    int ret = strcpy_s(tempStr, MAX_STRVALUE_NUM, substr);
    if (ret != 0) {
        KNET_ERR("Strcpy failed, ret %d", ret);
        return -1;
    }

    char *hyphen = strchr(tempStr, '-');
    if (hyphen == NULL) {
        KNET_ERR("Invalid substr in core list");
        return -1;
    }

    *hyphen = '\0';
    char *leftStr = tempStr;
    int leftNum = CheckCoreListNum(leftStr);
    if (leftNum < 0) {
        KNET_ERR("Check left core num failed");
        return -1;
    }

    char *rightStr = hyphen + 1;
    int rightNum = CheckCoreListNum(rightStr);
    if (rightNum < 0) {
        KNET_ERR("Check right core num failed");
        return -1;
    }

    ret = CheckRangeStr(leftNum, rightNum);
    if (ret != 0) {
        KNET_ERR("Check range core string failed");
        return -1;
    }

    for (int n = leftNum; n <= rightNum; ++n) {
        ret = KnetCoreListAppend(n);
        if (ret != 0) {
            KNET_ERR("Append core list failed");
            return -1;
        }
    }

    return 0;
}

static int CoreListInit(char *coreListGlobal)
{
    if (coreListGlobal == NULL) {
        KNET_ERR("Core list global is null");
        return -1;
    }

    char coreListStr[MAX_STRING_LEN] = {0};

    int memcpyRet = memcpy_s(coreListStr, sizeof(coreListStr), coreListGlobal, strlen(coreListGlobal) + 1);
    if (memcpyRet != 0) {
        KNET_ERR("Core list init failed by memcpy: %d", memcpyRet);
        return -1;
    }

    int ret = 0;
    char *substr = NULL;
    char *nextSubStr = NULL;
    for (substr = strtok_s(coreListStr, ",", &nextSubStr); substr != NULL; substr =  strtok_s(NULL, ",", &nextSubStr)) {
        if (strchr(substr, '-') == NULL) {
            ret = CheckCoreListNum(substr);
            if (ret < 0) {
                KNET_ERR("Check num failed");
                return -1;
            }
            ret = KnetCoreListAppend(ret);
            if (ret != 0) {
                KNET_ERR("Core list append cpu failed");
                return -1;
            }
        } else {
            ret = PhraseRangeStr(substr);
            if (ret != 0) {
                KNET_ERR("Phrase range str failed");
                return -1;
            }
        }
    }

    if (KnetCheckDuplicateCore()) {
        KNET_ERR("There are duplicate cores in core list");
        return -1;
    }
    
    if (!KnetCheckAvailableCore()) {
        KNET_ERR("Some cores in core list are not available");
        return -1;
    }

    KnetQueueInit();
    return 0;
}

int CoreListGlobalSetter(cJSON *jsonValue, union KNET_CfgValue *value, union KnetCfgValidateParam *param)
{
    int ret = 0;
    if (KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue == 1) {
        value->strValue[0] = '\0';
        // 此时还没有设置全局worker num，可以获取到正确值，为了获取rpc传输时正常的g_coreListIndex
        int workerNum = KNET_GetCfg(CONF_TCP_MAX_WORKER_NUM)->intValue;
        KnetSetQueueNum(workerNum);
        KnetQueueInit();
        return 0;
    }

    if (JsonChecker(jsonValue, value, param, cJSON_String) != 0) {
        return -1;
    }

    ret = CoreListInit(jsonValue->valuestring);
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

int BdfNumsSetter(cJSON* jsonValue, union KNET_CfgValue* value, union KnetCfgValidateParam* param)
{
    if (JsonChecker(jsonValue, value, param, cJSON_Array) != 0) {
        return -1;
    }

    int bdfCount = cJSON_GetArraySize(jsonValue);
    if (bdfCount < 1 || bdfCount > MAX_BDF_COUNT) {
        KNET_ERR("Bdf count should be in range [1, 2]");
        return -1;
    }

    if (KNET_GetCfg(CONF_INTERFACE_BOND_ENABLE)->intValue == 0 && bdfCount != 1) {
        KNET_ERR("Bond is not enabled, bdf count should to be 1");
        return -1;
    }

    if (KNET_GetCfg(CONF_INTERFACE_BOND_ENABLE)->intValue == 1 && bdfCount != MAX_BDF_COUNT) {
        KNET_ERR("Bond is enabled, bdf count should to be 2");
        return -1;
    }

    union KnetCfgValidateParam knetCfgValidateParam = {.pattern =
                                                           "^[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\\.[0-7]$"};

    for (int i = 0; i < bdfCount; i++) {
        cJSON* bdfNum = cJSON_GetArrayItem(jsonValue, i);
        if (bdfNum == NULL) {
            KNET_ERR("K-NET conf bdf_nums[%d] is not found", i);
            return -1;
        }

        bool matched = KNET_RegMatch(knetCfgValidateParam.pattern, bdfNum->valuestring);
        if (matched == false) {
            KNET_ERR("K-NET conf bdf_nums[%d] is invalid", i);
            return -1;
        }

        size_t jsonStrLen = strlen(bdfNum->valuestring) + 1;
        int memcpyRet = memcpy_s(value->strValueArr[i], sizeof(value->strValueArr[i]), bdfNum->valuestring, jsonStrLen);
        if (memcpyRet != 0) {
            KNET_ERR("K-NET conf bdf_nums[%d] memcpy failed, ret %d", i, memcpyRet);
            return -1;
        }
    }
    if (KNET_GetCfg(CONF_INTERFACE_BOND_ENABLE)->intValue == 1 &&
        memcmp(value->strValueArr[0], value->strValueArr[1], MAX_STRVALUE_NUM) == 0) {
        KNET_ERR("K-NET conf bdf_nums[1] cannot be the same as bdf_nums[0]");
        return -1;
    }

    return 0;
}