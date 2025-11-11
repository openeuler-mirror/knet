/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: telemetry debug操作
 */
#include <unistd.h>

#include "cJSON.h"
#include "rte_eal.h"
#include "rte_memzone.h"

#include "dp_debug_api.h"
#include "knet_log.h"
#include "knet_dpdk_telemetry.h"
#include "knet_config.h"

typedef struct {
    const char *param;
    DP_StatType_t type;
} StatMapping;

static const StatMapping KNET_STAT_MAPPINGS[DP_STAT_MAX] = {
    {"tcp", DP_STAT_TCP},
    {"conn", DP_STAT_TCP_CONN},
    {"pkt", DP_STAT_PKT},
    {"abn", DP_STAT_ABN},
    {"mem", DP_STAT_MEM},
    {"pbuf", DP_STAT_PBUF}
};

char g_knetDebugOutput[MAX_OUTPUT_LEN] = {0};
char g_knetDebugKey[DP_STAT_MAX][MAX_KEY_NUM][MAX_KEY_LEN] = {0};
uint64_t g_knetDebugValue[DP_STAT_MAX][MAX_KEY_NUM] = {0};

KNET_DpShowStatisticsHook g_dpShowStatisticsHook = NULL;
int KNET_TelemetryDpShowStatisticsHookReg(KNET_DpShowStatisticsHook hook)
{
    if (hook == NULL) {
        KNET_ERR("K-NET register telemetry run dp show statistics hook is null");
        return KNET_ERROR;
    }

    g_dpShowStatisticsHook = hook;
    return KNET_OK;
}

/**
 * @brief DP_ShowStatistics 调用后触发，将 dp 返回的信息写入共享内存
 * @attention 只能被 KNET_ACC_Debug 调用，调用前 output 已做判空
 */
int KNET_DebugOutputToTelemetry(const char *output, uint32_t len)
{
    if (len > MAX_OUTPUT_LEN - 1) {
        KNET_ERR("Output too long, len %d", len);
        return KNET_ERROR;
    }

    if (KNET_GetCfg(CONF_COMMON_MODE).intValue == KNET_RUN_MODE_SINGLE) {
        int ret = snprintf_s(g_knetDebugOutput, MAX_OUTPUT_LEN, len, "%s", output);
        if (ret < 0) {
            KNET_ERR("Snprintf failed, ret %d", ret);
            return KNET_ERROR;
        }
    } else {
        const struct rte_memzone *mz = rte_memzone_lookup(KNET_TELEMETRY_MZ_NAME);
        if (mz == NULL || mz->addr == NULL) {
            KNET_ERR("Subprocess couldn't allocate memory for dp debug info");
            return KNET_ERROR;
        }

        TelemetryInfo *telemetryInfo = mz->addr;
        int queId = KNET_GetCfg(CONF_INNER_QID).intValue;
        if (strncpy_s(telemetryInfo->message[queId], MAX_OUTPUT_LEN, output, len) != 0) {
            KNET_ERR("K-NET acc debug strncpy failed");
            return KNET_ERROR;
        }
    }

    return KNET_OK;
}

/**
 * @brief 将 json 中 key 对应的字符串类型 value 解析为 uint64_t 类型，添加到字典中
 * @attention 只能被 KNET_TelemetryStatisticCallback 调用，调用前 json key data 已做判空
 */
KNET_STATIC int KnetAddJsonToData(cJSON *json, const char *key, struct rte_tel_data *data)
{
    cJSON *num = cJSON_GetObjectItemCaseSensitive(json, key);
    if (num == NULL || num->valuestring == NULL) {
        KNET_ERR("Add json to info failed, num is null");
        return KNET_ERROR;
    }

    uint64_t value = strtoull(num->valuestring, NULL, DECIMAL_NUM);
    if (rte_tel_data_add_dict_u64(data, key, value) != 0) {
        KNET_ERR("Rte telemetry data add dict u64 failed");
        return KNET_ERROR;
    }

    return KNET_OK;
}

static int ValidateInputAndParseParams(
    const char *cmd, const char *params, struct rte_tel_data *data, DP_StatType_t *type)
{
    if (data == NULL || cmd == NULL) {
        KNET_ERR("Rte telemetry data is null");
        return KNET_ERROR;
    }

    /* 内部判断 params 是否为 NULL，并打印日志 */
    *type = KNET_GetStatTypeFromString(params);
    if (*type == DP_STAT_MAX) {
        return KNET_ERROR;
    }

    return KNET_OK;
}

