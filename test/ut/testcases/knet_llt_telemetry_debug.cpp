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

#include <stdio.h>
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
#include "rte_eal.h"
#include "rte_hash.h"
#include "rte_errno.h"
#include "rte_memzone.h"

#include "dp_debug_api.h"

#include "knet_mock.h"
#include "knet_types.h"
#include "knet_log.h"
#include "knet_dpdk_init.h"
#include "tcp_fd.h"
#include "knet_transmission.h"

#include "securec.h"
#include "common.h"
#include "mock.h"

#define OVER_MAX_LEN 16385
#define QUEUEID_EXCEED_MAX_QUEUENUM 1024
#define MOCK_PID 12345
#define FLOW_TABLE_LEN 2048

extern "C" {
#include "rte_telemetry.h"
#include "knet_telemetry_call.h"
#include "knet_telemetry_debug.h"
int AddJsonToData(cJSON *json, const char *key, struct rte_tel_data *data);
DP_StatType_t GetStatTypeFromString(const char *cmd);
int ProcessSocketState(KNET_SocketState socketState, struct rte_tel_data* socketStatDic);
KNET_STATIC int AddJsonToStringData(cJSON *json, const char *key, struct rte_tel_data *data);
KNET_STATIC int CreateSockInfoAndAddReply(DP_SockDetails_t *dpSockDetails, struct rte_tel_data* data);
KNET_STATIC int CreateInetSkInfoAndAddReply(DP_SockDetails_t *dpSockDetails, struct rte_tel_data* data);
KNET_STATIC int CreateTcpBaseInfoAndAddReply(DP_SockDetails_t *dpSockDetails, struct rte_tel_data* data);
KNET_STATIC int CreateTcpTransInfoAndAddReply(DP_SockDetails_t *dpSockDetails, struct rte_tel_data* data);
KNET_STATIC int ProcessSocketInfo(DP_SockDetails_t *socketDetails, struct rte_tel_data* data);
KNET_STATIC struct rte_hash *KnetGetFdirHandle(void);
void KNET_Usleep(uint64_t usec);
int AddPidTid(struct rte_tel_data *data, uint32_t queId);
extern char g_knetDebugOutput[MAX_OUTPUT_LEN];

}

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

struct rte_tel_data {};

static union KNET_CfgValue g_cfg = {.intValue = 1};
static union KNET_CfgValue *MockKnetGetCfg(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    g_cfg.intValue = 1;
    return &g_cfg;
}

static struct rte_memzone *MockRteMemzoneLookup(char *memzoneName)
{
    static struct rte_memzone mz = {};
    static KNET_TelemetryInfo telemetryInfo = {};
    telemetryInfo.pid[1] = MOCK_PID;
    mz.addr = (void *)&telemetryInfo;
    return &mz;
}

static struct cJSON* MockcJSONGetObjectNULL(cJSON *json, const char *key)
{
    return NULL;
}

static struct cJSON* MockcJSONGetObjectNotNULL(cJSON *json, const char *key)
{
    static struct cJSON data = {};
    data.valuestring = "1";
    return &data;
}

static KNET_QueIdMapPidTid_t* MockKnetGetQueIdMapPidTidLcoreInfo(void)
{
    static KNET_QueIdMapPidTid_t data = {};
    data.workerId = 1;
    data.tid = 1;
    return &data;
}

static int MockTelDataAddDictContainer(struct rte_tel_data *d, const char *name, struct rte_tel_data *val, int keep)
{
    // 原函数在内部释放val，打桩防止内存泄漏
    free(val);
    return 0;
}

static struct rte_tel_data* MockRteTelDataAlloc(void)
{
    static struct rte_tel_data data = {};
    return &data;
}

KNET_STATIC int CreateSockInfoAndAddReply(DP_SockDetails_t *dpSockDetails, struct rte_tel_data* data);
KNET_STATIC int CreateInetSkInfoAndAddReply(DP_SockDetails_t *dpSockDetails, struct rte_tel_data* data);
KNET_STATIC int CreateTcpBaseInfoAndAddReply(DP_SockDetails_t *dpSockDetails, struct rte_tel_data* data);
KNET_STATIC int CreateTcpTransInfoAndAddReply(DP_SockDetails_t *dpSockDetails, struct rte_tel_data* data);
KNET_STATIC int ProcessSocketInfo(DP_SockDetails_t *socketDetails, struct rte_tel_data* data);

DTEST_CASE_F(SAL_FUN, TEST_KNET_STAT_OUTPUT_TO_TELEMETRY_NULLPTR, NULL, NULL)
{
    int ret;

    ret = KNET_DebugOutputToTelemetry(NULL, 0);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    ret = KNET_DebugOutputToTelemetry(NULL, 1);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    ret = KNET_DebugOutputToTelemetry("test_string", strlen("test_string"));
    DT_ASSERT_EQUAL(ret, (int)KNET_OK);

    ret = KNET_DebugOutputToTelemetry("unexpected_string", 1);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    ret = KNET_DebugOutputToTelemetry("test_string", OVER_MAX_LEN);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    struct rte_tel_data *data = {};
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    Mock->Create(rte_memzone_lookup, MockRteMemzoneLookup);
    Mock->Create(strncpy_s, TEST_GetFuncRetPositive(0));
    ret = KNET_DebugOutputToTelemetry("test_string", strlen("test_string"));
    DT_ASSERT_EQUAL(ret, (int)KNET_OK);
    Mock->Delete(KNET_GetCfg);
    Mock->Delete(strncpy_s);
    Mock->Delete(rte_memzone_lookup);
    DeleteMock(Mock);
}

