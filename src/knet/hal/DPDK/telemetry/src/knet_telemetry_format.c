/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: telemetry 持久化文件格式化输出相关操作
 */

#include <dirent.h>
#include <limits.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "cJSON.h"
#include "rte_ethdev.h"
#include "rte_memzone.h"

#include "knet_config.h"
#include "knet_lock.h"
#include "knet_log.h"
#include "knet_rpc.h"
#include "knet_types.h"
#include "knet_telemetry.h"
#include "knet_telemetry_debug.h"
#include "knet_telemetry_format.h"

#define TRY_GET_DATA_TIME 3 // 超时等待次数
#define DECIMAL_NUM 10  // 十进制
#define DPDK_MAX_XSTATE 1024 // xstate key最大长度，目前最多是32队列下708个
#define PERSIST_MULTI_PROCESS_WAIT_TIME 100000

static const char *g_dpStatsKeysTcp[] = {
    "Accepts",                  "Closed",                   "ConnAttempt",              "ConnDrops",
    "Connects",                 "DelayedAck",               "Drops",                    "KeepDrops",
    "KeepProbe",                "KeepTMO",                  "PersistDrops",             "PersistTMO",
    "RcvAckBytes",              "RcvAckPkts",               "RcvAckTooMuch",            "RcvDupAck",
    "RcvBadOff",                "RcvBadSum",                "RcvLocalAddr",             "RcvNoTcpHash",
    "RcvErrAckFlags",           "RcvNotSynInListen",        "RcvInvalidWidInPassive",   "RcvDupSyn",
    "RcvNonAckInSynRcv",        "RcvInvalidAck",            "RcvErrSeq",                "RcvErrOpt",
    "RcvDataSyn",               "RcvBytes",                 "RcvBytesPassive",          "RcvBytesActive",
    "RcvDupBytes",              "RcvDupPkts",               "RcvAfterWndPkts",          "RcvAfterWndBytes",
    "RcvPartDupBytes",          "RcvPartDupPkts",           "RcvOutOrderPkts",          "RcvOutOrderBytes",
    "RcvShort",                 "RcvTotal",                 "RcvPkts",                  "RcvWndProbe",
    "RcvWndUpdate",             "RcvRST",                   "RcvInvalidRST",            "RcvSynEstab",
    "RcvFIN",                   "RcvRxmtFIN",               "RexmtTMO",                 "RTTUpdated",
    "SegsTimed",                "SndBytes",                 "SndBytesPassive",          "SndBytesActive",
    "SndCtl",                   "SndPkts",                  "SndProbe",                 "SndRexmtBytes",
    "SndAcks",                  "SndRexmtPkts",             "SndTotal",                 "SndWndUpdate",
    "TMODrop",                  "RcvExdWndRst",             "DropCtlPkts",              "DropDataPkts",
    "SndRST",                   "SndFIN",                   "FinWait2Drops",            "RespChallAcks",
    "OnceDrivePassiveTsqCnt",   "AgeDrops",                 "BbrSampleCnt",             "BbrSlowBWCnt",
    "FrtoSpurios",              "FrtoReal",                 "GenCtrlPktDrop"
};

static const char *g_dpStatsKeysConn[] = {
    "Listen",            "SynSent",           "SynRcvd",           "PAEstablished",     "ACEstablished",
    "PACloseWait",       "ACCloseWait",       "PAFinWait1",        "ACFinWait1",        "PAClosing",
    "ACClosing",         "PALastAck",         "ACLastAck",         "PAFinWait2",
    "ACFinWait2",        "PATimeWait",        "ACTimeWait",        "Abort"
};