static int HandleTelemetryHook(DP_StatType_t type)
{
    if (g_dpShowStatisticsHook == NULL) {
        KNET_ERR("K-NET dp show statistics hook is null");
        return KNET_ERROR;
    }
    g_dpShowStatisticsHook(type, -1, KNET_STAT_OUTPUT_TO_TELEMETRY);
    return KNET_OK;
}

static int ProcessJsonData(cJSON *json, struct rte_tel_data *data)
{
    if (rte_tel_data_start_dict(data) != 0) {
        KNET_ERR("Rte telemetry data start dict failed");
        return KNET_ERROR;
    }

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, json)
    {
        if (item == NULL || item->string == NULL) {
            continue;
        }
        if (KnetAddJsonToData(json, item->string, data) != KNET_OK) {
            KNET_ERR("K-NET add json to data failed");
            return KNET_ERROR;
        }
    }
    return KNET_OK;
}

int KNET_TelemetryStatisticCallback(const char *cmd, const char *params, struct rte_tel_data *data)
{
    DP_StatType_t type = DP_STAT_MAX;
    if (ValidateInputAndParseParams(cmd, params, data, &type) != KNET_OK) {
        /* 内部已打印日志 */
        return KNET_ERROR;
    }

    /* 通过已注册的钩子调用 DP_ShowStatistics,内部已打印日志 */
    if (HandleTelemetryHook(type) != KNET_OK) {
        return KNET_ERROR;
    }

    cJSON *json = cJSON_Parse(g_knetDebugOutput);
    if (json == NULL) {
        KNET_ERR("K-NET telemetry statistic parse cjson failed");
        return KNET_ERROR;
    }

    if (ProcessJsonData(json, data) != KNET_OK) {
        KNET_ERR("K-NET process cjson failed");
        cJSON_Delete(json);
        return KNET_ERROR;
    }

    cJSON_Delete(json);
    return KNET_OK;
}

DP_StatType_t KNET_GetStatTypeFromString(const char *param)
{
    if (param == NULL) {
        KNET_ERR("Telemetry get param string null");
        return DP_STAT_MAX;
    }

    for (int i = 0; i < DP_STAT_MAX; i++) {
        if (strcmp(KNET_STAT_MAPPINGS[i].param, param) == 0) {
            return KNET_STAT_MAPPINGS[i].type;
        }
    }

    KNET_ERR("Telemetry param error");
    return DP_STAT_MAX;
}

/**
 * @brief 多进程场景计算统计信息加和
 * @attention 只能被 KnetGetStatisticInfo 调用，调用前 json key 已做判空
 */
KNET_STATIC int KnetStatisticMpInfo(cJSON *json, const char *key, const DP_StatType_t type, int index)
{
    if (g_knetDebugKey[type][index][0] == '\0') {
        int ret = snprintf_s(g_knetDebugKey[type][index], MAX_KEY_LEN, MAX_KEY_LEN - 1, "%s", key);
        if (ret < 0) {
            KNET_ERR("Snprintf failed, ret %d", ret);
            return KNET_ERROR;
        }
    }

    cJSON *num = cJSON_GetObjectItemCaseSensitive(json, key);
    if (num == NULL || num->valuestring == NULL) {
        KNET_ERR("Add json to info failed, num is null");
        return KNET_ERROR;
    }

    uint64_t value = strtoull(num->valuestring, NULL, DECIMAL_NUM);
    if (g_knetDebugValue[type][index] > UINT64_MAX - value) {
        KNET_ERR("Overflow detected, %llu", value);
        return KNET_ERROR;
    }

    g_knetDebugValue[type][index] += value;

    return KNET_OK;
}

static void InitializeTelemetryInfo(TelemetryInfo *telemetryInfo, DP_StatType_t type)
{
    for (int i = 0; i < MAX_QUEUE_NUM; i++) {
        if (KNET_IsQueueIdUsed(i)) {
            telemetryInfo->msgReady[i] = 1;
        }
    }
    telemetryInfo->statType = type;
}

