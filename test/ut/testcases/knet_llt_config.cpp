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
#include "knet_config_setter.h"
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
int CheckEpollData(char* endptr, const char* str, uint64_t result);
int GetnicNeedId(void *hv, const char *interfaceName, int type);
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