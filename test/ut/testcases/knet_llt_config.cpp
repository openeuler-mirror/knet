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

#include "securec.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>

#include "rte_timer.h"
#include "rte_ethdev.h"

#include "knet_log.h"
#include "knet_config_hw_scan.h"
#include "knet_config_core_queue.h"

extern "C" {
#include "knet_config_setter.h"
}

#include "knet_utils.h"
#include "knet_rpc.h"
#include "knet_config_rpc.h"
#include "knet_config.h"
#include "common.h"
#include "mock.h"

#define KNET_RIGHT_PORT_STEP 512
extern "C" {
void SetPortStepSize(void);
char *GetKnetCfgContent(const char *fileName);
void DelKnetCfgContent(char *cfgCtx);
extern char *g_primaryCfg;
int CtrlVcpuCheck(void);
void SetCfgDpNewPort(void);
int CheckLocalPort(void);
int SetMultiModeLocalCfgValue(enum KNET_ProcType procType);
int KnetHwOffloadCheck(void);
int IsNeedStopQueue(void);
int LoadCfgFromRpc(void);
int SendConfRpcHandler(int clientId, struct KNET_RpcMessage *knetRpcRequest,
    struct KNET_RpcMessage *knetRpcResponse);
int PhraseRangeStr(char *substr);
int CheckRangeStr(int leftNum, int rightNum);
int CheckEpollData(char* endptr, const char* str, uint64_t result);
int GetnicNeedId(void *hv, const char *interfaceName, int type);
int CheckCoreNum(enum KNET_ProcType procType);
int CheckQueueNum(void);
int CheckCfgValid(void);
}

static union KNET_CfgValue g_cfg = {.intValue = 1};
static union KNET_CfgValue *MockKnetGetCfg(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    g_cfg.intValue = 1;
    return &g_cfg;
}

static union KNET_CfgValue *MockKnetKernelGetCfg(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    if (key == CONF_HW_BIFUR_ENABLE) {
        g_cfg.intValue = KERNEL_FORWARD_ENABLE;
    } else {
        g_cfg.intValue = 1;
    }

    return &g_cfg;
}

char *MockRealPath(char *path, char resPath)
{
    return NULL;
}

DTEST_CASE_F(KNET_CONFIG, TEST_CONFIG_INIT_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KnetCheckCompatibleNic, TEST_GetFuncRetPositive(0));
    Mock->Create(GetnicNeedId, TEST_GetFuncRetPositive(0));
    int ret = KNET_InitCfg(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(GetnicNeedId);
    Mock->Delete(KnetCheckCompatibleNic);

    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_CONFIG, TEST_KNET_CTRL_VCPU_CHECK, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KNET_CpuDetected, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_FindCoreInList, TEST_GetFuncRetNegative(1));
    int ret = CtrlVcpuCheck();
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(KNET_FindCoreInList);
    Mock->Delete(KNET_CpuDetected);

    Mock->Create(KNET_CpuDetected, TEST_GetFuncRetNegative(1));
    ret = CtrlVcpuCheck();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_CpuDetected);

    Mock->Create(KNET_CpuDetected, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_FindCoreInList, TEST_GetFuncRetPositive(1));
    ret = CtrlVcpuCheck();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_FindCoreInList);
    Mock->Delete(KNET_CpuDetected);
    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_CONFIG, TEST_KNET_SET_PORT_STEP, NULL, NULL)
{
    SetPortStepSize();
    DT_ASSERT_EQUAL(KNET_GetCfg(CONF_INNER_PORT_STEP)->intValue, KNET_RIGHT_PORT_STEP);
}

