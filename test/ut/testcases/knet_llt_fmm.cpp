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
#include "securec.h"
#include "knet_log.h"
#include "knet_rpc.h"
#include "knet_mock.h"
#include "knet_fmm.h"

extern "C" {
#define FMM_REQUEST_DATA_SIZE_TEST 96

static union KNET_CfgValue g_cfg = {.intValue = 1};
static union KNET_CfgValue *GetCfgProcTypePrimaryMock(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    g_cfg.intValue = (int)KNET_PROC_TYPE_PRIMARY;
    return &g_cfg;
}

static union KNET_CfgValue *GetCfgProcTypeSecondaryMock(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    g_cfg.intValue = (int)KNET_PROC_TYPE_SECONDARY;
    return &g_cfg;
}

enum FmmRequestTypeTest {
    FMM_REQ_TYPE_GET_TEST = 0,
    FMM_REQ_TYPE_FREE_TEST,
    FMM_REQ_TYPE_OTHER_TEST
};

struct FmmRequestTest {
    enum FmmRequestTypeTest type;
    char padding[4];
    uint8_t data[FMM_REQUEST_DATA_SIZE_TEST];
};

extern int FmmRequestHandler(int clientId, struct KNET_RpcMessage *knetRpcRequest,
    struct KNET_RpcMessage *knetRpcResponse);
extern int FmmDisconnetHandler(int clientId, struct KNET_RpcMessage *knetRpcRequest,
    struct KNET_RpcMessage *knetRpcResponse);
extern uint32_t FmmDestroyPool(uint32_t poolId, enum KNET_ProcType procType);
uint32_t FmmDestroyPoolFromPrimary(uint32_t poolId);
extern uint32_t FmmAllocPoolId(uint32_t *poolId);
extern void FmmFreePoolId(uint32_t poolId);
extern bool FmmPoolIsValid(uint32_t poolId);
extern int FmmGetPoolId(KNET_FmmPoolCfg *cfg, uint32_t *poolId);
void FmmPoolObjInitCallback(struct rte_mempool *mp, void *para, void *obj, unsigned index);
uint32_t FmmCreatePool(KNET_FmmPoolCfg *cfg, uint32_t *poolId);
uint32_t FmmCreatePoolFromPrimary(KNET_FmmPoolCfg *cfg, uint32_t *poolId);

static int g_fmmObjInitCalled = 0;
static void MockFmmObjInitCb(void *obj)
{
    (void)obj;
    g_fmmObjInitCalled = 1;
}

static int g_mempoolLookupCount = 0;
static struct rte_mempool *MockMempoolLookupMulti(const char *name)
{
    (void)name;
    g_mempoolLookupCount++;
    if (g_mempoolLookupCount == 1) {
        return NULL;
    }
    return (struct rte_mempool *)0x1;
}

static union KNET_CfgValue g_cfgQid = {0};
static union KNET_CfgValue *MockGetCfgQid(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfgQid, sizeof(g_cfgQid), 0, sizeof(g_cfgQid));
    if (key == CONF_INNER_QID) {
        g_cfgQid.intValue = 1;
    } else {
        g_cfgQid.intValue = (int)KNET_PROC_TYPE_SECONDARY;
    }
    return &g_cfgQid;
}

static int g_rpcRegCallCount = 0;
static int32_t MockRpcRegServerFirstOkSecondFail()
{
    g_rpcRegCallCount++;
    if (g_rpcRegCallCount == 1) {
        return 0;
    }
    return -1;
}

static int32_t MockRpcCallSetRet(void *mod, void *request, struct KNET_RpcMessage *response)
{
    (void)mod;
    (void)request;
    response->ret = 1;
    return 0;
}
}

