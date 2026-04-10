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
#include <unistd.h>
#include <fcntl.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/epoll.h>

#include "rte_ethdev.h"
#include "rte_pdump.h"
#include "dp_cfg_api.h"
#include "securec.h"
#include "common.h"
#include "mock.h"

#include "knet_mock.h"
#include "knet_types.h"
#include "knet_log.h"
#include "knet_pktpool.h"
#include "knet_hash_table.h"
#include "knet_lock.h"
#include "knet_dpdk_init.h"
#include "knet_rpc.h"
#include "knet_transmission.h"
#include "knet_config.h"
#include "knet_fmm.h"
#include "knet_telemetry.h"
#include "knet_pdump.h"

extern "C" {
int MainDaemon(int argc, char *argv[]);
int DaemonInitResource();
int DaemonInitPublicResource();
int DaemonUninitResource();
int DaemonMainLooper();
int DaemonUninitPublicResource();
}

static union KNET_CfgValue g_cfg = {.intValue = 1};
static union KNET_CfgValue *MOCK_KNET_GetCfg(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    if (key == CONF_COMMON_MODE) {
        g_cfg.intValue = KNET_RUN_MODE_MULTIPLE;
    } else if (key == CONF_INTERFACE_BOND_ENABLE) {
        g_cfg.intValue = 0;
    }

    return &g_cfg;
}

static const struct rte_memzone* MOCK_KNET_MultiPdumpInit(void)
{
    const static struct rte_memzone ret = {0};
    return &ret;
}

void MOCK_KNET_LogUninit(void) {}
void MOCK_KNET_LogInit(void) {}
void MOCK_KNET_LogNormal() {}
void MOCK_KNET_LogLevelSetByStr(const char *levelStr) {}

DTEST_CASE_F(MAIN_DAEMON, TEST_INIT_PUBLIC_RESOURCE, NULL, NULL)
{
    int32_t ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KNET_InitDpdk, TEST_GetFuncRetNegative(1));
    ret = DaemonInitPublicResource();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_InitDpdk);

    Mock->Create(KNET_InitDpdk, TEST_GetFuncRetPositive(0));
    ret = DaemonInitPublicResource();
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Create(KNET_InitHash, TEST_GetFuncRetNegative(1));
    Mock->Create(KNET_UninitDpdk, TEST_GetFuncRetPositive(0));
    ret = DaemonInitPublicResource();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_InitHash);

    Mock->Create(KNET_InitFmm, TEST_GetFuncRetNegative(1));
    Mock->Create(KNET_UninitHash, TEST_GetFuncRetPositive(0));
    ret = DaemonInitPublicResource();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_InitFmm);

    Mock->Create(KNET_InitFmm, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_InitHash, TEST_GetFuncRetPositive(0));
    ret = DaemonInitPublicResource();
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KNET_InitHash);
    Mock->Delete(KNET_InitFmm);
    Mock->Delete(KNET_InitDpdk);
    Mock->Delete(KNET_UninitDpdk);
    Mock->Delete(KNET_UninitHash);
    DeleteMock(Mock);
}

DTEST_CASE_F(MAIN_DAEMON, TEST_INIT_RESOURCE, NULL, NULL)
{
    int32_t ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_LogInit, MOCK_KNET_LogInit);
    Mock->Create(KNET_LogNormal, MOCK_KNET_LogNormal);
    Mock->Create(KNET_LogLevelSetByStr, MOCK_KNET_LogLevelSetByStr);
    Mock->Create(KNET_InitCfg, TEST_GetFuncRetNegative(1));

    Mock->Create(KNET_MultiPdumpInit, MOCK_KNET_MultiPdumpInit);
    Mock->Create(DaemonInitPublicResource, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_TelemetryStartPersistThread, TEST_GetFuncRetPositive(1));

    ret = DaemonInitResource();
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KNET_InitCfg);
    Mock->Create(KNET_InitCfg, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_GetCfg, MOCK_KNET_GetCfg);

    ret = DaemonInitResource();
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(KNET_InitCfg);
    Mock->Delete(KNET_LogInit);
    Mock->Delete(KNET_TelemetryStartPersistThread);
    Mock->Delete(KNET_LogNormal);
    Mock->Delete(KNET_LogLevelSetByStr);
    Mock->Delete(KNET_GetCfg);
    Mock->Delete(DaemonInitPublicResource);
    Mock->Delete(KNET_MultiPdumpInit);
    DeleteMock(Mock);
}


