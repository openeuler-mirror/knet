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

#include "securec.h"

#include "utils_statistic.h"
#include "utils_base.h"
#include "utils_cfg.h"
#include "utils_log.h"

#include "dp_debug_api.h"
#include "dp_show_api.h"
#include "dp_log_api.h"

#define LEN_PBUF 128
#define LEN_STAT 2048

/* LOG信息等级，默认记录ERROR等级以上信息 */
DP_LogLevel_E g_logLevel = DP_LOG_LEVEL_ERROR;

/* 日志输出全局结构体变量 */
DP_LogHook g_logOutput = NULL;

/* 维测信息show全局结构体变量 */
DP_DebugShowHook g_debugShow = NULL;

static const DP_MibDetail_t g_mibDetail[] = {
    {DP_STAT_TCP,      DP_TCP_STAT_MAX,      "Tcp Statistic"},
    {DP_STAT_TCP_CONN, DP_TCP_CONN_STAT_MAX, "Tcp connect Statistic"},
    {DP_STAT_PKT,      DP_PKT_STAT_MAX,      "Pakcet Statistic"},
    {DP_STAT_ABN,      DP_ABN_STAT_MAX,      "Abnormal Statistic"},
};

uint32_t DP_DebugShowHookReg(DP_DebugShowHook hook)
{
    if ((g_debugShow != NULL) || (hook == NULL)) {
        return 1;
    }

    g_debugShow = hook;
    return 0;
}

static uint32_t GetAllMibStat(DP_StatType_t type, uint32_t fieldMax, DP_MibStatistic_t *showStat)
{
    uint32_t fieldId;
    uint32_t ret;

    for (fieldId = 0; fieldId < fieldMax; fieldId++) {
        ret = GetFieldName(fieldId, type, showStat);
        if (ret != 0) {
            return 1;
        }
        if (type == DP_STAT_ABN) {
            showStat[fieldId].fieldValue += GetFieldValue(0, fieldId, type);
            continue;
        }
        for (int wid = 0; wid < CFG_GET_VAL(DP_CFG_WORKER_MAX); wid++) {
            showStat[fieldId].fieldValue += GetFieldValue(wid, fieldId, type);
        }
    }
    return 0;
}

static uint32_t GetWorkerMibStat(DP_StatType_t type, uint32_t fieldMax, DP_MibStatistic_t *showStat, int workerId)
{
    uint32_t fieldId;
    uint32_t ret;

    if (type == DP_STAT_ABN) {
        DP_LOG_WARN("The ABN statistic is not supported by workerId.");
        return 1;
    }

    for (fieldId = 0; fieldId < fieldMax; fieldId++) {
        ret = GetFieldName(fieldId, type, showStat);
        if (ret != 0) {
            return 1;
        }
        showStat[fieldId].fieldValue += GetFieldValue(workerId, fieldId, type);
    }
    return 0;
}

static void DebugShowMibStat(DP_MibStatistic_t *showStat, uint32_t fieldMax, uint32_t flag)
{
    uint32_t fieldId;
    uint32_t offset = 0;
    char output[LEN_STAT] = {0};

    offset = (uint32_t)snprintf_truncated_s(output, LEN_STAT, "{");
    for (fieldId = 0; fieldId < fieldMax - 1; fieldId++) {
        offset += (uint32_t)snprintf_truncated_s((output + offset), (LEN_STAT - offset),
            " \"%s\": \"%lu\",", showStat[fieldId].fieldName, showStat[fieldId].fieldValue);
    }
    offset += (uint32_t)snprintf_truncated_s((output + offset), (LEN_STAT - offset),
        " \"%s\": \"%lu\" }\n", showStat[fieldMax - 1].fieldName, showStat[fieldMax - 1].fieldValue);
    DEBUG_SHOW(flag, output, offset);
}

static void DP_ShowMibStat(DP_StatType_t type, int workerId, uint32_t flag)
{
    uint32_t ret = 0;
    DP_MibStatistic_t *showStat = NULL;
    uint32_t fieldMax = g_mibDetail[type].fieldNum;
    size_t showLen = sizeof(DP_MibStatistic_t) * fieldMax;

    showStat = (DP_MibStatistic_t *)MEM_MALLOC(showLen, MOD_DBG, DP_MEM_FREE);
    if (showStat == NULL) {
        DP_LOG_ERR("Malloc memory failed for mib statistic.");
        return;
    }
    (void)memset_s(showStat, showLen, 0, showLen);

    if (workerId == -1) {
        ret = GetAllMibStat(type, fieldMax, showStat);
    } else {
        ret = GetWorkerMibStat(type, fieldMax, showStat, workerId);
    }

    if (ret != 0) {
        DP_LOG_ERR("Get statistic failed with type [%d] workerid [%d].", (int)type, workerId);
        MEM_FREE(showStat, DP_MEM_FREE);
        return;
    }

    DebugShowMibStat(showStat, fieldMax, flag);
    MEM_FREE(showStat, DP_MEM_FREE);
    return;
}

