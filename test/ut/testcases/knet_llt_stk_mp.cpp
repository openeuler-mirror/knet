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

#include <rte_lcore.h>
#include "securec.h"
#include "common.h"
#include "mock.h"

#include "knet_config.h"
#include "knet_stk_mp.h"

#define KNET_STK_MP_MAX_WORKER_NUM_TEST 32      // 与 knet_stk_mp.c 中的 KNET_STK_MP_MAX_WORKER_NUM 保持一致
#define MALLOC_SUCCESS_MAX_NUM_FAILED 2        // malloc 在第 2 次时失败，此时正在创建第二个 worker 的 mp.每次worker调用2次malloc创建所需的内存
#define MALLOC_SUCCESS_MAX_NUM_SUCCESS 100      // 足够大的值，确保 malloc 不会失败

extern bool g_knetStkMpIsInit;
extern unsigned g_knetStkMpNum;

struct KnetStkMpTest {  // 此结构体的内存布局需与 knet_stk_mp.c 中的 KnetStkMp 保持一致
    void** bufs;
    uint32_t count;
    uint32_t off;
    uint32_t mallocCount;
};

extern struct KnetStkMpTest g_knetStkMps[KNET_STK_MP_MAX_WORKER_NUM_TEST];

static int g_mallocCnt = 0;         // 调用 malloc 的次数
static int g_mallocCntMax = 0;      // malloc 返回成功的最大次数
static int g_workerNum = 0;         // worker num

static const union KNET_CfgValue *KNET_GetCfgMock(enum KNET_ConfKey key)
{
    static union KNET_CfgValue cfgVal = {0};
    cfgVal.intValue = g_workerNum;
    return &cfgVal;
}

static void *StkMpMallocMock(uint32_t size)
{
    ++g_mallocCnt;
    if (g_mallocCnt > g_mallocCntMax) {
        return NULL;
    }

    return calloc(1, size);
}

/**
 * @brief KNET_StkMpInit 函数，malloc 失败场景测试
 *  测试步骤：
 *  1. 置零全局变量 g_knetStkMpIsInit, g_knetStkMpNum 和 g_knetStkMps；
 *  2. 打桩 KNET_GetCfg，使其返回的 workerNum 为 2，有预期结果 1；
 *  3. 打桩 malloc，使其在第一次调用时失败，有预期结果 1；
 *  4. 入参 size 为 1，count 为 10，调用 KNET_StkMpInit，有预期结果 2；
 *  5. 打桩 malloc，使其在第三次调用时（创建第二个 mp 时）失败，有预期结果 1；
 *  6. 入参 size 为 1，count 为 10，调用 KNET_StkMpInit，有预期结果 2；
 *  预期结果：
 *  1. 打桩成功；
 *  2. 返回失败，全局变量仍为 0，无内存泄漏；
 */
DTEST_CASE_F(STK_MP, TEST_STK_MP_INIT_MALLOC_FAILED, NULL, NULL)
{
    g_knetStkMpIsInit = false;
    g_knetStkMpNum = 0;
    (void)memset_s(g_knetStkMps, sizeof(g_knetStkMps), 0, sizeof(g_knetStkMps));

    uint32_t ret;
    uint32_t size = 1;
    uint32_t count = 10;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    
    // workerNum 为 2
    g_workerNum = 2;
    Mock->Create(KNET_GetCfg, KNET_GetCfgMock);

    // malloc 在第一次调用时即失败
    g_mallocCntMax = 0;
    g_mallocCnt = 0;
    Mock->Create(malloc, StkMpMallocMock);

    ret = KNET_StkMpInit(size, count);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);
    DT_ASSERT_EQUAL(g_knetStkMpIsInit, false);
    DT_ASSERT_EQUAL(g_knetStkMpNum, 0);

    for (uint32_t i = 0; i < KNET_STK_MP_MAX_WORKER_NUM_TEST; ++i) {
        DT_ASSERT_EQUAL(g_knetStkMps[i].count, 0);
        DT_ASSERT_EQUAL(g_knetStkMps[i].off, 0);
        DT_ASSERT_EQUAL(g_knetStkMps[i].bufs, NULL);
    }
    
    // malloc 在创建第二个 worker 的 mp 时失败
    g_mallocCntMax = MALLOC_SUCCESS_MAX_NUM_FAILED;
    g_mallocCnt = 0;

    ret = KNET_StkMpInit(size, count);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);
    DT_ASSERT_EQUAL(g_knetStkMpIsInit, false);
    DT_ASSERT_EQUAL(g_knetStkMpNum, 0);

    for (uint32_t i = 0; i < KNET_STK_MP_MAX_WORKER_NUM_TEST; ++i) {
        DT_ASSERT_EQUAL(g_knetStkMps[i].count, 0);
        DT_ASSERT_EQUAL(g_knetStkMps[i].off, 0);
        DT_ASSERT_EQUAL(g_knetStkMps[i].bufs, NULL);
    }

    Mock->Delete(KNET_GetCfg);
    Mock->Delete(malloc);
    DeleteMock(Mock);
}