DTEST_CASE_F(KNET_CONFIG, TEST_KNET_SET_CFG_TCP_NEW_PORT, NULL, NULL)
{
    SetCfgDpNewPort();
    unsigned int minPort = (unsigned)KNET_GetCfg(CONF_TCP_MIN_PORT)->intValue;
    unsigned int portStep = (unsigned)KNET_GetCfg(CONF_INNER_PORT_STEP)->intValue;
    unsigned int minPortLeftBoundary = minPort & ~(portStep - 1);
    DT_ASSERT_EQUAL(minPort, minPortLeftBoundary);

    unsigned int maxPort = (unsigned)KNET_GetCfg(CONF_TCP_MAX_PORT)->intValue;
    unsigned int maxPortLeftBoundary = maxPort & ~(portStep - 1);
    DT_ASSERT_EQUAL(maxPort, maxPortLeftBoundary);
}

DTEST_CASE_F(KNET_CONFIG, TEST_KNET_CTRL_CHECK_LOCAL_PORT, NULL, NULL)
{
    int ret = CheckLocalPort();
    DT_ASSERT_EQUAL(ret, -1);
}

DTEST_CASE_F(KNET_CONFIG, TEST_KNET_SetMultiModeLocalCfgValue, NULL, NULL)
{
    int ret = SetMultiModeLocalCfgValue(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, 0);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KnetGetQueueIdFromPrimary, TEST_GetFuncRetNegative(1));
    ret = SetMultiModeLocalCfgValue(KNET_PROC_TYPE_SECONDARY);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KnetGetQueueIdFromPrimary);

    Mock->Create(KnetGetQueueIdFromPrimary, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetGetCoreByQueueId, TEST_GetFuncRetNegative(1));
    ret = SetMultiModeLocalCfgValue(KNET_PROC_TYPE_SECONDARY);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KnetGetCoreByQueueId);

    Mock->Create(KnetGetCoreByQueueId, TEST_GetFuncRetPositive(0));
    Mock->Create(sprintf_s, TEST_GetFuncRetNegative(1));
    ret = SetMultiModeLocalCfgValue(KNET_PROC_TYPE_SECONDARY);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(sprintf_s);

    Mock->Create(sprintf_s, TEST_GetFuncRetPositive(0));
    ret = SetMultiModeLocalCfgValue(KNET_PROC_TYPE_SECONDARY);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(sprintf_s);

    Mock->Delete(KnetGetCoreByQueueId);
    Mock->Delete(KnetGetQueueIdFromPrimary);

    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_CONFIG, TEST_KNET_KnetHwOffloadCheck, NULL, NULL)
{
    int ret = KnetHwOffloadCheck();
    DT_ASSERT_EQUAL(ret, 0);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    ret = KnetHwOffloadCheck();
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(KNET_GetCfg);

    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_CONFIG, TEST_KNET_IsNeedStopQueue, NULL, NULL)
{
    int ret = IsNeedStopQueue();
    DT_ASSERT_EQUAL(ret, KNET_NOT_STOP_QUEUE);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KNET_GetCfg, MockKnetKernelGetCfg);
    ret = IsNeedStopQueue();
    DT_ASSERT_EQUAL(ret, KNET_STOP_QUEUE);
    Mock->Delete(KNET_GetCfg);

    DeleteMock(Mock);
}

char *MockGetKnetCfgContent(const char *fileName)
{
    return NULL;
}
char *MockGetKnetCfgContentNotNull(const char *fileName)
{
    return "knet_comm.conf";
}

