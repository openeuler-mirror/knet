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

#include "knet_hash_table.h"
#include "knet_atomic.h"
#include "knet_config.h"
#include "rte_jhash.h"
#include "knet_rpc.h"

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
#include "rte_hash.h"

extern "C" {
int KnetDestroyHashTblMultiple(void);
struct rte_hash *KnetCreateHashTblMultiple(uint32_t entries, uint32_t keySize, char *queueName, int queueNameSize);
int FindFreeHandleSlot(void);
int HashRequestHandler(int id, struct KNET_RpcMessage *knetRpcRequest, struct KNET_RpcMessage *knetRpcResponse);
int KNET_InitHash(enum KNET_ProcType procType);
int KNET_UninitHash(enum KNET_ProcType procType);
extern void ReleaseHashTblId(uint32_t tableId);
extern int32_t GetHashTblId(uint32_t *tableId);
extern KNET_HASH_FUNC g_hashFunc;
extern uint32_t DefaultHashFunc(uint8_t *key, uint32_t keyLen);
}

#define DEFAULT_HASH_TBL_NUM 128

static union KNET_CfgValue g_cfg = {.intValue = 1};
static union KNET_CfgValue *MockKnetGetCfg(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    g_cfg.intValue = 1;
    return &g_cfg;
}

uint32_t FuncHash(uint8_t *key, uint32_t keyLen)
{
    return (uint32_t)*key + keyLen;
}

DTEST_CASE_F(HASH, TEST_HASH_INIT_NORMAL, NULL, NULL)
{
    int ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    ret = KNET_HashTblInit();
    DT_ASSERT_EQUAL(ret, 0);

    KNET_HashTblDeinit();

    DeleteMock(Mock);
}

DTEST_CASE_F(HASH, TEST_HASH_GET_ID_NORMAL, TEST_HASH_INIT_NORMAL, NULL)
{
    uint32_t tableId[1];
    tableId[0] = 1;
    int ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_SpinlockLock, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_RwlockInit, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_SpinlockUnlock, TEST_GetFuncRetPositive(0));

    ret = KNET_HashTblInit();
    DT_ASSERT_EQUAL(ret, 0);

    ret = GetHashTblId(tableId);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KNET_SpinlockLock);
    Mock->Delete(KNET_RwlockInit);
    Mock->Delete(KNET_SpinlockUnlock);

    KNET_HashTblDeinit();
    DeleteMock(Mock);
}

DTEST_CASE_F(HASH, TEST_HASH_RES_NORMAL, TEST_HASH_INIT_NORMAL, NULL)
{
    uint32_t tableId = 0;
    int ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    ret = KNET_HashTblInit();
    DT_ASSERT_EQUAL(ret, 0);

    ReleaseHashTblId(tableId);

    KNET_HashTblDeinit();

    DeleteMock(Mock);
}