/**
 * @brief KNET_StkMpInit 函数，成功场景，然后调用 KNET_StkMpDeInit 释放内存池
 *  测试步骤：
 *  1. 置零全局变量 g_knetStkMpIsInit, g_knetStkMpNum 和 g_knetStkMps；
 *  2. 未初始化内存池前，调用 KNET_StkMpDeInit 函数，有预期结果 1；
 *  2. 打桩 KNET_GetCfg，使其返回的 workerNum 为 2，有预期结果 2；
 *  3. 打桩 malloc，使其在调用过程中不失败，有预期结果 2；
 *  4. 入参 size 为 1，count 为 10，调用 KNET_StkMpInit，有预期结果 3；
 *  5. 调用 KNET_StkMpDeInit，有预期结果 1；
 *  预期结果：
 *  1. 返回成功，全局变量为 0，无内存泄漏；
 *  2. 打桩成功；
 *  3. 返回成功，全局变量不再为 0；
 */
DTEST_CASE_F(STK_MP, TEST_STK_MP_INIT_NORMAL, NULL, NULL)
{
    g_knetStkMpIsInit = false;
    g_knetStkMpNum = 0;
    (void)memset_s(g_knetStkMps, sizeof(g_knetStkMps), 0, sizeof(g_knetStkMps));

    uint32_t ret;
    uint32_t size = 1;
    uint32_t count = 10;

    ret = KNET_StkMpDeInit();
    DT_ASSERT_EQUAL(ret, KNET_OK);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    // workerNum 为 2
    g_workerNum = 2;
    Mock->Create(KNET_GetCfg, KNET_GetCfgMock);
    DT_ASSERT_EQUAL(g_knetStkMpIsInit, false);
    DT_ASSERT_EQUAL(g_knetStkMpNum, 0);

    // malloc 在创建过程中不失败
    g_mallocCntMax = MALLOC_SUCCESS_MAX_NUM_SUCCESS;
    g_mallocCnt = 0;
    Mock->Create(malloc, StkMpMallocMock);

    ret = KNET_StkMpInit(size, count);
    DT_ASSERT_EQUAL(ret, KNET_OK);
    DT_ASSERT_EQUAL(g_knetStkMpIsInit, true);
    DT_ASSERT_EQUAL(g_knetStkMpNum, g_workerNum);

    for (uint32_t i = 0; i < g_workerNum; ++i) {    // 两个 worker 的 mp 创建成功
        DT_ASSERT_EQUAL(g_knetStkMps[i].count, count);
        DT_ASSERT_EQUAL(g_knetStkMps[i].off, count);
        DT_ASSERT_NOT_EQUAL(g_knetStkMps[i].bufs, NULL);
    }

    for (uint32_t i = g_workerNum; i < KNET_STK_MP_MAX_WORKER_NUM_TEST; ++i) {    // 其他的仍未被创建
        DT_ASSERT_EQUAL(g_knetStkMps[i].count, 0);
        DT_ASSERT_EQUAL(g_knetStkMps[i].off, 0);
        DT_ASSERT_EQUAL(g_knetStkMps[i].bufs, NULL);
    }
    
    // 释放创建的内存池
    ret = KNET_StkMpDeInit();
    DT_ASSERT_EQUAL(ret, KNET_OK);
    DT_ASSERT_EQUAL(g_knetStkMpIsInit, false);
    DT_ASSERT_EQUAL(g_knetStkMpNum, 0);

    for (uint32_t i = 0; i < KNET_STK_MP_MAX_WORKER_NUM_TEST; ++i) {
        DT_ASSERT_EQUAL(g_knetStkMps[i].count, 0);
        DT_ASSERT_EQUAL(g_knetStkMps[i].off, 0);
        DT_ASSERT_EQUAL(g_knetStkMps[i].bufs, NULL);
    }

    Mock->Delete(KNET_GetCfg);
    Mock->Delete(malloc);
    DeleteMock(Mock);
}

// 由于无法直接打桩 dpdk static inline 函数 rte_lcore_id()，直接修改线程变量来控制其返回值
extern __thread unsigned int per_lcore__lcore_id;