DTEST_CASE_F(FMM, TEST_FMM_CREATE_POOL_PRIMARAY_NORMAL, NULL, NULL)
{
    uint32_t ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_mempool_lookup, TEST_GetFuncRetPositive(1));
    Mock->Create(KNET_GetCfg, GetCfgProcTypePrimaryMock);
    Mock->Create(rte_mempool_create, mock_rte_mempool_create);

    KNET_FmmPoolCfg cfg;
    cfg.name[0] = '\0';
    uint32_t poolId = 0;

    ret = KNET_FmmCreatePool(NULL, NULL);
    DT_ASSERT_EQUAL(ret, KNET_FMM_ERROR);

    ret = KNET_FmmCreatePool(&cfg, &poolId);
    DT_ASSERT_EQUAL(ret, KNET_FMM_ERROR);

    cfg.name[0] = 't';
    ret = KNET_FmmCreatePool(&cfg, &poolId);
    DT_ASSERT_EQUAL(ret, KNET_OK);

    ret = KNET_FmmCreatePool(&cfg, &poolId);
    DT_ASSERT_EQUAL(ret, KNET_FMM_ERROR);

    Mock->Delete(rte_mempool_lookup);
    Mock->Delete(KNET_GetCfg);
    Mock->Delete(rte_mempool_create);
    DeleteMock(Mock);
}

DTEST_CASE_F(FMM, TEST_FMM_CREATE_POOL_SECONDARY_NORMAL, NULL, NULL)
{
    uint32_t ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_mempool_lookup, TEST_GetFuncRetPositive(1));
    Mock->Create(KNET_GetCfg, GetCfgProcTypeSecondaryMock);
    Mock->Create(KNET_RpcCall, TEST_GetFuncRetPositive(0));

    KNET_FmmPoolCfg cfg;
    cfg.name[0] = 'e';
    uint32_t poolId = 0;

    ret = KNET_FmmCreatePool(&cfg, &poolId);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);

    Mock->Delete(rte_mempool_lookup);
    Mock->Delete(KNET_GetCfg);
    Mock->Delete(KNET_RpcCall);
    DeleteMock(Mock);
}

DTEST_CASE_F(FMM, TEST_FMM_DESTROY_POOL_NORMAL, NULL, NULL)
{
    uint32_t ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_mempool_free, TEST_GetFuncRetPositive(0));

    KNET_FmmDestroyPool(0);
    DT_ASSERT_EQUAL(ret, KNET_OK);

    Mock->Delete(rte_mempool_free);
    DeleteMock(Mock);
}

DTEST_CASE_F(FMM, TEST_FMM_INIT_FMM_NORMAL, NULL, NULL)
{
    uint32_t ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KNET_RpcRegServer, TEST_GetFuncRetPositive(0));

    ret = KNET_InitFmm(KNET_PROC_TYPE_SECONDARY);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);

    ret = KNET_InitFmm(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, KNET_OK);

    Mock->Delete(KNET_RpcRegServer);
    DeleteMock(Mock);
}

DTEST_CASE_F(FMM, TEST_FMM_UNINIT_FMM_NORMAL, NULL, NULL)
{
    uint32_t ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KNET_RpcDesServer, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_mempool_free, TEST_GetFuncRetPositive(0));

    ret = KNET_UnInitFmm(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, KNET_OK);

    Mock->Delete(KNET_RpcDesServer);
    Mock->Delete(rte_mempool_free);
    DeleteMock(Mock);
}

DTEST_CASE_F(FMM, TEST_FMM_REQUEST_HANDLER_ABNORMAL, NULL, NULL)
{
    int ret = 0;
    struct KNET_RpcMessage knetRpcRequest;
    struct KNET_RpcMessage knetRpcReponse;

    struct FmmRequestTest *req = (struct FmmRequestTest *)knetRpcRequest.fixedLenData;
    req->type = FMM_REQ_TYPE_OTHER_TEST;

    ret = FmmRequestHandler(0, &knetRpcRequest, &knetRpcReponse);
    DT_ASSERT_EQUAL(ret, -1);
}