DTEST_CASE_F(HASH, TEST_HASH_CREAT_ABNORMAL, TEST_HASH_INIT_NORMAL, NULL)
{
    KNET_HashTblCfg cfg = { 0 };
    uint32_t tableId = { 1 };
    int ret = 0;

    ret = KNET_CreateHashTbl(NULL, &tableId);
    DT_ASSERT_EQUAL(ret, -1);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(GetHashTblId, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_hash_create, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    Mock->Create(rte_hash_find_existing, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_hash_free, TEST_GetFuncRetPositive(0));

    ret = KNET_HashTblInit();
    DT_ASSERT_EQUAL(ret, 0);

    ret = KNET_CreateHashTbl(&cfg, &tableId);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(GetHashTblId);
    Mock->Delete(rte_hash_create);
    Mock->Delete(KNET_GetCfg);
    Mock->Delete(rte_hash_find_existing);
    Mock->Delete(rte_hash_free);

    KNET_HashTblDeinit();

    DeleteMock(Mock);
}

DTEST_CASE_F(HASH, TEST_HASH_DES_NORMAL, TEST_HASH_INIT_NORMAL, NULL)
{
    uint32_t tableId = 0;
    int ret = 0;
 
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(ReleaseHashTblId, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_hash_free, TEST_GetFuncRetPositive(0));
 
    ret = KNET_HashTblInit();
    DT_ASSERT_EQUAL(ret, 0);
 
    ret = KNET_DestroyHashTbl(tableId);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(ReleaseHashTblId);
    Mock->Delete(rte_hash_free);
 
    KNET_HashTblDeinit();

    DeleteMock(Mock);
}

DTEST_CASE_F(HASH, TEST_HASH_LOOK_NORMAL, TEST_HASH_INIT_NORMAL, NULL)
{
    uint32_t tableId = 0;
    const uint8_t key = 100;
    uint8_t data = 100;
    int ret = 0;
    g_hashFunc = FuncHash;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_hash_lookup_with_hash_data, TEST_GetFuncRetPositive(0));

    ret = KNET_HashTblInit();
    DT_ASSERT_EQUAL(ret, 0);
    GetHashTblId(&tableId);

    ret = KNET_HashTblLookupEntry(tableId, NULL, &data); // 传入空指针
    DT_ASSERT_EQUAL(ret, -1);

    ret = KNET_HashTblLookupEntry(tableId, &key, &data);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Create(rte_hash_lookup_with_hash_data, TEST_GetFuncRetNegative(1)); // 键不存在
    ret = KNET_HashTblLookupEntry(tableId, &key, &data);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(rte_hash_lookup_with_hash_data);

    Mock->Create(rte_hash_free, TEST_GetFuncRetPositive(0));
    KNET_HashTblDeinit();
    Mock->Delete(rte_hash_free);

    DeleteMock(Mock);
}

DTEST_CASE_F(HASH, TEST_HASH_GET_INFO_NORMAL, TEST_HASH_INIT_NORMAL, NULL)
{
    uint32_t tableId = 0;
    KNET_HashTblInfo temInfo = { FuncHash, 0, 100, 1, 32, 3200 };
    KNET_HashTblInfo *info = &temInfo;
    int ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_HalAtomicRead64, TEST_GetFuncRetPositive(0));

    ret = KNET_HashTblInit();
    DT_ASSERT_EQUAL(ret, 0);

    GetHashTblId(&tableId);
    ret = KNET_GetHashTblInfo(tableId, info);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KNET_HalAtomicRead64);

    Mock->Create(rte_hash_free, TEST_GetFuncRetPositive(0));
    KNET_HashTblDeinit();
    Mock->Delete(rte_hash_free);

    DeleteMock(Mock);
}

DTEST_CASE_F(HASH, TEST_HASH_ADD_NORMAL, NULL, NULL)
{
    uint32_t tableId[1];
    tableId[0] = 0;
    int ret = 0;
    const uint8_t key = 100;
    const uint8_t data = 100;
    g_hashFunc = FuncHash;

    KNET_HashTblInit();
    GetHashTblId(tableId);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_RwlockWriteLock, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_RwlockWriteUnlock, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_hash_lookup_with_hash, TEST_GetFuncRetPositive(1));

    ret = KNET_HashTblAddEntry(tableId[0], NULL, &data); // 传入空指针
    DT_ASSERT_EQUAL(ret, -1);

    ret = KNET_HashTblAddEntry(tableId[0], &key, &data);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Create(rte_hash_lookup_with_hash, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_hash_add_key_with_hash_data, TEST_GetFuncRetPositive(1));

    ret = KNET_HashTblAddEntry(tableId[0], &key, &data);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Create(rte_hash_lookup_with_hash, TEST_GetFuncRetPositive(1)); // 键已经存在
    ret = KNET_HashTblAddEntry(tableId[0], &key, &data);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KNET_RwlockWriteLock);
    Mock->Delete(KNET_RwlockWriteUnlock);
    Mock->Delete(rte_hash_lookup_with_hash);
    Mock->Delete(rte_hash_add_key_with_hash_data);

    KNET_HashTblDeinit();

    DeleteMock(Mock);
}