DTEST_CASE_F(KNET_CONFIG, TEST_KNET_LoadCfgFromRpc, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(GetKnetCfgContent, MockGetKnetCfgContent);
    int ret = LoadCfgFromRpc();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(GetKnetCfgContent);

    Mock->Create(GetKnetCfgContent, MockGetKnetCfgContentNotNull);
    Mock->Create(KNET_RpcCall, TEST_GetFuncRetPositive(0));
    Mock->Create(memcmp, TEST_GetFuncRetPositive(0));
    Mock->Create(DelKnetCfgContent, TEST_GetFuncRetPositive(0));
    ret = LoadCfgFromRpc();
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(memcmp);
    Mock->Delete(KNET_RpcCall);
    Mock->Delete(GetKnetCfgContent);
    Mock->Delete(DelKnetCfgContent);

    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_CONFIG, TEST_KNET_SendConfRpcHandler, NULL, NULL)
{
    int clientId = 0;
    struct KNET_RpcMessage knetRpcRequest;
    struct KNET_RpcMessage knetRpcReponse;
    g_primaryCfg = "knet_comm.conf";
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(memcpy_s, TEST_GetFuncRetPositive(0));

    int ret = SendConfRpcHandler(clientId, &knetRpcRequest, &knetRpcReponse);
    DT_ASSERT_EQUAL(ret, 0);
    free(knetRpcReponse.variableLenData);

    g_primaryCfg = NULL;
    ret = SendConfRpcHandler(clientId, &knetRpcRequest, &knetRpcReponse);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(memcpy_s);

    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_CONFIG, TEST_KNET_PhraseRangeStr, NULL, NULL)
{
    char *substr = "0-1";
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(strcpy_s, TEST_GetFuncRetNegative(1));
    int ret = PhraseRangeStr(substr);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(strcpy_s);

    Mock->Create(KnetCoreListAppend, TEST_GetFuncRetPositive(0));
    ret = PhraseRangeStr(substr);
    DT_ASSERT_EQUAL(ret, 0);

    substr = "1-1";
    ret = PhraseRangeStr(substr);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KnetCoreListAppend);

    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_CONFIG, TEST_CheckEpollDataStr, NULL, NULL)
{
    char *str1 = "-123";
    char* endptr;
    errno = 0;
    /* 转10进制 */
    int base = 10;
    uint64_t result = strtoull(str1, &endptr, base);
    DT_ASSERT_NOT_EQUAL(errno, ERANGE);
    int ret = CheckEpollData(endptr, str1, result);
    DT_ASSERT_EQUAL(ret, -1);
    
    char *str2 = " -123";
    errno = 0;
    result = strtoull(str2, &endptr, base);
    DT_ASSERT_NOT_EQUAL(errno, ERANGE);
    ret = CheckEpollData(endptr, str2, result);
    DT_ASSERT_EQUAL(ret, -1);

    char *str3 = "123.2";
    errno = 0;
    result = strtoull(str3, &endptr, base);
    DT_ASSERT_NOT_EQUAL(errno, ERANGE);
    ret = CheckEpollData(endptr, str3, result);
    DT_ASSERT_EQUAL(ret, -1);

    char *str4 = "123123123125415234523412424352412312423411231224312";
    errno = 0;
    result = strtoull(str4, &endptr, base);
    DT_ASSERT_EQUAL(errno, ERANGE);
    ret = CheckEpollData(endptr, str4, result);
    DT_ASSERT_EQUAL(ret, -1);

    char *str5 = "123123456";
    errno = 0;
    result = strtoull(str5, &endptr, base);
    DT_ASSERT_NOT_EQUAL(errno, ERANGE);
    ret = CheckEpollData(endptr, str5, result);
    DT_ASSERT_EQUAL(ret, 0);
}

/* ===== 新增覆盖率测试 ===== */

/* CheckQueueNum: workerNum > queueNum 在单进程模式下 */
static int g_mockWorkerNum = 1;
static int g_mockQueueNum = 1;
static int g_mockMode = 1;
static int g_mockCothread = 0;
static int g_mockHwLro = 0;
static int g_mockHwTso = 0;
static int g_mockHwChecksum = 1;

static union KNET_CfgValue *MockKnetGetCfgMulti(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    switch (key) {
        case CONF_TCP_MAX_WORKER_NUM:
            g_cfg.intValue = g_mockWorkerNum;
            break;
        case CONF_DPDK_QUEUE_NUM:
            g_cfg.intValue = g_mockQueueNum;
            break;
        case CONF_COMMON_MODE:
            g_cfg.intValue = g_mockMode;
            break;
        case CONF_COMMON_COTHREAD:
            g_cfg.intValue = g_mockCothread;
            break;
        case CONF_HW_LRO:
            g_cfg.intValue = g_mockHwLro;
            break;
        case CONF_HW_TSO:
            g_cfg.intValue = g_mockHwTso;
            break;
        case CONF_HW_TCP_CHECKSUM:
            g_cfg.intValue = g_mockHwChecksum;
            break;
        default:
            g_cfg.intValue = 1;
            break;
    }
    return &g_cfg;
}

