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
#include "dp_init_api.h"
#include "dp_netdev_api.h"
#include "dp_worker_api.h"
#include "dp_cfg_api.h"
#include "dp_cpd_api.h"
#include "dp_tbm_api.h"

#include "knet_log.h"
#include "knet_config.h"
#include "knet_thread.h"
#include "knet_pkt.h"
#include "knet_socket_bridge.h"
#include "knet_tcp_symbols.h"
#include "knet_sal_tcp.h"
#include "knet_dpdk_init.h"
#include "knet_hash_table.h"
#include "knet_capability.h"
#include "knet_init.h"
#include "knet_tun.h"
#include "knet_telemetry.h"
#include "knet_init_tcp.h"
#include "knet_sal_inner.h"
#include "knet_tcp_api_init.h"
#include "knet_signal_tcp.h"
#include "tcp_fd.h"
#include "tcp_os.h"

#include "common.h"
#include "mock.h"

extern "C" {
#include "knet_telemetry.h"
int32_t KnetCreateSignalBlockMonitorThread(void);
int32_t CreateCpThread(void);
int32_t CreateMultiPdumpThread(void);
int32_t StartDpThread(void);
void Uninit(void);
void ConfigInit(void);
void ShowDpStats(KNET_TelemetryInfo *telemetryInfo, int queId);
int32_t DpdkStackInit(void);
void *CpThreadFunc(void *args);
void KnetInit(void);
void KnetUninit(void);
int LcoreMainloop(void *arg);
void ProcessTelemetryQueueMapWorker();
int JoinDpdkAndStackThread(void);
}

#define MAX_WORKER_ID 512
typedef struct {
    KNET_SpinLock lock;
    uint64_t threadID;
    bool isCreated;
} KnetThreadInfo;

static union KNET_CfgValue g_cfg = {.intValue = 1};
static union KNET_CfgValue *MockKnetGetCfg(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    g_cfg.intValue = 1;
    return &g_cfg;
}

static union KNET_CfgValue *MockKnetGetCfg0(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    g_cfg.intValue = 0;
    return &g_cfg;
}

void MOCK_KNET_HashTblDeinit() {}

bool MOCK_KNET_DpIsForkedParent()
{
    return true;
}

int MOCK_OsGetsockopt(int sockfd, int level, int optname, int *optval, socklen_t *optlen)
{
    *optval = SOCK_STREAM;
    return 0;
}
static void MOCK_KNET_LogLevelSetByStr(const char *levelStr) {}