DTEST_CASE_F(HASH, TEST_HASH_DEL_NORMAL, NULL, NULL)
{
    uint32_t tableId[1];
    tableId[0] = 0;
    int ret = 0;
    const uint8_t key = 100;
    g_hashFunc = FuncHash;

    KNET_HashTblInit();
    GetHashTblId(tableId);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_RwlockWriteLock, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_RwlockWriteUnlock, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_hash_lookup_with_hash_data, TEST_GetFuncRetNegative(1));

    ret = KNET_HashTblDelEntry(tableId[0], &key);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Create(rte_hash_lookup_with_hash_data, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_hash_del_key_with_hash, TEST_GetFuncRetNegative(1));

    ret = KNET_HashTblDelEntry(tableId[0], &key);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Create(rte_hash_del_key_with_hash, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_HalAtomicAdd64, TEST_GetFuncRetPositive(0));

    ret = KNET_HashTblDelEntry(tableId[0], &key);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KNET_RwlockWriteLock);
    Mock->Delete(KNET_RwlockWriteUnlock);
    Mock->Delete(rte_hash_lookup_with_hash_data);
    Mock->Delete(rte_hash_del_key_with_hash);
    Mock->Delete(KNET_HalAtomicAdd64);

    Mock->Create(rte_hash_iterate, TEST_GetFuncRetNegative(2)); // rte_hash_iterate返回-2表示表位空
    KNET_HashTblDeinit();
    Mock->Delete(rte_hash_iterate);

    DeleteMock(Mock);
}

DTEST_CASE_F(HASH, TEST_HASH_MOD_NORMAL, NULL, NULL)
{
    uint32_t tableId[1];
    tableId[0] = 0;
    int ret = 0;
    const uint8_t key = 100;
    const uint8_t data = 100;
    g_hashFunc = FuncHash;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_RwlockWriteLock, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_RwlockWriteUnlock, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_hash_lookup_with_hash_data, TEST_GetFuncRetPositive(0));
    Mock->Create(memcpy_s, TEST_GetFuncRetPositive(0));

    KNET_HashTblInit();
    GetHashTblId(tableId);
    ret = KNET_HashTblModifyEntry(tableId[0], &key, &data);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Create(rte_hash_lookup_with_hash_data, TEST_GetFuncRetNegative(1)); // 键不存在
    ret = KNET_HashTblModifyEntry(tableId[0], &key, &data);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KNET_RwlockWriteLock);
    Mock->Delete(KNET_RwlockWriteUnlock);
    Mock->Delete(rte_hash_lookup_with_hash_data);
    Mock->Delete(memcpy_s);

    KNET_HashTblDeinit();

    DeleteMock(Mock);
}

DTEST_CASE_F(HASH, TEST_HASH_GET_FIRST_NORMAL, NULL, NULL)
{
    uint32_t tableId[1];
    tableId[0] = 0;
    int ret = 0;
    uint8_t *key = (uint8_t *)1;
    uint8_t *data = (uint8_t *)1;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_RwlockReadLock, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_RwlockReadUnlock, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_hash_iterate, TEST_GetFuncRetPositive(0));

    ret = KNET_HashTblInit();
    DT_ASSERT_EQUAL(ret, 0);

    ret = GetHashTblId(tableId);
    DT_ASSERT_EQUAL(ret, 0);

    ret = KNET_GetHashTblFirstEntry(tableId[0], NULL, data); // 传入空指针
    DT_ASSERT_EQUAL(ret, -1);

    ret = KNET_GetHashTblFirstEntry(tableId[0], key, data);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Create(rte_hash_iterate, TEST_GetFuncRetNegative(1)); // 获取失败
    ret = KNET_GetHashTblFirstEntry(tableId[0], key, data);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KNET_RwlockReadLock);
    Mock->Delete(KNET_RwlockReadUnlock);
    Mock->Delete(rte_hash_iterate);

    Mock->Create(rte_hash_free, TEST_GetFuncRetPositive(0));
    KNET_HashTblDeinit();
    Mock->Delete(rte_hash_free);

    DeleteMock(Mock);
}