/**
 * @brief KNET_StkMpAlloc 函数测试
 *  测试步骤：
 *  1. 置零全局变量 g_knetStkMpIsInit, g_knetStkMpNum 和 g_knetStkMps；
 *  2. 未初始化内存池前，调用 KNET_StkMpAlloc 函数，有预期结果 1；
 *  3. 打桩 KNET_GetCfg，使其返回的 workerNum 为 2，有预期结果 2；
 *  4. 打桩 malloc，使其在调用过程中不失败，有预期结果 2；
 *  5. 入参 size 为 1，count 为 1，调用 KNET_StkMpInit，有预期结果 3；
 *  6. 调用 KNET_StkMpAlloc，有预期结果 4；
 *  7. 再一次调用 KNET_StkMpAlloc，有预期结果 1；
 *  8. 调用 KNET_StkMpDeInit，有预期结果 5；
 *  9. 调用 KNET_StkMpFree 释放 6 申请的内存，有预期结果 6；
 *  10. 调用 KNET_StkMpDeInit，有预期结果 7；
 *  预期结果：
 *  1. 内存申请失败，返回空指针；
 *  2. 打桩成功；
 *  3. 返回成功，全局变量不再为 0；
 *  4. 内存申请成功；
 *  5. 返回失败，全局变量不为 0；
 *  6. 释放成功；
 *  7. 返回成功，全局变量为 0，无内存泄漏；
 */
DTEST_CASE_F(STK_MP, TEST_STK_MP_ALLOC_NORMAL, NULL, NULL)
{
    g_knetStkMpIsInit = false;
    g_knetStkMpNum = 0;
    (void)memset_s(g_knetStkMps, sizeof(g_knetStkMps), 0, sizeof(g_knetStkMps));

    uint32_t ret;
    uint32_t size = 1;
    uint32_t count = 1;
    void *bufs[2] = {0};

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    per_lcore__lcore_id = 0;    // 令 rte_lcore_id() 返回 0

    bufs[0] = KNET_StkMpAlloc();
    DT_ASSERT_EQUAL(bufs[0], NULL);

    // workerNum 为 2
    g_workerNum = 2;
    Mock->Create(KNET_GetCfg, KNET_GetCfgMock);
    DT_ASSERT_EQUAL(g_knetStkMpIsInit, false);
    DT_ASSERT_EQUAL(g_knetStkMpNum, 0);

    // malloc 在创建过程中不失败
    g_mallocCntMax = MALLOC_SUCCESS_MAX_NUM_SUCCESS;
    g_mallocCnt = 0;
    Mock->Create(malloc, StkMpMallocMock);

    ret = KNET_StkMpInit(size, count);
    DT_ASSERT_EQUAL(ret, KNET_OK);
    DT_ASSERT_EQUAL(g_knetStkMpIsInit, true);
    DT_ASSERT_EQUAL(g_knetStkMpNum, g_workerNum);

    for (uint32_t i = 0; i < g_workerNum; ++i) {    // 两个 worker 的 mp 创建成功
        DT_ASSERT_EQUAL(g_knetStkMps[i].count, count);
        DT_ASSERT_EQUAL(g_knetStkMps[i].off, count);
        DT_ASSERT_NOT_EQUAL(g_knetStkMps[i].bufs, NULL);
    }

    for (uint32_t i = g_workerNum; i < KNET_STK_MP_MAX_WORKER_NUM_TEST; ++i) {    // 其他的仍未被创建
        DT_ASSERT_EQUAL(g_knetStkMps[i].count, 0);
        DT_ASSERT_EQUAL(g_knetStkMps[i].off, 0);
        DT_ASSERT_EQUAL(g_knetStkMps[i].bufs, NULL);
    }

    bufs[0] = KNET_StkMpAlloc();
    DT_ASSERT_NOT_EQUAL(bufs[0], NULL);
    bufs[1] = KNET_StkMpAlloc();
    DT_ASSERT_EQUAL(bufs[1], NULL);

    // 存在被使用内存单元的情况下调用 KNET_StkMpDeInit
    ret = KNET_StkMpDeInit();
    DT_ASSERT_EQUAL(ret, KNET_ERROR);
    DT_ASSERT_EQUAL(g_knetStkMpIsInit, true);
    DT_ASSERT_EQUAL(g_knetStkMpNum, g_workerNum);

    KNET_StkMpFree(bufs[0]);
    ret = KNET_StkMpDeInit();
    DT_ASSERT_EQUAL(ret, KNET_OK);
    DT_ASSERT_EQUAL(g_knetStkMpIsInit, false);
    DT_ASSERT_EQUAL(g_knetStkMpNum, 0);

    for (uint32_t i = 0; i < KNET_STK_MP_MAX_WORKER_NUM_TEST; ++i) {
        DT_ASSERT_EQUAL(g_knetStkMps[i].count, 0);
        DT_ASSERT_EQUAL(g_knetStkMps[i].off, 0);
        DT_ASSERT_EQUAL(g_knetStkMps[i].bufs, NULL);
    }

    Mock->Delete(KNET_GetCfg);
    Mock->Delete(malloc);
    DeleteMock(Mock);
}