DTEST_CASE_F(KNET_INIT, TEST_KNET_UNINIT, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    
    Mock->Create(KNET_DpIsForkedParent, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_HashTblDeinit, MOCK_KNET_HashTblDeinit);
    Mock->Create(KNET_JoinThread, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_DpMaxWorkerIdGet, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_PktBatchFree, TEST_GetFuncRetPositive(0));
    Uninit();
    Mock->Delete(KNET_DpIsForkedParent);
    Mock->Delete(KNET_HashTblDeinit);
    Mock->Delete(KNET_JoinThread);
    Mock->Delete(KNET_DpMaxWorkerIdGet);
    Mock->Delete(KNET_PktBatchFree);
    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_INIT, TEST_KNET_INIT_UNINIT_NORMAL_CASE1, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(memcpy_s, TEST_GetFuncRetPositive(0));
    Mock->Create(strcpy_s, TEST_GetFuncRetPositive(0));
    Mock->Create(memset_s, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_InitCfg, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_SAL_Init, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_InitDpdk, TEST_GetFuncRetNegative(1));
    Mock->Create(KNET_LogLevelSetByStr, MOCK_KNET_LogLevelSetByStr);

    ConfigInit();
    KNET_DpSignalRegAll();
    KNET_TrafficResourcesInit();
    KNET_WARN("knet init success");

    Mock->Delete(memcpy_s);
    Mock->Delete(strcpy_s);
    Mock->Delete(memset_s);
    Mock->Delete(KNET_InitCfg);
    Mock->Delete(KNET_SAL_Init);
    Mock->Delete(KNET_InitDpdk);
    Mock->Delete(KNET_LogLevelSetByStr);
    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_INIT, TEST_KNET_INIT_UNINIT_NORMAL_CASE2, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(memcpy_s, TEST_GetFuncRetPositive(0));
    Mock->Create(strcpy_s, TEST_GetFuncRetPositive(0));
    Mock->Create(memset_s, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_InitCfg, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_SAL_Init, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_InitDpdk, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_HashTblInit, TEST_GetFuncRetNegative(1));
    Mock->Create(KNET_LogLevelSetByStr, MOCK_KNET_LogLevelSetByStr);

    ConfigInit();
    KNET_DpSignalRegAll();
    KNET_TrafficResourcesInit();
    KNET_WARN("knet init success");

    Mock->Delete(memcpy_s);
    Mock->Delete(strcpy_s);
    Mock->Delete(memset_s);
    Mock->Delete(KNET_InitCfg);
    Mock->Delete(KNET_SAL_Init);
    Mock->Delete(KNET_InitDpdk);
    Mock->Delete(KNET_HashTblInit);
    Mock->Delete(KNET_LogLevelSetByStr);

    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_INIT, TEST_KNET_INIT_UNINIT_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(memcpy_s, TEST_GetFuncRetPositive(0));
    Mock->Create(strcpy_s, TEST_GetFuncRetPositive(0));
    Mock->Create(memset_s, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_InitCfg, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_SAL_Init, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_InitDpdk, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_HashTblInit, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_Cfg, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_Init, TEST_GetFuncRetPositive(0));
    /* DP_CreateNetdev应该返回指针，这里返回1需要保证后续不会用到指针指向的地址 */
    Mock->Create(DP_CreateNetdev, TEST_GetFuncRetPositive(1));
    Mock->Create(DP_ProcIfreq, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_RtCfg, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_CpdInit, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_CreateThread, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_ThreadNameSet, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_DpMaxWorkerIdGet, TEST_GetFuncRetPositive(2)); // 表示有2个worker
    Mock->Create(DP_CpdRunOnce, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_eal_remote_launch, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_HashTblDeinit, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_TAPCreate, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetSetDpCfg, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_LogLevelSetByStr, MOCK_KNET_LogLevelSetByStr);

    ConfigInit();
    KNET_DpSignalRegAll();
    KNET_TrafficResourcesInit();
    KNET_WARN("knet init success");

    Mock->Delete(KnetSetDpCfg);
    Mock->Delete(KNET_TAPCreate);
    Mock->Delete(memcpy_s);
    Mock->Delete(strcpy_s);
    Mock->Delete(memset_s);
    Mock->Delete(KNET_InitCfg);
    Mock->Delete(KNET_SAL_Init);
    Mock->Delete(KNET_InitDpdk);
    Mock->Delete(KNET_HashTblInit);
    Mock->Delete(DP_Init);
    Mock->Delete(DP_CreateNetdev);
    Mock->Delete(DP_ProcIfreq);
    Mock->Delete(DP_RtCfg);
    Mock->Delete(DP_CpdInit);
    Mock->Delete(KNET_CreateThread);
    Mock->Delete(KNET_ThreadNameSet);
    Mock->Delete(KNET_DpMaxWorkerIdGet);
    Mock->Delete(DP_CpdRunOnce);
    Mock->Delete(rte_eal_remote_launch);
    Mock->Delete(KNET_HashTblDeinit);
    Mock->Delete(KNET_LogLevelSetByStr);

    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_INIT, TEST_KNET_UNINIT_PARENT, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    
    Mock->Create(KNET_DpIsForkedParent, MOCK_KNET_DpIsForkedParent);
    Mock->Create(KNET_AllHijackFdsClose, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    Mock->Create(KNET_UninitDpdkTelemetry, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_TapFree, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_HashTblDeinit, MOCK_KNET_HashTblDeinit);
    Mock->Create(KNET_JoinThread, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_DpMaxWorkerIdGet, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_PktBatchFree, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_SocketCountGet, TEST_GetFuncRetPositive(0));
    Mock->Create(JoinDpdkAndStackThread, TEST_GetFuncRetPositive(0));
    Uninit();
    Mock->Delete(JoinDpdkAndStackThread);
    Mock->Delete(DP_SocketCountGet);
    Mock->Delete(KNET_DpIsForkedParent);
    Mock->Delete(KNET_HashTblDeinit);
    Mock->Delete(KNET_JoinThread);
    Mock->Delete(KNET_DpMaxWorkerIdGet);
    Mock->Delete(KNET_PktBatchFree);
    Mock->Delete(KNET_TapFree);
    Mock->Delete(KNET_UninitDpdkTelemetry);
    Mock->Delete(KNET_GetCfg);
    Mock->Delete(KNET_AllHijackFdsClose);
    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_INIT, TEST_KNET_CONFIG_INIT, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    
    Mock->Create(KNET_InitCfg, TEST_GetFuncRetNegative(1));
    Mock->Create(KNET_LogLevelSetByStr, TEST_GetFuncRetPositive(0));
    ConfigInit();

    Mock->Delete(KNET_InitCfg);
    Mock->Delete(KNET_LogLevelSetByStr);
    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_INIT, TEST_KNET_STACK_INIT_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    
    Mock->Create(KNET_TAPCreate, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetSetDpCfg, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_Init, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_CreateNetdev, TEST_GetFuncRetPositive(1)); // 注：打桩返回1地址，指针不可使用
    Mock->Create(DP_ProcIfreq, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_RtCfg, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_GetCap, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_ClearCap, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_CpdInit, TEST_GetFuncRetPositive(0));

    KNET_InitDp();

    Mock->Delete(KNET_TAPCreate);
    Mock->Delete(KnetSetDpCfg);
    Mock->Delete(DP_Init);
    Mock->Delete(DP_CreateNetdev);
    Mock->Delete(DP_ProcIfreq);
    Mock->Delete(DP_RtCfg);
    Mock->Delete(KNET_GetCap);
    Mock->Delete(KNET_ClearCap);
    Mock->Delete(DP_CpdInit);
    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_INIT, TEST_KNET_THREAD_STOP, NULL, NULL)
{
    KNET_SetDpdkAndStackThreadStop();
}

DTEST_CASE_F(KNET_INIT, TEST_KNET_API_INIT, NULL, NULL)
{
    KNET_FdInit();
    KNET_AllHijackFdsClose();
}

DTEST_CASE_F(KNET_INIT, TEST_KNET_SHOW_TCP_STATS, NULL, NULL)
{
    KNET_TelemetryInfo telemetryInfo = {0};
    int queId = 0; // 以queue 0 为例
    telemetryInfo.msgReady[queId] == 1;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(DP_ShowStatistics, TEST_GetFuncRetPositive(0));
    ShowDpStats(&telemetryInfo, queId);
    DT_ASSERT_EQUAL(telemetryInfo.msgReady[queId], 0);
    Mock->Delete(DP_ShowStatistics);

    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_INIT, TEST_KNET_DPDK_STACK_INIT, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_SAL_Init, TEST_GetFuncRetNegative(1));
    int32_t ret = DpdkStackInit();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_SAL_Init);

    Mock->Create(KNET_SAL_Init, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_InitDpdk, TEST_GetFuncRetNegative(1));
    ret = DpdkStackInit();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_InitDpdk);

    Mock->Create(KNET_InitDpdk, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_InitDp, TEST_GetFuncRetNegative(1));
    ret = DpdkStackInit();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_InitDp);

    Mock->Create(KNET_InitDp, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetCreateSignalBlockMonitorThread, TEST_GetFuncRetNegative(1));
    ret = DpdkStackInit();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KnetCreateSignalBlockMonitorThread);

    Mock->Create(KnetCreateSignalBlockMonitorThread, TEST_GetFuncRetPositive(0));
    Mock->Create(CreateMultiPdumpThread, TEST_GetFuncRetNegative(1));
    ret = DpdkStackInit();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(CreateMultiPdumpThread);

    Mock->Create(CreateMultiPdumpThread, TEST_GetFuncRetPositive(0));
    Mock->Create(CreateCpThread, TEST_GetFuncRetNegative(1));
    ret = DpdkStackInit();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(CreateCpThread);

    Mock->Create(CreateCpThread, TEST_GetFuncRetPositive(0));
    Mock->Create(StartDpThread, TEST_GetFuncRetNegative(1));
    ret = DpdkStackInit();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(StartDpThread);

    Mock->Create(StartDpThread, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_SetDpInited, TEST_GetFuncRetPositive(0));
    ret = DpdkStackInit();
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(KNET_SAL_Init);
    Mock->Delete(KNET_InitDpdk);
    Mock->Delete(KNET_InitDp);
    Mock->Delete(KnetCreateSignalBlockMonitorThread);
    Mock->Delete(CreateMultiPdumpThread);
    Mock->Delete(CreateCpThread);
    Mock->Delete(StartDpThread);
    Mock->Delete(KNET_SetDpInited);

    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_INIT, TEST_KNET_START_DP_THREAD, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_DpMaxWorkerIdGet, TEST_GetFuncRetPositive(1));
    Mock->Create(rte_eal_remote_launch, TEST_GetFuncRetPositive(0));
    int32_t ret = StartDpThread();
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(rte_eal_remote_launch);

    Mock->Create(rte_eal_remote_launch, TEST_GetFuncRetNegative(1));
    ret = StartDpThread();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(rte_eal_remote_launch);
    Mock->Delete(KNET_DpMaxWorkerIdGet);

    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_INIT, TEST_KNET_CREATE_MULTI_PDUMP_THREAD, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_CreateThread, TEST_GetFuncRetNegative(1));
    int32_t ret = CreateMultiPdumpThread();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_CreateThread);

    Mock->Create(KNET_CreateThread, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_ThreadNameSet, TEST_GetFuncRetPositive(0));
    ret = CreateMultiPdumpThread();
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(KNET_ThreadNameSet);

    Mock->Create(KNET_ThreadNameSet, TEST_GetFuncRetNegative(1));
    ret = CreateMultiPdumpThread();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_ThreadNameSet);
    Mock->Delete(KNET_CreateThread);

    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_INIT, TEST_KNET_CREATE_CP_THREAD, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_CreateThread, TEST_GetFuncRetNegative(1));
    int32_t ret = CreateCpThread();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_CreateThread);

    Mock->Create(KNET_CreateThread, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_ThreadNameSet, TEST_GetFuncRetPositive(0));
    ret = CreateCpThread();
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(KNET_ThreadNameSet);

    Mock->Create(KNET_ThreadNameSet, TEST_GetFuncRetNegative(1));
    ret = CreateCpThread();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_ThreadNameSet);
    Mock->Delete(KNET_CreateThread);

    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_INIT, TEST_KNET_CREATE_SINGLE_THREAD, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_CreateThread, TEST_GetFuncRetNegative(1));
    int32_t ret = KnetCreateSignalBlockMonitorThread();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_CreateThread);

    Mock->Create(KNET_CreateThread, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_ThreadNameSet, TEST_GetFuncRetPositive(0));
    ret = KnetCreateSignalBlockMonitorThread();
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(KNET_ThreadNameSet);

    Mock->Create(KNET_ThreadNameSet, TEST_GetFuncRetNegative(1));
    ret = KnetCreateSignalBlockMonitorThread();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_ThreadNameSet);
    Mock->Delete(KNET_CreateThread);

    DeleteMock(Mock);
}

