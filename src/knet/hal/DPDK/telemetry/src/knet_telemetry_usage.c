/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: telemetry测带宽和包率
 */
#include <unistd.h>
#include "cJSON.h"
#include "knet_log.h"
#include "knet_telemetry_call.h"
#include "knet_telemetry_debug.h"
#include "rte_ethdev.h"

#define ETHDEV_USAGE_INFO_MAX_NUM 60
#define ETHDEV_USAGE_INTERVAL 1
#define MAX_BITRATE_STR_SIZE 60
#define MAX_USAGE_OUTPUT_LEN 8192
#define UNIT_PREFIX_COUNT 7
#define KILO_UNIT_SI 1000.0

#define KEY_STR_TIME "t"
#define KEY_STR_TX "tx"
#define KEY_STR_RX "rx"

struct NetDevUsageInfo {
    uint64_t ipps;
    uint64_t opps;
    uint64_t ibps;
    uint64_t obps;
};

typedef enum {
    USAGE_PARAM_PORT = 0,
    USAGE_PARAM_TIME,
    USAGE_PARAM_NUM
} UsageIndex;

KNET_STATIC char g_usageDebugOutput[MAX_USAGE_OUTPUT_LEN] = {0};
KNET_STATIC const char *g_bitUnits[UNIT_PREFIX_COUNT] = {
    "bit/s", "Kbit/s", "Mbit/s", "Gbit/s", "Tbit/s", "Pbit/s", "Ebit/s"};

KNET_STATIC int ParseEthdevUsageQueryParam(const char *param, uint32_t *port, uint32_t *time)
{
    uint32_t paramsArr[USAGE_PARAM_NUM] = {0};
    if (ParseTelemetryParams(param, paramsArr, USAGE_PARAM_NUM) != USAGE_PARAM_NUM) {
        KNET_ERR("Netdev usage query, rte telemetry invalid input failed, except "
                 "format <port> <time>");
        return KNET_ERROR;
    }
    *port = paramsArr[USAGE_PARAM_PORT];
    *time = paramsArr[USAGE_PARAM_TIME];
    if (*time <= 0 || *time > ETHDEV_USAGE_INFO_MAX_NUM) {
        KNET_ERR("Netdev usage query, rte telemetry invalid input failed, time "
                 "must be in range (0, %d]",
                 ETHDEV_USAGE_INFO_MAX_NUM);
        return KNET_ERROR;
    }
    return KNET_OK;
}

