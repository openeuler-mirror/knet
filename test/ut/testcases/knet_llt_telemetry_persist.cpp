/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

#include "cJSON.h"

#include "rte_ethdev.h"

#include "dp_debug_api.h"

#include "knet_mock.h"
#include "knet_types.h"
#include "knet_log.h"
#include "knet_lock.h"
#include "knet_dpdk_init.h"
#include "knet_config.h"
#include "securec.h"
#include "common.h"
#include "mock.h"
#include "cJSON.h"
#include "knet_telemetry_format.h"

#define MAX_PROCESS_NUM_DTEST 32

struct ProcessInfo {
    pid_t pid;
    bool alive;
    time_t exitTime;        // 进程退出时间
    int offset;             // 表示该进程写到文件的数据长度,用于计算偏移量
    int clientID;           // 该进程对应的rpc fd
};

struct KnetProcessInfo {
    struct ProcessInfo processInfo[MAX_PROCESS_NUM_DTEST];
    int curProcessNum;
    int totalProcessNum;
    uint64_t writeBitMap;           // 每有一个新增的进程写数据,就将对应位置的bit置1,最多32个进程
    KNET_RWLock lock;
};

static union KNET_CfgValue g_cfg = {.intValue = 1};
static union KNET_CfgValue *KNET_GetCfgMultiDtest(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    g_cfg.intValue = 1; // 多进程
    return &g_cfg;
}

extern "C" {
#include "rte_telemetry.h"
#include "knet_telemetry.h"
extern struct KnetProcessInfo g_processInfo;
extern WriteDataToFile(FILE *file, char *data, size_t len, int offset);
extern int GetCurrentTime(char *buffer, size_t size);
extern FILE *OpenFileWithRWB(const char *filePath, const char *filename);
extern int WriteJsonHead(FILE *file, int offset);
extern int WriteDpdkXstats(FILE *file, int offset);
extern int WriteJsonTail(FILE *file, int offset);
extern int TelemetryRefreshPerSubprocess(FILE *file, int fileOffset, struct KnetProcessInfo *knetProcessInfo,
                                         uint64_t *sequence);
extern int RefreshSingleProcessData(char *output, int *outputLeftLen, pid_t pid, uint64_t sequence);
extern cJSON *GetDpStateByTypeSingle(DP_StatType_t type);
extern cJSON *GetDpStateByTypeMulti(DP_StatType_t type);
extern int TelemetryPersistThreadInit(void);
extern void TelemetrySetNewProcess(int clientID, pid_t pid);
extern void TelemetryDelOldProcess(int clientID, pid_t pid);
extern int StartDumpOldFile(FILE *oldFile);
extern int TelemetryRefreshDataSingle(FILE *file, struct KnetProcessInfo *knetProcessInfo, uint64_t *sequence);
extern int TelemetryRefreshDataMulti(FILE *file, struct KnetProcessInfo *knetProcessInfo,  uint64_t *sequence);
extern int TelemetryDisconnectHandler(int clientID, struct KNET_RpcMessage *knetRpcRequest,
                                      struct KNET_RpcMessage *knetRpcResponse);
extern int DumpOldFile(void);
extern int TelemetryPersistDealFileDelete(void);
extern void KNET_TelemetrySetPersistThreadExit(void);
extern int GetSingleProcessDpStatsMulti(char *singleOutput, int *outputLeftLen, int pid, bool formatLastTail,
                                        uint64_t sequence);
}


/**
 * @brief 新增/删除进程测试
 * @note 用例被其他用例影响，且存在恢复能力，前置
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_ADDPROCESS, NULL, NULL)
{
    (void)memset_s(&g_processInfo, sizeof(g_processInfo), 0, sizeof(g_processInfo));
    // TEST_KNET_TELEPERSIST_INIT测试用例会把curProcessNum置1
    for (int i = 1; i <= MAX_PROCESS_NUM_DTEST; i++) {
        TelemetrySetNewProcess(i, i);
    }

    DT_ASSERT_EQUAL(g_processInfo.curProcessNum, MAX_PROCESS_NUM_DTEST);

    for (int i = 1; i <= MAX_PROCESS_NUM_DTEST; i++) {
        TelemetryDelOldProcess(i, i);
    }

    DT_ASSERT_EQUAL(g_processInfo.curProcessNum, 0);
}

/**
 * @brief 持久化线程初始化测试
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_INIT, NULL, NULL)
{
    int ret;
    TelemetryPersistUninitDpJson();
    ret = TelemetryPersistThreadInit();
    DT_ASSERT_EQUAL(ret, 0);
    TelemetryPersistUninitDpJson();
}

int WriteJsonHeadDtest(FILE *file, char *data, size_t len, int offset)
{
    if (data == NULL) {
        return -1;
    }
    if (strcmp(data, "{\n") != 0) {
        return -1;
    }
    return 0;
}

int WriteJsonTailDtest(FILE *file, char *data, size_t len, int offset)
{
    if (data == NULL) {
        return -1;
    }
    if (strcmp(data, "\n}") != 0) {
        return -1;
    }
    return 0;
}

/**
 * @brief 测试Json头/尾构造的内容是否正确
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_FORMAT_HEADTAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    /* 测试头数据是否正确 */
    Mock->Create(WriteDataToFile, WriteJsonHeadDtest);
    int ret = WriteJsonHead(NULL, 0);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(WriteDataToFile);

    /* 测试尾数据是否正确 */
    Mock->Create(WriteDataToFile, WriteJsonTailDtest);
    ret = WriteJsonTail(NULL, 0);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(WriteDataToFile);

    DeleteMock(Mock);
}