typedef struct {
    uint32_t ctrlVcpuID; // 控制面绑核核号
    char padding[4];   // 填充字节，确保结构体8 字节对齐
} CtrlThreadArgs;

extern bool g_threadStop;
DTEST_CASE_F(KNET_INIT, TEST_KNET_CP_THREAD_FUNC, NULL, NULL)
{
    CtrlThreadArgs ctrlArgs = {0};
    ctrlArgs.ctrlVcpuID = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_SetThreadAffinity, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_SpinlockLock, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_CpdRunOnce, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_SpinlockUnlock, TEST_GetFuncRetPositive(0));
    g_threadStop = true;
    CpThreadFunc(&ctrlArgs);
    Mock->Delete(KNET_SpinlockUnlock);
    Mock->Delete(DP_CpdRunOnce);
    Mock->Delete(KNET_SpinlockLock);
    Mock->Delete(KNET_SetThreadAffinity);

    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_INIT, TEST_KNET_LCORE_MAIN_LOOP, NULL, NULL)
{
    KNET_DpWorkerInfo workerInfo = {0};
    workerInfo.workerId = 0;
    workerInfo.lcoreId = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetIfIndex, TEST_GetFuncRetPositive(1));
    Mock->Create(DP_GetNetdevQueMap, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_lcore_id, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_get_timer_hz, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_get_timer_cycles, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_SpinlockLock, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_RunWorkerOnce, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_SpinlockUnlock, TEST_GetFuncRetPositive(0));
    g_threadStop = true;
    LcoreMainloop(&workerInfo);
    Mock->Delete(KNET_SpinlockUnlock);
    Mock->Delete(DP_RunWorkerOnce);
    Mock->Delete(KNET_SpinlockLock);
    Mock->Delete(rte_get_timer_cycles);
    Mock->Delete(rte_get_timer_hz);
    Mock->Delete(rte_lcore_id);
    Mock->Delete(KNET_GetIfIndex);
    Mock->Delete(DP_GetNetdevQueMap);

    DeleteMock(Mock);
}

