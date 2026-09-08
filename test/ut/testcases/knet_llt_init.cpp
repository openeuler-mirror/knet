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
#include "knet_utils.h"
#include "knet_mem.h"
#include "knet_transmission.h"
#include "knet_pdump.h"
#include "knet_rand.h"

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
void ProcessTelemetryShowStats(bool flag, KNET_TelemetryInfo *telemetryInfo, int queId);
void ProcessTelemetryPersist(bool flag, KNET_TelemetryPersistInfo *telemetryPersistInfo, pid_t pid);
int CreateTelemetryPersistThread(void);
void PrepareAllDpStates(KNET_TelemetryPersistInfo *info);
void *MultiPdumpThreadFunc(void *args);
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
    telemetryInfo.msgReady[queId] = 1;

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

/* ===== knet_init.c 未覆盖函数测试 ===== */

/**
 * @brief KNET_PosixOpsApiInit 正常路径
 */
DTEST_CASE_F(KNET_INIT, TEST_KNET_POSIX_OPS_API_INIT, NULL, NULL)
{
    struct KNET_PosixApiOps ops = {0};
    int32_t ret = KNET_PosixOpsApiInit(&ops);
    DT_ASSERT_EQUAL(ret, 0);
}

/**
 * @brief ProcessTelemetryShowStats flag=true 路径
 */
DTEST_CASE_F(KNET_INIT, TEST_PROCESS_TELEMETRY_SHOW_STATS, NULL, NULL)
{
    KNET_TelemetryInfo telemetryInfo = {0};
    int queId = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_SpinlockLock, TEST_GetFuncRetPositive(0));
    Mock->Create(ShowDpStats, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_SpinlockUnlock, TEST_GetFuncRetPositive(0));

    ProcessTelemetryShowStats(true, &telemetryInfo, queId);

    Mock->Delete(KNET_SpinlockLock);
    Mock->Delete(ShowDpStats);
    Mock->Delete(KNET_SpinlockUnlock);
    DeleteMock(Mock);
}

/**
 * @brief ProcessTelemetryPersist flag=true 且条件满足
 */
DTEST_CASE_F(KNET_INIT, TEST_PROCESS_TELEMETRY_PERSIST, NULL, NULL)
{
    KNET_TelemetryPersistInfo telemetryPersistInfo = {0};
    telemetryPersistInfo.curPid = getpid();
    telemetryPersistInfo.state = KNET_TELE_PERSIST_WAITSECOND;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_SpinlockLock, TEST_GetFuncRetPositive(0));
    Mock->Create(PrepareAllDpStates, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_SpinlockUnlock, TEST_GetFuncRetPositive(0));

    ProcessTelemetryPersist(true, &telemetryPersistInfo, getpid());

    Mock->Delete(KNET_SpinlockLock);
    Mock->Delete(PrepareAllDpStates);
    Mock->Delete(KNET_SpinlockUnlock);
    DeleteMock(Mock);
}

/**
 * @brief CreateTelemetryPersistThread KNET_TelemetryStartPersistThread失败
 */
DTEST_CASE_F(KNET_INIT, TEST_CREATE_TELEMETRY_PERSIST_THREAD_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfg0);
    Mock->Create(KNET_TelemetryStartPersistThread, TEST_GetFuncRetPositive(0));

    int32_t ret = CreateTelemetryPersistThread();
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KNET_GetCfg);
    Mock->Delete(KNET_TelemetryStartPersistThread);
    DeleteMock(Mock);
}

/**
 * @brief JoinDpdkAndStackThread rte_eal_wait_lcore失败路径
 */