DTEST_CASE_F(FMM, TEST_FMM_REQUEST_HANDLER_GET_FAILED, NULL, NULL)
{
    int ret = 0;
    struct KNET_RpcMessage knetRpcRequest = {0};
    struct KNET_RpcMessage knetRpcReponse = {0};
    
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_mempool_create, mock_rte_mempool_create);

    struct FmmRequestTest *req = (struct FmmRequestTest *)knetRpcRequest.fixedLenData;
    req->type = FMM_REQ_TYPE_GET_TEST;
    KNET_FmmPoolCfg *cfg = (KNET_FmmPoolCfg *)req->data;

    ret = FmmRequestHandler(0, &knetRpcRequest, &knetRpcReponse);
    DT_ASSERT_NOT_EQUAL(ret, KNET_OK);

    Mock->Delete(rte_mempool_create);
    DeleteMock(Mock);
}

DTEST_CASE_F(FMM, TEST_FMM_REQUEST_HANDLER_FREE_NORAML, NULL, NULL)
{
    int ret = 0;
    struct KNET_RpcMessage knetRpcRequest = {0};
    struct KNET_RpcMessage knetRpcReponse = {0};
    
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_mempool_free, TEST_GetFuncRetPositive(0));
    Mock->Create(FmmDestroyPool, TEST_GetFuncRetPositive(0));

    struct FmmRequestTest *req = (struct FmmRequestTest *)knetRpcRequest.fixedLenData;
    req->type = FMM_REQ_TYPE_FREE_TEST;
    *(uint32_t *)req->data = 0;

    ret = FmmRequestHandler(0, &knetRpcRequest, &knetRpcReponse);
    DT_ASSERT_EQUAL(ret, KNET_OK);

    Mock->Delete(rte_mempool_free);
    Mock->Delete(FmmDestroyPool);
    DeleteMock(Mock);
}

DTEST_CASE_F(FMM, TEST_FMM_DISCONNECT_HANDLER_NORMAL, NULL, NULL)
{
    int ret = 0;
    struct KNET_RpcMessage knetRpcRequest = {0};
    struct KNET_RpcMessage knetRpcReponse = {0};

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_mempool_free, TEST_GetFuncRetPositive(0));

    ret = FmmDisconnetHandler(0, &knetRpcRequest, &knetRpcReponse);

    Mock->Delete(rte_mempool_free);
    DT_ASSERT_EQUAL(ret, KNET_OK);
    DeleteMock(Mock);
}

DTEST_CASE_F(FMM, TEST_FMM_ALLOC_ABNORMAL, NULL, NULL)
{
    uint32_t ret = 0;
    void *ptr = NULL;

    ret = KNET_FmmAlloc(0, NULL);
    DT_ASSERT_EQUAL(ret, KNET_FMM_ERROR);

    ret = KNET_FmmAlloc(KNET_FMM_POOL_MAX_NUM, &ptr);
    DT_ASSERT_EQUAL(ret, KNET_FMM_ERROR);
}

DTEST_CASE_F(FMM, TEST_FMM_DESTORY_FROM_PRIMARY_ABNORMAL, NULL, NULL)
{
    uint32_t ret = 0;
    uint32_t poolId = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(memcpy_s, TEST_GetFuncRetNegative(1));

    ret = FmmDestroyPoolFromPrimary(poolId);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);
    
    Mock->Delete(memcpy_s);
    Mock->Create(memcpy_s, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_RpcCall, TEST_GetFuncRetNegative(1));
    ret = FmmDestroyPoolFromPrimary(poolId);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);
    
    Mock->Delete(KNET_RpcCall);
    Mock->Create(KNET_RpcCall, TEST_GetFuncRetPositive(0));
    Mock->Create(FmmDestroyPool, TEST_GetFuncRetPositive(0));

    ret = FmmDestroyPoolFromPrimary(poolId);
    DT_ASSERT_EQUAL(ret, KNET_OK);

    Mock->Delete(memcpy_s);
    Mock->Delete(KNET_RpcCall);
    Mock->Delete(FmmDestroyPool);
    DeleteMock(Mock);
}