static const char *g_dpStatsKeysPkts[] = {
    "LinkInPkts",                "EthInPkts",                 "NetInPkts",                 "IcmpOutPkts",
    "ArpDeliverPkts",            "IpBroadcastDeliverPkts",    "NonFragDelverPkts",         "UptoCtrlPlanePkts",
    "ReassInFragPkts",           "ReassOutReassPkts",         "NetOutPkts",                "EthOutPkts",
    "FragInPkts",                "FragOutPkts",               "ArpMissResvPkts",           "ArpSearchInPkts",
    "ArpHaveNormalPkts",         "RcvIcmpPkts",               "NetBadVersionPkts",         "NetBadHdrLenPkts",
    "NetBadLenPkts",             "NetTooShortPkts",           "NetBadChecksumPkts",        "NetNoProtoPkts",
    "NetNoRoutePkts",            "TcpReassPkts",              "UdpInPkts",                 "UdpOutPkts",
    "TcpInPkts",                 "SndBufInPkts",              "SndBufOutPkts",             "SndBufFreePkts",
    "RcvBufInPkts",              "RcvBufOutPkts",             "RcvBufFreePkts",            "Ip6InPkts",
    "Ip6TooShortPkts",           "Ip6BadVerPkts",             "Ip6BadHeadLenPkts",         "Ip6BadLenPkts",
    "Ip6MutiCastDeliverPkts",    "Ip6ExtHdrCntErrPkts",       "Ip6ExtHdrOverflowPkts",     "Ip6HbhHdrErrPkts",
    "Ip6NoUpperProtoPkts",       "Ip6ReassInFragPkts",        "Ip6FragHdrErrPkts",         "Ip6OutPkts",
    "Ip6FragOutPkts",            "KernelFdirCacheMiss"
};

static const char *g_dpStatsKeysAbn[] = {
    "AbnBase",               "TimerCycle",            "TimerNodeExist",        "TimerExpiredInval",
    "TimerActiveExcept",     "ConnByListenSk",        "RepeatConn",            "RefusedConn",
    "ConnInProg",            "AcceptNoChild",         "SetOptInval",           "KpIdInval",
    "KpInInval",             "KpCnInval",             "MaxsegInval",           "MaxsegDisStat",
    "DeferAcDisStat",        "CongetsionAlgInval",    "SetOptNotSup",          "TcpInfoInval",
    "GetOptInval",           "GetOptNotSup",          "SndConnRefused",        "SndCantSend",
    "SndConnClosed",         "SndNoSpace",            "SndbufNoMem",           "RcvConnRefused",
    "RcvConnClosed",         "WorkerMissMatch",       "PortIntervalPutErr",    "PortIntervalCntErr"
};

static const char *g_dpStatsKeysMem[] = {
    "InitInitMem",           "InitFreeMem",           "CpdInitMem",            "CpdFreeMem",
    "DebugInitMem",          "DebugFreeMem",          "NetdevInitMem",         "NetdevFreeMem",
    "NamespaceInitMem",      "NamespaceFreeMem",      "PbufInitMem",           "PbufFreeMem",
    "PmgrInitMem",           "PmgrFreeMem",           "ShmInitMem",            "ShmFreeMem",
    "TbmInitMem",            "TbmFreeMem",            "UtilsInitMem",          "UtilsFreeMem",
    "WorkerInitMem",         "WorkerFreeMem",         "FdInitMem",             "FdFreeMem",
    "EpollInitMem",          "EpollFreeMem",          "PollInitMem",           "PollFreeMem",
    "SelectInitMem",         "SelectFreeMem",         "SocketInitMem",         "SocketFreeMem",
    "NetlinkInitMem",        "NetlinkFreeMem",        "EthInitMem",            "EthFreeMem",
    "IpInitMem",             "IpFreeMem",             "Ip6InitMem",            "Ip6FreeMem",
    "TcpInitMem",            "TcpFreeMem",            "UdpInitMem",            "UdpFreeMem"
};

static const char *g_dpStatsKeysPbuf[] = {
    "ipFragPktNum",      "tcpReassPktNum",    "sendBufPktNum",     "recvBufPktNum"
};

