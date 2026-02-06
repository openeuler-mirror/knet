/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: telemetry debug 相关操作
 */
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "cJSON.h"
#include "rte_eal.h"
#include "rte_hash.h"
#include "rte_errno.h"
#include "rte_memzone.h"

#include "dp_debug_api.h"
#include "dp_socket_api.h"
#include "knet_log.h"
#include "knet_types.h"
#include "knet_config.h"
#include "knet_utils.h"
#include "knet_offload.h"
#include "knet_telemetry.h"
#include "knet_transmission.h"
#include "knet_telemetry_call.h"
#include "knet_telemetry_debug.h"

#define TELEMETRY_DEBUG_USLEEP 100000
#define MAX_KEY_NUM 128
#define MAX_KEY_LEN 32
#define TIMEOUT_TIMES 10
#define DECIMAL_NUM 10
#define PID_MAX_LEN 20
#define FLOW_TABLE_MAX_NUM 256
#define RIGHT_MOVE_16BITS 16
#define IP_32BITS_FULL_MASK 0xFFFF
#define INET_IP_STR_LEN 16
#define INET_PORT_STR_LEN 8
#define MASK_STR_LEN 16
#define INVALID_TID 0U
#define FLOW_PROTO_STR_MAX_LEN 16
#define FLOW_ACTION_STR_MAX_LEN 64
#define FLOW_ARP_QUEUE_ID_STR_LEN 64

typedef struct {
    const char *cmd;
    DP_StatType_t type;
} StatMapping;

static const StatMapping KNET_STAT_MAPPINGS[DP_STAT_MAX] = {
    {"/knet/stack/tcp_stat", DP_STAT_TCP},
    {"/knet/stack/conn_stat", DP_STAT_TCP_CONN},
    {"/knet/stack/pkt_stat", DP_STAT_PKT},
    {"/knet/stack/abn_stat", DP_STAT_ABN},
    {"/knet/stack/mem_stat", DP_STAT_MEM},
    {"/knet/stack/pbuf_stat", DP_STAT_PBUF}
};

typedef enum {
    FLOW_TABLE_START_INDEX = 0,
    FLOW_TABLE_COUNT,
    FLOW_TABLE_PARAMS_MAX
} FlowTableIndex;

char g_knetDebugOutput[MAX_OUTPUT_LEN] = {0};

KNET_DpTelemetryHooks g_dpTelemetryHooks = {NULL, NULL, NULL, NULL, NULL};
int KNET_DpTelemetryHookReg(KNET_DpTelemetryHooks dpTelemetryHooks)
{
    if (dpTelemetryHooks.dpShowStatisticsHook == NULL || dpTelemetryHooks.dpSocketCountGetHook == NULL||
        dpTelemetryHooks.dpGetSocketStateHook == NULL || dpTelemetryHooks.dpGetSocketDetailsHook == NULL ||
        dpTelemetryHooks.dpGetEpollDetailsHook == NULL) {
        KNET_ERR("K-NET register dp telemetry hook is null");
        return KNET_ERROR;
    }
 
    g_dpTelemetryHooks.dpShowStatisticsHook = dpTelemetryHooks.dpShowStatisticsHook;
    g_dpTelemetryHooks.dpSocketCountGetHook = dpTelemetryHooks.dpSocketCountGetHook;
    g_dpTelemetryHooks.dpGetSocketStateHook = dpTelemetryHooks.dpGetSocketStateHook;
    g_dpTelemetryHooks.dpGetSocketDetailsHook = dpTelemetryHooks.dpGetSocketDetailsHook;
    g_dpTelemetryHooks.dpGetEpollDetailsHook = dpTelemetryHooks.dpGetEpollDetailsHook;

    return KNET_OK;
}

/**
 * @brief DP_ShowStatistics 调用后触发，将 tcp 返回的信息写入共享内存
 * @attention 只能被 KNET_ACC_Debug 调用，调用前 output 已做判空
 */