static int g_mockCoreNum = 1;
static int MockKnetGetCoreNum(void)
{
    return g_mockCoreNum;
}

/**
 * @brief CheckQueueNum: workerNum > queueNum
 */
DTEST_CASE_F(KNET_CONFIG, TEST_CONFIG_CHECK_QUEUE_NUM_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    g_mockWorkerNum = 10;
    g_mockQueueNum = 1;
    g_mockMode = KNET_RUN_MODE_SINGLE;
    Mock->Create(KNET_GetCfg, MockKnetGetCfgMulti);

    int ret = CheckQueueNum();
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

/**
 * @brief CheckCoreNum: workerNum != coreNum
 */
DTEST_CASE_F(KNET_CONFIG, TEST_CONFIG_CHECK_CORE_NUM_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    g_mockCothread = 0;
    g_mockWorkerNum = 10;
    g_mockCoreNum = 1;
    Mock->Create(KNET_GetCfg, MockKnetGetCfgMulti);
    Mock->Create(KnetGetCoreNum, MockKnetGetCoreNum);

    int ret = CheckCoreNum(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KnetGetCoreNum);
    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

/**
 * @brief KnetHwOffloadCheck: LRO/TSO enabled but checksum disabled
 */
DTEST_CASE_F(KNET_CONFIG, TEST_CONFIG_HW_OFFLOAD_CHECKSUM_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    g_mockHwLro = 1;
    g_mockHwTso = 0;
    g_mockHwChecksum = 0;
    Mock->Create(KNET_GetCfg, MockKnetGetCfgMulti);

    int ret = KnetHwOffloadCheck();
    DT_ASSERT_EQUAL(ret, -1);

    g_mockHwLro = 0;
    g_mockHwTso = 1;
    g_mockHwChecksum = 0;
    ret = KnetHwOffloadCheck();
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

/**
 * @brief CheckCoreNum: cothread enabled -> return 0
 */
DTEST_CASE_F(KNET_CONFIG, TEST_CONFIG_CHECK_CORE_NUM_COTHREAD, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    g_mockCothread = 1;
    Mock->Create(KNET_GetCfg, MockKnetGetCfgMulti);

    int ret = CheckCoreNum(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

/**
 * @brief GetKnetCfgContent: 文件不存在
 */
DTEST_CASE_F(KNET_CONFIG, TEST_CONFIG_GET_CFG_CONTENT_NO_FILE, NULL, NULL)
{
    char *ret = GetKnetCfgContent("/nonexistent/path/file.conf");
    DT_ASSERT_EQUAL(ret, NULL);
}

/**
 * @brief CheckQueueNum: cothread模式, queue/worker不整除
 */
DTEST_CASE_F(KNET_CONFIG, TEST_CONFIG_CHECK_QUEUE_NUM_COTHREAD_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    g_mockWorkerNum = 2;
    g_mockQueueNum = 3;
    g_mockMode = KNET_RUN_MODE_SINGLE;
    g_mockCothread = 1;
    Mock->Create(KNET_GetCfg, MockKnetGetCfgMulti);

    int ret = CheckQueueNum();
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

/**
 * @brief CheckCoreNum: secondary进程, coreNum=1
 */
DTEST_CASE_F(KNET_CONFIG, TEST_CONFIG_CHECK_CORE_NUM_SECONDARY, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    g_mockCothread = 0;
    g_mockWorkerNum = 1;
    g_mockCoreNum = 10;
    Mock->Create(KNET_GetCfg, MockKnetGetCfgMulti);
    Mock->Create(KnetGetCoreNum, MockKnetGetCoreNum);

    /* secondary进程时coreNum强制为1, workerNum=1, 应该匹配 */
    int ret = CheckCoreNum(KNET_PROC_TYPE_SECONDARY);
    DT_ASSERT_EQUAL(ret, 0);

    /* workerNum != 1 (coreNum for secondary) -> -1 */
    g_mockWorkerNum = 2;
    ret = CheckCoreNum(KNET_PROC_TYPE_SECONDARY);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KnetGetCoreNum);
    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

/* ===== knet_config_setter.c 新增覆盖率测试 ===== */

/**
 * @brief CheckEpollData: 各种无效输入路径
 */
DTEST_CASE_F(KNET_CONFIG, TEST_CONFIG_CHECK_EPOLL_DATA_INVALID, NULL, NULL)
{
    /* 1. endptr == str -> 无效格式 */
    char str[] = "abc";
    char *endptr = str;
    int ret = CheckEpollData(endptr, str, 0);
    DT_ASSERT_EQUAL(ret, -1);

    /* 2. *endptr != '\0' -> 无效格式 */
    char str2[] = "12abc";
    endptr = str2 + 2;  /* 指向 'a' */
    ret = CheckEpollData(endptr, str2, 12);
    DT_ASSERT_EQUAL(ret, -1);

    /* 3. 有效输入 */
    char str3[] = "12345";
    endptr = str3 + 5;  /* 指向 '\0' */
    ret = CheckEpollData(endptr, str3, 12345);
    DT_ASSERT_EQUAL(ret, 0);

    /* 4. 负号场景: snprintf结果与str不匹配 */
    char str4[] = "-1";
    char convertStr[32] = {0};
    snprintf(convertStr, sizeof(convertStr), "%llu", (uint64_t)1);
    /* convertStr = "1", str4 = "-1" -> strcmp不匹配 */
    endptr = str4 + 2;  /* 指向 '\0' */
    ret = CheckEpollData(endptr, str4, 1);
    DT_ASSERT_EQUAL(ret, -1);
}

/**
 * @brief Uint64Setter: ERANGE错误和无效字符串
 */
DTEST_CASE_F(KNET_CONFIG, TEST_CONFIG_UINT64_SETTER_INVALID, NULL, NULL)
{
    union KNET_CfgValue value = {0};
    union KnetCfgValidateParam param = {0};

    /* 1. 无效: 非数字字符串 */
    cJSON *json = cJSON_CreateString("abc");
    int ret = Uint64Setter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);
    cJSON_Delete(json);

    /* 2. 无效: 空字符串 */
    json = cJSON_CreateString("");
    ret = Uint64Setter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);
    cJSON_Delete(json);

    /* 3. 无效: 不是String类型 */
    json = cJSON_CreateNumber(100);
    ret = Uint64Setter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);
    cJSON_Delete(json);

    /* 4. 有效: 正常uint64值 */
    json = cJSON_CreateString("12345");
    ret = Uint64Setter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, 0);
    DT_ASSERT_EQUAL(value.uint64Value, 12345);
    cJSON_Delete(json);

    /* 5. NULL参数 */
    ret = Uint64Setter(NULL, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);
}

/**
 * @brief IntSetter: 超出范围、非整数、类型错误
 */
DTEST_CASE_F(KNET_CONFIG, TEST_CONFIG_INT_SETTER_INVALID, NULL, NULL)
{
    union KNET_CfgValue value = {0};
    union KnetCfgValidateParam param = {0};
    param.intValue.min = 0;
    param.intValue.max = 100;

    /* 1. 超出int32范围 */
    cJSON *json = cJSON_CreateNumber(2147483648.0);  /* INT32_MAX + 1 */
    int ret = IntSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);
    cJSON_Delete(json);

    /* 2. 非整数 */
    json = cJSON_CreateNumber(3.14);
    ret = IntSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);
    cJSON_Delete(json);

    /* 3. 超出范围 [0, 100] */
    json = cJSON_CreateNumber(200);
    ret = IntSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);
    cJSON_Delete(json);

    /* 4. 超出范围 - 负数 */
    json = cJSON_CreateNumber(-1);
    ret = IntSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);
    cJSON_Delete(json);

    /* 5. 类型错误 - 不是Number */
    json = cJSON_CreateString("100");
    ret = IntSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);
    cJSON_Delete(json);

    /* 6. NULL参数 */
    ret = IntSetter(NULL, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);

    /* 7. 有效值 */
    json = cJSON_CreateNumber(50);
    ret = IntSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, 0);
    DT_ASSERT_EQUAL(value.intValue, 50);
    cJSON_Delete(json);
}

