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
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include "securec.h"

#include "knet_lock.h"
#include "common.h"
#include "mock.h"
#include "rte_config.h"
#include "rte_errno.h"
#include "rte_mempool.h"
#include "knet_log.h"
#include "knet_mock.h"
#include "knet_rpc.h"
#include "knet_config.h"
#include "knet_config_rpc.h"

extern "C" {
enum ConfigRequestTypeTest {
    CONF_REQ_TYPE_GET_TEST = 0,
    CONF_REQ_TYPE_FREE_TEST
};

struct ConfigRequestTest {
    enum ConfigRequestTypeTest type;
    int queueId;
};

int ConfigRequestHandler(int clientId, struct KNET_RpcMessage *knetRpcRequest, struct KNET_RpcMessage *knetRpcResponse);
int ConfigDisconnetHandler(int clientId, struct KNET_RpcMessage *knetRpcRequest,
                           struct KNET_RpcMessage *knetRpcResponse);
int GetEnvQueueId(void);
}

DTEST_CASE_F(CONFIG_RPC, TEST_GET_QUEUE_ID_NORMAL, NULL, NULL)
{
    int ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KNET_RpcCall, TEST_GetFuncRetPositive(0));
    ret = KnetGetQueueIdFromPrimary();
    DT_ASSERT_NOT_EQUAL(ret, -1);

    Mock->Delete(KNET_RpcCall);
    DeleteMock(Mock);
}

DTEST_CASE_F(CONFIG_RPC, TEST_FREE_QUEUE_ID_NORMAL, NULL, NULL)
{
    int ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KNET_RpcCall, TEST_GetFuncRetPositive(0));
    ret = KnetFreeQueueIdFromPrimary(0);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KNET_RpcCall);
    DeleteMock(Mock);
}

DTEST_CASE_F(CONFIG_RPC, TEST_REG_CONFIG_RPC_HANDLER_NORMAL, NULL, NULL)
{
    int ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_RpcRegServer, TEST_GetFuncRetPositive(0));

    ret = KnetRegConfigRpcHandler(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, 0);

    ret = KnetRegConfigRpcHandler(KNET_PROC_TYPE_SECONDARY);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KNET_RpcRegServer);
    DeleteMock(Mock);
}

DTEST_CASE_F(CONFIG_RPC, TEST_CONFIG_REQUEST_HANDLER_GET, NULL, NULL)
{
    int ret = 0;
    struct KNET_RpcMessage knetRpcRequest = {0};
    struct KNET_RpcMessage knetRpcReponse = {0};

    struct ConfigRequestTest req = {0};
    req.type = CONF_REQ_TYPE_GET_TEST;
    (void)memcpy_s(knetRpcRequest.fixedLenData, RPC_MESSAGE_SIZE, (char *)&req, sizeof(struct ConfigRequestTest));

    ret = ConfigRequestHandler(-1, &knetRpcRequest, &knetRpcReponse);
    DT_ASSERT_EQUAL(ret, -1);

    ret = ConfigRequestHandler(0, &knetRpcRequest, &knetRpcReponse);
    DT_ASSERT_EQUAL(ret, 0);
}

DTEST_CASE_F(CONFIG_RPC, TEST_CONFIG_REQUEST_HANDLER_FREE, NULL, NULL)
{
    int ret = 0;
    struct KNET_RpcMessage knetRpcRequest = {0};
    struct KNET_RpcMessage knetRpcReponse = {0};

    struct ConfigRequestTest req = {0};
    req.type = CONF_REQ_TYPE_FREE_TEST;
    req.queueId = 0;
    (void)memcpy_s(knetRpcRequest.fixedLenData, RPC_MESSAGE_SIZE, (char *)&req, sizeof(struct ConfigRequestTest));

    ret = ConfigRequestHandler(-1, &knetRpcRequest, &knetRpcReponse);
    DT_ASSERT_EQUAL(ret, -1);

    ret = ConfigRequestHandler(0, &knetRpcRequest, &knetRpcReponse);
    DT_ASSERT_EQUAL(ret, 0);
}