void ConfigInit(void);
DTEST_CASE_F(KNET_INIT, TEST_KNET_INIT, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(ConfigInit, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_DpSignalRegAll, TEST_GetFuncRetPositive(0));
    KnetInit();
    Mock->Delete(KNET_DpSignalRegAll);
    Mock->Delete(ConfigInit);

    DeleteMock(Mock);
}
void Uninit(void);
DTEST_CASE_F(KNET_INIT, TEST_KNET_KUNINIT, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(Uninit, TEST_GetFuncRetPositive(0));
    KnetUninit();
    Mock->Delete(Uninit);

    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_INIT, TEST_KNET_JoinDpdkAndStackThread, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_JoinThread, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_DpMaxWorkerIdGet, TEST_GetFuncRetPositive(1));
    Mock->Create(rte_eal_wait_lcore, TEST_GetFuncRetPositive(0));
    int ret = JoinDpdkAndStackThread();
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(rte_eal_wait_lcore);
    Mock->Delete(KNET_DpMaxWorkerIdGet);
    Mock->Delete(KNET_JoinThread);

    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_INIT, TEST_KNET_DpExit, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KNET_FdInit, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_DpSignalSetWaitExit, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_FdMaxGet, TEST_GetFuncRetPositive(1));
    Mock->Create(KNET_IsFdHijack, TEST_GetFuncRetPositive(1));
    Mock->Create(g_origOsApi.getsockopt, MOCK_OsGetsockopt);
    Mock->Create(DP_PosixSetsockopt, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_Close, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_SocketCountGet, TEST_GetFuncRetPositive(1));

    KNET_SetDpInited(); // 内部设置g_tcpInited = true;
    KNET_DpExit();

    Mock->Delete(DP_SocketCountGet);
    Mock->Delete(KNET_Close);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(DP_PosixSetsockopt);
    Mock->Delete(g_origOsApi.getsockopt);
    Mock->Delete(KNET_IsFdHijack);
    Mock->Delete(KNET_FdMaxGet);
    Mock->Delete(KNET_DpSignalSetWaitExit);
    Mock->Delete(KNET_FdInit);

    g_tcpInited = false;

    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_INIT, TEST_KNET_ProcessTelemetryQueueMapWorker, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    ProcessTelemetryQueueMapWorker();
    Mock->Delete(KNET_GetCfg);

    Mock->Create(KNET_GetCfg, MockKnetGetCfg0);
    Mock->Create(DP_GetNetdevQueMap, TEST_GetFuncRetPositive(0));
    ProcessTelemetryQueueMapWorker();
    Mock->Delete(KNET_GetCfg);
    Mock->Delete(DP_GetNetdevQueMap);

    DeleteMock(Mock);
}
