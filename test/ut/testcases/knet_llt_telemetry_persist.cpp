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