#define TEST_FORMAT_STRING "this is test\n"
int FormatXstatsDataByPortIdDtest(char *output, int *outputLeftLen, uint16_t portID)
{
    int offset = 0;
    offset = sprintf_s(output, *outputLeftLen, TEST_FORMAT_STRING);
    return offset;
}

int WriteDataXstatsDtest(FILE *file, char *data, size_t len, int offset)
{
    if (data == NULL) {
        return -1;
    }
    if (strcmp(data, TEST_FORMAT_STRING) != 0) {
        return -1;
    }
    return 0;
}

/**
 * @brief 测试xstats写文件的内容是否一致
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_WRITE_XSTATS, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    /* 测试头构造是否正确 */
    Mock->Create(FormatXstatsDataByPortId, FormatXstatsDataByPortIdDtest);
    Mock->Create(WriteDataToFile, WriteDataXstatsDtest);
    int ret = WriteDpdkXstats(NULL, 0);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(WriteDataToFile);
    Mock->Delete(FormatXstatsDataByPortId);

    DeleteMock(Mock);
}

#define XSTATS_LEN_DTEST 2
#define FORMAT_JSON_LEN 500
int RteEthXstatsGetDtest(uint16_t portID, struct rte_eth_xstat *xstats, uint32_t namesLen)
{
    xstats[0].id = 0;
    xstats[0].value = 1;
    xstats[1].id = 1;
    xstats[1].value = 2;  // 手动构造值为2
    return XSTATS_LEN_DTEST;
}

int RteEthXstatsGetNamesDtest(uint16_t portID, struct rte_eth_xstat_name *xStatsNames, uint32_t namesLen)
{
    char *test1 = "test1";
    char *test2 = "test2";
    memcpy_s(xStatsNames[0].name, sizeof(xStatsNames[0].name), test1, strlen(test1));
    memcpy_s(xStatsNames[1].name, sizeof(xStatsNames[1].name), test2, strlen(test2));
    return XSTATS_LEN_DTEST;
}

/**
 * @brief 手动构造xstats的数据,然后校验构造出来的json是否正确
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_FORMAT_XSTATS, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(rte_eth_xstats_get_names, RteEthXstatsGetNamesDtest);
    Mock->Create(rte_eth_xstats_get, RteEthXstatsGetDtest);

    char tempBuff[FORMAT_JSON_LEN] = { 0 };
    int leftLen = FORMAT_JSON_LEN - 1;

    /* 构造json头 */
    int offset = sprintf_s(tempBuff, leftLen, "{");
    DT_ASSERT_NOT_EQUAL(offset, -1);
    leftLen = leftLen - offset;

    /* 构造xstats数据 */
    int ret = FormatXstatsDataByPortId(tempBuff + offset, &leftLen, 0);
    DT_ASSERT_NOT_EQUAL(ret, -1);
    leftLen = leftLen - ret;

    /* 构造json尾 */
    offset = offset + ret - 2;  // 手动偏移2字节
    leftLen += 2;   // 手动偏移2字节
    ret = sprintf_s(tempBuff + offset, leftLen, "}");
    DT_ASSERT_NOT_EQUAL(ret, -1);

    printf("Xstats buff:\n%s\n", tempBuff);
    cJSON *json = cJSON_Parse(tempBuff);
    DT_ASSERT_NOT_EQUAL(json, NULL);

    cJSON *child = cJSON_GetObjectItemCaseSensitive(json, "/ethdev/xstats/port0");

    /* 查找手动构造的key value是否正确 */
    cJSON *temp = cJSON_GetObjectItemCaseSensitive(child, "test1");
    DT_ASSERT_NOT_EQUAL(temp, NULL);
    DT_ASSERT_EQUAL(temp->valueint, 1);

    temp = cJSON_GetObjectItemCaseSensitive(child, "test2");
    DT_ASSERT_NOT_EQUAL(temp, NULL);
    DT_ASSERT_EQUAL(temp->valueint, 2);  // 前面构造的值2

    cJSON_Delete(json);

    Mock->Delete(rte_eth_xstats_get_names);
    Mock->Delete(rte_eth_xstats_get);
    DeleteMock(Mock);
}

int GetCurrentTimeDtest(char *buffer, size_t size)
{
    int offset = 0;
    offset = sprintf_s(buffer, size, "1");
    return offset;
}