DTEST_CASE_F(FMM, TEST_FMM_FREE_ABNORMAL, NULL, NULL)
{
    uint32_t ret = 0;
    void *ptr = NULL;

    ret = KNET_FmmFree(0, NULL);
    DT_ASSERT_EQUAL(ret, KNET_FMM_ERROR);

    ret = KNET_FmmFree(KNET_FMM_POOL_MAX_NUM, ptr);
    DT_ASSERT_EQUAL(ret, KNET_FMM_ERROR);
}

DTEST_CASE_F(FMM, TEST_FMM_ALLOC_INVALID_POOL, NULL, NULL)
{
    uint32_t ret = 0;
    void *ptr = NULL;

    ret = KNET_FmmAlloc(0, &ptr);
    DT_ASSERT_EQUAL(ret, KNET_FMM_ERROR);
}

DTEST_CASE_F(FMM, TEST_FMM_FREE_INVALID_POOL, NULL, NULL)
{
    uint32_t ret = 0;
    void *ptr = (void *)0x1;

    ret = KNET_FmmFree(0, ptr);
    DT_ASSERT_EQUAL(ret, KNET_FMM_ERROR);
}

DTEST_CASE_F(FMM, TEST_FMM_DESTROY_POOL_INVALID, NULL, NULL)
{
    uint32_t ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, GetCfgProcTypePrimaryMock);

    ret = KNET_FmmDestroyPool(0);
    DT_ASSERT_EQUAL(ret, KNET_FMM_ERROR);

    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

DTEST_CASE_F(FMM, TEST_FMM_DESTROY_POOL_SECONDARY, NULL, NULL)
{
    uint32_t ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, GetCfgProcTypeSecondaryMock);
    Mock->Create(KNET_RpcCall, TEST_GetFuncRetPositive(0));
    Mock->Create(FmmDestroyPool, TEST_GetFuncRetPositive(0));

    ret = KNET_FmmDestroyPool(0);
    DT_ASSERT_EQUAL(ret, KNET_OK);

    Mock->Delete(KNET_GetCfg);
    Mock->Delete(KNET_RpcCall);
    Mock->Delete(FmmDestroyPool);
    DeleteMock(Mock);
}

DTEST_CASE_F(FMM, TEST_FMM_INIT_RPC_FAIL, NULL, NULL)
{
    int ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_RpcRegServer, TEST_GetFuncRetNegative(1));

    ret = KNET_InitFmm(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_NOT_EQUAL(ret, 0);

    Mock->Delete(KNET_RpcRegServer);
    DeleteMock(Mock);
}

DTEST_CASE_F(FMM, TEST_FMM_CREATE_POOL_MEMPOOL_NULL, NULL, NULL)
{
    uint32_t ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, GetCfgProcTypePrimaryMock);
    Mock->Create(rte_mempool_create, TEST_GetFuncRetPositive(0));

    KNET_FmmPoolCfg cfg = {0};
    cfg.name[0] = 'n';
    uint32_t poolId = 0;

    ret = KNET_FmmCreatePool(&cfg, &poolId);
    DT_ASSERT_EQUAL(ret, KNET_FMM_ERROR);

    Mock->Delete(KNET_GetCfg);
    Mock->Delete(rte_mempool_create);
    DeleteMock(Mock);
}

DTEST_CASE_F(FMM, TEST_FMM_REQUEST_HANDLER_GET_SUCCESS, NULL, NULL)
{
    int ret = 0;
    struct KNET_RpcMessage knetRpcRequest = {0};
    struct KNET_RpcMessage knetRpcReponse = {0};

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_mempool_create, mock_rte_mempool_create);

    struct FmmRequestTest *req = (struct FmmRequestTest *)knetRpcRequest.fixedLenData;
    req->type = FMM_REQ_TYPE_GET_TEST;
    KNET_FmmPoolCfg *cfg = (KNET_FmmPoolCfg *)req->data;
    cfg->name[0] = 's';

    ret = FmmRequestHandler(0, &knetRpcRequest, &knetRpcReponse);
    DT_ASSERT_EQUAL(ret, KNET_OK);

    Mock->Delete(rte_mempool_create);
    DeleteMock(Mock);
}