DTEST_CASE_F(KNET_INIT, TEST_JOIN_THREAD_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    Mock->Create(KNET_JoinThread, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_DpMaxWorkerIdGet, TEST_GetFuncRetPositive(1));
    Mock->Create(rte_eal_wait_lcore, TEST_GetFuncRetNegative(1));

    int ret = JoinDpdkAndStackThread();
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(rte_eal_wait_lcore);
    Mock->Delete(KNET_DpMaxWorkerIdGet);
    Mock->Delete(KNET_JoinThread);
    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

/**
 * @brief KNET_AllThreadLock / KNET_AllThreadUnlock worker循环路径
 */
DTEST_CASE_F(KNET_INIT, TEST_KNET_ALL_THREAD_LOCK_UNLOCK, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    Mock->Create(KNET_SpinlockLock, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_SpinlockUnlock, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_DpMaxWorkerIdGet, TEST_GetFuncRetPositive(1));

    KNET_AllThreadLock();
    KNET_AllThreadUnlock();

    Mock->Delete(KNET_DpMaxWorkerIdGet);
    Mock->Delete(KNET_SpinlockLock);
    Mock->Delete(KNET_SpinlockUnlock);
    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

/* ===== knet_init.c 补充覆盖率测试 ===== */

/**
 * @brief MultiPdumpThreadFunc - g_threadStop=true, pdumpRequestMz=NULL, telemetryFlag=false
 * 覆盖函数主体setup和单次循环退出路径
 */
DTEST_CASE_F(KNET_INIT, TEST_MULTI_PDUMP_THREAD_FUNC_NULL_MZ, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_memzone_lookup, TEST_GetFuncRetPositive(0)); /* 返回NULL */
    Mock->Create(KNET_GetCfg, MockKnetGetCfg0); /* 所有cfg返回0, telemetryFlag=false, persistFlag=false */
    Mock->Create(getpid, TEST_GetFuncRetPositive(1));
    Mock->Create(KNET_Usleep, TEST_GetFuncRetPositive(0));

    g_threadStop = true;
    void *ret = MultiPdumpThreadFunc(NULL);
    DT_ASSERT_EQUAL(ret, (void *)NULL);

    Mock->Delete(KNET_Usleep);
    Mock->Delete(getpid);
    Mock->Delete(KNET_GetCfg);
    Mock->Delete(rte_memzone_lookup);
    DeleteMock(Mock);
}

/**
 * @brief MultiPdumpThreadFunc - pdumpRequestMz有效, KNET_SetPdumpRxTxCbs调用
 */
static struct rte_memzone g_testMz;
static struct rte_memzone *MockMemzoneLookupPdump(const char *name)
{
    return &g_testMz;
}
DTEST_CASE_F(KNET_INIT, TEST_MULTI_PDUMP_THREAD_FUNC_WITH_MZ, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_memzone_lookup, MockMemzoneLookupPdump);
    Mock->Create(KNET_GetCfg, MockKnetGetCfg0);
    Mock->Create(getpid, TEST_GetFuncRetPositive(1));
    Mock->Create(KNET_Usleep, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_SpinlockLock, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_SpinlockUnlock, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_SetPdumpRxTxCbs, TEST_GetFuncRetPositive(0));

    g_threadStop = true;
    void *ret = MultiPdumpThreadFunc(NULL);
    DT_ASSERT_EQUAL(ret, (void *)NULL);

    Mock->Delete(KNET_SetPdumpRxTxCbs);
    Mock->Delete(KNET_SpinlockUnlock);
    Mock->Delete(KNET_SpinlockLock);
    Mock->Delete(KNET_Usleep);
    Mock->Delete(getpid);
    Mock->Delete(KNET_GetCfg);
    Mock->Delete(rte_memzone_lookup);
    DeleteMock(Mock);
}

/**
 * @brief CreateTelemetryPersistThread - 成功路径
 */
DTEST_CASE_F(KNET_INIT, TEST_CREATE_TELEMETRY_PERSIST_THREAD_SUCCESS, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfg0); /* runMode != MULTIPLE, procType != SECONDARY */
    Mock->Create(KNET_TelemetryStartPersistThread, TEST_GetFuncRetPositive(1)); /* tid != 0 */

    int32_t ret = CreateTelemetryPersistThread();
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KNET_TelemetryStartPersistThread);
    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

/**
 * @brief ConfigInit - KNET_RandInit失败路径
 */
DTEST_CASE_F(KNET_INIT, TEST_CONFIG_INIT_RAND_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_RandInit, TEST_GetFuncRetNegative(1));
    Mock->Create(KNET_LogLevelSetByStr, MOCK_KNET_LogLevelSetByStr);

    ConfigInit();

    Mock->Delete(KNET_RandInit);
    Mock->Delete(KNET_LogLevelSetByStr);
    DeleteMock(Mock);
}

/**
 * @brief Uninit - 信号处理中 + forked parent 路径
 */
DTEST_CASE_F(KNET_INIT, TEST_UNINIT_SIGNAL_PARENT, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_DpSignalIsInSigHandler, TEST_GetFuncRetPositive(1)); /* true */
    Mock->Create(KNET_DpIsForkedParent, MOCK_KNET_DpIsForkedParent); /* true */
    Mock->Create(KNET_DpExit, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_Usleep, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_TelemetrySetPersistThreadExit, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_FreeTapGlobal, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_PktBatchFree, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_LogLevelSet, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_MemSetFlagInSignalQuiting, TEST_GetFuncRetPositive(0));

    Uninit();

    Mock->Delete(KNET_MemSetFlagInSignalQuiting);
    Mock->Delete(KNET_LogLevelSet);
    Mock->Delete(KNET_PktBatchFree);
    Mock->Delete(KNET_FreeTapGlobal);
    Mock->Delete(KNET_TelemetrySetPersistThreadExit);
    Mock->Delete(KNET_Usleep);
    Mock->Delete(KNET_DpExit);
    Mock->Delete(KNET_DpIsForkedParent);
    Mock->Delete(KNET_DpSignalIsInSigHandler);
    DeleteMock(Mock);
}