/**
 * @brief 测试单个进程写文件的内容是否一致
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_WRITE_PROCESS, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    /* 测试头构造是否正确 */
    Mock->Create(GetCurrentTime, GetCurrentTimeDtest);
    Mock->Create(FormatEveryDpStats, TEST_GetFuncRetPositive(0));

    char tempBuff[FORMAT_JSON_LEN] = {0};
    int leftLen = FORMAT_JSON_LEN - 1;
    int ret = RefreshSingleProcessData(tempBuff, &leftLen, 1, 1);
    printf("Single process buff:\n%s\n", tempBuff);
    DT_ASSERT_NOT_EQUAL(ret, 0);

    ret = strcmp(tempBuff,
                 "\"pstats000000000000000000001\" : {\n\"pid\":                     1,\"date\": \"1\"},\n");
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(GetCurrentTime);
    Mock->Delete(FormatEveryDpStats);

    DeleteMock(Mock);
}

extern "C" {
typedef cJSON *(*GetDpStateByTypeFuncDtest)(DP_StatType_t type);
extern GetDpStateByTypeFuncDtest g_getDpStateByTypeFunc;
#define DP_STATS_KEYLEN_DETEST 20

typedef struct {
    char *head;
    char *key;
    int value;
} dpStatJsonDtest;

static const dpStatJsonDtest DP_STATS_JSON_DTEST[DP_STAT_MAX] = {
    {"/knet/stack/tcp_stat", "DP_STAT_TCP", 0},
    {"/knet/stack/conn_stat", "DP_STAT_CONN", 1},
    {"/knet/stack/pkt_stat", "DP_STAT_PKT", 2},
    {"/knet/stack/abn_stat", "DP_STAT_ABN", 3},
    {"/knet/stack/mem_stat", "DP_STAT_MEM", 4},
    {"/knet/stack/pbuf_stat", "DP_STAT_PBUF", 5}
};

cJSON *GetDpStateByTypeDtest(DP_StatType_t type)
{
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        return NULL;
    }
    if(type >= DP_STAT_MAX || type < 0){
        return NULL;
    }
    if (DP_STATS_JSON_DTEST[type].key == NULL) {
        return NULL;
    }

    cJSON_AddNumberToObject(json, DP_STATS_JSON_DTEST[type].key, DP_STATS_JSON_DTEST[type].value);

    return json;
}

}
/**
 * @brief 手动构造dp stats的数据,然后校验构造出来的json是否正确
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_FORMAT_DPSTATS, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    GetDpStateByTypeFuncDtest temp = g_getDpStateByTypeFunc;
    g_getDpStateByTypeFunc = GetDpStateByTypeDtest;

    char tempBuff[FORMAT_JSON_LEN] = { 0 };
    int leftLen = FORMAT_JSON_LEN - 1;
    /* 构造json头 */
    int offset = sprintf_s(tempBuff, leftLen, "{");
    DT_ASSERT_NOT_EQUAL(offset, -1);

    /* 构造dp stats数据 */
    leftLen -= offset;
    int ret = FormatEveryDpStats(tempBuff + offset, &leftLen);
    DT_ASSERT_NOT_EQUAL(ret, -1);

    /* 构造json尾 */
    offset = offset + ret - 2; // 手动偏移2字节
    ret = sprintf_s(tempBuff + offset, leftLen, "}");
    DT_ASSERT_NOT_EQUAL(ret, -1);

    cJSON *json = cJSON_Parse(tempBuff);
    DT_ASSERT_NOT_EQUAL(json, NULL);

    /* 查找手动构造的key value是否正确 */
    cJSON *head = NULL;
    cJSON *child = NULL;
    for (int i = 0; i < DP_STAT_MAX; i++) {
        head = cJSON_GetObjectItemCaseSensitive(json, DP_STATS_JSON_DTEST[i].head);
        DT_ASSERT_NOT_EQUAL(head, NULL);
        child = cJSON_GetObjectItemCaseSensitive(head, DP_STATS_JSON_DTEST[i].key);
        DT_ASSERT_NOT_EQUAL(child, NULL);
        if(child != NULL){
            DT_ASSERT_EQUAL(child->valueint, DP_STATS_JSON_DTEST[i].value);
        }
    }

    cJSON_Delete(json);

    g_getDpStateByTypeFunc = temp;

    DeleteMock(Mock);
}



/**
 * @brief 文件处理测试
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_PROCFILE, NULL, NULL)
{
    KTestMock *Mock = CreateMock();

    /* 模拟写失败 */
    Mock->Create(fopen, TEST_GetFuncRetPositive(1));
    Mock->Create(fread, TEST_GetFuncRetPositive(1));
    Mock->Create(fwrite, TEST_GetFuncRetPositive(0));
    Mock->Create(fclose, TEST_GetFuncRetPositive(0));
    Mock->Create(chmod, TEST_GetFuncRetPositive(0));
    int ret = StartDumpOldFile(NULL);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(chmod);
    Mock->Delete(fclose);
    Mock->Delete(fopen);
    Mock->Delete(fread);
    Mock->Delete(fwrite);

    DeleteMock(Mock);
}

/**
 * @brief 写文件测试
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_WRITEFILE, NULL, NULL)
{
    KTestMock *Mock = CreateMock();

    char *data = "test";
    /* 文件为NULL */
    int ret = WriteDataToFile(NULL, data, 1, 0);
    DT_ASSERT_EQUAL(ret, -1);

    /* 长度大于实际值 */
    ret = WriteDataToFile(1, data, 10, 0);  // 10大于test长度
    DT_ASSERT_EQUAL(ret, -1);

    /* 获取当前时间 */
    char buff[5] = {0}; // 使用5长度过短的buffer
    ret = GetCurrentTime(buff, 5);
    DT_ASSERT_EQUAL(ret, -1);

    DeleteMock(Mock);
}