DTEST_CASE_F(FMM, TEST_FMM_UNINIT_WITH_POOL, NULL, NULL)
{
    int ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, GetCfgProcTypePrimaryMock);
    Mock->Create(rte_mempool_create, mock_rte_mempool_create);
    Mock->Create(rte_mempool_free, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_RpcDesServer, TEST_GetFuncRetPositive(0));

    KNET_FmmPoolCfg cfg = {0};
    cfg.name[0] = 'u';
    uint32_t poolId = 0;

    ret = KNET_FmmCreatePool(&cfg, &poolId);
    DT_ASSERT_EQUAL(ret, KNET_OK);

    ret = KNET_UnInitFmm(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, KNET_OK);

    Mock->Delete(KNET_GetCfg);
    Mock->Delete(rte_mempool_create);
    Mock->Delete(rte_mempool_free);
    Mock->Delete(KNET_RpcDesServer);
    DeleteMock(Mock);
}

/* ===== knet_fmm.c 补充覆盖率测试 ===== */

/** @brief FmmPoolObjInitCallback - 直接调用覆盖函数体 */
DTEST_CASE_F(FMM, TEST_FMM_POOL_OBJ_INIT_CALLBACK, NULL, NULL)
{
    g_fmmObjInitCalled = 0;
    struct rte_mempool mp = {0};
    FmmPoolObjInitCallback(&mp, (void *)MockFmmObjInitCb, NULL, 0);
    DT_ASSERT_EQUAL(g_fmmObjInitCalled, 1);
}

/** @brief FmmCreatePool - 所有pool slot已用尽, FmmAllocPoolId失败 */
DTEST_CASE_F(FMM, TEST_FMM_CREATE_POOL_ALL_SLOTS_USED, NULL, NULL)
{
    /* 先清理所有pool slot */
    for (uint32_t i = 0; i < KNET_FMM_POOL_MAX_NUM; i++) {
        FmmFreePoolId(i);
    }
    /* 耗尽所有slot */
    uint32_t poolId = 0;
    for (uint32_t i = 0; i < KNET_FMM_POOL_MAX_NUM; i++) {
        FmmAllocPoolId(&poolId);
    }
    /* 再尝试创建pool, FmmAllocPoolId应失败 */
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, GetCfgProcTypePrimaryMock);
    Mock->Create(rte_mempool_create, mock_rte_mempool_create);

    KNET_FmmPoolCfg cfg = {0};
    cfg.name[0] = 'z';
    uint32_t newPoolId = 0;
    uint32_t ret = KNET_FmmCreatePool(&cfg, &newPoolId);
    DT_ASSERT_EQUAL(ret, KNET_FMM_ERROR);

    Mock->Delete(KNET_GetCfg);
    Mock->Delete(rte_mempool_create);
    DeleteMock(Mock);

    /* 清理: 释放所有pool slot */
    for (uint32_t i = 0; i < KNET_FMM_POOL_MAX_NUM; i++) {
        FmmFreePoolId(i);
    }
}

/** @brief FmmGetRequestHandler - rte_mempool_create返回NULL
 *  通过FmmRequestHandler发送GET请求, pool name有效但create失败 */
DTEST_CASE_F(FMM, TEST_FMM_REQUEST_HANDLER_GET_CREATE_NULL, NULL, NULL)
{
    /* 确保至少一个空闲slot */
    FmmFreePoolId(0);

    struct KNET_RpcMessage knetRpcRequest = {0};
    struct KNET_RpcMessage knetRpcReponse = {0};

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_mempool_create, TEST_GetFuncRetPositive(0)); /* 返回0=NULL */

    struct FmmRequestTest *req = (struct FmmRequestTest *)knetRpcRequest.fixedLenData;
    req->type = FMM_REQ_TYPE_GET_TEST;
    KNET_FmmPoolCfg *cfg = (KNET_FmmPoolCfg *)req->data;
    cfg->name[0] = 'c';

    int ret = FmmRequestHandler(0, &knetRpcRequest, &knetRpcReponse);
    DT_ASSERT_NOT_EQUAL(ret, KNET_OK);

    Mock->Delete(rte_mempool_create);
    DeleteMock(Mock);
    /* 清理 */
    FmmFreePoolId(0);
}