typedef struct {
    DP_StatType_t type;
    const char **keys;
    const size_t keysCount;
    cJSON *json;
} DP_STATS_JSON;

static DP_STATS_JSON g_dpStatsJson[DP_STAT_MAX] = {
    {DP_STAT_TCP, g_dpStatsKeysTcp, sizeof(g_dpStatsKeysTcp) / sizeof(g_dpStatsKeysTcp[0]), NULL},
    {DP_STAT_TCP_CONN, g_dpStatsKeysConn, sizeof(g_dpStatsKeysConn) / sizeof(g_dpStatsKeysConn[0]), NULL},
    {DP_STAT_PKT, g_dpStatsKeysPkts, sizeof(g_dpStatsKeysPkts) / sizeof(g_dpStatsKeysPkts[0]), NULL},
    {DP_STAT_ABN, g_dpStatsKeysAbn, sizeof(g_dpStatsKeysAbn) / sizeof(g_dpStatsKeysAbn[0]), NULL},
    {DP_STAT_MEM, g_dpStatsKeysMem, sizeof(g_dpStatsKeysMem) / sizeof(g_dpStatsKeysMem[0]), NULL},
    {DP_STAT_PBUF, g_dpStatsKeysPbuf, sizeof(g_dpStatsKeysPbuf) / sizeof(g_dpStatsKeysPbuf[0]), NULL}
};

typedef struct {
    DP_StatType_t type;
    const char *formatHead;
    const char *formatTail;
} dpStatFormat;

typedef struct {
    const char *formatHead;
    const char *formatTail;
} xStatesFormat;

static const dpStatFormat DP_STATES_FORMATING[DP_STAT_MAX] = {
    {DP_STAT_TCP, "\"/knet/stack/tcp_stat\": {", "},\n"},
    {DP_STAT_TCP_CONN, "\"/knet/stack/conn_stat\": {", "},\n"},
    {DP_STAT_PKT, "\"/knet/stack/pkt_stat\": {", "},\n"},
    {DP_STAT_ABN, "\"/knet/stack/abn_stat\": {", "},\n"},
    {DP_STAT_MEM, "\"/knet/stack/mem_stat\": {", "},\n"},
    {DP_STAT_PBUF, "\"/knet/stack/pbuf_stat\": {", "},\n"}
};

static const xStatesFormat DPDK_XSTATES_FORMATING = {"\"/ethdev/xstats/port%d\": {", "},\n"};

typedef struct {
    DP_StatType_t type;
    int outputLen; // 输出长度
    int formatLen; // 格式化输出长度
} DpStateTypeLong;

char g_knetTeleToFileDpOutput[MAX_OUTPUT_LEN] = {0};
typedef cJSON *(*GetDpStateByTypeFunc)(DP_StatType_t type);
GetDpStateByTypeFunc  g_getDpStateByTypeFunc = NULL; // 根据单进程或者多进程获取dp统计数据
KNET_DpShowStatisticsHook g_dpShowStatisticsHookPersist = NULL;

int KNET_DpShowStatisticsHookRegPersist(KNET_DpShowStatisticsHook hook)
{
    if (hook == NULL) {
        return KNET_ERROR;
    }
    g_dpShowStatisticsHookPersist = hook;
    return KNET_OK;
}

int TelemetryPersistMzInit(void)
{
    const struct rte_memzone *memZone = NULL;
    memZone = rte_memzone_reserve(KNET_TELEMETRY_PERSIST_MZ_NAME, sizeof(KNET_TelemetryPersistInfo), SOCKET_ID_ANY, 0);
    if (memZone == NULL) {
        KNET_ERR("Allocate memory for telemetry persist mz failed");
        return KNET_ERROR;
    }

    KNET_TelemetryPersistInfo *telemetryInfo = memZone->addr;
    (void)memset_s(telemetryInfo, sizeof(KNET_TelemetryPersistInfo), 0, sizeof(KNET_TelemetryPersistInfo));

    return KNET_OK;
}