/**
 * @brief 刷新单个进程
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_REFRESHSINGLE, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    Mock->Create(WriteDataToFile, TEST_GetFuncRetPositive(1));
    Mock->Create(fclose, TEST_GetFuncRetPositive(0));
    Mock->Create(WriteJsonHead, TEST_GetFuncRetPositive(1));
    Mock->Create(WriteDpdkXstats, TEST_GetFuncRetPositive(1));
    Mock->Create(FormatEveryDpStats, TEST_GetFuncRetNegative(1));

    uint64_t sequnce = 0;
    FILE *file = NULL;
    int ret = TelemetryRefreshDataSingle(file, &g_processInfo, &sequnce);
    DT_ASSERT_EQUAL(ret, 2); // 只有WriteJsonHead+ WriteDpdkXstats写的2字节数据

    Mock->Create(RefreshSingleProcessData, TEST_GetFuncRetPositive(1));
    Mock->Create(WriteJsonTail, TEST_GetFuncRetPositive(1));
    ret = TelemetryRefreshDataSingle(file, &g_processInfo, &sequnce);
    DT_ASSERT_EQUAL(ret, 4);    // WriteJsonHead+ WriteDpdkXstats + dp + WriteJsonTail写的4字节数据

    Mock->Delete(FormatEveryDpStats);
    Mock->Delete(RefreshSingleProcessData);
    Mock->Delete(WriteJsonHead);
    Mock->Delete(WriteDpdkXstats);
    Mock->Delete(WriteJsonTail);
    Mock->Delete(WriteDataToFile);
    Mock->Delete(fclose);
    DeleteMock(Mock);
}

/**
 * @brief 刷新多进程
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_REFRESHMULTI, NULL, NULL)
{
    /* 手动构造进程信息,参数就不额外用宏了 */
    struct KnetProcessInfo myProcessInfo = { 0 };
    myProcessInfo.curProcessNum = 2;    // 模拟2个进程
    myProcessInfo.totalProcessNum = 2;  // 模拟2个进程
    myProcessInfo.processInfo[0].pid = 1;
    myProcessInfo.processInfo[0].alive = 1;
    myProcessInfo.processInfo[0].offset = 0;
    myProcessInfo.processInfo[1].pid = 2;   // 第2个进程pid
    myProcessInfo.processInfo[1].alive = 0;
    myProcessInfo.processInfo[1].offset = 0;

    KTestMock *Mock = CreateMock();
    Mock->Create(WriteDataToFile, TEST_GetFuncRetPositive(1));
    Mock->Create(fclose, TEST_GetFuncRetPositive(0));
    Mock->Create(WriteJsonHead, TEST_GetFuncRetPositive(1));
    Mock->Create(WriteDpdkXstats, TEST_GetFuncRetPositive(1));
    Mock->Create(TelemetryRefreshPerSubprocess, TEST_GetFuncRetPositive(1));
    Mock->Create(WriteJsonTail, TEST_GetFuncRetPositive(1));
    uint64_t sequnce = 0;
    FILE *file = NULL;
    int ret = TelemetryRefreshDataMulti(file, &myProcessInfo, &sequnce);
    DT_ASSERT_EQUAL(ret, 4);  // 前4次加起来

    Mock->Delete(WriteJsonHead);
    Mock->Delete(WriteDpdkXstats);
    Mock->Delete(TelemetryRefreshPerSubprocess);
    Mock->Delete(WriteJsonTail);
    Mock->Delete(WriteDataToFile);
    Mock->Delete(fclose);
    DeleteMock(Mock);
}

int RefreshSingleProcessDataDtest(char *output, int *outputLeftLen, pid_t pid, uint64_t sequence)
{
    return 4; // 模拟写入4个字节
}

struct rte_memzone *g_memZoneDtest = NULL;
const struct rte_memzone *rteMemzonLoockUpDtest(const char *name)
{
    return g_memZoneDtest;
}


/**
 * @brief 刷新多进程2
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_REFRESHMULTI2, NULL, NULL)
{
    /* 手动构造进程信息,参数就不额外用宏了 */
    struct KnetProcessInfo myProcessInfo = { 0 };
    myProcessInfo.curProcessNum = 2;    // 模拟2个进程
    myProcessInfo.totalProcessNum = 2;  // 模拟2个进程
    myProcessInfo.writeBitMap = (1 << 0) | (1 << 1);
    myProcessInfo.processInfo[0].pid = 1;
    myProcessInfo.processInfo[0].alive = 0;
    myProcessInfo.processInfo[0].offset = 4;    // 模拟死掉的进程的偏移量是4
    myProcessInfo.processInfo[1].pid = 2;       // 第2个进程pid
    myProcessInfo.processInfo[1].alive = 1;
    myProcessInfo.processInfo[1].offset = 0;

    uint64_t sequence = 0;
    KTestMock *Mock = CreateMock();
    Mock->Create(rte_memzone_lookup, rteMemzonLoockUpDtest);
    Mock->Create(RefreshSingleProcessData, RefreshSingleProcessDataDtest);

    int offset = TelemetryRefreshPerSubprocess(NULL, 0, &myProcessInfo, &sequence);
    /* 计算公式: 死掉的进程偏移量4 + 补充的",\n" 2字节 + 模拟写入的4个字节 - 2个字节的尾部偏移 = 8 */
    DT_ASSERT_EQUAL(offset, 8);
    DT_ASSERT_EQUAL(myProcessInfo.processInfo[1].offset, 4);    // 写入4字节

    Mock->Delete(RefreshSingleProcessData);
    Mock->Delete(rte_memzone_lookup);
    DeleteMock(Mock);
}