DTEST_CASE_F(HASH, TEST_HASH_GET_NEXT_NORMAL, NULL, NULL)
{
    uint32_t tableId[1];
    tableId[0] = 0;
    int ret = 0;
    uint8_t key = 100;
    const uint8_t curKey = 100;
    uint8_t data = 100;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_RwlockReadLock, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_RwlockReadUnlock, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_hash_lookup_with_hash, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_hash_iterate, TEST_GetFuncRetPositive(0));

    ret = KNET_HashTblInit();
    DT_ASSERT_EQUAL(ret, 0);

    ret = GetHashTblId(tableId);
    DT_ASSERT_EQUAL(ret, 0);

    ret = KNET_GetHashTblNextEntry(tableId[0], NULL, NULL, &data); // 传入空指针
    DT_ASSERT_EQUAL(ret, -1);

    ret = KNET_GetHashTblNextEntry(tableId[0], &curKey, &key, &data);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KNET_RwlockReadLock);
    Mock->Delete(KNET_RwlockReadUnlock);
    Mock->Delete(rte_hash_iterate);
    Mock->Delete(rte_hash_lookup_with_hash);

    Mock->Create(rte_hash_free, TEST_GetFuncRetPositive(0));
    KNET_HashTblDeinit();
    Mock->Delete(rte_hash_free);

    DeleteMock(Mock);
}

DTEST_CASE_F(HASH, TEST_HASH_DES_MULTI_NORMAL, NULL, NULL)
{
    int ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_RpcCall, TEST_GetFuncRetNegative(1));

    ret = KnetDestroyHashTblMultiple();
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Create(KNET_RpcCall, TEST_GetFuncRetPositive(0));
    ret = KnetDestroyHashTblMultiple();
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KNET_RpcCall);
    DeleteMock(Mock);
}

/* HashRequest 结构, 与 knet_hash_rpc.c 中定义一致, 用于构造测试数据 */
struct TestHashRequest {
    char name[32];
    uint32_t entries;
    uint32_t reserved;
    uint32_t keyLen;
    uint32_t hashFuncInitval;
    int socketId;
    uint8_t extraFlag;
    uint8_t type;
    char padding[2];
};

/**
 * @brief KnetCreateHashTblMultiple, KNET_RpcCall 失败路径
 */
DTEST_CASE_F(HASH, TEST_HASH_CREATE_MULTI_RPC_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    Mock->Create(KNET_RpcCall, TEST_GetFuncRetNegative(1));
    struct rte_hash *ret = KnetCreateHashTblMultiple(16, 4, (char *)"test", 5);
    DT_ASSERT_EQUAL(ret, NULL);
    Mock->Delete(KNET_RpcCall);
    DeleteMock(Mock);
}

/**
 * @brief KnetCreateHashTblMultiple, KNET_RpcCall 成功, rte_hash_find_existing 返回 NULL
 */
DTEST_CASE_F(HASH, TEST_HASH_CREATE_MULTI_RET_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    Mock->Create(KNET_RpcCall, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_hash_find_existing, TEST_GetFuncRetPositive(0)); // 返回 NULL
    struct rte_hash *ret = KnetCreateHashTblMultiple(16, 4, (char *)"test", 5);
    DT_ASSERT_EQUAL(ret, NULL);
    Mock->Delete(rte_hash_find_existing);
    Mock->Delete(KNET_RpcCall);
    DeleteMock(Mock);
}

/**
 * @brief KnetCreateHashTblMultiple, 成功路径
 */
DTEST_CASE_F(HASH, TEST_HASH_CREATE_MULTI_OK, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    Mock->Create(KNET_RpcCall, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_hash_find_existing, TEST_GetFuncRetPositive(1));
    struct rte_hash *ret = KnetCreateHashTblMultiple(16, 4, (char *)"test", 5);
    DT_ASSERT_NOT_EQUAL(ret, NULL);
    Mock->Delete(rte_hash_find_existing);
    Mock->Delete(KNET_RpcCall);
    DeleteMock(Mock);
}