int KNET_DebugOutputToFile(const char *output, uint32_t len)
{
    if (len > MAX_OUTPUT_LEN - 1) {
        KNET_ERR("Output too long, len %u", len);
        return KNET_ERROR;
    }
    if (KNET_GetCfg(CONF_COMMON_MODE)->intValue == KNET_RUN_MODE_SINGLE) {
        int ret = snprintf_s(g_knetTeleToFileDpOutput, MAX_OUTPUT_LEN, len, "%s", output);
        if (ret < 0) {
            KNET_ERR("K-NET write telemetry to file buffer failed with errno %d", errno);
            return KNET_ERROR;
        }
        return KNET_OK;
    }
    const struct rte_memzone *memZone = rte_memzone_lookup(KNET_TELEMETRY_PERSIST_MZ_NAME);
    if (memZone == NULL || memZone->addr == NULL) {
        KNET_ERR("Subprocess couldn't allocate memory for telemetry persist mz");
        return KNET_ERROR;
    }
    KNET_TelemetryPersistInfo *telemetryInfo = memZone->addr;
    DP_StatType_t type = telemetryInfo->msgType;
    if (type >= DP_STAT_MAX || type < 0) {
        KNET_ERR("K-NET telemetry persist msg type %d is invalid", type);
        telemetryInfo->state = KNET_TELE_PERSIST_ERROR;
        return KNET_ERROR;
    }
    if (strncpy_s(telemetryInfo->message[type], MAX_OUTPUT_LEN, output, len) != 0) {
        KNET_ERR("K-NET telemetry persist strncpy_s failed");
        telemetryInfo->state = KNET_TELE_PERSIST_ERROR;
        return KNET_ERROR;
    }
    return KNET_OK;
}

void TelemetryPersistUninitDpJson(void)
{
    for (int i = 0; i < DP_STAT_MAX; i++) {
        cJSON_Delete(g_dpStatsJson[i].json);
        g_dpStatsJson[i].json = NULL;
    }
    return;
}

/**
 * @brief 初始化DP(数据平面)统计信息的JSON持久化结构
 *
 * 为所有DP统计类型创建JSON对象，并为每个统计项添加初始值为0的数值字段。
 * 如果初始化过程中发生错误，会自动调用清理函数并返回错误。
 *
 * @return int 返回操作结果
 * @retval KNET_OK 初始化成功
 * @retval KNET_ERROR 初始化失败
 *
 * @note 失败时会自动调用TelemetryPersistUninitDpJson()进行清理
 */
int TelemetryPersistInitDpJson(void)
{
    size_t keysCount = 0;
    cJSON *root = NULL;
    cJSON *ret = NULL;
    for (int i = 0; i < DP_STAT_MAX; i++) {
        keysCount = g_dpStatsJson[i].keysCount;
        root = cJSON_CreateObject();
        if (root == NULL) {
            KNET_ERR("K-NET telemetry persist cJSON_CreateObject failed");
            goto ERR;
        }
        g_dpStatsJson[i].json = root;
        for (size_t j = 0; j < keysCount; j++) {
            ret = cJSON_AddNumberToObject(root, g_dpStatsJson[i].keys[j], 0);
            if (ret == NULL) {
                KNET_ERR("K-NET telemetry persist add data to json failed");
                goto ERR;
            }
        }
    }
    return 0;
ERR:
    TelemetryPersistUninitDpJson();
    return -1;
}

/**
 * @brief 使用自定义格式字符串格式化输出到缓冲区
 *
 * @param output 输出缓冲区指针
 * @param outputLeftLen 输入时为缓冲区剩余长度，输出时为格式化后剩余长度
 * @param fmt 格式化字符串
 * @param ... 可变参数列表
 *
 * @return int 成功返回写入的字节数，失败返回KNET_ERROR
 *
 * @note 该函数会更新outputLeftLen为格式化后剩余的缓冲区长度
 * @warning 缓冲区长度不足时会返回错误
 */