/**
 * @brief StringSetter: regex匹配失败
 */
DTEST_CASE_F(KNET_CONFIG, TEST_CONFIG_STRING_SETTER_INVALID, NULL, NULL)
{
    union KNET_CfgValue value = {0};
    union KnetCfgValidateParam param = {0};
    (void)strcpy_s(param.pattern, sizeof(param.pattern), "^[a-z]+$");

    /* 1. 不匹配pattern */
    cJSON *json = cJSON_CreateString("ABC123");
    int ret = StringSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);
    cJSON_Delete(json);

    /* 2. 类型错误 */
    json = cJSON_CreateNumber(100);
    ret = StringSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);
    cJSON_Delete(json);

    /* 3. NULL参数 */
    ret = StringSetter(NULL, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);

    /* 4. 有效值 */
    json = cJSON_CreateString("abc");
    ret = StringSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, 0);
    cJSON_Delete(json);
}

/**
 * @brief IpSetter: 无效IP地址
 */
DTEST_CASE_F(KNET_CONFIG, TEST_CONFIG_IP_SETTER_INVALID, NULL, NULL)
{
    union KNET_CfgValue value = {0};
    union KnetCfgValidateParam param = {0};

    /* 1. 无效IP */
    cJSON *json = cJSON_CreateString("999.999.999.999");
    int ret = IpSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);
    cJSON_Delete(json);

    /* 2. 无效IP - 非IP格式 */
    json = cJSON_CreateString("not_an_ip");
    ret = IpSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);
    cJSON_Delete(json);

    /* 3. 类型错误 */
    json = cJSON_CreateNumber(100);
    ret = IpSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);
    cJSON_Delete(json);

    /* 4. NULL参数 */
    ret = IpSetter(NULL, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);

    /* 5. 有效IP */
    json = cJSON_CreateString("192.168.1.1");
    ret = IpSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, 0);
    cJSON_Delete(json);
}

