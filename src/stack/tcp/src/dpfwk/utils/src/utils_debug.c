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
#include "utils_cb_cnt.h"

#include "dp_debug_api.h"
#include "dp_show_api.h"
#include "dp_log_api.h"

#define LEN_PBUF 128

/* LOG信息等级，默认记录ERROR等级以上信息 */
DP_LogLevel_E g_logLevel = DP_LOG_LEVEL_ERROR;

/* 日志输出全局结构体变量 */
DP_LogHook g_fnLogOut = NULL;

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
    if (UTILS_IsCfgInited() != 0) {
        DP_LOG_ERR("DebugShow reg failed, init already!");
        return 1;
    }
    if (g_debugShow != NULL) {
        DP_LOG_ERR("DebugShow reg failed, reg already!");
        return 1;
    }
    if (hook == NULL) {
        DP_LOG_ERR("DebugShow reg failed, invalid hook!");
        return 1;
    }

    g_debugShow = hook;
    return 0;
}

int DP_GetMibStat(DP_StatType_t type, uint32_t fieldId, DP_MibStatistic_t *showStat)
{
    if ((type >= sizeof(g_mibDetail) / sizeof(g_mibDetail[0])) || (fieldId >= g_mibDetail[type].fieldNum)) {
        DP_LOG_ERR("DP_GetMibStat failed by invalid type or fieldId, type = %u, fieldId = %u!", type, fieldId);
        return -1;
    }
    if (showStat == NULL) {
        DP_LOG_ERR("DP_GetMibStat failed by showStat NULL!");
        return -1;
    }
    showStat->fieldValue = 0;
    (void)memset_s(showStat->fieldName, sizeof(showStat->fieldName), 0, sizeof(showStat->fieldName));
    uint32_t ret = GetFieldName(fieldId, type, showStat);
    if (ret != 0) {
        return -1;
    }
    if (type == DP_STAT_ABN) {
        showStat->fieldValue += GetFieldValue(0, fieldId, type);
        return 0;
    }
    for (int wid = 0; wid < CFG_GET_VAL(DP_CFG_WORKER_MAX); wid++) {
        showStat->fieldValue += GetFieldValue(wid, fieldId, type);
    }
    return 0;
}

static uint32_t GetAllMibStat(DP_StatType_t type, uint32_t fieldMax, DP_MibStatistic_t *showStat)
{
    uint32_t fieldId;
    int ret;

    for (fieldId = 0; fieldId < fieldMax; fieldId++) {
        ret = DP_GetMibStat(type, fieldId, &showStat[fieldId]);
        if (ret != 0) {
            DP_LOG_ERR("GetAllMibStat failed, type = %u, fieldId = %u", type, fieldId);
            return 1;
        }
    }
    return 0;
}

static uint32_t GetWorkerMibStat(DP_StatType_t type, uint32_t fieldMax, DP_MibStatistic_t *showStat, int workerId)
{
    uint32_t fieldId;
    uint32_t ret;

    if (type == DP_STAT_ABN) {
        DP_LOG_ERR("GetWorkerMibStat failed, ABN statistic is not supported by workerId, please use workerId = -1.");
        return 1;
    }

    for (fieldId = 0; fieldId < fieldMax; fieldId++) {
        ret = GetFieldName(fieldId, type, &showStat[fieldId]);
        if (ret != 0) {
            return 1;
        }
        showStat[fieldId].fieldValue += GetFieldValue(workerId, fieldId, type);
    }
    return 0;
}

#define RESERVE_STAT_LEN 64         // 单组信息字符串不会超过64