int KNET_DebugOutputToTelemetry(const char *output, uint32_t len)
{
    if (len > MAX_OUTPUT_LEN - 1) {
        KNET_ERR("Output too long, len %d", len);
        return KNET_ERROR;
    }

    if (KNET_GetCfg(CONF_COMMON_MODE)->intValue == KNET_RUN_MODE_SINGLE) {
        int ret = snprintf_s(g_knetDebugOutput, MAX_OUTPUT_LEN, len, "%s", output);
        if (ret < 0) {
            KNET_ERR("Snprintf failed, ret %d", ret);
            return KNET_ERROR;
        }

        return KNET_OK;
    }

    const struct rte_memzone* mz = rte_memzone_lookup(KNET_TELEMETRY_MZ_NAME);
    if (mz == NULL || mz->addr == NULL) {
        KNET_ERR("Subprocess couldn't allocate memory for tcp debug info");
        return KNET_ERROR;
    }

    KNET_TelemetryInfo* telemetryInfo = mz->addr;
    int queId = KNET_GetCfg(CONF_INNER_QID)->intValue;
    if (strncpy_s(telemetryInfo->message[queId], MAX_OUTPUT_LEN, output, len) != 0) {
        KNET_ERR("K-NET acc debug strncpy failed");
        return KNET_ERROR;
    }

    return KNET_OK;
}

/**
 * @brief 将 json 中 key 对应的字符串类型 value 解析为 uint64_t 类型，添加到字典中
 * @attention 只能被 KnetTelemetryStatisticCallback 调用，调用前 json key data 已做判空
 */
KNET_STATIC int AddJsonToData(cJSON *json, const char *key, struct rte_tel_data *data)
{
    cJSON *num = cJSON_GetObjectItemCaseSensitive(json, key);
    if (num == NULL || num->valuestring == NULL) {
        KNET_ERR("Add json to info failed, num is null");
        return KNET_ERROR;
    }

    errno = 0;
    uint64_t value = strtoull(num->valuestring, NULL, DECIMAL_NUM);
    if (errno == ERANGE) {
        KNET_ERR("Telemetry add json to data failed, value is out of range");
        return KNET_ERROR;
    }

    if (ADD_DICT_INT_FUNC(data, key, value) != 0) {
        KNET_ERR("Rte telemetry data add dict u64 failed");
        return KNET_ERROR;
    }

    return KNET_OK;
}


KNET_STATIC DP_StatType_t GetStatTypeFromString(const char *param)
{
    if (param == NULL) {
        KNET_ERR("Telemetry get param string null");
        return DP_STAT_MAX;
    }

    for (int i = 0; i < DP_STAT_MAX; i++) {
        if (strcmp(KNET_STAT_MAPPINGS[i].cmd, param) == 0) {
            return KNET_STAT_MAPPINGS[i].type;
        }
    }

    return DP_STAT_MAX;
}

KNET_STATIC int HandleTelemetryHook(DP_StatType_t type)
{
    if (g_dpTelemetryHooks.dpShowStatisticsHook == NULL) {
        KNET_ERR("K-NET dp show statistics hook is null");
        return KNET_ERROR;
    }
    g_dpTelemetryHooks.dpShowStatisticsHook(type, -1, KNET_STAT_OUTPUT_TO_TELEMETRY);
    return KNET_OK;
}

KNET_STATIC int ProcessJsonData(cJSON *json, struct rte_tel_data *data)
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
        if (AddJsonToData(json, item->string, data) != KNET_OK) {
            KNET_ERR("K-NET add json to data failed");
            return KNET_ERROR;
        }
    }
    return KNET_OK;
}

KNET_STATIC int ValidateParams(const char *params, struct rte_tel_data *data)
{
    if (data == NULL) {
        KNET_ERR("Rte telemetry data is null");
        return KNET_ERROR;
    }
    /* 单进程场景该回调允许params为空，或者为该进程pid */
    if (params != NULL) {
        uint32_t inputPid = 0;
        if (KNET_TransStrToNum(params, &inputPid) != KNET_OK) {
            KNET_ERR("Telemetry statistic validate params failed, invalid params");
            return KNET_ERROR;
        }
        if (inputPid != (uint32_t)getpid()) {
            KNET_ERR("Telemetry statistic validate params failed, input pid is not knet process pid");
            return KNET_ERROR;
        }
    }
    return KNET_OK;
}