/**
 * @brief NetMaskSetter: 无效子网掩码
 */
DTEST_CASE_F(KNET_CONFIG, TEST_CONFIG_NETMASK_SETTER_INVALID, NULL, NULL)
{
    union KNET_CfgValue value = {0};
    union KnetCfgValidateParam param = {0};

    /* 1. 无效地址格式 */
    cJSON *json = cJSON_CreateString("invalid");
    int ret = NetMaskSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);
    cJSON_Delete(json);

    /* 2. 无效子网掩码 - 非连续掩码 (如 255.0.255.0) */
    json = cJSON_CreateString("255.0.255.0");
    ret = NetMaskSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);
    cJSON_Delete(json);

    /* 3. 类型错误 */
    json = cJSON_CreateNumber(100);
    ret = NetMaskSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);
    cJSON_Delete(json);

    /* 4. NULL参数 */
    ret = NetMaskSetter(NULL, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);

    /* 5. 有效掩码 */
    json = cJSON_CreateString("255.255.255.0");
    ret = NetMaskSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, 0);
    cJSON_Delete(json);
}

/**
 * @brief MacSetter: 无效MAC地址
 */
DTEST_CASE_F(KNET_CONFIG, TEST_CONFIG_MAC_SETTER_INVALID, NULL, NULL)
{
    union KNET_CfgValue value = {0};
    union KnetCfgValidateParam param = {0};

    /* 1. 无效MAC */
    cJSON *json = cJSON_CreateString("zz:zz:zz:zz:zz:zz");
    int ret = MacSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);
    cJSON_Delete(json);

    /* 2. 太短的MAC */
    json = cJSON_CreateString("00:11:22:33:44");
    ret = MacSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);
    cJSON_Delete(json);

    /* 3. 类型错误 */
    json = cJSON_CreateNumber(100);
    ret = MacSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);
    cJSON_Delete(json);

    /* 4. NULL参数 */
    ret = MacSetter(NULL, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);

    /* 5. 有效MAC */
    json = cJSON_CreateString("00:11:22:33:44:55");
    ret = MacSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, 0);
    cJSON_Delete(json);
}

