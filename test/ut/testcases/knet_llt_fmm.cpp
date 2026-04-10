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