KNET_STATIC int GetEthdevUsageInfos(struct NetDevUsageInfo *infos, int port, int time)
{
    struct rte_eth_stats lastStats = {0};
    struct rte_eth_stats curStats = {0};
    int ret = rte_eth_stats_get(port, &lastStats);
    if (ret != 0) {
        KNET_ERR("Get Netdev Usage Infos failed, port %d is not exist", port);
        return KNET_ERROR;
    }

    for (int i = 0; i < time; i++) {
        sleep(ETHDEV_USAGE_INTERVAL);
        ret = rte_eth_stats_get(port, &curStats);
        if (ret != 0) {
            KNET_ERR("Get Netdev Usage Infos failed, port %d is not exist", port);
            return KNET_ERROR;
        }
        infos[i].ipps = curStats.ipackets - lastStats.ipackets;
        infos[i].opps = curStats.opackets - lastStats.opackets;
        infos[i].ibps = curStats.ibytes - lastStats.ibytes;
        infos[i].obps = curStats.obytes - lastStats.obytes;
        lastStats = curStats;
    }
    return KNET_OK;
}
KNET_STATIC int FormatBitrate(char *output, size_t outputSize, uint64_t bytes, int interval)
{
    double bitPerSec = bytes * 8.0 / interval;
    int i = 0;
    while ((i + 1) < UNIT_PREFIX_COUNT && bitPerSec >= KILO_UNIT_SI) {
        bitPerSec /= KILO_UNIT_SI;
        ++i;
    }
    int ret = snprintf_s(output, outputSize, (outputSize - 1), "%.2f %s",
                         bitPerSec, g_bitUnits[i]);
    if (ret < 0) {
        KNET_ERR("snprintf bitrate failed, ret %d", ret);
        return KNET_ERROR;
    }
    return KNET_OK;
}
KNET_STATIC int GetEthdevUsageOutput(struct NetDevUsageInfo *infos, int time)
{
    char *curPos = g_usageDebugOutput;
    size_t remain = MAX_USAGE_OUTPUT_LEN;
    int ret;
    char outBdString[MAX_BITRATE_STR_SIZE] = {0};
    char inBdString[MAX_BITRATE_STR_SIZE] = {0};
    for (int i = 0; i < time; ++i) {
        ret = FormatBitrate(outBdString, MAX_BITRATE_STR_SIZE, infos[i].obps,
                            ETHDEV_USAGE_INTERVAL);
        if (ret != KNET_OK) {
            KNET_ERR("Format out bitrate failed, bytes %lu, interval %d", infos[i].obps, ETHDEV_USAGE_INTERVAL);
            return KNET_ERROR;
        }
        ret = FormatBitrate(inBdString, MAX_BITRATE_STR_SIZE, infos[i].ibps,
                            ETHDEV_USAGE_INTERVAL);
        if (ret != KNET_OK) {
            KNET_ERR("Format in bitrate failed, bytes %lu, interval %d", infos[i].ibps, ETHDEV_USAGE_INTERVAL);
            return KNET_ERROR;
        }
        ret = snprintf_s(curPos, remain, (MAX_USAGE_OUTPUT_LEN - 1) / time,
                         "%s{\"%s\":\"%d-%ds\",\"%s\":\"%s, %lu "
                         "p/s\",\"%s\":\"%s, %lu p/s\"}%s",
                         (i == 0) ? "[" : "", KEY_STR_TIME, i, i + 1, KEY_STR_TX,
                         outBdString, infos[i].opps, KEY_STR_RX, inBdString,
                         infos[i].ipps, (i == time - 1) ? "]" : ",");
        if (ret < 0) {
            KNET_ERR("Snprintf failed, ret %d ,remain %d, size %d", ret, remain, (MAX_USAGE_OUTPUT_LEN - 1) / time);
            return KNET_ERROR;
        }
        curPos += strlen(curPos);
        remain -= strlen(curPos);
    }
    return KNET_OK;
}
KNET_STATIC int AddStrToSub(cJSON *entry, struct rte_tel_data *sub)
{
    int ret;
    const char *keys[] = {KEY_STR_TX, KEY_STR_RX};
    size_t keyNum = sizeof(keys) / sizeof(keys[0]);
    for (size_t i = 0; i < keyNum; i++) {
        cJSON *item = cJSON_GetObjectItem(entry, keys[i]);
        if (item == NULL || !cJSON_IsString(item)) {
            KNET_ERR("cJSON get objiect item failed, key %s", keys[i]);
            return KNET_ERROR;
        }
        ret = rte_tel_data_add_dict_string(sub, keys[i], item->valuestring);
        if (ret != KNET_OK) {
            KNET_ERR("Rte telemetry data add dict string failed, ret %d, errno %d, "
                     "key %s,value %s",
                     ret, errno, keys[i], item->valuestring);
            return KNET_ERROR;
        }
    }
    return KNET_OK;
}
KNET_STATIC int AddSubToData(cJSON *entry, struct rte_tel_data *sub, struct rte_tel_data *data)
{
    int ret;
    rte_tel_data_start_dict(sub);
    ret = AddStrToSub(entry, sub);
    if (ret != KNET_OK) {
        KNET_ERR("Add str to sub failed,ret %d", ret);
        return KNET_ERROR;
    }
    cJSON *t = cJSON_GetObjectItem(entry, KEY_STR_TIME);
    if (t == NULL || !cJSON_IsString(t)) {
        KNET_ERR("cJSON get objiect item failed, key %s", KEY_STR_TIME);
        return KNET_ERROR;
    }
    ret = rte_tel_data_add_dict_container(data, t->valuestring, sub, 0);
    if (ret != KNET_OK) {
        KNET_ERR("Rte telemetry data add dict container failed, ret %d, errno %d",
                 ret, errno);
        return KNET_ERROR;
    }
    return KNET_OK;
}
KNET_STATIC int AddOutputJsonToData(struct rte_tel_data *data, cJSON *json)
{
    cJSON *entry = NULL;
    int ret;
    cJSON_ArrayForEach(entry, json)
    {
        struct rte_tel_data *sub = rte_tel_data_alloc();
        if (sub == NULL) {
            KNET_ERR("Rte telemetry data alloc failed");
            return KNET_ERROR;
        }
        ret = AddSubToData(entry, sub, data);
        if (ret != KNET_OK) {
            KNET_ERR("Add sub to rte tel data failed");
            rte_tel_data_free(sub);
            return KNET_ERROR;
        }
    }
    return KNET_OK;
}
KNET_STATIC int ParseEthdevUsageOutput(struct rte_tel_data *data)
{
    rte_tel_data_start_dict(data);
    cJSON *json = cJSON_Parse(g_usageDebugOutput);
    if (json == NULL) {
        KNET_ERR("Parse cjson failed");
        return KNET_ERROR;
    }
    int ret = AddOutputJsonToData(data, json);
    if (ret != KNET_OK) {
        KNET_ERR("Add output json to data failed");
        cJSON_Delete(json);
        return KNET_ERROR;
    }
    cJSON_Delete(json);
    return KNET_OK;
}
// 实现网卡带宽包率的查询
int KnetTelemetryEthdevUsageCallback(const char *cmd, const char *param, struct rte_tel_data *data)
{
    if (cmd == NULL || param == NULL || data == NULL) {
        KNET_ERR("Netdev usage query, rte telemetry invalid input failed, something is null");
        return KNET_ERROR;
    }

    uint32_t port;
    uint32_t time;
    int ret = ParseEthdevUsageQueryParam(param, &port, &time);
    if (ret != KNET_OK) {
        KNET_ERR("Netdev usage query, parse netdev usage query param failed");
        return KNET_ERROR;
    }

    struct NetDevUsageInfo infos[ETHDEV_USAGE_INFO_MAX_NUM] = {0};
    ret = GetEthdevUsageInfos(infos, port, time);
    if (ret != KNET_OK) {
        KNET_ERR("Netdev usage query, get netdev usage infos failed");
        return KNET_ERROR;
    }
    ret = GetEthdevUsageOutput(infos, time);
    if (ret != KNET_OK) {
        KNET_ERR("Netdev usage query, get netdev usage output failed");
        return KNET_ERROR;
    }
    ret = ParseEthdevUsageOutput(data);
    if (ret != KNET_OK) {
        KNET_ERR("Netdev usage query, parse ethdev usage output failed");
        return KNET_ERROR;
    }
    return KNET_OK;
}
