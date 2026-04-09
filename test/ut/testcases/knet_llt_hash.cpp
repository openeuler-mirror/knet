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
extern void ReleaseHashTblId(uint32_t tableId);
extern int32_t GetHashTblId(uint32_t *tableId);
extern KNET_HASH_FUNC g_hashFunc;
}

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