/**
 * @brief Uninit - 信号处理中 + 非forked parent 路径
 */
DTEST_CASE_F(KNET_INIT, TEST_UNINIT_SIGNAL_NOT_PARENT, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_DpSignalIsInSigHandler, TEST_GetFuncRetPositive(1)); /* true */
    Mock->Create(KNET_DpIsForkedParent, TEST_GetFuncRetPositive(0)); /* false */
    Mock->Create(KNET_DpExit, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_Usleep, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    Mock->Create(KNET_UninitDpdk, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_HashTblDeinit, MOCK_KNET_HashTblDeinit);
    Mock->Create(KNET_JoinThread, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_DpMaxWorkerIdGet, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_PktBatchFree, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_TelemetrySetPersistThreadExit, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_LogLevelSet, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_MemSetFlagInSignalQuiting, TEST_GetFuncRetPositive(0));
    Mock->Create(JoinDpdkAndStackThread, TEST_GetFuncRetPositive(0));

    Uninit();

    Mock->Delete(JoinDpdkAndStackThread);
    Mock->Delete(KNET_MemSetFlagInSignalQuiting);
    Mock->Delete(KNET_LogLevelSet);
    Mock->Delete(KNET_TelemetrySetPersistThreadExit);
    Mock->Delete(KNET_PktBatchFree);
    Mock->Delete(KNET_DpMaxWorkerIdGet);
    Mock->Delete(KNET_JoinThread);
    Mock->Delete(KNET_HashTblDeinit);
    Mock->Delete(KNET_UninitDpdk);
    Mock->Delete(KNET_GetCfg);
    Mock->Delete(KNET_Usleep);
    Mock->Delete(KNET_DpExit);
    Mock->Delete(KNET_DpIsForkedParent);
    Mock->Delete(KNET_DpSignalIsInSigHandler);
    DeleteMock(Mock);
}

/**
 * @brief LcoreMainloop - 多进程模式路径
 */
static union KNET_CfgValue g_cfgMulti = {.intValue = 0};
static union KNET_CfgValue *MockKnetGetCfgMultiMode(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfgMulti, sizeof(g_cfgMulti), 0, sizeof(g_cfgMulti));
    if (key == CONF_COMMON_MODE) {
        g_cfgMulti.intValue = KNET_RUN_MODE_MULTIPLE;
    } else {
        g_cfgMulti.intValue = 1;
    }
    return &g_cfgMulti;
}
DTEST_CASE_F(KNET_INIT, TEST_LCORE_MAINLOOP_MULTI_MODE, NULL, NULL)
{
    KNET_DpWorkerInfo workerInfo = {0};
    workerInfo.workerId = 0;
    workerInfo.lcoreId = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfgMultiMode);
    Mock->Create(rte_lcore_id, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_get_timer_hz, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_get_timer_cycles, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_SpinlockLock, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_RunWorkerOnce, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_SpinlockUnlock, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_SetQueIdMapPidTidLcoreInfo, TEST_GetFuncRetPositive(0));

    g_threadStop = true;
    int ret = LcoreMainloop(&workerInfo);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KNET_SetQueIdMapPidTidLcoreInfo);
    Mock->Delete(KNET_SpinlockUnlock);
    Mock->Delete(DP_RunWorkerOnce);
    Mock->Delete(KNET_SpinlockLock);
    Mock->Delete(rte_get_timer_cycles);
    Mock->Delete(rte_get_timer_hz);
    Mock->Delete(rte_lcore_id);
    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

/**
 * @brief CpThreadFunc - kernelForwardEnabled != KERNEL_FORWARD_ENABLE 路径
 */
static union KNET_CfgValue g_cfgNotKernel = {.intValue = 0};
static union KNET_CfgValue *MockKnetGetCfgNotKernel(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfgNotKernel, sizeof(g_cfgNotKernel), 0, sizeof(g_cfgNotKernel));
    g_cfgNotKernel.intValue = 0; /* != KERNEL_FORWARD_ENABLE */
    return &g_cfgNotKernel;
}
DTEST_CASE_F(KNET_INIT, TEST_CP_THREAD_FUNC_NOT_KERNEL, NULL, NULL)
{
    CtrlThreadArgs ctrlArgs = {0};
    ctrlArgs.ctrlVcpuID = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_SetThreadAffinity, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_GetCfg, MockKnetGetCfgNotKernel);
    Mock->Create(KNET_SpinlockLock, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_CpdRunOnce, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_SpinlockUnlock, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_Usleep, TEST_GetFuncRetPositive(0));

    g_threadStop = true;
    CpThreadFunc(&ctrlArgs);

    Mock->Delete(KNET_Usleep);
    Mock->Delete(KNET_SpinlockUnlock);
    Mock->Delete(DP_CpdRunOnce);
    Mock->Delete(KNET_SpinlockLock);
    Mock->Delete(KNET_GetCfg);
    Mock->Delete(KNET_SetThreadAffinity);
    DeleteMock(Mock);
}