/** @brief FmmRequestHandler - FREE分支失败 */
DTEST_CASE_F(FMM, TEST_FMM_REQUEST_HANDLER_FREE_FAIL, NULL, NULL)
{
    struct KNET_RpcMessage knetRpcRequest = {0};
    struct KNET_RpcMessage knetRpcReponse = {0};

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_mempool_free, TEST_GetFuncRetPositive(0));
    Mock->Create(FmmDestroyPool, TEST_GetFuncRetNegative(1)); /* 返回非0=失败 */

    struct FmmRequestTest *req = (struct FmmRequestTest *)knetRpcRequest.fixedLenData;
    req->type = FMM_REQ_TYPE_FREE_TEST;
    *(uint32_t *)req->data = 0;

    int ret = FmmRequestHandler(0, &knetRpcRequest, &knetRpcReponse);
    DT_ASSERT_NOT_EQUAL(ret, KNET_OK);

    Mock->Delete(rte_mempool_free);
    Mock->Delete(FmmDestroyPool);
    DeleteMock(Mock);
}

/** @brief FmmDisconnetHandler - 有匹配clientId的pool */
DTEST_CASE_F(FMM, TEST_FMM_DISCONNECT_HANDLER_WITH_POOL, NULL, NULL)
{
    /* 先创建一个pool, 通过FmmRequestHandler GET使poolCtrl有used=1且clientId=5 */
    FmmFreePoolId(0);
    struct KNET_RpcMessage reqMsg = {0};
    struct KNET_RpcMessage respMsg = {0};

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_mempool_create, mock_rte_mempool_create);

    struct FmmRequestTest *req = (struct FmmRequestTest *)reqMsg.fixedLenData;
    req->type = FMM_REQ_TYPE_GET_TEST;
    KNET_FmmPoolCfg *cfg = (KNET_FmmPoolCfg *)req->data;
    cfg->name[0] = 'd';

    int ret = FmmRequestHandler(5, &reqMsg, &respMsg); /* clientId=5 */
    DT_ASSERT_EQUAL(ret, KNET_OK);

    Mock->Delete(rte_mempool_create);

    /* 现在调用FmmDisconnetHandler, clientId=5匹配 */
    Mock->Create(rte_mempool_free, TEST_GetFuncRetPositive(0));
    struct KNET_RpcMessage discReq = {0};
    struct KNET_RpcMessage discResp = {0};
    ret = FmmDisconnetHandler(5, &discReq, &discResp);
    DT_ASSERT_EQUAL(ret, KNET_OK);

    Mock->Delete(rte_mempool_free);
    DeleteMock(Mock);
    /* 清理 */
    FmmFreePoolId(0);
}

/** @brief KNET_InitFmm - 第二次RpcRegServer失败
 *  第一次RpcRegServer成功, 第二次失败 */
DTEST_CASE_F(FMM, TEST_FMM_INIT_SECOND_REG_FAIL, NULL, NULL)
{
    g_rpcRegCallCount = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_RpcRegServer, MockRpcRegServerFirstOkSecondFail);

    int ret = KNET_InitFmm(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_NOT_EQUAL(ret, 0);

    Mock->Delete(KNET_RpcRegServer);
    DeleteMock(Mock);
}

/** @brief KNET_UnInitFmm - FmmDestroyPool失败
 *  先创建pool, 再mock FmmDestroyPool返回失败 */