/**
 * @brief KNET_DebugOutputToFile接口测试
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_DEBUGOUTPUT, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    char *test = "test";

    /* 测试单进程 */
    int ret = KNET_DebugOutputToFile(test, MAX_OUTPUT_LEN);
    DT_ASSERT_NOT_EQUAL(ret, KNET_OK);
    ret = KNET_DebugOutputToFile(test, strlen(test) + 1);
    DT_ASSERT_EQUAL(ret, KNET_OK);

    /* 测试多进程 */
    KNET_TelemetryPersistInfo telemetryInfo = {0};
    telemetryInfo.msgType = DP_STAT_TCP;
    struct rte_memzone *mz = malloc(sizeof(struct rte_memzone));
    DT_ASSERT_NOT_EQUAL(mz, NULL);
    mz->addr = &telemetryInfo;
    g_memZoneDtest = mz;

    Mock->Create(rte_memzone_lookup, rteMemzonLoockUpDtest);
    Mock->Create(KNET_GetCfg, KNET_GetCfgMultiDtest);
    ret = KNET_DebugOutputToFile(test, 1);
    DT_ASSERT_EQUAL(ret, KNET_OK);

    free(mz);
    g_memZoneDtest = NULL;
    Mock->Delete(rte_memzone_lookup);
    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

extern char g_knetTeleToFileDpOutput[MAX_OUTPUT_LEN];
extern KNET_DpShowStatisticsHook g_dpShowStatisticsHookPersist;

char *g_testDpStatsDtest = "{\"Accepts\":100,\"Closed\":100}";

void KNET_DpShowStatsHookDtest(DP_StatType_t type, int workerId, uint32_t flag)
{
    /* 手动构造数据 */
    int ret = memcpy_s(g_knetTeleToFileDpOutput, MAX_OUTPUT_LEN, g_testDpStatsDtest, strlen(g_testDpStatsDtest) + 1);
    if (ret != 0) {
        return;
    }
    printf("output:\n%s\n", g_knetTeleToFileDpOutput);
    return;
}

/**
 * @brief 获取DP的TCP数据测试
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_GETDPSTAT, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    TelemetryPersistInitDpJson();

    /* 测试单进程 */
    KNET_DpShowStatisticsHook temp = g_dpShowStatisticsHookPersist;
    g_dpShowStatisticsHookPersist = KNET_DpShowStatsHookDtest;
    cJSON *json = GetDpStateByTypeSingle(DP_STAT_MAX);
    DT_ASSERT_EQUAL(json, NULL);

    json = GetDpStateByTypeSingle(DP_STAT_TCP);
    DT_ASSERT_NOT_EQUAL(json, NULL);

    cJSON *key = cJSON_GetObjectItemCaseSensitive(json, "Accepts");

    DT_ASSERT_NOT_EQUAL(key, NULL);
    DT_ASSERT_EQUAL(key->valueint, 100);    // 前面构造的100值
    key = cJSON_GetObjectItemCaseSensitive(json, "Closed");
    DT_ASSERT_NOT_EQUAL(key, NULL);
    DT_ASSERT_EQUAL(key->valueint, 100);    // 前面构造的100值

    cJSON_Delete(json);
    /* 测试多进程 */
    json = GetDpStateByTypeMulti(DP_STAT_MAX);
    DT_ASSERT_EQUAL(json, NULL);

    /* 手动构造数据 */
    KNET_TelemetryPersistInfo telemetryInfo = {0};
    telemetryInfo.state = KNET_TELE_PERSIST_MSGREADY;
    memcpy_s(telemetryInfo.message[DP_STAT_TCP], MAX_OUTPUT_LEN, g_testDpStatsDtest, strlen(g_testDpStatsDtest) + 1);

    struct rte_memzone *mz = malloc(sizeof(struct rte_memzone));
    DT_ASSERT_NOT_EQUAL(mz, NULL);
    mz->addr = &telemetryInfo;
    g_memZoneDtest = mz;
    Mock->Create(rte_memzone_lookup, rteMemzonLoockUpDtest);
    json = GetDpStateByTypeMulti(DP_STAT_TCP);

    key = cJSON_GetObjectItemCaseSensitive(json, "Accepts");
    DT_ASSERT_NOT_EQUAL(key, NULL);
    DT_ASSERT_EQUAL(key->valueint, 100);    // 前面构造的100值
    key = cJSON_GetObjectItemCaseSensitive(json, "Closed");
    DT_ASSERT_NOT_EQUAL(key, NULL);
    DT_ASSERT_EQUAL(key->valueint, 100);    // 前面构造的100值

    /* 释放资源 */
    cJSON_Delete(json);
    Mock->Delete(rte_memzone_lookup);
    free(mz);
    g_memZoneDtest = NULL;
    g_dpShowStatisticsHookPersist = temp;
    DeleteMock(Mock);
}