static int HandleTimeout(TelemetryInfo *telemetryInfo, int i)
{
    int timeoutTimes = TIMEOUT_TIMES;
    while (timeoutTimes > 0 && telemetryInfo->msgReady[i] != 0) {
        timeoutTimes--;
        usleep(TELEMETRY_DEBUG_USLEEP);
    }

    if (timeoutTimes == 0) {
        KNET_ERR("Telemetry debug callback time out");
        return KNET_ERROR;
    }

    return KNET_OK;
}

static int ParseJsonAndProcess(TelemetryInfo *telemetryInfo, DP_StatType_t statType, int i)
{
    cJSON *json = cJSON_Parse(telemetryInfo->message[i]);
    if (json == NULL) {
        KNET_ERR("Cjson parse telemetry info message failed");
        return KNET_ERROR;
    }

    int jsonSize = cJSON_GetArraySize(json);
    if (jsonSize >= MAX_KEY_NUM) {
        KNET_ERR("Json entry: %d too much, it must less %d", jsonSize, MAX_KEY_NUM);
        cJSON_Delete(json);
        return KNET_ERROR;
    }

    for (int j = 0; j < jsonSize; j++) {
        cJSON *item = cJSON_GetArrayItem(json, j);
        if (item == NULL || item->string == NULL) {
            continue;
        }

        if (KnetStatisticMpInfo(json, item->string, statType, j) != KNET_OK) {
            KNET_ERR("K-NET Statistic Mutiprocess Info failed");
            return KNET_ERROR;
        }
    }

    cJSON_Delete(json);
    return KNET_OK;
}

/**
 * @brief KNET_TelemetryStatisticCallbackMp 调用后触发，将 dp 返回的信息写入共享内存
 * @attention 只能被 KNET_TelemetryStatisticCallbackMp 调用，调用前 telemetryInfo 已做判空
 */
KNET_STATIC int KnetGetStatisticInfo(TelemetryInfo *telemetryInfo, DP_StatType_t statType)
{
    for (int i = 0; i < MAX_QUEUE_NUM; i++) {
        if (!KNET_IsQueueIdUsed(i)) {
            continue;
        }

        if (HandleTimeout(telemetryInfo, i) != KNET_OK) {
            return KNET_ERROR;
        }

        if (ParseJsonAndProcess(telemetryInfo, statType, i) != KNET_OK) {
            return KNET_ERROR;
        }
    }

    return KNET_OK;
}

static int PrintTelemetryStats(struct rte_tel_data *data, DP_StatType_t type)
{
    for (int i = 0; i < MAX_KEY_NUM; i++) {
        if (g_knetDebugKey[type][i][0] == '\0') {
            continue;
        }

        if (rte_tel_data_add_dict_u64(data, g_knetDebugKey[type][i], g_knetDebugValue[type][i]) != 0) {
            KNET_ERR("Rte telemetry data add dict u64 failed");
            g_knetDebugValue[type][i] = 0;
            return KNET_ERROR;
        }

        g_knetDebugValue[type][i] = 0;
    }
    return KNET_OK;
}

int KNET_TelemetryStatisticCallbackMp(const char *cmd, const char *params, struct rte_tel_data *data)
{
    DP_StatType_t type = DP_STAT_MAX;
    if (ValidateInputAndParseParams(cmd, params, data, &type) != KNET_OK) {
        /* 内部已打印日志 */
        return KNET_ERROR;
    }

    const struct rte_memzone *mz = rte_memzone_lookup(KNET_TELEMETRY_MZ_NAME);
    if (mz == NULL || mz->addr == NULL) {
        KNET_ERR("Subprocess couldn't allocate memory for dp debug info");
        return KNET_ERROR;
    }

    TelemetryInfo *telemetryInfo = mz->addr;
    InitializeTelemetryInfo(telemetryInfo, type);

    if (rte_tel_data_start_dict(data) != 0) {
        KNET_ERR("Rte telemetry data start dict failed");
        return KNET_ERROR;
    }

    if (KnetGetStatisticInfo(telemetryInfo, type) != KNET_OK) {
        /* 内部已打印日志 */
        return KNET_ERROR;
    }

    if (PrintTelemetryStats(data, type) != KNET_OK) {
        /* 内部已打印日志 */
        return KNET_ERROR;
    }

    return KNET_OK;
}