/**
 * @brief LogLevelSetter: 无效日志级别
 */
DTEST_CASE_F(KNET_CONFIG, TEST_CONFIG_LOGLEVEL_SETTER_INVALID, NULL, NULL)
{
    union KNET_CfgValue value = {0};
    union KnetCfgValidateParam param = {0};

    /* 1. 无效日志级别 */
    cJSON *json = cJSON_CreateString("VERBOSE");
    int ret = LogLevelSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);
    cJSON_Delete(json);

    /* 2. 类型错误 */
    json = cJSON_CreateNumber(100);
    ret = LogLevelSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);
    cJSON_Delete(json);

    /* 3. NULL参数 */
    ret = LogLevelSetter(NULL, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);

    /* 4. 有效值 - ERROR */
    json = cJSON_CreateString("error");
    ret = LogLevelSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, 0);
    cJSON_Delete(json);

    /* 5. 有效值 - DEBUG */
    json = cJSON_CreateString("DEBUG");
    ret = LogLevelSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, 0);
    cJSON_Delete(json);

    /* 6. 有效值 - INFO */
    json = cJSON_CreateString("info");
    ret = LogLevelSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, 0);
    cJSON_Delete(json);

    /* 7. 有效值 - WARNING */
    json = cJSON_CreateString("WARNING");
    ret = LogLevelSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, 0);
    cJSON_Delete(json);
}

/**
 * @brief BdfNumsSetter: 各种错误路径
 */
DTEST_CASE_F(KNET_CONFIG, TEST_CONFIG_BDF_SETTER_INVALID, NULL, NULL)
{
    union KNET_CfgValue value = {0};
    union KnetCfgValidateParam param = {0};

    /* 1. 类型错误 - 不是Array */
    cJSON *json = cJSON_CreateNumber(100);
    int ret = BdfNumsSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);
    cJSON_Delete(json);

    /* 2. 空数组 - bdfCount < 1 */
    json = cJSON_CreateArray();
    ret = BdfNumsSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);
    cJSON_Delete(json);

    /* 3. 超过最大数量 - bdfCount > MAX_BDF_COUNT(2) */
    json = cJSON_CreateArray();
    cJSON_AddItemToArray(json, cJSON_CreateString("0000:00:1f.0"));
    cJSON_AddItemToArray(json, cJSON_CreateString("0000:00:1f.1"));
    cJSON_AddItemToArray(json, cJSON_CreateString("0000:00:1f.2"));
    ret = BdfNumsSetter(json, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);
    cJSON_Delete(json);

    /* 4. NULL参数 */
    ret = BdfNumsSetter(NULL, &value, &param);
    DT_ASSERT_EQUAL(ret, -1);
}

/**
 * @brief CheckRangeStr: 各种无效输入
 */
DTEST_CASE_F(KNET_CONFIG, TEST_CONFIG_CHECK_RANGE_STR_INVALID, NULL, NULL)
{
    /* 1. leftNum < 0 */
    int ret = CheckRangeStr(-1, 10);
    DT_ASSERT_EQUAL(ret, -1);

    /* 2. rightNum < 0 */
    ret = CheckRangeStr(1, -1);
    DT_ASSERT_EQUAL(ret, -1);

    /* 3. leftNum >= rightNum */
    ret = CheckRangeStr(10, 10);
    DT_ASSERT_EQUAL(ret, -1);

    /* 4. rightNum >= MAX_CORE_NUM (320) */
    ret = CheckRangeStr(1, 320);
    DT_ASSERT_EQUAL(ret, -1);

    /* 5. 有效范围 */
    ret = CheckRangeStr(1, 10);
    DT_ASSERT_EQUAL(ret, 0);
}