/**
 * @brief KNET_InitHash 非主进程, 返回错误
 */
DTEST_CASE_F(HASH, TEST_HASH_RPC_INIT_NON_PRIMARY, NULL, NULL)
{
    int ret = KNET_InitHash(KNET_PROC_TYPE_SECONDARY);
    DT_ASSERT_EQUAL(ret, -1);
}

/**
 * @brief KNET_InitHash 主进程, KNET_RpcRegServer 第一次失败
 */
DTEST_CASE_F(HASH, TEST_HASH_RPC_INIT_REQ_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    Mock->Create(KNET_RpcRegServer, TEST_GetFuncRetNegative(1));
    int ret = KNET_InitHash(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_RpcRegServer);
    DeleteMock(Mock);
}

/**
 * @brief KNET_InitHash 主进程正常流程, 覆盖完整初始化 + 注册
 */
DTEST_CASE_F(HASH, TEST_HASH_RPC_INIT_OK, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    Mock->Create(KNET_RpcRegServer, TEST_GetFuncRetPositive(0));
    int ret = KNET_InitHash(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(KNET_RpcRegServer);
    DeleteMock(Mock);
}

/**
 * @brief KNET_UninitHash 非主进程
 */
DTEST_CASE_F(HASH, TEST_HASH_RPC_UNINIT_NON_PRIMARY, NULL, NULL)
{
    int ret = KNET_UninitHash(KNET_PROC_TYPE_SECONDARY);
    DT_ASSERT_EQUAL(ret, -1);
}

/**
 * @brief KNET_UninitHash 主进程正常流程
 */
DTEST_CASE_F(HASH, TEST_HASH_RPC_UNINIT_OK, NULL, NULL)
{
    int ret = KNET_UninitHash(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, 0);
}

/**
 * @brief HashRequestHandler 非法 type 分支
 */
DTEST_CASE_F(HASH, TEST_HASH_REQ_INVALID_TYPE, NULL, NULL)
{
    struct KNET_RpcMessage req = {0};
    struct KNET_RpcMessage res = {0};
    struct TestHashRequest *hr = (struct TestHashRequest *)req.fixedLenData;
    hr->type = 99; // 非法

    int ret = HashRequestHandler(0, &req, &res);
    DT_ASSERT_EQUAL(ret, -1);
    DT_ASSERT_EQUAL(res.ret, -1);
}

/**
 * @brief HashRequestHandler HASH_DESTROY 分支 (无匹配 clientID, 仍返回 0)
 */
DTEST_CASE_F(HASH, TEST_HASH_REQ_DESTROY, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    Mock->Create(KNET_RpcRegServer, TEST_GetFuncRetPositive(0));
    KNET_InitHash(KNET_PROC_TYPE_PRIMARY); // 初始化 g_hcMap, clientID 全置 -1

    struct KNET_RpcMessage req = {0};
    struct KNET_RpcMessage res = {0};
    struct TestHashRequest *hr = (struct TestHashRequest *)req.fixedLenData;
    hr->type = 1; // HASH_DESTROY

    int ret = HashRequestHandler(999, &req, &res); // id=999 无匹配
    DT_ASSERT_EQUAL(ret, 0);
    DT_ASSERT_EQUAL(res.ret, 0);

    Mock->Delete(KNET_RpcRegServer);
    DeleteMock(Mock);
}

/**
 * @brief HashRequestHandler HASH_CREATE, rte_hash_create 返回 NULL (失败路径)
 */
DTEST_CASE_F(HASH, TEST_HASH_REQ_CREATE_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    Mock->Create(KNET_RpcRegServer, TEST_GetFuncRetPositive(0));
    KNET_InitHash(KNET_PROC_TYPE_PRIMARY);

    Mock->Create(rte_hash_create, TEST_GetFuncRetPositive(0)); // 返回 NULL

    struct KNET_RpcMessage req = {0};
    struct KNET_RpcMessage res = {0};
    struct TestHashRequest *hr = (struct TestHashRequest *)req.fixedLenData;
    hr->type = 0; // HASH_CREATE
    hr->entries = 16;
    hr->keyLen = 4;

    int ret = HashRequestHandler(0, &req, &res);
    DT_ASSERT_EQUAL(ret, -1);
    DT_ASSERT_EQUAL(res.ret, -1);

    Mock->Delete(rte_hash_create);
    Mock->Delete(KNET_RpcRegServer);
    DeleteMock(Mock);
}

/**
 * @brief HashRequestHandler HASH_CREATE, rte_hash_create 返回非 NULL (成功路径)
 */
DTEST_CASE_F(HASH, TEST_HASH_REQ_CREATE_OK, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    Mock->Create(KNET_RpcRegServer, TEST_GetFuncRetPositive(0));
    KNET_InitHash(KNET_PROC_TYPE_PRIMARY);

    Mock->Create(rte_hash_create, TEST_GetFuncRetPositive(1)); // 返回非 NULL

    struct KNET_RpcMessage req = {0};
    struct KNET_RpcMessage res = {0};
    struct TestHashRequest *hr = (struct TestHashRequest *)req.fixedLenData;
    hr->type = 0; // HASH_CREATE
    hr->entries = 16;
    hr->keyLen = 4;

    int ret = HashRequestHandler(0, &req, &res);
    DT_ASSERT_EQUAL(ret, 0);
    DT_ASSERT_EQUAL(res.ret, 0);

    /* 清理: HASH_DESTROY 释放创建的 handle */
    Mock->Create(rte_hash_create, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_hash_free, TEST_GetFuncRetPositive(0));
    struct KNET_RpcMessage dreq = {0};
    struct KNET_RpcMessage dres = {0};
    struct TestHashRequest *dhr = (struct TestHashRequest *)dreq.fixedLenData;
    dhr->type = 1; // HASH_DESTROY
    HashRequestHandler(0, &dreq, &dres);

    Mock->Delete(rte_hash_free);
    Mock->Delete(rte_hash_create);
    Mock->Delete(KNET_RpcRegServer);
    DeleteMock(Mock);
}

/**
 * @brief FindFreeHandleSlot 直接调用
 */
DTEST_CASE_F(HASH, TEST_FIND_FREE_SLOT, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    Mock->Create(KNET_RpcRegServer, TEST_GetFuncRetPositive(0));
    KNET_InitHash(KNET_PROC_TYPE_PRIMARY);

    int idx = FindFreeHandleSlot();
    DT_ASSERT_NOT_EQUAL(idx, -1);

    Mock->Delete(KNET_RpcRegServer);
    DeleteMock(Mock);
}

/**
 * @brief KNET_HashTblDelEntry: tblInfo为NULL(key传入NULL)
 */
DTEST_CASE_F(HASH, TEST_HASH_DEL_NULL_KEY, NULL, NULL)
{
    KNET_HashTblInit();
    uint32_t tableId = 0;
    GetHashTblId(&tableId);

    int ret = KNET_HashTblDelEntry(tableId, NULL);
    DT_ASSERT_EQUAL(ret, -1);

    KNET_HashTblDeinit();
}

/**
 * @brief KNET_HashTblModifyEntry: tblInfo为NULL(key/data传入NULL)
 */
DTEST_CASE_F(HASH, TEST_HASH_MOD_NULL_PARAMS, NULL, NULL)
{
    KNET_HashTblInit();
    uint32_t tableId = 0;
    GetHashTblId(&tableId);

    uint8_t key = 100;
    int ret = KNET_HashTblModifyEntry(tableId, &key, NULL);
    DT_ASSERT_EQUAL(ret, -1);

    ret = KNET_HashTblModifyEntry(tableId, NULL, &key);
    DT_ASSERT_EQUAL(ret, -1);

    KNET_HashTblDeinit();
}

/**
 * @brief KNET_GetHashTblInfo: info为NULL
 */
DTEST_CASE_F(HASH, TEST_HASH_GET_INFO_NULL, NULL, NULL)
{
    KNET_HashTblInit();
    uint32_t tableId = 0;
    GetHashTblId(&tableId);

    int ret = KNET_GetHashTblInfo(tableId, NULL);
    DT_ASSERT_EQUAL(ret, -1);

    KNET_HashTblDeinit();
}

/**
 * @brief KNET_HashTblAddEntry: tblInfo为NULL(key/data传入NULL)
 */
DTEST_CASE_F(HASH, TEST_HASH_ADD_NULL_PARAMS, NULL, NULL)
{
    KNET_HashTblInit();
    uint32_t tableId = 0;
    GetHashTblId(&tableId);

    uint8_t key = 100;
    uint8_t data = 100;
    int ret = KNET_HashTblAddEntry(tableId, NULL, &data);
    DT_ASSERT_EQUAL(ret, -1);

    ret = KNET_HashTblAddEntry(tableId, &key, NULL);
    DT_ASSERT_EQUAL(ret, -1);

    KNET_HashTblDeinit();
}

/**
 * @brief KNET_GetHashTblNextEntry: key不存在(lookup失败)
 */
DTEST_CASE_F(HASH, TEST_HASH_GET_NEXT_KEY_NOT_EXIST, NULL, NULL)
{
    uint32_t tableId = 0;
    uint8_t key = 100;
    const uint8_t curKey = 100;
    uint8_t data = 100;
    g_hashFunc = FuncHash;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_RwlockReadLock, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_RwlockReadUnlock, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_hash_lookup_with_hash, TEST_GetFuncRetNegative(1));

    KNET_HashTblInit();
    GetHashTblId(&tableId);

    int ret = KNET_GetHashTblNextEntry(tableId, &curKey, &key, &data);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KNET_RwlockReadLock);
    Mock->Delete(KNET_RwlockReadUnlock);
    Mock->Delete(rte_hash_lookup_with_hash);
    KNET_HashTblDeinit();
    DeleteMock(Mock);
}