static void DebugShowMibStat(DP_MibStatistic_t *showStat, uint32_t fieldMax, DP_StatType_t type, uint32_t flag)
{
    uint32_t fieldId;
    uint32_t offset = 0;
    char* output = MEM_MALLOC(LEN_STAT, MOD_DBG, DP_MEM_FREE);
    if (output == NULL) {
        DP_LOG_ERR("Malloc memory failed for show mib output.");
        return;
    }
    (void)memset_s(output, LEN_STAT, 0, LEN_STAT);
    offset = (uint32_t)snprintf_truncated_s(output, LEN_STAT, "{");
    for (fieldId = 0; fieldId < fieldMax; fieldId++) {
        if ((type == DP_STAT_TCP || type == DP_STAT_ABN) && showStat[fieldId].fieldValue == 0) {
            continue;           // 两类统计信息多为异常打点，值为0时跳过不显示
        }
        offset += (uint32_t)snprintf_truncated_s((output + offset), (LEN_STAT - offset),
            " \"%s\": \"%lu\",", showStat[fieldId].fieldName, showStat[fieldId].fieldValue);
        if ((LEN_STAT - offset) < RESERVE_STAT_LEN) {
            // 保持json格式，将信息过长传出；保留','，方便后续处理
            offset += (uint32_t)snprintf_truncated_s((output + offset), (LEN_STAT - offset),
                " \"infoTooLong\": \"1\",");
            break;
        }
    }
    if (offset != 1) {          // 非空时删除最后一个悬余的逗号
        offset--;
    }
    offset += (uint32_t)snprintf_truncated_s((output + offset), (LEN_STAT - offset), " }\n");
    DEBUG_SHOW(flag, output, offset);

    MEM_FREE(output, DP_MEM_FREE);
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

    DebugShowMibStat(showStat, fieldMax, type, flag);
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
    uint64_t zcopySend = 0;
    uint64_t zcopyRecv = 0;
    uint8_t modName[MOD_LEN] = {0};

    if (workerId != -1) {
        DP_LOG_ERR("Memory statistic is not supported by workerId, please use workerId = -1.");
        return;
    }

    char* output = MEM_MALLOC(LEN_STAT, MOD_DBG, DP_MEM_FREE);
    if (output == NULL) {
        DP_LOG_ERR("Malloc memory failed for show mem output.");
        return;
    }
    (void)memset_s(output, LEN_STAT, 0, LEN_STAT);
    // MEM相关信息长度不会超过LEN_STAT限制
    offset = (uint32_t)snprintf_truncated_s(output, LEN_STAT, "{ ");
    for (mod = 0; mod < MOD_MAX; mod++) {
        fixMem = DP_MemCntGet(mod, DP_MEM_FIX);
        freeMem = DP_MemCntGet(mod, DP_MEM_FREE);
        if (GetMemModName(mod, modName) != 0) {
            MEM_FREE(output, DP_MEM_FREE);
            return;
        }
        offset += (uint32_t)snprintf_truncated_s((output + offset), (LEN_STAT - offset),
            "\"%sInitMem\": \"%lu\", \"%sFreeMem\": \"%lu\", ", modName, fixMem, modName, freeMem);
    }
    if (CFG_GET_VAL(DP_CFG_ZERO_COPY) != 0) {
        for (uint32_t wid = 0; wid < (uint32_t)CFG_GET_VAL(DP_CFG_WORKER_MAX); wid++) {
            zcopySend = DP_MemCntGet(wid, DP_MEM_ZCOPY_SEND);       // 对于零拷贝内存，mod字段按wid区分
            zcopyRecv = DP_MemCntGet(wid, DP_MEM_ZCOPY_RECV);       // 对于零拷贝内存，mod字段按wid区分
            offset += (uint32_t)snprintf_truncated_s((output + offset), (LEN_STAT - offset),
                "\"worker%dZcopySendMem\": \"%lu\", \"worker%dZcopyRecvMem\": \"%lu\", ",
                wid, zcopySend, wid, zcopyRecv);
        }
    }

    offset -= 2;    // 2: 去掉末尾的两个字符 ", "
    offset += (uint32_t)snprintf_truncated_s((output + offset), (LEN_STAT - offset), " }\n");
    DEBUG_SHOW(flag, output, offset);

    MEM_FREE(output, DP_MEM_FREE);
}

static void DP_ShowPBUFStat(DP_StatType_t type, int workerId, uint32_t flag)
{
    (void)type;
    uint32_t offset = 0;
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

    char* output = MEM_MALLOC(LEN_STAT, MOD_DBG, DP_MEM_FREE);
    if (output == NULL) {
        DP_LOG_ERR("Malloc memory failed for show pbuf output.");
        return;
    }
    (void)memset_s(output, LEN_STAT, 0, LEN_STAT);
    // PBUF相关信息长度不会超过LEN_STAT限制
    offset = (uint32_t)snprintf_truncated_s(output, LEN_STAT, "{ \"ipFragPktNum\": \"%lu\",", pbufStat.ipFragPktNum);
    offset += (uint32_t)snprintf_truncated_s((output + offset), (LEN_STAT - offset),
        " \"tcpReassPktNum\": \"%lu\",", pbufStat.tcpReassPktNum);
    offset += (uint32_t)snprintf_truncated_s((output + offset), (LEN_STAT - offset),
        " \"sendBufPktNum\": \"%lu\",", pbufStat.sendBufPktNum);
    offset += (uint32_t)snprintf_truncated_s((output + offset), (LEN_STAT - offset),
        " \"recvBufPktNum\": \"%lu\" }\n", pbufStat.recvBufPktNum);
    DEBUG_SHOW(flag, output, offset);

    MEM_FREE(output, DP_MEM_FREE);
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
        DP_LOG_ERR("ShowStatistics is unavailable without show hook registered.");
        return;
    }

    if (UTILS_IsStatInited() == 0) {
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
    if (UTILS_IsCfgInited() != 0) {
        DP_LOG_ERR("log reg failed, init already!");
        return 1;
    }
    if (g_fnLogOut != NULL) {
        DP_LOG_ERR("log reg failed, reg already!");
        return 1;
    }
    if (fnHook == NULL) {
        // 未注册，无法打印日志
        return 1;
    }

    g_fnLogOut = fnHook;
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

int DP_SocketCountGet(int type)
{
    switch (type) {
        case DP_SOCKET_TYPE_TCP:
            return (int)ATOMIC32_Load(&g_tcpCbCnt);
        case DP_SOCKET_TYPE_UDP:
            return (int)ATOMIC32_Load(&g_udpCbCnt);
        case DP_SOCKET_TYPE_EPOLL:
            return (int)ATOMIC32_Load(&g_epollCbCnt);
        default:
            return -1;
    }
}