DTEST_CASE_F(CONFIG_RPC, TEST_CONFIG_DISCONNET_HANDLER_NORAML, NULL, NULL)
{
    int ret = 0;
    struct KNET_RpcMessage knetRpcRequest;
    struct KNET_RpcMessage knetRpcReponse;

    ret = ConfigDisconnetHandler(-1, &knetRpcRequest, &knetRpcReponse);
    DT_ASSERT_EQUAL(ret, -1);

    ret = ConfigDisconnetHandler(0, &knetRpcRequest, &knetRpcReponse);
    DT_ASSERT_EQUAL(ret, -1);
}

char *MockGetEnv(char *string)
{
    return NULL;
}

char *MockGetEnvNotNull(char *string)
{
    return "0";
}

DTEST_CASE_F(CONFIG_RPC, TEST_CONFIG_GetEnvQueueId, NULL, NULL)
{
    int ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(getenv, MockGetEnv);
    ret = GetEnvQueueId();
    DT_ASSERT_EQUAL(ret, KNET_QUEUE_ID_NULL);
    Mock->Delete(getenv);

    Mock->Create(getenv, MockGetEnvNotNull);
    Mock->Create(sscanf_s, TEST_GetFuncRetPositive(0));
    ret = GetEnvQueueId();
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(sscanf_s);
    Mock->Delete(getenv);

    DeleteMock(Mock);
}

DTEST_CASE_F(CONFIG_RPC, TEST_REG_TELEMETRY_NOTIFY_FUNC, NULL, NULL)
{
    int ret = KNET_RpcRegTelemetryNotifyFunc(NULL);
    DT_ASSERT_EQUAL(ret, -1);

    struct KnetRpcReqNotifyTelemetry notifyFunc = {0};
    notifyFunc.addNewProcess = (KnetRpcRequestHandle)0x1;
    notifyFunc.delOldProcess = (KnetRpcRequestHandle)0x1;
    ret = KNET_RpcRegTelemetryNotifyFunc(&notifyFunc);
    DT_ASSERT_EQUAL(ret, 0);

    struct KnetRpcReqNotifyTelemetry zeroFunc = {0};
    KNET_RpcRegTelemetryNotifyFunc(&zeroFunc);
}

DTEST_CASE_F(CONFIG_RPC, TEST_REG_CONFIG_RPC_HANDLER_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_RpcRegServer, TEST_GetFuncRetNegative(1));

    int ret = KnetRegConfigRpcHandler(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KNET_RpcRegServer);
    DeleteMock(Mock);
}

char *MockGetEnvInvalid(char *string)
{
    (void)string;
    return "abc";
}

DTEST_CASE_F(CONFIG_RPC, TEST_CONFIG_GetEnvQueueId_Invalid, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(getenv, MockGetEnvInvalid);
    int ret = GetEnvQueueId();
    DT_ASSERT_EQUAL(ret, KNET_QUEUE_ID_INVALID);
    Mock->Delete(getenv);

    DeleteMock(Mock);
}

DTEST_CASE_F(CONFIG_RPC, TEST_CONFIG_GetEnvQueueId_SscanFail, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(getenv, MockGetEnvNotNull);
    Mock->Create(sscanf_s, TEST_GetFuncRetNegative(1));
    int ret = GetEnvQueueId();
    DT_ASSERT_EQUAL(ret, KNET_QUEUE_ID_INVALID);
    Mock->Delete(sscanf_s);
    Mock->Delete(getenv);

    DeleteMock(Mock);
}

static union KNET_CfgValue g_rpcCfgVal = {0};
static union KNET_CfgValue *MockRpcGetCfg(enum KNET_ConfKey key)
{
    (void)key;
    (void)memset_s(&g_rpcCfgVal, sizeof(g_rpcCfgVal), 0, sizeof(g_rpcCfgVal));
    return &g_rpcCfgVal;
}

DTEST_CASE_F(CONFIG_RPC, TEST_GET_QUEUE_ID_RPC_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockRpcGetCfg);
    Mock->Create(memcpy_s, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_RpcCall, TEST_GetFuncRetNegative(1));
    int ret = KnetGetQueueIdFromPrimary();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_RpcCall);
    Mock->Delete(memcpy_s);
    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

DTEST_CASE_F(CONFIG_RPC, TEST_FREE_QUEUE_ID_MEMCPY_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(memcpy_s, TEST_GetFuncRetNegative(1));
    int ret = KnetFreeQueueIdFromPrimary(0);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(memcpy_s);
    DeleteMock(Mock);
}