/**
 * @brief KNET_GetHashTblNextEntry: iterate失败
 */
DTEST_CASE_F(HASH, TEST_HASH_GET_NEXT_ITERATE_FAIL, NULL, NULL)
{
    uint32_t tableId = 0;
    uint8_t key = 100;
    const uint8_t curKey = 100;
    uint8_t data = 100;
    g_hashFunc = FuncHash;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_RwlockReadLock, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_RwlockReadUnlock, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_hash_lookup_with_hash, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_hash_iterate, TEST_GetFuncRetNegative(1));

    KNET_HashTblInit();
    GetHashTblId(&tableId);

    int ret = KNET_GetHashTblNextEntry(tableId, &curKey, &key, &data);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KNET_RwlockReadLock);
    Mock->Delete(KNET_RwlockReadUnlock);
    Mock->Delete(rte_hash_lookup_with_hash);
    Mock->Delete(rte_hash_iterate);
    KNET_HashTblDeinit();
    DeleteMock(Mock);
}

/**
 * @brief GetValidHashTbl: tableId超出范围
 */
DTEST_CASE_F(HASH, TEST_HASH_GET_VALID_INVALID_ID, NULL, NULL)
{
    KNET_HashTblInit();

    /* tableIdNum=0时已测,这里测tableId>=tableIdNum */
    uint8_t key = 100;
    uint8_t data = 100;
    int ret = KNET_HashTblAddEntry(999, &key, &data);
    DT_ASSERT_EQUAL(ret, -1);

    ret = KNET_HashTblLookupEntry(999, &key, &data);
    DT_ASSERT_EQUAL(ret, -1);

    KNET_HashTblDeinit();
}

/**
 * @brief DestroyHashTblLocked: 未初始化
 *        KNET_HashTblDeinit: 未初始化
 */
DTEST_CASE_F(HASH, TEST_HASH_DESTROY_NOT_INIT, NULL, NULL)
{
    /* 不调用KNET_HashTblInit, tableIdNum=0 */
    int ret = KNET_DestroyHashTbl(0);
    DT_ASSERT_EQUAL(ret, -1);

    /* KNET_HashTblDeinit 未初始化的路径 */
    KNET_HashTblDeinit();
}