int FormatingInCustom(char *output, int *outputLeftLen, const char *fmt, ...)
{
    int offset = 0;
    int leftLen = *outputLeftLen;
    va_list args;
    va_start(args, fmt);
    offset = vsprintf_s(output, leftLen, fmt, args);
    va_end(args);

    if (offset < 0) {
        KNET_ERR("K-NET telemetry statistic persist format custom failed with errno %d", errno);
        return -1;
    }

    leftLen = leftLen - offset;
    if (leftLen <= 0) {
        KNET_ERR("K-NET telemetry statistic persist format custom failed. No enough space to write data");
        return -1;
    }
    *outputLeftLen = leftLen;
    return offset;
}

int FormatingSingleDpStats(char *output, int *outputLeftLen, cJSON *json)
{
    cJSON *item = NULL;
    int offset = 0;
    int ret = 0;
    int leftLen = *outputLeftLen;
    uint64_t value;
    /* 构造数据体 */
    cJSON_ArrayForEach(item, json) {
        if (item == NULL || item->string == NULL) {
            continue;
        }
        if (cJSON_IsNumber(item)) {
            ret = sprintf_s(output + offset, leftLen, "\"%s\": %*d,", item->string, FORMAT_INT_WIDTH, item->valueint);
            offset = FORMAT_SUCCESS(ret, offset);
        } else if (cJSON_IsString(item)) {
            errno = 0;
            value = strtoull(item->valuestring, NULL, DECIMAL_NUM);
            if (errno == ERANGE) {
                KNET_ERR("K-NET telemetry add json to data failed, value is out of range");
                return -1;
            }
            ret = sprintf_s(output + offset, leftLen, "\"%s\": %*llu,", item->string, FORMAT_INT_WIDTH, value);
            offset = FORMAT_SUCCESS(ret, offset);
        } else {
            KNET_ERR("K-NET telemetry stats persist add dp json data failed, type is not supported");
            continue;
        }
        if (offset < 0) {
            KNET_ERR("K-NET telemetry stats persist format dp stats failed with leftLen %d, errno %d", leftLen, errno);
            return offset;
        }
        leftLen = leftLen - ret; // 这里的leftLen是output剩余的长度
        if (leftLen <= 0) {
            KNET_ERR("K-NET telemetry stats persist format dp stats failed. No enough space to write data");
            return offset;
        }
    }
    *outputLeftLen = leftLen;
    return offset;
}

/**
 * @brief 获取并合并DP统计信息的JSON数据
 *
 * 该函数从默认JSON模板和动态获取的统计JSON中合并数据，返回最终的统计JSON对象。
 *
 * @param[in] output 动态获取的统计JSON字符串
 * @param[in] type 统计类型枚举值(DP_StatType_t)
 *
 * @return cJSON* 合并后的JSON对象指针
 * @retval NULL 输入参数无效或默认JSON模板不存在
 * @retval cJSON* 成功合并后的JSON对象(需调用者释放)
 *
 * @note 1. 当output解析失败时返回默认JSON模板
 *       2. 函数内部会复制默认模板和动态JSON中的值，调用者需负责释放返回的JSON对象
 *       3. 仅替换默认模板中存在于output JSON的字段，其他字段保持不变
 */