DTEST_CASE_F(MAIN_DAEMON, TEST_KNET_MAIN, NULL, NULL)
{
    int32_t ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(DaemonInitResource, TEST_GetFuncRetNegative(1));
    ret = MainDaemon(0, NULL);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(DaemonInitResource);
    
    Mock->Create(DaemonInitResource, TEST_GetFuncRetPositive(0));
    Mock->Create(DaemonMainLooper, TEST_GetFuncRetPositive(0));
    Mock->Create(DaemonUninitResource, TEST_GetFuncRetNegative(1));
    ret = MainDaemon(0, NULL);
    DT_ASSERT_EQUAL(ret, -1);
 
    Mock->Delete(DaemonUninitResource);
 
    Mock->Create(DaemonUninitResource, TEST_GetFuncRetPositive(0));

    ret = MainDaemon(0, NULL);

    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(DaemonInitResource);
    Mock->Delete(DaemonMainLooper);
    Mock->Delete(DaemonUninitResource);
    DeleteMock(Mock);
}

DTEST_CASE_F(MAIN_DAEMON, TEST_UNINIT, NULL, NULL)
{
    int32_t ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    
    Mock->Create(DaemonUninitPublicResource, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_UninitDpdkTelemetry, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_MultiPdumpUninit, TEST_GetFuncRetPositive(0));

    Mock->Create(KNET_LogUninit, MOCK_KNET_LogUninit);
    Mock->Create(DaemonInitPublicResource, TEST_GetFuncRetPositive(0));

 
    ret = DaemonUninitResource();
    DT_ASSERT_EQUAL(ret, 0);
 
    Mock->Delete(DaemonInitPublicResource);
    Mock->Delete(KNET_UninitDpdkTelemetry);
    Mock->Delete(KNET_MultiPdumpUninit);
    Mock->Delete(KNET_LogUninit);
    Mock->Delete(DaemonUninitPublicResource);
 
    DeleteMock(Mock);
}

DTEST_CASE_F(MAIN_DAEMON, TEST_UNINIT_PUBLIC_RESOURCE, NULL, NULL)
{
    int32_t ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KNET_UninitTrans, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_UninitDpdk, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_UninitHash, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_UnInitFmm, TEST_GetFuncRetPositive(0));

    ret = DaemonUninitPublicResource();
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KNET_UninitTrans);
    Mock->Delete(KNET_UninitHash);
    Mock->Delete(KNET_UnInitFmm);
    Mock->Delete(KNET_UninitDpdk);
    DeleteMock(Mock);
}

DTEST_CASE_F(MAIN_DAEMON, TEST_MAIN_lOOPER_NORMAL, NULL, NULL)
{
    int32_t ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KNET_RpcRun, TEST_GetFuncRetPositive(0));
    Mock->Create(DaemonInitResource, TEST_GetFuncRetPositive(0));
    ret = DaemonMainLooper();
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KNET_RpcRun);
    Mock->Delete(DaemonInitResource);
    DeleteMock(Mock);
}

DTEST_CASE_F(MAIN_DAEMON, TEST_MAIN_lOOPER_ABNORMAL, NULL, NULL)
{
    int32_t ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KNET_RpcRun, TEST_GetFuncRetNegative(1));

    ret = DaemonMainLooper();
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KNET_RpcRun);
    DeleteMock(Mock);
}