/**
 * @brief KNET_StkMpFree 函数测试，释放失败场景
 *  测试步骤：
 *  1. 置零全局变量 g_knetStkMpIsInit, g_knetStkMpNum 和 g_knetStkMps；
 *  2. 未初始化内存池前，调用 KNET_StkMpFree 函数，有预期结果 1；
 *  3. 打桩 KNET_GetCfg，使其返回的 workerNum 为 2，有预期结果 2；
 *  4. 打桩 malloc，使其在调用过程中不失败，有预期结果 2；
 *  5. 入参 size 为 1，count 为 1，调用 KNET_StkMpInit，有预期结果 3；
 *  6. 调用 KNET_StkMpFree，有预期结果 1；
 *  7. 调用 KNET_StkMpDeInit，有预期结果 4；
 *  预期结果：
 *  1. 释放失败，内存池无变化；
 *  2. 打桩成功；
 *  3. 返回成功，全局变量不再为 0；
 *  4. 返回成功，全局变量为 0，无内存泄漏；
 */
DTEST_CASE_F(STK_MP, TEST_STK_MP_FREE_FAILED, NULL, NULL)
{
    g_knetStkMpIsInit = false;
    g_knetStkMpNum = 0;
    (void)memset_s(g_knetStkMps, sizeof(g_knetStkMps), 0, sizeof(g_knetStkMps));

    uint32_t ret;
    uint32_t size = 1;
    uint32_t count = 1;
    void *buf = malloc(1);
    DT_ASSERT_NOT_EQUAL(buf, NULL);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    per_lcore__lcore_id = 0;    // 令 rte_lcore_id() 返回 0

    // 释放失败，off 无变化
    uint32_t preOff = g_knetStkMps[0].off;
    KNET_StkMpFree(buf);
    DT_ASSERT_EQUAL(g_knetStkMps[0].off, g_knetStkMps[0].off);

    // workerNum 为 2
    g_workerNum = 2;
    Mock->Create(KNET_GetCfg, KNET_GetCfgMock);
    DT_ASSERT_EQUAL(g_knetStkMpIsInit, false);
    DT_ASSERT_EQUAL(g_knetStkMpNum, 0);

    // malloc 在创建过程中不失败
    g_mallocCntMax = MALLOC_SUCCESS_MAX_NUM_SUCCESS;
    g_mallocCnt = 0;
    Mock->Create(malloc, StkMpMallocMock);

    ret = KNET_StkMpInit(size, count);
    DT_ASSERT_EQUAL(ret, KNET_OK);
    DT_ASSERT_EQUAL(g_knetStkMpIsInit, true);
    DT_ASSERT_EQUAL(g_knetStkMpNum, g_workerNum);

    for (uint32_t i = 0; i < g_workerNum; ++i) {    // 两个 worker 的 mp 创建成功
        DT_ASSERT_EQUAL(g_knetStkMps[i].count, count);
        DT_ASSERT_EQUAL(g_knetStkMps[i].off, count);
        DT_ASSERT_NOT_EQUAL(g_knetStkMps[i].bufs, NULL);
    }

    for (uint32_t i = g_workerNum; i < KNET_STK_MP_MAX_WORKER_NUM_TEST; ++i) {    // 其他的仍未被创建
        DT_ASSERT_EQUAL(g_knetStkMps[i].count, 0);
        DT_ASSERT_EQUAL(g_knetStkMps[i].off, 0);
        DT_ASSERT_EQUAL(g_knetStkMps[i].bufs, NULL);
    }

    // 释放失败 off 无变化
    preOff = g_knetStkMps[0].off;
    KNET_StkMpFree(buf);
    DT_ASSERT_EQUAL(g_knetStkMps[0].off, g_knetStkMps[0].off);

    ret = KNET_StkMpDeInit();
    DT_ASSERT_EQUAL(ret, KNET_OK);
    DT_ASSERT_EQUAL(g_knetStkMpIsInit, false);
    DT_ASSERT_EQUAL(g_knetStkMpNum, 0);

    for (uint32_t i = 0; i < KNET_STK_MP_MAX_WORKER_NUM_TEST; ++i) {
        DT_ASSERT_EQUAL(g_knetStkMps[i].count, 0);
        DT_ASSERT_EQUAL(g_knetStkMps[i].off, 0);
        DT_ASSERT_EQUAL(g_knetStkMps[i].bufs, NULL);
    }

    free(buf);
    Mock->Delete(KNET_GetCfg);
    Mock->Delete(malloc);
    DeleteMock(Mock);
}