cJSON *GetDpStatsJson(char *output, DP_StatType_t type)
{
    if (type >= DP_STAT_MAX || type < 0) {
        KNET_ERR("K-NET telemetry persist get dp state json by type %d failed.", type);
        return NULL;
    }
    /* 复制一份默认的json数据 */
    cJSON *defaultJson = cJSON_Duplicate(g_dpStatsJson[type].json, 1);
    if (defaultJson == NULL) {
        KNET_ERR("K-NET telemetry persist get dp state by type %d failed. Default json is null", type);
        return NULL;
    }
    /* 解析统计数据 */
    cJSON *json = cJSON_Parse(output);
    if (json == NULL) {
        /* 没有则返回默认的json */
        return defaultJson;
    }
    /* 遍历default中的key，然后根据key去查找dp返回回来的json中是否有对应的值，有则替换 */
    cJSON *temp = NULL;
    cJSON *next = NULL;
    cJSON *item = defaultJson->child;
    while (item != NULL) {
        next = item->next;
        if (item->string == NULL) {
            item = next;
            continue;
        }
        temp = cJSON_GetObjectItemCaseSensitive(json, item->string);
        if (temp != NULL) {
            cJSON_ReplaceItemInObject(defaultJson, item->string, cJSON_Duplicate(temp, 1));
        }
        item = next; // 由于cJSON_ReplaceItemInObject会释放掉item.因此需要提前保存下一个节点，然后手动偏移
    }
    cJSON_Delete(json);
    return defaultJson;
}

cJSON *GetDpStateByTypeSingle(DP_StatType_t type)
{
    if (type >= DP_STAT_MAX || type < 0) {
        KNET_ERR("K-NET telemetry persist get dp state json by type %d failed.", type);
        return NULL;
    }
    (void)memset_s(g_knetTeleToFileDpOutput, MAX_OUTPUT_LEN, 0, MAX_OUTPUT_LEN);
    
    if (g_dpShowStatisticsHookPersist == NULL) {
        KNET_ERR("K-NET telemetry persist get dp state json by type %d failed. Show stats hook is null");
        return NULL;
    }
    g_dpShowStatisticsHookPersist(type, -1, KNET_STAT_OUTPUT_TO_FILE);
    return GetDpStatsJson(g_knetTeleToFileDpOutput, type);
}

cJSON *GetDpStateByTypeMulti(DP_StatType_t type)
{
    if (type >= DP_STAT_MAX || type < 0) {
        KNET_ERR("K-NET telemetry persist get dp state json by type %d failed.", type);
        return NULL;
    }

    const struct rte_memzone *memZone = rte_memzone_lookup(KNET_TELEMETRY_PERSIST_MZ_NAME);
    if (memZone == NULL || memZone->addr == NULL) {
        KNET_ERR("Subprocess couldn't allocate memory for telemetry persist mz");
        return NULL;
    }
    KNET_TelemetryPersistInfo *telemetryInfo = memZone->addr;
    int try = TRY_GET_DATA_TIME;
    bool msgReady = false;
    while (try--) {
        if (telemetryInfo->state == KNET_TELE_PERSIST_ERROR) {
            KNET_ERR("K-NET telemetry persist get dp state json by type failed, pid %d, state %d, type %d",
                     telemetryInfo->curPid, telemetryInfo->state, telemetryInfo->msgType);
            return NULL;
        }
        if (telemetryInfo->state == KNET_TELE_PERSIST_MSGREADY) {
            msgReady = true;
            break;
        }
        usleep(PERSIST_MULTI_PROCESS_WAIT_TIME); // 100ms
    }
    if (!msgReady) {
        KNET_ERR("K-NET telemetry persist get dp state json by type failed, pid %d, state %d,type %d",
                 telemetryInfo->curPid, telemetryInfo->state, telemetryInfo->msgType);
        return NULL;
    }
    /* 解析统计数据 */
    return GetDpStatsJson(telemetryInfo->message[type], type);
}