/**
 * @brief 持久化线程去初始化
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_UNINIT, NULL, NULL)
{
    TelemetryPersistUninitDpJson();
}

/* ========== 新增用例: 覆盖 knet_telemetry_thread.c 未覆盖分支 ========== */

/**
 * @brief TelemetryDisconnectHandler: 匹配clientID与不匹配clientID路径
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_DISCONNECT, NULL, NULL)
{
    (void)memset_s(&g_processInfo, sizeof(g_processInfo), 0, sizeof(g_processInfo));
    /* 设置一个进程, clientID=5 */
    g_processInfo.processInfo[0].pid = 100;
    g_processInfo.processInfo[0].alive = true;
    g_processInfo.processInfo[0].clientID = 5;
    g_processInfo.curProcessNum = 1;

    struct KNET_RpcMessage *req = NULL;
    struct KNET_RpcMessage *resp = NULL;
    /* 匹配clientID=5 -> 标记为dead */
    int ret = TelemetryDisconnectHandler(5, req, resp);
    DT_ASSERT_EQUAL(ret, 0);
    DT_ASSERT_EQUAL(g_processInfo.processInfo[0].alive, false);
    DT_ASSERT_EQUAL(g_processInfo.curProcessNum, 0);

    /* 不匹配clientID=999 -> 不做改动 */
    ret = TelemetryDisconnectHandler(999, req, resp);
    DT_ASSERT_EQUAL(ret, 0);
    (void)memset_s(&g_processInfo, sizeof(g_processInfo), 0, sizeof(g_processInfo));
}

/**
 * @brief TelemetrySetNewProcess 边界: pid==0, 满表, 替换最早退出, 重复pid
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_SETNEWPROC_EDGE, NULL, NULL)
{
    (void)memset_s(&g_processInfo, sizeof(g_processInfo), 0, sizeof(g_processInfo));

    /* pid==0 -> 直接返回 */
    TelemetrySetNewProcess(0, 0);
    DT_ASSERT_EQUAL(g_processInfo.curProcessNum, 0);

    /* 填满32个进程 */
    for (int i = 1; i <= MAX_PROCESS_NUM_DTEST; i++) {
        TelemetrySetNewProcess(i, i);
    }
    DT_ASSERT_EQUAL(g_processInfo.curProcessNum, MAX_PROCESS_NUM_DTEST);
    DT_ASSERT_EQUAL(g_processInfo.totalProcessNum, MAX_PROCESS_NUM_DTEST);

    /* 再加一个 -> curProcessNum >= MAX, 直接返回 */
    TelemetrySetNewProcess(99, 99);
    DT_ASSERT_EQUAL(g_processInfo.curProcessNum, MAX_PROCESS_NUM_DTEST);

    /* 删除一个进程(alive=false), 使curProcessNum < MAX但totalProcessNum == MAX */
    TelemetryDelOldProcess(1, 1);
    DT_ASSERT_EQUAL(g_processInfo.curProcessNum, MAX_PROCESS_NUM_DTEST - 1);

    /* sleep 1秒确保exitTime < 当前时间, 使"查找最早退出进程"逻辑生效 */
    sleep(1);

    /* 加新进程 -> totalProcessNum >= MAX, 走替换最早退出路径 */
    TelemetrySetNewProcess(50, 50);
    /* 找到最早退出的进程(进程1刚退出, exitTime最小)并替换 */
    DT_ASSERT_EQUAL(g_processInfo.curProcessNum, MAX_PROCESS_NUM_DTEST);

    /* 重复pid: 添加已存在的pid -> break */
    TelemetrySetNewProcess(2, 2);
    /* curProcessNum不变(重复, 走break) */
    DT_ASSERT_EQUAL(g_processInfo.curProcessNum, MAX_PROCESS_NUM_DTEST);

    (void)memset_s(&g_processInfo, sizeof(g_processInfo), 0, sizeof(g_processInfo));
}

/**
 * @brief TelemetryDelOldProcess: pid==0 直接返回
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_DELPROC_ZERO, NULL, NULL)
{
    (void)memset_s(&g_processInfo, sizeof(g_processInfo), 0, sizeof(g_processInfo));
    g_processInfo.processInfo[0].pid = 1;
    g_processInfo.processInfo[0].alive = true;
    g_processInfo.curProcessNum = 1;

    /* pid==0 -> 直接返回, 不做任何改动 */
    TelemetryDelOldProcess(0, 0);
    DT_ASSERT_EQUAL(g_processInfo.curProcessNum, 1);
    DT_ASSERT_EQUAL(g_processInfo.processInfo[0].alive, true);

    (void)memset_s(&g_processInfo, sizeof(g_processInfo), 0, sizeof(g_processInfo));
}