int KnetTelemetryStatisticCallback(const char *cmd, const char *params, struct rte_tel_data *data)
{
    DP_StatType_t type = GetStatTypeFromString(cmd);
    if (type == DP_STAT_MAX) {
        return KNET_ERROR; // 函数内部已经打印日志
    }
    if (ValidateParams(params, data) != KNET_OK) {
        return KNET_ERROR; // 函数内部已经打印日志
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

/**
 * @brief 多进程下更新queueId到pid和tid的映射关系
 * @param queId 队列号
 * @param pid 进程号
 * @param tid 线程号
 * return int 0, 成功; -1, 失败;
 */
int KNET_MaintainQueue2TidPidMp(uint32_t queId)
{
    if (queId >= MAX_QUEUE_NUM) {
        KNET_ERR("QueueId given exceed MAX_QUEUE_NUM(128)");
        return -1;
    }
    const struct rte_memzone* mz = rte_memzone_lookup(KNET_TELEMETRY_MZ_NAME);
    if (mz == NULL || mz->addr == NULL) {
        KNET_ERR("Subprocess couldn't allocate memory for tcp debug info");
        return KNET_ERROR;
    }

    KNET_TelemetryInfo* telemetryInfo = mz->addr;
    /* KNET_GetQueIdMapPidTidLcoreInfo拿到在lcore初始化时保存的pid/tid，多进程下每个从进程保存到索引0，单进程一直保存到索引n */
    KNET_QueIdMapPidTid_t* queIdMapPidTid = KNET_GetQueIdMapPidTidLcoreInfo();
    telemetryInfo->pid[queId] = queIdMapPidTid[0].pid;
    telemetryInfo->tid[queId] = queIdMapPidTid[0].tid;
    telemetryInfo->lcoreId[queId] = queIdMapPidTid[0].lcoreId;
    return KNET_OK;
}

void KnetUpdateSlaveProcessPidInfo(KNET_TelemetryInfo *telemetryInfo)
{
    telemetryInfo->telemetryType = KNET_TELEMETRY_UPDATE_QUE_INFO;
    for (int i = 0; i < MAX_QUEUE_NUM; i++) {
        if (KNET_IsQueueIdUsed(i)) {
            telemetryInfo->msgReady[i] = 1;
        }
    }
}

int KnetHandleTimeout(KNET_TelemetryInfo *telemetryInfo, int i)
{
    int timeoutTimes = TIMEOUT_TIMES;
    while (timeoutTimes > 0 && telemetryInfo->msgReady[i] != 0) {
        timeoutTimes--;
        usleep(TELEMETRY_DEBUG_USLEEP);
    }

    if (timeoutTimes == 0) {
        KNET_ERR("Process of queId %d telemetry debug callback time out", i);
        return KNET_ERROR;
    }

    return KNET_OK;
}

int KnetWaitAllSlavePorcessHandle(KNET_TelemetryInfo *telemetryInfo)
{
    for (int i = 0; i < MAX_QUEUE_NUM; i++) {
        if (!KNET_IsQueueIdUsed(i)) {
            continue;
        }
        if (KnetHandleTimeout(telemetryInfo, i) != KNET_OK) {
            return KNET_ERROR;  // 日志在KnetHandleTimeout内部打印
        }
    }

    return KNET_OK;
}

int KnetGetQueIdByPid(uint32_t pid, KNET_TelemetryInfo* telemetryInfo)
{
    int queId = -1;
    for (int i = 0; i < MAX_QUEUE_NUM; i++) {
        if (telemetryInfo->pid[i] == pid) {
            queId = i;
            break;
        }
    }
    return queId;
}

KNET_STATIC int ValidateParamsAndGetQueId(const char *params, int *queId, KNET_TelemetryInfo *telemetryInfo)
{
    uint32_t inputPid = 0;
    if (KNET_TransStrToNum(params, &inputPid) != KNET_OK) {
        KNET_ERR("K-NET telemetry statistic validata parmas failed, invalid params");
        return KNET_ERROR;
    }
    *queId = KnetGetQueIdByPid(inputPid, telemetryInfo);
    if (*queId == -1) {
        KNET_ERR("K-NET telemetry statistic invalid pid");
        return KNET_ERROR;
    }
    return KNET_OK;
}

int KnetTelemetryStatisticCallbackMp(const char *cmd, const char *params, struct rte_tel_data *data)
{
    if (data == NULL || params == NULL) {
        KNET_ERR("K-NET telemetry data or param is null");
        return KNET_ERROR;
    }
    DP_StatType_t type = GetStatTypeFromString(cmd);
    if (type == DP_STAT_MAX) {
        return KNET_ERROR; // 函数内部已经打印日志
    }

    const struct rte_memzone *mz = rte_memzone_lookup(KNET_TELEMETRY_MZ_NAME);
    if (mz == NULL || mz->addr == NULL) {
        KNET_ERR("Subprocess couldn't allocate memory for tcp debug info");
        return KNET_ERROR;
    }

    KNET_TelemetryInfo *telemetryInfo = mz->addr;
    KnetUpdateSlaveProcessPidInfo(telemetryInfo); // 初始化共享内存,内部记录从进程pid关系
    if (KnetWaitAllSlavePorcessHandle(telemetryInfo) != KNET_OK) {
        return KNET_ERROR; // 等待更新从进程pid, 日志在函数内打印
    }
    int queId = -1;
    if (ValidateParamsAndGetQueId(params, &queId, telemetryInfo) != KNET_OK) {
        return KNET_ERROR;
    }

    telemetryInfo->statType = type;
    telemetryInfo->telemetryType = KNET_TELEMETRY_STATISTIC;
    (void)memset_s(telemetryInfo->message[queId], MAX_OUTPUT_LEN, 0, MAX_OUTPUT_LEN);
    telemetryInfo->msgReady[queId] = 1;
    if (KnetHandleTimeout(telemetryInfo, queId) != KNET_OK) {
        KNET_ERR("K-NET telemetry statistic handle time out");
        return KNET_ERROR;
    }
    cJSON *json = cJSON_Parse(telemetryInfo->message[queId]);
    if (json == NULL) {
        KNET_ERR("K-NET telemetry statistic parse cjson failed");
        return KNET_ERROR;
    }
    if (ProcessJsonData(json, data) != KNET_OK) {
        KNET_ERR("K-NET telemetry statistic process cjson failed");
        cJSON_Delete(json);
        return KNET_ERROR;
    }
    cJSON_Delete(json);
    return KNET_OK;
}

int KnetGetTidByWorkerId(uint32_t workerId, uint32_t *tid)
{
    for (int queueId = 0; queueId < KNET_GetCfg(CONF_DPDK_QUEUE_NUM)->intValue; queueId++) {
        KNET_QueIdMapPidTid_t *queIdMapPidTid_t = KNET_GetQueIdMapPidTidLcoreInfo();
        if (workerId == queIdMapPidTid_t[queueId].workerId) {
            *tid = queIdMapPidTid_t[queueId].tid;
            return 0;
        }
    }
    KNET_ERR("Get tid by workerId %d failed", workerId);
    return KNET_ERROR;
}

int CheckAddContainerToDict(struct rte_tel_data *data, const char *name, struct rte_tel_data *value)
{
    int ret = rte_tel_data_add_dict_container(data, name, value, 0);
    if (ret != 0) {
        rte_tel_data_free(value);
        KNET_ERR("K-NET telemetry add_dict_container fail to add key %s, ret %d", name, ret);
        return -1;
    }
    return 0;
}


KNET_TelemetryInfo *KnetMultiSetTelemetrySHM(void)
{
    const struct rte_memzone *mz = rte_memzone_lookup(KNET_TELEMETRY_MZ_NAME);
    if (mz == NULL || mz->addr == NULL) {
        KNET_ERR("Subprocess couldn't allocate memory for tcp debug info");
        return NULL;
    }
    KNET_TelemetryInfo *telemetryInfo = (KNET_TelemetryInfo *)mz->addr;
    KnetUpdateSlaveProcessPidInfo(telemetryInfo);
    if (KnetWaitAllSlavePorcessHandle(telemetryInfo) != KNET_OK) {
        return NULL;
    }
    return telemetryInfo;
}

int ParseTelemetryParams(const char *params, uint32_t *paramsArr, int maxCount)
{
    if (params == NULL || paramsArr == NULL || maxCount <= 0) {
        return -1;
    }
    size_t paramsStrLen = strlen(params);
    char *paramsStrCopy = (char *)malloc(paramsStrLen + 1);
    if (paramsStrCopy == NULL) {
        return -1;
    }
    if (strncpy_s(paramsStrCopy, paramsStrLen + 1, params, paramsStrLen) != 0) {
        goto abnormal;
    }
    char *saveptr;
    char *token = strtok_r(paramsStrCopy, " ", &saveptr);
    int count = 0;
    while (token != NULL && count < maxCount) {
        if (KNET_TransStrToNum(token, &paramsArr[count]) != 0) {
            goto abnormal;
        }
        count++;
        token = strtok_r(NULL, " ", &saveptr);
    }
    if (token != NULL) {
        goto abnormal;
    }
    free(paramsStrCopy);
    return count;
abnormal:
    free(paramsStrCopy);
    return -1;
}
KNET_STATIC int ParseFlowTableParams(const char *params, uint32_t *startIndex, uint32_t *count)
{
    uint32_t paramsArr[FLOW_TABLE_PARAMS_MAX] = {0};
    if (ParseTelemetryParams(params, paramsArr, FLOW_TABLE_PARAMS_MAX) != FLOW_TABLE_PARAMS_MAX) {
        KNET_ERR("Flow table query, rte telemetry invalid input failed, except "
                 "format <startIndex> <count>");
        return KNET_ERROR;
    }
    *startIndex = paramsArr[FLOW_TABLE_START_INDEX];
    *count = paramsArr[FLOW_TABLE_COUNT];
    if (*count > FLOW_TABLE_MAX_NUM) {
        KNET_ERR("Flow table query, rte telemetry invalid input failed, count must be less than %u",
                 FLOW_TABLE_MAX_NUM);
        return KNET_ERROR;
    }
    return KNET_OK;
}

int KnetTelemetryFlowTableCallback(const char *cmd, const char *params, struct rte_tel_data *data)
{
    if (data == NULL || params == NULL) {
        KNET_ERR("Rte telemetry data is null or params is null");
        return KNET_ERROR;
    }

    uint32_t startIndex;
    uint32_t count;
    if (ParseFlowTableParams(params, &startIndex, &count) != KNET_OK) {
        return KNET_ERROR; // 内部打印日志
    }
    if (rte_tel_data_start_dict(data) != 0) {
        KNET_ERR("Rte telemetry data start flow table dict failed");
        return KNET_ERROR;
    }
    if (KNET_ProcessFlowTable(startIndex, count, data) != KNET_OK) {
        return KNET_ERROR;
    }
    return KNET_OK;
}

KNET_STATIC int ComposedKeybufAndAddDict(struct rte_tel_data *data, uint32_t pid, uint32_t tid, uint32_t lcoreId,
                                         uint32_t queId)
{
    struct rte_tel_data *queueDict = rte_tel_data_alloc();
    if (queueDict == NULL) {
        KNET_ERR("K-NET telemetry queue callback failed, queueDict alloc failed");
        return KNET_ERROR;
    }
    if (rte_tel_data_start_dict(queueDict) != 0) {
        KNET_ERR("K-NET telemetry queue callback failed, queueDict start dict failed");
        rte_tel_data_free(queueDict);
        return KNET_ERROR;
    }
    CHECK_ADD_VALUE_TO_DICT(ADD_DICT_INT_FUNC, queueDict, "pid", pid);
    CHECK_ADD_VALUE_TO_DICT(ADD_DICT_INT_FUNC, queueDict, "tid", tid);
    CHECK_ADD_VALUE_TO_DICT(ADD_DICT_INT_FUNC, queueDict, "lcoreId", lcoreId);
    char keyName[MAX_JSON_KEY_NAME_LEN] = "queue";
    (void)snprintf_s(keyName + strlen(keyName), MAX_JSON_KEY_NAME_LEN - strlen(keyName),
                     MAX_JSON_KEY_NAME_LEN - strlen(keyName) - 1, "%u", queId);
    if (CheckAddContainerToDict(data, keyName, queueDict) != 0) {
        return KNET_ERROR; // 失败在函数内释放内存
    }
    return KNET_OK;
}
int AddPidTid(struct rte_tel_data *data, uint32_t queId)
{
    if (queId >= MAX_QUEUE_NUM) {
        KNET_ERR("QueId exceed MAX_QUEUE_NUM");
        return KNET_ERROR;
    }
    KNET_QueIdMapPidTid_t *queMapPidTid = KNET_GetQueIdMapPidTidLcoreInfo();
    if (queMapPidTid[queId].tid == INVALID_TID) {
        return KNET_OK;
    }
    if (ComposedKeybufAndAddDict(data, queMapPidTid[queId].pid, queMapPidTid[queId].tid, queMapPidTid[queId].lcoreId,
                                 queId) != KNET_OK) {
        KNET_ERR("ComposedKey buf and add dict container failed, pid %u, tid %u, lcore %u, queId %u",
                 queMapPidTid[queId].pid, queMapPidTid[queId].tid, queMapPidTid[queId].lcoreId, queId);
        return KNET_ERROR;
    }
    return KNET_OK;
}
int KnetTelemetryQueIdMapPidTidCallback(const char *cmd, const char *params, struct rte_tel_data *data)
{
    if (data == NULL || cmd == NULL || params != NULL) {
        KNET_ERR("Rte telemetry data is null or params is null");
        return KNET_ERROR;
    }
    if (rte_tel_data_start_dict(data) != 0) {
        KNET_ERR("Rte telemetry data start dict failed");
        return KNET_ERROR;
    }
    for (uint32_t queId = 0; queId < KNET_GetCfg(CONF_DPDK_QUEUE_NUM)->intValue; queId++) {
        if (AddPidTid(data, queId) != KNET_OK) {
            KNET_ERR("Rte telemetry add pid tid failed");
            return KNET_ERROR;
        }
    }
    return KNET_OK;
}
KNET_STATIC int AddMpPidTid(struct rte_tel_data *data, KNET_TelemetryInfo *telemetryInfo, uint32_t queId)
{
    if (queId >= MAX_QUEUE_NUM) {
        KNET_ERR("QueId exceed MAX_QUEUE_NUM");
        return KNET_ERROR;
    }
    if (ComposedKeybufAndAddDict(data, telemetryInfo->pid[queId], telemetryInfo->tid[queId],
                                 telemetryInfo->lcoreId[queId], queId) != KNET_OK) {
        KNET_ERR("Composekey Mp buffer and add dict container failed, pid %u, tid %u, lcore %u, queId %u",
                 telemetryInfo->pid[queId], telemetryInfo->tid[queId], telemetryInfo->lcoreId[queId], queId);
        return KNET_ERROR;
    }
    return KNET_OK;
}
int KnetTelemetryQueIdMapPidTidCallbackMp(const char *cmd, const char *params, struct rte_tel_data *data)
{
    if (data == NULL || cmd == NULL || params != NULL) {
        KNET_ERR("Rte telemetry data is null or params is null");
        return KNET_ERROR;
    }
    if (rte_tel_data_start_dict(data) != 0) {
        KNET_ERR("Rte telemetry data start dict failed");
        return KNET_ERROR;
    }
    const struct rte_memzone *mz = rte_memzone_lookup(KNET_TELEMETRY_MZ_NAME);
    if (mz == NULL || mz->addr == NULL) {
        KNET_ERR("Subprocess couldn't allocate memory for histack debug info");
        return KNET_ERROR;
    }
    KNET_TelemetryInfo *telemetryInfo = mz->addr;
    telemetryInfo->telemetryType = KNET_TELEMETRY_UPDATE_QUE_INFO;
    for (int queId = 0; queId < KNET_GetCfg(CONF_DPDK_QUEUE_NUM)->intValue; queId++) {
        if (!KNET_IsQueueIdUsed(queId)) {
            continue;
        }
        telemetryInfo->msgReady[queId] = 1;
        if (KnetHandleTimeout(telemetryInfo, queId) != KNET_OK) {
            return KNET_ERROR;
        }
        if (AddMpPidTid(data, telemetryInfo, queId) != KNET_OK) {
            KNET_ERR("Add pid and tid to telemetry failed");
            return KNET_ERROR;
        }
    }
    return KNET_OK;
}