int FormatEveryDpStats(char *output, int *outputLeftLen)
{
    size_t dpStatTypeLen = sizeof(DP_STATES_FORMATING) / sizeof(DP_STATES_FORMATING[0]);
    int offset = 0;
    int leftLen = *outputLeftLen;
    
    /* 构造每一块数据 */
    for (size_t i = 0; i < dpStatTypeLen; i++) {
        if (*outputLeftLen <= 0) {
            KNET_ERR("K-NET telemetry stat persist format failed. No enough space to write data");
            offset = -1;
            goto END;
        }

        /* 构造数据头 */
        FORMAT_FUNC_SUCCESS(FormatingInCustom(output + offset, &leftLen, DP_STATES_FORMATING[i].formatHead), offset);
        if (offset < 0) {
            KNET_ERR("K-NET telemetry stat persist parse dp stats head failed with type %d",
                     DP_STATES_FORMATING[i].type);
            goto END;
        }

        cJSON *json = NULL;
        /* 单进程和多进程获取方式有区别 */
        if (g_getDpStateByTypeFunc != NULL) {
            json = g_getDpStateByTypeFunc(DP_STATES_FORMATING[i].type);
        }
        if (json == NULL) {
            KNET_ERR("K-NET telemetry stat persist get dp stats json failed with type %d", DP_STATES_FORMATING[i].type);
            offset = -1;
            goto END;
        }

        /* 格式化统计数据 */
        FORMAT_FUNC_SUCCESS(FormatingSingleDpStats(output + offset, &leftLen, json), offset);
        cJSON_Delete(json);
        if (offset < 0) {
            KNET_ERR("K-NET telemetry stat persist format dp stats failed with type %d", DP_STATES_FORMATING[i].type);
            goto END;
        }

        /* 构造数据尾 */
        offset -= 1;
        leftLen += 1; // 回退一个字符
        FORMAT_FUNC_SUCCESS(FormatingInCustom(output + offset, &leftLen, DP_STATES_FORMATING[i].formatTail), offset);
        if (offset < 0) {
            KNET_ERR("K-NET telemetry stat persist format dp stats tail failed with type %d",
                     DP_STATES_FORMATING[i].type);
            goto END;
        }
    }
    *outputLeftLen = leftLen;
END:
    return offset;
}

int FormatingXstatsNameAndValue(char *output, int *outputLeftLen, const char *name, uint64_t value)
{
    int offset = 0;
    int leftLen = *outputLeftLen;
    offset = sprintf_s(output, leftLen, "\"%s\": %*llu,", name, FORMAT_INT_WIDTH, value);
    if (offset < 0) {
        KNET_ERR("K-NET telemetry stats persistence format xstats failed");
        return offset;
    }
    leftLen = leftLen - offset;

    if (leftLen <= 0) {
        KNET_ERR("K-NET telemetry stat persist format xstats failed. No enough space to write data");
        return KNET_ERROR;
    }
    *outputLeftLen = leftLen;
    return offset;
}

int FormatXstatsData(char *output, int *outputLeftLen, uint16_t portId, struct rte_eth_xstat *xStats,
                     struct rte_eth_xstat_name *xStatsName, int namesCount)
{
    int offset = 0;
    int leftLen = *outputLeftLen;
    /* 构造数据头 */
    FORMAT_FUNC_SUCCESS(FormatingInCustom(output + offset, &leftLen, DPDK_XSTATES_FORMATING.formatHead, portId),
                        offset);
    if (offset < 0) {
        KNET_ERR("K-NET telemetry persist format DPDK xstats head failed with port %d", portId);
        return KNET_ERROR;
    }
    /* 格式化键值对 */
    for (int i = 0; i < namesCount; i++) {
        FORMAT_FUNC_SUCCESS(FormatingXstatsNameAndValue(output + offset, &leftLen, xStatsName[i].name, xStats[i].value),
                            offset);
        if (offset < 0) {
            KNET_ERR("K-NET telemetry persist format xstats name and value failed");
            return KNET_ERROR;
        }
    }

    /* 构造数据尾 */
    offset -= 1;
    leftLen += 1; // 回退一个字符
    FORMAT_FUNC_SUCCESS(FormatingInCustom(output + offset, &leftLen, DPDK_XSTATES_FORMATING.formatTail), offset);
    if (offset < 0) {
        KNET_ERR("K-NET telemetry persist format DPDK xstats tail failed");
        return KNET_ERROR;
    }
    *outputLeftLen = leftLen;
    return offset;
}