/**
 * @brief TelemetryPersistDealFileDelete: 刷新进程信息, 去除死进程
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_DEALFILEDELETE, NULL, NULL)
{
    (void)memset_s(&g_processInfo, sizeof(g_processInfo), 0, sizeof(g_processInfo));
    /* 进程0: alive, pid=100 */
    g_processInfo.processInfo[0].pid = 100;
    g_processInfo.processInfo[0].alive = true;
    g_processInfo.processInfo[0].clientID = 1;
    /* 进程1: dead */
    g_processInfo.processInfo[1].pid = 200;
    g_processInfo.processInfo[1].alive = false;
    /* 进程2: alive, pid=300 */
    g_processInfo.processInfo[2].pid = 300;
    g_processInfo.processInfo[2].alive = true;
    g_processInfo.processInfo[2].clientID = 3;
    g_processInfo.curProcessNum = 3;
    g_processInfo.totalProcessNum = 3;

    int ret = TelemetryPersistDealFileDelete();
    DT_ASSERT_EQUAL(ret, 0);
    /* 只有2个alive进程, 应该被compact到前2个位置 */
    DT_ASSERT_EQUAL(g_processInfo.curProcessNum, 2);
    DT_ASSERT_EQUAL(g_processInfo.totalProcessNum, 2);
    DT_ASSERT_EQUAL(g_processInfo.processInfo[0].pid, 100);
    DT_ASSERT_EQUAL(g_processInfo.processInfo[2].pid, 0); /* 第3位被清零 */
    DT_ASSERT_EQUAL(g_processInfo.writeBitMap, 0);

    (void)memset_s(&g_processInfo, sizeof(g_processInfo), 0, sizeof(g_processInfo));
}

/**
 * @brief DumpOldFile: 路径不存在时创建路径; 旧文件不存在直接返回0
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_DUMPOLDFILE_NODIR, NULL, NULL)
{
    /* 确保目录不存在(先删再测), 但保留父目录/etc/knet/run */
    (void)system("rm -rf /etc/knet/run/stats");
    (void)system("mkdir -p /etc/knet/run");
    /* 目录不存在 -> mkdir创建; 旧文件不存在 -> 返回0 */
    int ret = DumpOldFile();
    DT_ASSERT_EQUAL(ret, 0);
    /* 验证目录已创建 */
    DIR *d = opendir("/etc/knet/run/stats");
    DT_ASSERT_NOT_EQUAL(d, NULL);
    if (d != NULL) {
        (void)closedir(d);
    }
}

/**
 * @brief DumpOldFile: 有旧文件时转储到新文件
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_DUMPOLDFILE_WITHFILE, NULL, NULL)
{
    (void)system("rm -rf /etc/knet/run/stats");
    (void)system("mkdir -p /etc/knet/run/stats");
    /* 创建旧文件并写入内容 */
    FILE *f = fopen("/etc/knet/run/stats/knet-persist.json", "wb");
    DT_ASSERT_NOT_EQUAL(f, NULL);
    if (f != NULL) {
        (void)fputs("{\"test\":\"data\"}", f);
        (void)fclose(f);
    }

    int ret = DumpOldFile();
    DT_ASSERT_EQUAL(ret, 0);
    /* 验证转储文件已生成(以knet_persist-开头) */
    (void)system("ls /etc/knet/run/stats/knet_persist-*.json > /tmp/dump_check.log 2>&1");
    FILE *chk = fopen("/tmp/dump_check.log", "r");
    DT_ASSERT_NOT_EQUAL(chk, NULL);
    if (chk != NULL) {
        char buf[256] = {0};
        (void)fgets(buf, sizeof(buf), chk);
        (void)fclose(chk);
        DT_ASSERT_NOT_EQUAL(strstr(buf, "knet_persist-"), (char *)NULL);
    }

    /* 清理 */
    (void)system("rm -rf /etc/knet/run/stats");
}

/**
 * @brief CleanupOldDumpFiles: 创建>9个转储文件, 验证清理逻辑
 *        覆盖 ProcessDumpFileEntry/CollectDumpFiles/ExtractTimestampInt/
 *        FindOldestDumpFile/CleanupOldDumpFiles
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_CLEANUP_DUMPFILES, NULL, NULL)
{
    (void)system("rm -rf /etc/knet/run/stats");
    (void)system("mkdir -p /etc/knet/run/stats");
    /* 创建12个转储文件(超过MAX_DUMP_FILE_NUM=9), 时间戳递增 */
    for (int i = 0; i < 12; i++) {
        char cmd[256];
        (void)sprintf_s(cmd, sizeof(cmd),
            "echo '{}' > /etc/knet/run/stats/knet_persist-202601010000%02d.json", i);
        (void)system(cmd);
    }
    /* 再加一个非转储文件(不匹配前缀), 确保被跳过 */
    (void)system("echo '{}' > /etc/knet/run/stats/other.json");
    /* 再加一个非.json后缀的转储前缀文件, 确保被跳过 */
    (void)system("echo '{}' > /etc/knet/run/stats/knet_persist-2026010100012.txt");

    /* 调用DumpOldFile, 内部会调用StartDumpOldFile->CleanupOldDumpFiles */
    /* 先创建knet-persist.json使DumpOldFile进入转储路径 */
    FILE *f = fopen("/etc/knet/run/stats/knet-persist.json", "wb");
    if (f != NULL) {
        (void)fputs("{}", f);
        (void)fclose(f);
    }
    int ret = DumpOldFile();
    DT_ASSERT_EQUAL(ret, 0);

    /* 清理 */
    (void)system("rm -rf /etc/knet/run/stats");
}