/**
 * @brief PhraseRangeStr: 无效范围字符串
 */
DTEST_CASE_F(KNET_CONFIG, TEST_CONFIG_PHRASE_RANGE_STR_INVALID, NULL, NULL)
{
    /* 1. 没有连字符 */
    char substr1[] = "12345";
    int ret = PhraseRangeStr(substr1);
    DT_ASSERT_EQUAL(ret, -1);

    /* 2. 左侧无效数字 */
    char substr2[] = "abc-10";
    ret = PhraseRangeStr(substr2);
    DT_ASSERT_EQUAL(ret, -1);

    /* 3. 右侧无效数字 */
    char substr3[] = "1-abc";
    ret = PhraseRangeStr(substr3);
    DT_ASSERT_EQUAL(ret, -1);

    /* 4. 左大于右 */
    char substr4[] = "10-1";
    ret = PhraseRangeStr(substr4);
    DT_ASSERT_EQUAL(ret, -1);
}

/**
 * @brief CheckCfgValid: 各检查函数返回失败
 */
DTEST_CASE_F(KNET_CONFIG, TEST_CONFIG_CHECK_CFG_VALID_ERROR_PATHS, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    /* 1. CheckQueueNum returns -1 */
    Mock->Create(CheckQueueNum, TEST_GetFuncRetNegative(1));
    int ret = CheckCfgValid();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(CheckQueueNum);

    /* 2. CtrlVcpuCheck returns -1 */
    Mock->Create(CheckQueueNum, TEST_GetFuncRetPositive(0));
    Mock->Create(CtrlVcpuCheck, TEST_GetFuncRetNegative(1));
    ret = CheckCfgValid();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(CtrlVcpuCheck);
    Mock->Delete(CheckQueueNum);

    /* 3. KnetCheckCompatibleNic returns -1 */
    Mock->Create(CheckQueueNum, TEST_GetFuncRetPositive(0));
    Mock->Create(CtrlVcpuCheck, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetCheckCompatibleNic, TEST_GetFuncRetNegative(1));
    ret = CheckCfgValid();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KnetCheckCompatibleNic);
    Mock->Delete(CtrlVcpuCheck);
    Mock->Delete(CheckQueueNum);

    /* 4. KnetHwOffloadCheck returns -1 */
    Mock->Create(CheckQueueNum, TEST_GetFuncRetPositive(0));
    Mock->Create(CtrlVcpuCheck, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetCheckCompatibleNic, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetHwOffloadCheck, TEST_GetFuncRetNegative(1));
    ret = CheckCfgValid();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KnetHwOffloadCheck);
    Mock->Delete(KnetCheckCompatibleNic);
    Mock->Delete(CtrlVcpuCheck);
    Mock->Delete(CheckQueueNum);

    DeleteMock(Mock);
}

/**
 * @brief CtrlVcpuCheck: 重复vcpu id
 */
static union KNET_CfgValue g_ctrlVcpuCfg = {0};
static union KNET_CfgValue *MockKnetGetCfgCtrlVcpuDup(enum KNET_ConfKey key)
{
    (void)memset_s(&g_ctrlVcpuCfg, sizeof(g_ctrlVcpuCfg), 0, sizeof(g_ctrlVcpuCfg));
    switch (key) {
        case CONF_COMMON_CTRL_VCPU_NUMS:
            g_ctrlVcpuCfg.intValue = 2;
            break;
        case CONF_COMMON_CTRL_VCPU_IDS:
            g_ctrlVcpuCfg.intValueArr[0] = 1;
            g_ctrlVcpuCfg.intValueArr[1] = 1; /* duplicate! */
            break;
        default:
            g_ctrlVcpuCfg.intValue = 0;
            break;
    }
    return &g_ctrlVcpuCfg;
}

DTEST_CASE_F(KNET_CONFIG, TEST_CONFIG_CTRL_VCPU_CHECK_DUPLICATE, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfgCtrlVcpuDup);

    int ret = CtrlVcpuCheck();
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}