static void DP_ShowMEMStat(DP_StatType_t type, int workerId, uint32_t flag)
{
    (void)type;
    uint32_t mod = 0;
    uint32_t offset = 0;
    uint64_t fixMem = 0;
    uint64_t freeMem = 0;
    char output[LEN_STAT] = {0};
    uint8_t modName[MOD_LEN] = {0};

    if (workerId != -1) {
        DP_LOG_WARN("Memory statistic is not supported by workerId.");
        return;
    }
    offset = (uint32_t)snprintf_truncated_s(output, LEN_STAT, "{ ");
    for (mod = 0; mod < MOD_MAX - 1; mod++) {
        fixMem += DP_MemCntGet(mod, DP_MEM_FIX);
        freeMem += DP_MemCntGet(mod, DP_MEM_FREE);
        if (GetMemModName(mod, modName) != 0) {
            return;
        }
        offset += (uint32_t)snprintf_truncated_s((output + offset), (LEN_STAT - offset),
            "\"%sInitMem\": \"%lu\", \"%sFreeMem\": \"%lu\", ", modName, fixMem, modName, freeMem);
        fixMem = 0;
        freeMem = 0;
    }
    fixMem += DP_MemCntGet(mod, DP_MEM_FIX);
    freeMem += DP_MemCntGet(mod, DP_MEM_FREE);
    if (GetMemModName(mod, modName) != 0) {
        return;
    }
    offset += (uint32_t)snprintf_truncated_s((output + offset), (LEN_STAT - offset),
        "\"%sInitMem\": \"%lu\", \"%sFreeMem\": \"%lu\" }\n", modName, fixMem, modName, freeMem);
    DEBUG_SHOW(flag, output, offset);
    return;
}

static void DP_ShowPBUFStat(DP_StatType_t type, int workerId, uint32_t flag)
{
    (void)type;
    uint32_t offset = 0;
    char output[LEN_STAT] = {0};
    DP_PbufStat_t pbufStat = {0};

    if (workerId == -1) {
        for (int wid = 0; wid < CFG_GET_VAL(DP_CFG_WORKER_MAX); wid++) {
            pbufStat.ipFragPktNum += DP_GET_PKT_STAT(wid, DP_PKT_FRAG_IN);
            pbufStat.tcpReassPktNum += DP_GET_PKT_STAT(wid, DP_PKT_TCP_REASS_PKT);
            pbufStat.sendBufPktNum += GetSendBufPktNum(wid);
            pbufStat.recvBufPktNum += GetRecvBufPktNum(wid);
        }
    } else {
        pbufStat.ipFragPktNum = DP_GET_PKT_STAT(workerId, DP_PKT_FRAG_IN);
        pbufStat.tcpReassPktNum = DP_GET_PKT_STAT(workerId, DP_PKT_TCP_REASS_PKT);
        pbufStat.sendBufPktNum = GetSendBufPktNum(workerId);
        pbufStat.recvBufPktNum = GetRecvBufPktNum(workerId);
    }

    offset = (uint32_t)snprintf_truncated_s(output, LEN_STAT, "{ \"ipFragPktNum\": \"%lu\",", pbufStat.ipFragPktNum);
    offset += (uint32_t)snprintf_truncated_s((output + offset), (LEN_STAT - offset),
        " \"tcpReassPktNum\": \"%lu\",", pbufStat.tcpReassPktNum);
    offset += (uint32_t)snprintf_truncated_s((output + offset), (LEN_STAT - offset),
        " \"sendBufPktNum\": \"%lu\",", pbufStat.sendBufPktNum);
    offset += (uint32_t)snprintf_truncated_s((output + offset), (LEN_STAT - offset),
        " \"recvBufPktNum\": \"%lu\" }\n", pbufStat.recvBufPktNum);
    DEBUG_SHOW(flag, output, offset);
    return;
}

static void DP_ShowErrStat(DP_StatType_t type, int workerId, uint32_t flag)
{
    (void)type;
    (void)workerId;
    (void)flag;
    DP_LOG_ERR("Type value as %d invalid, see DP_StatType_t.", type);
    return;
}

static const DP_TYPE_STATIS_S g_showStatistics[] = {
    {DP_STAT_TCP,      DP_ShowMibStat },
    {DP_STAT_TCP_CONN, DP_ShowMibStat },
    {DP_STAT_PKT,      DP_ShowMibStat },
    {DP_STAT_ABN,      DP_ShowMibStat },
    {DP_STAT_MEM,      DP_ShowMEMStat },
    {DP_STAT_PBUF,     DP_ShowPBUFStat },
    {DP_STAT_MAX,      DP_ShowErrStat },
};

void DP_ShowStatistics(DP_StatType_t type, int workerId, uint32_t flag)
{
    if (DEBUG_SHOW == NULL) {
        DP_LOG_WARN("ShowStatistics is unavailable without show hook registered.");
        return;
    }

    if (UTILE_IsStatInited() == 0) {
        DP_LOG_ERR("ShowStatistics is forbidden without dp stats inited.");
        return;
    }

    if ((workerId != -1) && ((workerId < 0) || (workerId >= CFG_GET_VAL(DP_CFG_WORKER_MAX)))) {
        DP_LOG_ERR("The workerId value [%d] is Invalid.", workerId);
        return;
    }

    for (uint32_t i = 0; i < DP_STAT_MAX; i++) {
        if (type == g_showStatistics[i].type) {
            g_showStatistics[i].showStat(type, workerId, flag);
            return;
        }
    }
    DP_ShowErrStat(type, workerId, flag);
    return;
}

uint32_t DP_LogHookReg(DP_LogHook fnHook)
{
    if ((g_logOutput != NULL) || (fnHook == NULL)) {
        return 1;
    }

    g_logOutput = fnHook;
    DP_LOG_INFO("Register hook(logOutput) success!");

    return 0;
}

void DP_LogLevelSet(DP_LogLevel_E logLevel)
{
    if (logLevel < DP_LOG_LEVEL_CRITICAL || logLevel > DP_LOG_LEVEL_DEBUG) {
        DP_LOG_ERR("Set loglevel value as [%d] invalid, see DP_LogLevel_E", logLevel);
        return;
    }
    g_logLevel = logLevel;
    DP_LOG_INFO("Set loglevel success!");
}

uint32_t DP_LogLevelGet(void)
{
    return g_logLevel;
}