int FormatXstatsDataByPortId(char *output, int *outputLeftLen, uint16_t portID)
{
    size_t xStatsLen = sizeof(struct rte_eth_xstat) * DPDK_MAX_XSTATE;
    struct rte_eth_xstat *xStats = malloc(xStatsLen);
    if (xStats == NULL) {
        KNET_ERR("K-NET persist malloc xstats failed with errno %d", errno);
        return -1;
    }
    size_t xStatsNamesLen = sizeof(struct rte_eth_xstat_name) * DPDK_MAX_XSTATE;
    struct rte_eth_xstat_name *xStatsNames = malloc(xStatsNamesLen);
    if (xStatsNames == NULL) {
        free(xStats);
        KNET_ERR("K-NET persist malloc xstats names failed with errno %d", errno);
        return KNET_ERROR;
    }
    (void)memset_s(xStats, xStatsLen, 0, xStatsLen);
    (void)memset_s(xStatsNames, xStatsNamesLen, 0, xStatsNamesLen);
    int offset = 0;
    /* 获取统计数据的key数组 */
    int namesCount = rte_eth_xstats_get_names(portID, xStatsNames, DPDK_MAX_XSTATE);
    if (namesCount <= 0 || namesCount >= DPDK_MAX_XSTATE) {
        KNET_ERR("K-NET persist get DPDK xstats names failed, namesCount %d", namesCount);
        offset = -1;
        goto END;
    }

    /* 获取统计数据的value数组 */
    int valueCount = rte_eth_xstats_get(portID, xStats, DPDK_MAX_XSTATE);
    if (valueCount <= 0 || valueCount > DPDK_MAX_XSTATE) {
        KNET_ERR("K-NET persist get DPDK xstats values failed, valueCount %d", valueCount);
        offset = -1;
        goto END;
    }
    /* 确保xstats的names和values是一一对应的 */
    if (valueCount != namesCount) {
        KNET_ERR("K-NET persist get DPDK xstats names and values count not equal, namesCount %d, valueCount %d",
                 namesCount, valueCount);
        offset = -1;
        goto END;
    }

    /* 构造xstats数据 */
    offset = FormatXstatsData(output, outputLeftLen, portID, xStats, xStatsNames, namesCount);
    if (offset < 0) {
        KNET_ERR("K-NET persist format DPDK xstats data failed");
    }
END:
    free(xStatsNames);
    free(xStats);
    return offset;
}

int NotifySubprocessRefreshDpState(pid_t pid)
{
    const struct rte_memzone *memZone = rte_memzone_lookup(KNET_TELEMETRY_PERSIST_MZ_NAME);
    if (memZone == NULL || memZone->addr == NULL) {
        KNET_ERR("K-NET telemetry persist notify subprocess failed, mz is null");
        return KNET_ERROR;
    }
    KNET_TelemetryPersistInfo *telemetryInfo = memZone->addr;
    (void)memset_s(telemetryInfo, sizeof(KNET_TelemetryPersistInfo), 0, sizeof(KNET_TelemetryPersistInfo));
    telemetryInfo->state = KNET_TELE_PERSIST_WAITSECOND;
    telemetryInfo->curPid = pid;
    return KNET_OK;
}

int TelemetryPersistInitGetDpStatFunc(int32_t runMode)
{
    if (runMode == KNET_RUN_MODE_SINGLE) {
        g_getDpStateByTypeFunc = GetDpStateByTypeSingle;
    } else if (runMode == KNET_RUN_MODE_MULTIPLE) {
        g_getDpStateByTypeFunc = GetDpStateByTypeMulti;
    } else {
        KNET_ERR("K-NET telemetry persist set dp stat func failed. Mode %d is invalid", runMode);
        return -1;
    }
    return 0;
}