char *GetLongJsonString(int entryCount)
{
    cJSON *json = cJSON_CreateObject();

    for (int i = 0; i < entryCount; i++) {
        char key[10];
        char value[20];
        (void)snprintf_s(key, sizeof(key), sizeof(key) - 1, "key%d", i);
        (void)snprintf_s(value, sizeof(value), sizeof(value) - 1, "%d", i);
        cJSON_AddStringToObject(json, key, value);
    }

    char *jsonString = cJSON_Print(json);
    cJSON_Delete(json);
    
    return jsonString;
}

static union KNET_CfgValue *KNET_GetCfgMock(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    g_cfg.intValue = KNET_RUN_MODE_SINGLE;
    return &g_cfg;
}

DTEST_CASE_F(SAL_FUN, TEST_KNET_STAT_OUTPUT_TO_TELEMETRY_NORMAL, NULL, NULL)
{
    int ret;
    char *jsonString = GetLongJsonString(8192);
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, KNET_GetCfgMock);

    ret = KNET_DebugOutputToTelemetry(jsonString, -1);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    ret = KNET_DebugOutputToTelemetry(jsonString, 0);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    int testOutputLen = 1000;
    ret = KNET_DebugOutputToTelemetry(jsonString, testOutputLen);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    ret = KNET_DebugOutputToTelemetry(jsonString, strlen(jsonString));
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);
    free(jsonString);

    jsonString = GetLongJsonString(1);
    ret = KNET_DebugOutputToTelemetry(jsonString, strlen(jsonString));
    DT_ASSERT_EQUAL(ret, (int)KNET_OK);
    free(jsonString);

    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

DTEST_CASE_F(SAL_FUN, TEST_KNET_ADD_JSON_TO_DATA_JSON_INPUT, NULL, NULL)
{
    int ret;
    cJSON *json;
    struct rte_tel_data data = {};
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    json = cJSON_Parse("{ \"key\" : \"1\" }");
    Mock->Create(rte_tel_data_add_dict_u64, TEST_GetFuncRetPositive(1));
    ret = AddJsonToData(json, "key", &data);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);
    Mock->Delete(rte_tel_data_add_dict_u64);
    cJSON_Delete(json);

    json = cJSON_Parse("{ \"key\" : \"\" }");
    Mock->Create(rte_tel_data_add_dict_u64, TEST_GetFuncRetPositive(0));
    ret = AddJsonToData(json, "key", &data);
    DT_ASSERT_EQUAL(ret, (int)KNET_OK);
    Mock->Delete(rte_tel_data_add_dict_u64);
    cJSON_Delete(json);
    DeleteMock(Mock);
}

DTEST_CASE_F(SAL_FUN, TEST_KNET_ADD_JSON_TO_DATA_NULLPTR, NULL, NULL)
{
    int ret = AddJsonToData(NULL, NULL, NULL);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    ret = AddJsonToData(NULL, "key", NULL);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    cJSON *json = cJSON_Parse("{ \"key\" : 1 }");
    ret = AddJsonToData(json, "key", NULL);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);
    cJSON_Delete(json);
}

static void hook1(DP_StatType_t type, int workerId, uint32_t flag)
{
    char *jsonString = "{ \"key\" : 1 }";
    KNET_DebugOutputToTelemetry(jsonString, strlen(jsonString));
}

static int hook2(int type)
{
    return 0;
}

static int hook3(int fd, DP_SocketState_t* state)
{
    return 0;
}

static int hook4(int fd, DP_SockDetails_t* info)
{
    return 0;
}

static int hook5(int epFd, DP_EpollDetails_t *details, int len, int *wid)
{
    return 0;
}

