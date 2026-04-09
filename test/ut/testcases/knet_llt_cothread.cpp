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
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/unistd.h>

#include "securec.h"
#include "common.h"
#include "mock.h"
#include "rte_lcore.h"
#include "dp_worker_api.h"
#include "knet_log.h"
#include "knet_config.h"
#include "knet_init.h"
#include "knet_init_tcp.h"
#include "knet_thread.h"
#include "knet_cothread_inner.h"
#include "knet_socket_api.h"

extern "C" {

#define MAX_CPU_NUM 128
#define INVALID_WORKER_ID (-1)
extern __thread int32_t g_currentWorkerId;
extern int32_t g_nextWorkerId; // 初始化为最大值，第一个worker id为0
extern bool g_knetCothreadInited;
extern bool IsCtrlCpuInThread(void);
extern int KNET_GetIfIndex(void);
extern int32_t DP_GetNetdevQueMap(int32_t wid, int32_t ifIndex, uint32_t* queMap, int32_t mapCnt);

#define MAX_WORKER_ID 512
typedef struct {
    KNET_DpWorkerInfo workerInfo[MAX_WORKER_ID];
    uint32_t coreIdToWorkerId[MAX_WORKER_ID];
    uint32_t maxWorkerId;
    char padding[4];   // 填充字节，确保结构体8 字节对齐
} DpWorkerIdTable;
extern DpWorkerIdTable g_dpWorkerIdTable;

static union KNET_CfgValue g_cfg = {.intValue = 1};
static union KNET_CfgValue *MockKnetGetCfg(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    g_cfg.intValue = 1;
    return &g_cfg;
}
}
DTEST_CASE_F(COTHREAD, TEST_COTHREAD_KNET_INIT, NULL, NULL)
{
    int ret = 0;
    ret = knet_init();
    DT_ASSERT_EQUAL(ret, -1);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetIfIndex, TEST_GetFuncRetPositive(1));
    Mock->Create(DP_GetNetdevQueMap, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetPositive(0));
    ret = knet_init();
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(KNET_TrafficResourcesInit);

    Mock->Create(KNET_TrafficResourcesInit, TEST_GetFuncRetNegative(1));
    ret = knet_init();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_TrafficResourcesInit);
    Mock->Delete(KNET_GetIfIndex);
    Mock->Delete(DP_GetNetdevQueMap);
    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

static union KNET_CfgValue *MockKnetGetCfg1(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    return &g_cfg;
}
DTEST_CASE_F(COTHREAD, TEST_COTHREAD_KNET_WORKER_INIT, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    int ret = 0;
    Mock->Create(KNET_GetIfIndex, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_GetNetdevQueMap, TEST_GetFuncRetPositive(0));
    ret = knet_worker_init();
    DT_ASSERT_EQUAL(ret, -1);

    g_knetCothreadInited = false;
    ret = knet_worker_init();
    DT_ASSERT_EQUAL(ret, -1);

    g_knetCothreadInited = true;
    g_currentWorkerId = 0;
    ret = knet_worker_init();
    DT_ASSERT_EQUAL(ret, -1);

    g_currentWorkerId = INVALID_WORKER_ID;
    ret = knet_worker_init();
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Create(IsCtrlCpuInThread, TEST_GetFuncRetPositive(0));
    ret = knet_worker_init();
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(IsCtrlCpuInThread);
    Mock->Delete(DP_GetNetdevQueMap);
    Mock->Delete(KNET_GetIfIndex);
    DeleteMock(Mock);
    (void)memset_s(&g_dpWorkerIdTable, sizeof(DpWorkerIdTable), 0, sizeof(DpWorkerIdTable));
}

DTEST_CASE_F(COTHREAD, TEST_COTHREAD_KNET_WORKER_RUN, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    g_currentWorkerId = INVALID_WORKER_ID;
    knet_worker_run();

    Mock->Create(DP_RunWorkerOnce, TEST_GetFuncRetPositive(0));
    g_currentWorkerId = 0;
    knet_worker_run();
    Mock->Delete(DP_RunWorkerOnce);

    DeleteMock(Mock);
}

DTEST_CASE_F(COTHREAD, TEST_COTHREAD_KNET_IS_WORKER_THREAD, NULL, NULL)
{
    int ret = 0;
    ret = knet_is_worker_thread();
    DT_ASSERT_EQUAL(ret, -1);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    g_currentWorkerId = 0;
    ret = knet_is_worker_thread();
    DT_ASSERT_EQUAL(ret, 0);

    g_currentWorkerId = INVALID_WORKER_ID;
    ret = knet_is_worker_thread();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}