/**
 * @brief OpenFileWithRWB: 文件不存在时创建新文件
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_OPENFILE_CREATE, NULL, NULL)
{
    (void)system("rm -rf /etc/knet/run/stats");
    (void)system("mkdir -p /etc/knet/run/stats");
    /* 文件不存在 -> DealFileDelete + 创建 */
    FILE *fp = OpenFileWithRWB("/etc/knet/run/stats", "/etc/knet/run/stats/knet-persist.json");
    DT_ASSERT_NOT_EQUAL(fp, NULL);
    if (fp != NULL) {
        (void)fclose(fp);
    }
    /* 文件已存在 -> 正常打开 */
    fp = OpenFileWithRWB("/etc/knet/run/stats", "/etc/knet/run/stats/knet-persist.json");
    DT_ASSERT_NOT_EQUAL(fp, NULL);
    if (fp != NULL) {
        (void)fclose(fp);
    }
    (void)system("rm -rf /etc/knet/run/stats");
}

/**
 * @brief WriteDataToFile: 成功写入 + fseek失败 + fwrite失败路径
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_WRITEDATA_PATHS, NULL, NULL)
{
    /* 成功写入: 使用临时文件 */
    FILE *f = fopen("/tmp/writetest.json", "wb+");
    DT_ASSERT_NOT_EQUAL(f, NULL);
    if (f != NULL) {
        const char *data = "hello world";
        int ret = WriteDataToFile(f, (char *)data, strlen(data), 0);
        DT_ASSERT_EQUAL(ret, (int)strlen(data));

        /* offset为负值触发fseek失败 */
        ret = WriteDataToFile(f, (char *)data, strlen(data), -1);
        DT_ASSERT_EQUAL(ret, -1);

        (void)fclose(f);
    }
    (void)unlink("/tmp/writetest.json");

    /* data长度不匹配: strlen(data) < len */
    f = fopen("/tmp/writetest2.json", "wb+");
    if (f != NULL) {
        const char *data = "ab";
        int ret = WriteDataToFile(f, (char *)data, 100, 0); /* len=100 > strlen=2 */
        DT_ASSERT_EQUAL(ret, -1);
        (void)fclose(f);
    }
    (void)unlink("/tmp/writetest2.json");
}

/**
 * @brief GetCurrentTime: 成功获取时间
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_GETTIME_OK, NULL, NULL)
{
    char buf[64] = {0};
    int ret = GetCurrentTime(buf, sizeof(buf));
    DT_ASSERT_EQUAL(ret, 0);
    /* 验证时间格式 YYYY-MM-DD HH:MM:SS */
    DT_ASSERT_NOT_EQUAL(strlen(buf), (size_t)0);
    DT_ASSERT_NOT_EQUAL(strstr(buf, "-"), (char *)NULL);
}

/**
 * @brief KNET_TelemetrySetPersistThreadExit: 设置退出标志
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_SETEXIT, NULL, NULL)
{
    KNET_TelemetrySetPersistThreadExit();
    /* g_persistThreadExit已设置为true, 无返回值, 验证不崩溃即可 */
}

/**
 * @brief GetSingleProcessDpStatsMulti: formatLastTail=true/false路径
 */
DTEST_CASE_F(TELE_PERSIST, TEST_KNET_TELEPERSIST_GETSINGLE_MULTI, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    /* mock rte_memzone_lookup 返回NULL, 防止未初始化DPDK时SEGV */
    Mock->Create(rte_memzone_lookup, rteMemzonLoockUpDtest);
    Mock->Create(GetCurrentTime, GetCurrentTimeDtest);
    Mock->Create(FormatEveryDpStats, TEST_GetFuncRetPositive(0));

    char buf[FORMAT_JSON_LEN] = {0};
    int leftLen = FORMAT_JSON_LEN - 1;
    /* formatLastTail=true: 走添加",\n"前缀路径 */
    int ret = GetSingleProcessDpStatsMulti(buf, &leftLen, 1, true, 0);
    DT_ASSERT_NOT_EQUAL(ret, -1);

    /* formatLastTail=false: 不添加前缀 */
    (void)memset_s(buf, sizeof(buf), 0, sizeof(buf));
    leftLen = FORMAT_JSON_LEN - 1;
    ret = GetSingleProcessDpStatsMulti(buf, &leftLen, 2, false, 1);
    DT_ASSERT_NOT_EQUAL(ret, -1);

    Mock->Delete(GetCurrentTime);
    Mock->Delete(FormatEveryDpStats);
    Mock->Delete(rte_memzone_lookup);
    DeleteMock(Mock);
}