/**
 * @brief CreateCpThread - ctrlVcpuNum > MAX_VCPU_NUMS 路径
 */
static union KNET_CfgValue g_cfgBigVcpu = {.intValue = 0};
static union KNET_CfgValue *MockKnetGetCfgBigVcpu(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfgBigVcpu, sizeof(g_cfgBigVcpu), 0, sizeof(g_cfgBigVcpu));
    g_cfgBigVcpu.intValue = MAX_VCPU_NUMS + 1;
    return &g_cfgBigVcpu;
}
DTEST_CASE_F(KNET_INIT, TEST_CREATE_CP_THREAD_TOO_MANY, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfgBigVcpu);

    int32_t ret = CreateCpThread();
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

/**
 * @brief CreateCpThread - snprintf_s失败路径
 */
static int32_t g_snprintfCallCount = 0;
static int32_t MockSnprintfFail(char *dest, uint32_t destMax, uint32_t count, const char *fmt, ...)
{
    g_snprintfCallCount++;
    if (g_snprintfCallCount == 1) {
        return -1; /* 第一次调用(线程名)失败 */
    }
    return 0;
}
DTEST_CASE_F(KNET_INIT, TEST_CREATE_CP_THREAD_SNPRINTF_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfg); /* ctrlVcpuNum=1 */
    Mock->Create(KNET_CreateThread, TEST_GetFuncRetPositive(0));
    Mock->Create(snprintf_s, MockSnprintfFail);

    g_snprintfCallCount = 0;
    int32_t ret = CreateCpThread();
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(snprintf_s);
    Mock->Delete(KNET_CreateThread);
    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

/**
 * @brief StartDpThread - cothread模式直接返回
 */
static union KNET_CfgValue g_cfgCothread = {.intValue = 1};
static union KNET_CfgValue *MockKnetGetCfgCothread(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfgCothread, sizeof(g_cfgCothread), 0, sizeof(g_cfgCothread));
    g_cfgCothread.intValue = 1; /* CONF_COMMON_COTHREAD == 1 */
    return &g_cfgCothread;
}
DTEST_CASE_F(KNET_INIT, TEST_START_DP_THREAD_COTHREAD, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfgCothread);

    int32_t ret = StartDpThread();
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

/**
 * @brief CreateTelemetryPersistThread - 多进程+从进程返回0
 */
static union KNET_CfgValue g_cfgMultiSecondary = {.intValue = 0};
static union KNET_CfgValue *MockKnetGetCfgMultiSecondary(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfgMultiSecondary, sizeof(g_cfgMultiSecondary), 0, sizeof(g_cfgMultiSecondary));
    if (key == CONF_COMMON_MODE) {
        g_cfgMultiSecondary.intValue = KNET_RUN_MODE_MULTIPLE;
    } else if (key == CONF_INNER_PROC_TYPE) {
        g_cfgMultiSecondary.intValue = KNET_PROC_TYPE_SECONDARY;
    } else {
        g_cfgMultiSecondary.intValue = 1;
    }
    return &g_cfgMultiSecondary;
}
DTEST_CASE_F(KNET_INIT, TEST_CREATE_TELE_PERSIST_MULTI_SECONDARY, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfgMultiSecondary);

    int32_t ret = CreateTelemetryPersistThread();
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

/**
 * @brief JoinDpdkAndStackThread - cp thread join失败 + multidump join失败路径
 */
DTEST_CASE_F(KNET_INIT, TEST_JOIN_THREAD_CP_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfg); /* ctrlVcpuNum=1 */
    Mock->Create(KNET_JoinThread, TEST_GetFuncRetNegative(1)); /* join失败 */
    Mock->Create(KNET_DpMaxWorkerIdGet, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_eal_wait_lcore, TEST_GetFuncRetPositive(0));

    int ret = JoinDpdkAndStackThread();
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(rte_eal_wait_lcore);
    Mock->Delete(KNET_DpMaxWorkerIdGet);
    Mock->Delete(KNET_JoinThread);
    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}