DTEST_CASE_F(SAL_FUN, TEST_TCP_CALLBACK_NULLPTR, NULL, NULL)
{
    KNET_DpTelemetryHooks dpTelemetryhooks = {hook1, hook2, hook3, hook4, hook5};
    int ret = KNET_DpTelemetryHookReg(dpTelemetryhooks);
    DT_ASSERT_EQUAL(ret, KNET_OK);

    ret = KnetTelemetryStatisticCallback(NULL, NULL, NULL);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(getpid, TEST_GetFuncRetPositive(1));
    Mock->Create(rte_tel_data_start_dict, TEST_GetFuncRetPositive(0));
    struct rte_tel_data data = {};
    ret = KnetTelemetryStatisticCallback("/knet/stack/tcp", "", (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    Mock->Delete(rte_tel_data_start_dict);
    Mock->Delete(getpid);
    DeleteMock(Mock);
}

DTEST_CASE_F(DPDK_TELEMETRY, TEST_KNET_GET_STAT_TYPE_FROM_STRING_NORMAL, NULL, NULL)
{
    DP_StatType_t type;

    type = GetStatTypeFromString(NULL);
    DT_ASSERT_EQUAL(type, DP_STAT_MAX);
    for (int i = 0; i < DP_STAT_MAX; i++) {
        type = GetStatTypeFromString(KNET_STAT_MAPPINGS[i].cmd);
        DT_ASSERT_EQUAL(type, KNET_STAT_MAPPINGS[i].type);
    }
}

DTEST_CASE_F(DPDK_TELEMETRY, TEST_KNET_TELEMETRY_STATISTIC_CALLBACK_MP_NULLPTR, NULL, NULL)
{
    int ret;
    struct rte_tel_data data = {};

    ret = KnetTelemetryStatisticCallbackMp(NULL, NULL, NULL);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    ret = KnetTelemetryStatisticCallbackMp("cmd", NULL, NULL);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    ret = KnetTelemetryStatisticCallbackMp("/knet/stack/tcp_stat", NULL, (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    ret = KnetTelemetryStatisticCallbackMp("/knet/stack/tcp_stat", "12345", NULL);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_memzone_lookup, MockRteMemzoneLookup);
    Mock->Create(rte_tel_data_start_dict, TEST_GetFuncRetPositive(1));
    ret = KnetTelemetryStatisticCallbackMp("/knet/stack/tcp_stat", "12345", (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);
    Mock->Delete(rte_tel_data_start_dict);

    Mock->Create(rte_tel_data_start_dict, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_IsQueueIdUsed, TEST_GetFuncRetPositive(1));

    ret = KnetTelemetryStatisticCallbackMp("/knet/stack/tcp_stat", "12345", (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    Mock->Create(KNET_IsQueueIdUsed, TEST_GetFuncRetPositive(1));
    Mock->Create(KnetHandleTimeout, TEST_GetFuncRetPositive(0));
    ret = KnetTelemetryStatisticCallbackMp("/knet/stack/tcp_stat", "12345", (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    Mock->Delete(rte_tel_data_start_dict);
    Mock->Delete(KNET_IsQueueIdUsed);
    Mock->Delete(rte_memzone_lookup);
    Mock->Delete(KnetHandleTimeout);
    DeleteMock(Mock);
}


DTEST_CASE_F(DPDK_TELEMETRY, TEST_REG_DP_STATISTICS_HOOK_SUCCESS, NULL, NULL)
{
    KNET_DpTelemetryHooks dpTelemetryhooks1 = {};
    int ret = KNET_DpTelemetryHookReg(dpTelemetryhooks1);
    DT_ASSERT_EQUAL(ret, -1);

    KNET_DpTelemetryHooks dpTelemetryhooks2 = {hook1, hook2, hook3, hook4, hook5};
    ret = KNET_DpTelemetryHookReg(dpTelemetryhooks2);
    DT_ASSERT_EQUAL(ret, KNET_OK);
}


struct Entry {
    uint64_t ip_port;
    struct Map {
        int clientId;
        uint32_t entryId; // 新增哈希表项id用于维测顺序输出
        uint16_t queueIdSize;
        uint16_t dPortMask;
        KNET_ATOMIC64_T count;
        uint16_t queueId[KNET_MAX_QUEUES_PER_PORT];
        struct rte_flow_action action[MAX_ACTION_NUM]; // 维测输出action信息
        struct rte_flow_item pattern[MAX_TRANS_PATTERN_NUM]; // 维测输出协议栈
        struct rte_flow *flow;
        struct rte_flow *arpFlow;
    } map;
};


static int mock_rte_hash_iterate(struct rte_hash *h, const void **key, void **data, uint32_t *next)
{
    static int count = 2;
    static struct rte_flow_item pattern[] = {
        {RTE_FLOW_ITEM_TYPE_ETH, NULL, NULL, NULL},
        {RTE_FLOW_ITEM_TYPE_IPV4, NULL, NULL, NULL},
        {RTE_FLOW_ITEM_TYPE_TCP, NULL, NULL, NULL},
        {RTE_FLOW_ITEM_TYPE_END, NULL, NULL, NULL}
    };
    static struct rte_flow_action action[] = {
        {RTE_FLOW_ACTION_TYPE_QUEUE, NULL},
        {RTE_FLOW_ACTION_TYPE_END, NULL}
    };
    
    static struct Entry nextEntry = {};
    nextEntry.ip_port = 0;
    nextEntry.map.entryId = 1;
    nextEntry.map.clientId = 1;
    nextEntry.map.count.count = 1;
    nextEntry.map.dPortMask = 0xffff;
    nextEntry.map.flow = NULL;
    nextEntry.map.arpFlow = NULL;
    nextEntry.map.queueIdSize = 1;
    (void)memset_s(nextEntry.map.queueId, KNET_MAX_QUEUES_PER_PORT, 0, KNET_MAX_QUEUES_PER_PORT);
    (void)memcpy_s(nextEntry.map.pattern, MAX_TRANS_PATTERN_NUM, pattern, MAX_TRANS_PATTERN_NUM);
    (void)memcpy_s(nextEntry.map.action, MAX_TRANS_PATTERN_NUM, action, MAX_TRANS_PATTERN_NUM);
    *data = &nextEntry;
    count--;
    if(count == 0){
        return -1;
    }
    return 0;
}
extern struct rte_hash *KnetGetFdirHandle(void);
DTEST_CASE_F(DPDK_TELEMETRY, TEST_TelemetryFlowTableCallBack, NULL, NULL)
{
    int ret = KnetTelemetryFlowTableCallback(NULL, NULL, NULL);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_tel_data_start_dict, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_hash_iterate, mock_rte_hash_iterate);
    Mock->Create(KNET_GetMaxEntryId, TEST_GetFuncRetPositive(1));
    Mock->Create(KnetGetFdirHandle, TEST_GetFuncRetPositive(1));
    Mock->Create(rte_tel_data_add_dict_container, MockTelDataAddDictContainer);
    Mock->Create(rte_tel_data_add_dict_string, TEST_GetFuncRetPositive(0));

    struct rte_tel_data data = {};
    ret = KnetTelemetryFlowTableCallback("/knet/flow/list", "0 0", (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_OK);

    Mock->Delete(rte_tel_data_start_dict);
    Mock->Delete(rte_hash_iterate);
    Mock->Delete(rte_tel_data_add_dict_container);
    Mock->Delete(rte_tel_data_add_dict_string);
    Mock->Delete(KnetGetFdirHandle);
    Mock->Delete(KNET_GetMaxEntryId);
    DeleteMock(Mock);
}


DTEST_CASE_F(DPDK_TELEMETRY, TEST_GetQueIdMapPidTidInfo, NULL, NULL)
{
    void* ret = (void*)KNET_GetQueIdMapPidTidLcoreInfo();
    DT_ASSERT_NOT_EQUAL(ret, NULL);
}

DTEST_CASE_F(DPDK_TELEMETRY, TEST_SetQueIdMapPidTidInfo, NULL, NULL)
{
    uint32_t invalidQueId = 512;
    uint32_t validQueId = 0;
    uint32_t num = 123;
    int ret = KNET_SetQueIdMapPidTidLcoreInfo(invalidQueId, num, num, num, num);
    DT_ASSERT_EQUAL(ret, -1);
    ret = KNET_SetQueIdMapPidTidLcoreInfo(validQueId, num, num, num, num);
    DT_ASSERT_EQUAL(ret, 0);
}

DTEST_CASE_F(DPDK_TELEMETRY, TEST_MaintainQueue2TidPid, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(rte_memzone_lookup, MockRteMemzoneLookup);
    int ret = KNET_MaintainQueue2TidPidMp(0);
    DT_ASSERT_EQUAL(ret, (int)KNET_OK);

    ret = KNET_MaintainQueue2TidPidMp(QUEUEID_EXCEED_MAX_QUEUENUM);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    Mock->Delete(rte_memzone_lookup);
    DeleteMock(Mock);
}

static bool IsFdHijackMook(int fd)
{
    return true;
}


DTEST_CASE_F(DPDK_TELEMETRY, TEST_KnetTelemetryGetFdCountCallback, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    KNET_DpTelemetryHooks dpTelemetryhooks = {hook1, hook2, hook3, hook4, hook5};
    KNET_DpTelemetryHookReg(dpTelemetryhooks);
    struct rte_tel_data data = {};
    Mock->Create(getpid, TEST_GetFuncRetPositive(1));
    Mock->Create(rte_tel_data_string, TEST_GetFuncRetPositive(0));
    Mock->Create(sprintf_s, TEST_GetFuncRetPositive(0));
    int ret = KnetTelemetryGetFdCountCallback("/knet/stack/fd_count", "1 tcp", (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_OK);
    Mock->Delete(getpid);
    Mock->Delete(sprintf_s);
    Mock->Delete(rte_tel_data_string);
    DeleteMock(Mock);
}

DTEST_CASE_F(DPDK_TELEMETRY, TEST_KnetTelemetryGetFdCountCallbackMp, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    struct rte_tel_data data = {};
    Mock->Create(rte_memzone_lookup, MockRteMemzoneLookup);
    Mock->Create(rte_tel_data_string, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetWaitAllSlavePorcessHandle, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetHandleTimeout, TEST_GetFuncRetPositive(0));
    int ret = KnetTelemetryGetFdCountCallbackMp("/knet/stack/fd_count", "12345 tcp", (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_OK);
    Mock->Delete(rte_tel_data_string);
    Mock->Delete(rte_memzone_lookup);
    Mock->Delete(KnetWaitAllSlavePorcessHandle);
    Mock->Delete(KnetHandleTimeout);
    DeleteMock(Mock);
}

DTEST_CASE_F(DPDK_TELEMETRY, TEST_KnetTelemetryGetNetStatCallBack, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    struct rte_tel_data data = {};
    KNET_DpTelemetryHooks dpTelemetryhooks = {hook1, hook2, hook3, hook4};
    KNET_DpTelemetryHookReg(dpTelemetryhooks);
    Mock->Create(getpid, TEST_GetFuncRetPositive(1));
    Mock->Create(KNET_FdMaxGet, TEST_GetFuncRetPositive(1));
    Mock->Create(KNET_IsFdHijack, IsFdHijackMook);
    Mock->Create(KNET_GetFdType, TEST_GetFuncRetPositive(1));
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(1));
    Mock->Create(KnetGetTidByWorkerId, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_tel_data_add_dict_string, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_tel_data_add_dict_int, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_tel_data_add_dict_container, MockTelDataAddDictContainer);

    int ret = KnetTelemetryGetNetStatCallback("/knet/stack/net_stat", "1 0 1", (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_OK);

    Mock->Delete(getpid);
    Mock->Delete(KNET_IsFdHijack);
    Mock->Delete(KNET_GetFdType);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_FdMaxGet);
    Mock->Delete(KnetGetTidByWorkerId);
    Mock->Delete(rte_tel_data_add_dict_string);
    Mock->Delete(rte_tel_data_add_dict_int);
    Mock->Delete(rte_tel_data_add_dict_container);
    DeleteMock(Mock);
}

DTEST_CASE_F(DPDK_TELEMETRY, TEST_ProcessSocketState, NULL, NULL)
{
    KNET_SocketState socketState = {};
    struct rte_tel_data data = {};
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(rte_tel_data_start_dict, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_tel_data_add_dict_string, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_tel_data_add_dict_int, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_tel_data_add_dict_u64, TEST_GetFuncRetPositive(0));

    int ret = ProcessSocketState(socketState, (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_OK);

    Mock->Delete(rte_tel_data_start_dict);
    Mock->Delete(rte_tel_data_add_dict_string);
    Mock->Delete(rte_tel_data_add_dict_int);
    Mock->Delete(rte_tel_data_add_dict_u64);
    DeleteMock(Mock);
}

DTEST_CASE_F(DPDK_TELEMETRY, TEST_KnetTelemetryGetNetStatCallBackMp, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    struct rte_tel_data data = {};
    Mock->Create(rte_memzone_lookup, MockRteMemzoneLookup);
    Mock->Create(KnetWaitAllSlavePorcessHandle, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetHandleTimeout, TEST_GetFuncRetPositive(0));
 
    int ret = KnetTelemetryGetNetStatCallbackMp("/knet/stack/net_stat", "12345 0 1", (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);
 
    Mock->Delete(rte_memzone_lookup);
    Mock->Delete(KnetWaitAllSlavePorcessHandle);
    Mock->Delete(KnetHandleTimeout);
    DeleteMock(Mock);
}

DTEST_CASE_F(DPDK_TELEMETRY, TEST_KnetTelemetryGetSockInfoCallback, NULL, NULL)
{
    KTestMock* Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    struct rte_tel_data data = {};
    Mock->Create(getpid, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_IsFdHijack, IsFdHijackMook);
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_tel_data_start_dict, TEST_GetFuncRetNegative(1));

    int ret = KnetTelemetryGetSockInfoCallback("/knet/stack/socket_info", "1 1", (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    Mock->Delete(getpid);
    Mock->Delete(KNET_IsFdHijack);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(rte_tel_data_start_dict);
    DeleteMock(Mock);
}

struct rte_tel_data mock_data = {};
struct rte_tel_data *mock_rte_tel_data_alloc(void)
{
    return &mock_data;
}

/**
 * 模拟对各类SocketInfo的创建、处理、打印和释放
 * 覆盖CreateSockInfoAndAddReply，CreateInetSkInfoAndAddReply，
 * CreateTcpBaseInfoAndAddReply、CreateTcpTransInfoAndAddReply多个函数
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_ProcessSocketInfo, NULL, NULL)
{
    KTestMock* Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    struct rte_tel_data *data = rte_tel_data_alloc();
    DT_ASSERT_NOT_EQUAL(data, NULL);
    DP_SockDetails_t dpSockDetails = {};
    DP_TcpDetails_t tcpDetails = {};
    DP_TcpBaseDetails_t baseDetails = {};
    DP_TcpTransDetails_t transDetails = {};
    DP_InetDetails_t inetDetails = {};
    
    tcpDetails.transDetails = transDetails;
    tcpDetails.baseDetails = baseDetails;
    dpSockDetails.tcpDetails = tcpDetails;
    dpSockDetails.inetDetails = inetDetails;

    Mock->Create(rte_tel_data_start_dict, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_tel_data_add_dict_int, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_tel_data_add_dict_string, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_tel_data_add_dict_u64, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_tel_data_add_dict_container, MockTelDataAddDictContainer);

    int ret = ProcessSocketInfo(&dpSockDetails, data);
    DT_ASSERT_EQUAL(ret, (int)KNET_OK);

    Mock->Delete(rte_tel_data_start_dict);
    Mock->Delete(rte_tel_data_add_dict_int);
    Mock->Delete(rte_tel_data_add_dict_string);
    Mock->Delete(rte_tel_data_add_dict_u64);
    Mock->Delete(rte_tel_data_add_dict_container);
    rte_tel_data_free(data);
    DeleteMock(Mock);
}

/**
 * TEST_ProcessSockInfo 修改为 TEST_CreateSockInfoAndAddReply
 * 补充 telemetry sockInfo 失败测试分支
 * 打桩 rte_tel_data_start_dict，返回非0
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_CreateSockInfoAndAddReply, NULL, NULL)
{
    KTestMock* Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    struct rte_tel_data data = {};
    DP_SockDetails_t dpSockDetails = {};

    Mock->Create(rte_tel_data_start_dict, TEST_GetFuncRetPositive(1));

    int ret = CreateSockInfoAndAddReply(&dpSockDetails, (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    Mock->Delete(rte_tel_data_start_dict);
    DeleteMock(Mock);
}

/**
 * TEST_ProcessInetSkInfo 修改为 TEST_CreateInetSkInfoAndAddReply
 * 补充 telemetry InetSkInfo 失败测试分支
 * 打桩 rte_tel_data_start_dict，返回非0
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_CreateInetSkInfoAndAddReply, NULL, NULL)
{
    KTestMock* Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    struct rte_tel_data data = {};
    DP_SockDetails_t dpSockDetails = {};
    DP_InetDetails_t inetDetails = {};

    dpSockDetails.inetDetails = inetDetails;
    Mock->Create(rte_tel_data_start_dict, TEST_GetFuncRetPositive(0));

    int ret = CreateInetSkInfoAndAddReply(&dpSockDetails, (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    Mock->Delete(rte_tel_data_start_dict);
    DeleteMock(Mock);
}

/**
 * TEST_ProcessTcpBaseInfo 修改为 TEST_CreateTcpBaseInfoAndAddReply
 * 补充 telemetry BaseInfo 失败测试分支
 * 打桩 rte_tel_data_start_dict，返回非0
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_CreateTcpBaseInfoAndAddReply, NULL, NULL)
{
    KTestMock* Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    struct rte_tel_data data = {};
    DP_SockDetails_t dpSockDetails = {};
    DP_TcpDetails_t tcpDetails = {};
    DP_TcpBaseDetails_t baseDetails = {};

    tcpDetails.baseDetails = baseDetails;
    dpSockDetails.tcpDetails = tcpDetails;
    Mock->Create(rte_tel_data_start_dict, TEST_GetFuncRetPositive(0));

    int ret = CreateTcpBaseInfoAndAddReply(&dpSockDetails, (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    Mock->Delete(rte_tel_data_start_dict);
    DeleteMock(Mock);
}

/**
 * TEST_ProcessTcpTransInfo 修改为 TEST_CreateTcpTransInfoAndAddReply
 * 补充 telemetry TcpTransInfo 失败测试分支
 * 打桩 rte_tel_data_start_dict，返回非0
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_CreateTcpTransInfoAndAddReply, NULL, NULL)
{
    KTestMock* Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    struct rte_tel_data data = {};
    DP_SockDetails_t dpSockDetails = {};
    DP_TcpDetails_t tcpDetails = {};
    DP_TcpTransDetails_t transDetails = {};

    tcpDetails.transDetails = transDetails;
    dpSockDetails.tcpDetails = tcpDetails;
    Mock->Create(rte_tel_data_start_dict, TEST_GetFuncRetPositive(0));

    int ret = CreateTcpTransInfoAndAddReply(&dpSockDetails, (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    Mock->Delete(rte_tel_data_start_dict);
    DeleteMock(Mock);
}

DTEST_CASE_F(DPDK_TELEMETRY, TEST_KnetTelemetryGetSockInfoCallbackMp, NULL, NULL)
{
    KTestMock* Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    struct rte_tel_data data = {};
    Mock->Create(rte_memzone_lookup, MockRteMemzoneLookup);
    Mock->Create(KnetWaitAllSlavePorcessHandle, TEST_GetFuncRetPositive(0));
 
    int ret = KnetTelemetryGetSockInfoCallbackMp("/knet/stack/socket_info", "12345 1", (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);
 
    Mock->Delete(rte_memzone_lookup);
    Mock->Delete(KnetWaitAllSlavePorcessHandle);
    DeleteMock(Mock);
}


/**
 * 输入：workerId为1
 * 打桩：打桩KNET_GetQueIdMapPidTidLcoreInfo，返回workerId为1
 * 期望：workerId正确，返回0
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_KnetGetTidByWorkerId, NULL, NULL)
{
    KTestMock* Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    Mock->Create(KNET_GetQueIdMapPidTidLcoreInfo, MockKnetGetQueIdMapPidTidLcoreInfo);

    uint32_t workerId = 1;
    uint32_t tid[1] = {};

    int ret = KnetGetTidByWorkerId(workerId, tid);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KNET_GetCfg);
    Mock->Delete(KNET_GetQueIdMapPidTidLcoreInfo);
    DeleteMock(Mock);
}

/* ========== 新增用例: 覆盖 knet_telemetry_debug.c 未覆盖函数 ========== */

static void MockKnetUsleep(uint64_t usec) { (void)usec; }
static int MockIsQueueIdUsedQ0Q2(int queId) { return (queId == 0 || queId == 2) ? 1 : 0; }
static int MockIsQueueIdUsedQ0(int queId) { return (queId == 0) ? 1 : 0; }
static KNET_QueIdMapPidTid_t g_mockQueData = {};
static KNET_QueIdMapPidTid_t* MockGetQueIdMapStatic(void) { return &g_mockQueData; }
/* malloc版本: 返回动态分配内存, MockTelDataAddDictContainer可安全free */
static struct rte_tel_data* MockRteTelDataAllocMalloc(void)
{
    return (struct rte_tel_data *)calloc(1, sizeof(struct rte_tel_data));
}

/**
 * @brief ParseTelemetryParams: 成功解析/NULL输入/非法数字/参数过多
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_ParseTelemetryParams, NULL, NULL)
{
    uint32_t paramsArr[4] = {0};

    /* NULL输入 */
    int ret = ParseTelemetryParams(NULL, paramsArr, 4);
    DT_ASSERT_EQUAL(ret, -1);

    /* 成功解析2个参数 */
    ret = ParseTelemetryParams("1 2", paramsArr, 4);
    DT_ASSERT_EQUAL(ret, 2);
    DT_ASSERT_EQUAL(paramsArr[0], 1);
    DT_ASSERT_EQUAL(paramsArr[1], 2);

    /* 非法数字 */
    ret = ParseTelemetryParams("abc 2", paramsArr, 4);
    DT_ASSERT_EQUAL(ret, -1);

    /* 参数过多(maxCount=1, 但给了2个) */
    ret = ParseTelemetryParams("1 2", paramsArr, 1);
    DT_ASSERT_EQUAL(ret, -1);

    /* maxCount <= 0 */
    ret = ParseTelemetryParams("1", paramsArr, 0);
    DT_ASSERT_EQUAL(ret, -1);

    /* paramsArr为NULL */
    ret = ParseTelemetryParams("1", NULL, 4);
    DT_ASSERT_EQUAL(ret, -1);
}

/**
 * @brief KnetGetQueIdByPid: 找到/未找到
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_KnetGetQueIdByPid, NULL, NULL)
{
    KNET_TelemetryInfo telemetryInfo = {};
    telemetryInfo.pid[0] = 100;
    telemetryInfo.pid[5] = 200;
    telemetryInfo.pid[10] = 300;

    /* 找到pid=200 -> queId=5 */
    int queId = KnetGetQueIdByPid(200, &telemetryInfo);
    DT_ASSERT_EQUAL(queId, 5);

    /* 找到pid=100 -> queId=0 */
    queId = KnetGetQueIdByPid(100, &telemetryInfo);
    DT_ASSERT_EQUAL(queId, 0);

    /* 未找到pid=999 -> queId=-1 */
    queId = KnetGetQueIdByPid(999, &telemetryInfo);
    DT_ASSERT_EQUAL(queId, -1);
}

/**
 * @brief KnetHandleTimeout: msgReady=0直接返回OK, msgReady=1超时返回错误
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_KnetHandleTimeout, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    /* mock KNET_Usleep 避免实际睡眠 */
    Mock->Create(KNET_Usleep, MockKnetUsleep);

    KNET_TelemetryInfo telemetryInfo = {};

    /* msgReady[0]=0 -> while不执行, 直接返回OK */
    telemetryInfo.msgReady[0] = 0;
    int ret = KnetHandleTimeout(&telemetryInfo, 0);
    DT_ASSERT_EQUAL(ret, (int)KNET_OK);

    /* msgReady[1]=1 -> while执行10次后超时, 返回错误 */
    telemetryInfo.msgReady[1] = 1;
    ret = KnetHandleTimeout(&telemetryInfo, 1);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    Mock->Delete(KNET_Usleep);
    DeleteMock(Mock);
}

/**
 * @brief KnetUpdateSlaveProcessPidInfo: 更新队列映射
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_KnetUpdateSlaveProcessPidInfo, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    /* mock KNET_IsQueueIdUsed: queue 0和2返回true, 其他false */
    Mock->Create(KNET_IsQueueIdUsed, MockIsQueueIdUsedQ0Q2);

    KNET_TelemetryInfo telemetryInfo = {};
    KnetUpdateSlaveProcessPidInfo(&telemetryInfo);
    DT_ASSERT_EQUAL(telemetryInfo.telemetryType, KNET_TELEMETRY_UPDATE_QUE_INFO);
    DT_ASSERT_EQUAL(telemetryInfo.msgReady[0], 1);
    DT_ASSERT_EQUAL(telemetryInfo.msgReady[2], 1);
    DT_ASSERT_EQUAL(telemetryInfo.msgReady[1], 0);

    Mock->Delete(KNET_IsQueueIdUsed);
    DeleteMock(Mock);
}

/**
 * @brief KnetWaitAllSlavePorcessHandle: 无队列使用/成功/失败
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_KnetWaitAllSlavePorcessHandle, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KNET_Usleep, MockKnetUsleep);

    /* 所有队列都未使用 -> 直接返回OK */
    Mock->Create(KNET_IsQueueIdUsed, TEST_GetFuncRetPositive(0));
    KNET_TelemetryInfo telemetryInfo = {};
    int ret = KnetWaitAllSlavePorcessHandle(&telemetryInfo);
    DT_ASSERT_EQUAL(ret, (int)KNET_OK);

    Mock->Delete(KNET_IsQueueIdUsed);

    /* queue 0使用, msgReady[0]=0 -> 成功 */
    Mock->Create(KNET_IsQueueIdUsed, MockIsQueueIdUsedQ0);
    telemetryInfo.msgReady[0] = 0;
    ret = KnetWaitAllSlavePorcessHandle(&telemetryInfo);
    DT_ASSERT_EQUAL(ret, (int)KNET_OK);

    Mock->Delete(KNET_IsQueueIdUsed);

    /* queue 0使用, msgReady[0]=1 -> 超时失败 */
    Mock->Create(KNET_IsQueueIdUsed, MockIsQueueIdUsedQ0);
    telemetryInfo.msgReady[0] = 1;
    ret = KnetWaitAllSlavePorcessHandle(&telemetryInfo);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    Mock->Delete(KNET_IsQueueIdUsed);
    Mock->Delete(KNET_Usleep);
    DeleteMock(Mock);
}

/**
 * @brief KnetGetTidByWorkerId: workerId未找到失败路径
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_KnetGetTidByWorkerIdFail, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    /* MockKnetGetQueIdMapPidTidLcoreInfo返回workerId=1, 查找workerId=999不会匹配 */
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    Mock->Create(KNET_GetQueIdMapPidTidLcoreInfo, MockKnetGetQueIdMapPidTidLcoreInfo);

    uint32_t tid = 0;
    int ret = KnetGetTidByWorkerId(999, &tid);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    Mock->Delete(KNET_GetCfg);
    Mock->Delete(KNET_GetQueIdMapPidTidLcoreInfo);
    DeleteMock(Mock);
}

/**
 * @brief AddPidTid: queId超限/tid无效/成功路径
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_AddPidTid, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    struct rte_tel_data data = {};

    /* queId >= MAX_QUEUE_NUM -> 返回错误 */
    int ret = AddPidTid((struct rte_tel_data *)&data, QUEUEID_EXCEED_MAX_QUEUENUM);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    /* tid=INVALID_TID(0) -> 返回OK但不添加 */
    (void)memset_s(&g_mockQueData, sizeof(g_mockQueData), 0, sizeof(g_mockQueData));
    g_mockQueData.tid = 0; /* INVALID_TID */
    Mock->Create(KNET_GetQueIdMapPidTidLcoreInfo, MockGetQueIdMapStatic);
    ret = AddPidTid((struct rte_tel_data *)&data, 0);
    DT_ASSERT_EQUAL(ret, (int)KNET_OK);

    /* tid非0 -> 成功路径(使用malloc版alloc, MockTelDataAddDictContainer可安全free) */
    g_mockQueData.tid = 1;
    g_mockQueData.pid = 100;
    g_mockQueData.lcoreId = 2;
    g_mockQueData.workerId = 1;
    Mock->Create(rte_tel_data_alloc, MockRteTelDataAllocMalloc);
    Mock->Create(rte_tel_data_start_dict, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_tel_data_add_dict_u64, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_tel_data_add_dict_container, MockTelDataAddDictContainer);
    ret = AddPidTid((struct rte_tel_data *)&data, 0);
    DT_ASSERT_EQUAL(ret, (int)KNET_OK);

    Mock->Delete(KNET_GetQueIdMapPidTidLcoreInfo);
    Mock->Delete(rte_tel_data_alloc);
    Mock->Delete(rte_tel_data_start_dict);
    Mock->Delete(rte_tel_data_add_dict_u64);
    Mock->Delete(rte_tel_data_add_dict_container);
    DeleteMock(Mock);
}

/**
 * @brief KnetTelemetryQueIdMapPidTidCallback: data为NULL/成功路径
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_KnetTelemetryQueIdMapPidTidCallback, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    /* data为NULL -> 返回错误 */
    int ret = KnetTelemetryQueIdMapPidTidCallback("/knet/queue", NULL, NULL);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    /* params不为NULL -> 返回错误 */
    struct rte_tel_data data = {};
    ret = KnetTelemetryQueIdMapPidTidCallback("/knet/queue", "1", (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    /* 成功路径: data非NULL, params=NULL, 使用malloc版alloc */
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    Mock->Create(rte_tel_data_alloc, MockRteTelDataAllocMalloc);
    Mock->Create(rte_tel_data_start_dict, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_GetQueIdMapPidTidLcoreInfo, MockKnetGetQueIdMapPidTidLcoreInfo);
    Mock->Create(rte_tel_data_add_dict_u64, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_tel_data_add_dict_container, MockTelDataAddDictContainer);

    ret = KnetTelemetryQueIdMapPidTidCallback("/knet/queue", NULL, (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_OK);

    Mock->Delete(KNET_GetCfg);
    Mock->Delete(rte_tel_data_alloc);
    Mock->Delete(rte_tel_data_start_dict);
    Mock->Delete(KNET_GetQueIdMapPidTidLcoreInfo);
    Mock->Delete(rte_tel_data_add_dict_u64);
    Mock->Delete(rte_tel_data_add_dict_container);
    DeleteMock(Mock);
}

/**
 * @brief CheckAddContainerToDict: rte_tel_data_add_dict_container返回非0时的错误路径
 */
static int MockAddDictContainerErr(struct rte_tel_data *d, const char *name,
                                   struct rte_tel_data *val, int keep)
{
    (void)d; (void)name; (void)val; (void)keep;
    return 1;
}

DTEST_CASE_F(DPDK_TELEMETRY, TEST_CheckAddContainerToDictErr, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    struct rte_tel_data data = {};
    struct rte_tel_data value = {};

    /* mock add_dict_container 返回1(失败) -> 释放value并返回-1 */
    Mock->Create(rte_tel_data_add_dict_container, MockAddDictContainerErr);
    Mock->Create(rte_tel_data_free, TEST_GetFuncRetPositive(0));

    int ret = CheckAddContainerToDict((struct rte_tel_data *)&data, "test", (struct rte_tel_data *)&value);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(rte_tel_data_add_dict_container);
    Mock->Delete(rte_tel_data_free);
    DeleteMock(Mock);
}

/**
 * @brief KnetTelemetryFlowTableCallback: 参数解析失败/成功路径
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_KnetTelemetryFlowTableCallback, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    struct rte_tel_data data = {};

    /* data为NULL -> 返回错误 */
    int ret = KnetTelemetryFlowTableCallback("/knet/flow_table", NULL, NULL);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    /* 参数格式错误(只有一个参数) -> ParseFlowTableParams失败 */
    ret = KnetTelemetryFlowTableCallback("/knet/flow_table", "1", (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    /* 成功路径: 参数"0 10"(startIndex=0, count=10) */
    Mock->Create(rte_tel_data_start_dict, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_ProcessFlowTable, TEST_GetFuncRetPositive(0));

    ret = KnetTelemetryFlowTableCallback("/knet/flow_table", "0 10", (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_OK);

    /* count超过MAX(256) -> 失败 */
    ret = KnetTelemetryFlowTableCallback("/knet/flow_table", "0 257", (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    Mock->Delete(rte_tel_data_start_dict);
    Mock->Delete(KNET_ProcessFlowTable);
    DeleteMock(Mock);
}

/**
 * @brief KnetTelemetryStatisticCallback: 成功路径(单进程模式)
 *        覆盖 GetStatTypeFromString/ValidateParams/HandleTelemetryHook/ProcessJsonData
 */
static void MockShowStatisticsHook(DP_StatType_t type, int pid, uint32_t outputTo)
{
    (void)type; (void)pid; (void)outputTo;
    /* 设置g_knetDebugOutput为有效JSON */
    const char *json = "{\"key1\":\"123\",\"key2\":\"456\"}";
    (void)snprintf_s(g_knetDebugOutput, MAX_OUTPUT_LEN, strlen(json), "%s", json);
}

static int MockSocketCountGetHook(int socketType)
{
    (void)socketType;
    return 0;
}

static int MockGetSocketStateHook(int fd, DP_SocketState_t *state)
{
    (void)fd; (void)state;
    return 0;
}

static int MockGetSocketDetailsHook(int fd, DP_SockDetails_t *details)
{
    (void)fd; (void)details;
    return 0;
}

static int MockGetEpollDetailsHook(int epollfd, DP_EpollDetails_t *details, int size, int *workerId)
{
    (void)epollfd; (void)details; (void)size; (void)workerId;
    return 0;
}

static union KNET_CfgValue g_cfgSingle;
static union KNET_CfgValue *MockKnetGetCfgSingle(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfgSingle, sizeof(g_cfgSingle), 0, sizeof(g_cfgSingle));
    if (key == CONF_COMMON_MODE) {
        g_cfgSingle.intValue = 0; /* KNET_RUN_MODE_SINGLE */
    } else {
        g_cfgSingle.intValue = 1;
    }
    return &g_cfgSingle;
}

DTEST_CASE_F(DPDK_TELEMETRY, TEST_KnetTelemetryStatisticCallback, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    /* 注册hooks */
    KNET_DpTelemetryHooks hooks = {0};
    hooks.dpShowStatisticsHook = MockShowStatisticsHook;
    hooks.dpSocketCountGetHook = MockSocketCountGetHook;
    hooks.dpGetSocketStateHook = MockGetSocketStateHook;
    hooks.dpGetSocketDetailsHook = MockGetSocketDetailsHook;
    hooks.dpGetEpollDetailsHook = MockGetEpollDetailsHook;
    int ret = KNET_DpTelemetryHookReg(hooks);
    DT_ASSERT_EQUAL(ret, (int)KNET_OK);

    /* mock单进程模式 + rte_tel_data函数 */
    Mock->Create(KNET_GetCfg, MockKnetGetCfgSingle);
    Mock->Create(rte_tel_data_start_dict, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_tel_data_add_dict_u64, TEST_GetFuncRetPositive(0));

    struct rte_tel_data data = {};
    /* cmd="/knet/stack/tcp_stat" -> type=DP_STAT_TCP, params=NULL -> ValidateParams OK */
    ret = KnetTelemetryStatisticCallback("/knet/stack/tcp_stat", NULL, (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_OK);

    /* cmd无效 -> GetStatTypeFromString返回DP_STAT_MAX -> 失败 */
    ret = KnetTelemetryStatisticCallback("/invalid/cmd", NULL, (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    /* params为非NULL但非数字 -> ValidateParams失败 */
    ret = KnetTelemetryStatisticCallback("/knet/stack/tcp_stat", "abc", (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    Mock->Delete(KNET_GetCfg);
    Mock->Delete(rte_tel_data_start_dict);
    Mock->Delete(rte_tel_data_add_dict_u64);
    DeleteMock(Mock);
}