DTEST_CASE_F(FMM, TEST_FMM_UNINIT_DESTROY_FAIL, NULL, NULL)
{
    FmmFreePoolId(0);
    struct KNET_RpcMessage reqMsg = {0};
    struct KNET_RpcMessage respMsg = {0};

    /* 先创建一个pool */
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_mempool_create, mock_rte_mempool_create);
    struct FmmRequestTest *req = (struct FmmRequestTest *)reqMsg.fixedLenData;
    req->type = FMM_REQ_TYPE_GET_TEST;
    KNET_FmmPoolCfg *cfg = (KNET_FmmPoolCfg *)req->data;
    cfg->name[0] = 'f';
    FmmRequestHandler(0, &reqMsg, &respMsg);
    Mock->Delete(rte_mempool_create);

    /* mock FmmDestroyPool返回失败, 覆盖549,551 */
    Mock->Create(FmmDestroyPool, TEST_GetFuncRetNegative(1));
    Mock->Create(KNET_RpcDesServer, TEST_GetFuncRetPositive(0));
    int ret = KNET_UnInitFmm(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_NOT_EQUAL(ret, KNET_OK);

    Mock->Delete(FmmDestroyPool);
    Mock->Delete(KNET_RpcDesServer);
    DeleteMock(Mock);
    FmmFreePoolId(0);
}

/** @brief FmmDestroyPoolFromPrimary - RPC返回ret!=0
 *  Mock KNET_RpcCall: 返回0(成功)但设置response.ret=1 */
DTEST_CASE_F(FMM, TEST_FMM_DESTROY_FROM_PRIMARY_RPC_ERROR, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(memcpy_s, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_RpcCall, MockRpcCallSetRet);

    uint32_t ret = FmmDestroyPoolFromPrimary(0);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);

    Mock->Delete(KNET_RpcCall);
    Mock->Delete(memcpy_s);
    DeleteMock(Mock);
}

/** @brief FmmCreatePoolFromPrimary - NULL参数 */
DTEST_CASE_F(FMM, TEST_FMM_CREATE_FROM_PRIMARY_NULL_PARAM, NULL, NULL)
{
    uint32_t ret = FmmCreatePoolFromPrimary(NULL, NULL);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);
}

/** @brief FmmCreatePoolFromPrimary - GetCfgNew memcpy_s失败 */
DTEST_CASE_F(FMM, TEST_FMM_CREATE_FROM_PRIMARY_MEMCPY_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(memcpy_s, TEST_GetFuncRetNegative(1));

    KNET_FmmPoolCfg cfg = {0};
    cfg.name[0] = 'm';
    uint32_t poolId = 0;
    uint32_t ret = FmmCreatePoolFromPrimary(&cfg, &poolId);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);

    Mock->Delete(memcpy_s);
    DeleteMock(Mock);
}

/** @brief FmmCreatePoolFromPrimary - 完整成功路径 */
DTEST_CASE_F(FMM, TEST_FMM_CREATE_FROM_PRIMARY_SUCCESS, NULL, NULL)
{
    FmmFreePoolId(0);
    g_mempoolLookupCount = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockGetCfgQid); /* CONF_INNER_QID返回1 */
    Mock->Create(rte_mempool_lookup, MockMempoolLookupMulti); /* 第一次NULL, 第二次非NULL */
    Mock->Create(KNET_RpcCall, TEST_GetFuncRetPositive(0)); /* RPC成功 */
    Mock->Create(memcpy_s, TEST_GetFuncRetPositive(0));

    KNET_FmmPoolCfg cfg = {0};
    cfg.name[0] = 'p';
    uint32_t poolId = 0;
    uint32_t ret = FmmCreatePoolFromPrimary(&cfg, &poolId);
    DT_ASSERT_EQUAL(ret, KNET_OK);

    Mock->Delete(memcpy_s);
    Mock->Delete(KNET_RpcCall);
    Mock->Delete(rte_mempool_lookup);
    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
    FmmFreePoolId(0);
}
