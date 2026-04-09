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

#include <unistd.h>

#include "rte_malloc.h"
#include "rte_memzone.h"
#include "rte_ethdev.h"
#include "rte_eal.h"
#include "rte_hash.h"
#include "rte_errno.h"

#include "knet_log.h"
#include "knet_telemetry.h"
#include "tcp_fd.h"

#include "knet_types.h"
#include "knet_dpdk_init.h"

#include "securec.h"
#include "common.h"
#include "mock.h"

#define EPOLL_DETAILS_PARAMS_NUM 5
#define RESERVED_EPOLL_EVENT_AMOUNT 100
#define MAX_FD_NUM_LIMIT 256
#define MOCK_PID 12345

extern "C" {
#include "rte_telemetry.h"
#include "knet_telemetry_call.h"
#include "knet_telemetry_debug.h"
int ProcessDpFd(EpollTelemetryContext *context, char **jsonStr);
extern int GetEpollDetailsHookHandler(int dpFd, DP_EpollDetails_t *details, int allocEventAmount, int *workerId);
extern int GetJsonStringForEpoll(EpollTelemetryContext *context, DP_EpollDetails_t *details,
                                 int actualEventCount, int workerId, char **jsonStr);
extern int GetNumFromStr(const char *str, uint32_t *num);
char* JsonStringsToJsonArray(const char *jsonStrs[], size_t count);
int AllocateOrResizeMergedJsonStr(char **mergedJsonStr, size_t totalLen);
struct rte_tel_data {int array[64];};
extern void FreeSockDetailsResource(EpollTelemetryContext *epollctx);

int MockKnetHandleTimeout(KNET_TelemetryInfo *telemetryInfo, int i)
{
    static DP_EpollDetails_t testSockDetail = {
        .fd = 1,
        .expectEvents = 1,
        .readyEvents = 0,
        .notifiedEvents = 0,
        .shoted = 0
    };
    static DP_EpollDetails_t details[1] = {testSockDetail};
    static EpollTelemetryContext testEpollCtx = {0};
    testEpollCtx.isLast = false;
    testEpollCtx.isSecondary = true;
    testEpollCtx.pid = MOCK_PID;
    testEpollCtx.tid = 0;
    testEpollCtx.osFd = 1;
    testEpollCtx.dpFd = 2;
    testEpollCtx.maxSockFd = 1;
    testEpollCtx.details = details;
    static EpollTelemetryContext testEpollCtxTail = {
        .isLast = true
    };
    static EpollTelemetryContext epollCtx[2]={testEpollCtx, testEpollCtxTail};
    telemetryInfo->epollDetailCtx = epollCtx;
    return 0;
}

void MockFreeSockDetailsResource(EpollTelemetryContext *epollctx){
    return;
}
}

static void hook1(DP_StatType_t type, int workerId, uint32_t flag)
{
    return;
}

static int hook2(int type)
{
    return 0;
}

static int hook3(int fd, DP_SocketState_t* state)
{
    return 0;
}

static int hook4(int fd, DP_SockDetails_t* info)
{
    return 0;
}

static int hook5(int epFd, DP_EpollDetails_t *details, int len, int *wid)
{
    if(details == NULL || len == 0){
        return 1;
    } 
    static DP_EpollDetails_t testSockDetail = {
        .fd = 1,
        .expectEvents = 1,
        .readyEvents = 0,
        .notifiedEvents = 0,
        .shoted = 0
    };
    details[0] = testSockDetail;
    *wid = 1;
    return 1;
}

static bool IsFdHijackMock(int fd)
{
    return true;
}

static int MockTelDataAddDictContainer(struct rte_tel_data *d, const char *name, struct rte_tel_data *val, int keep)
{
    // 原函数在内部释放val，打桩防止内存泄漏
    free(val);
    return 0;
}

static struct rte_memzone *MockRteMemzoneLookup(char *memzoneName)
{
    static struct rte_memzone mz = {0};
    static KNET_TelemetryInfo telemetryInfo = {0};
    telemetryInfo.pid[1] = MOCK_PID;
    mz.addr = (void *)&telemetryInfo;
    return &mz;
}

DTEST_CASE_F(DPDK_TELEMETRY_EPOLL, TEST_KNET_TELEMETRY_EPOLL_DETAILS_CALLBACK, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    struct rte_tel_data data = {0};
    KNET_DpTelemetryHooks dpTelemetryhooks = {hook1, hook2, hook3, hook4, hook5};
    KNET_DpTelemetryHookReg(dpTelemetryhooks);
    Mock->Create(getpid, TEST_GetFuncRetPositive(1));
    Mock->Create(KNET_FdMaxGet, TEST_GetFuncRetPositive(2));
    Mock->Create(KNET_IsFdHijack, IsFdHijackMock);
    Mock->Create(KNET_GetFdType, TEST_GetFuncRetPositive(2));
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(1));
    Mock->Create(KnetGetTidByWorkerId, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_tel_data_add_dict_string, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_tel_data_add_dict_int, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_tel_data_add_dict_u64, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_tel_data_add_dict_container, MockTelDataAddDictContainer);
    int ret = KnetTelemetryEpollDetailsCallback("/knet/stack/epoll_stat", "1 0 1 0 1", (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_OK);

    Mock->Delete(getpid);
    Mock->Delete(KNET_IsFdHijack);
    Mock->Delete(KNET_GetFdType);
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(KNET_FdMaxGet);
    Mock->Delete(KnetGetTidByWorkerId);
    Mock->Delete(rte_tel_data_add_dict_string);
    Mock->Delete(rte_tel_data_add_dict_int);
    Mock->Delete(rte_tel_data_add_dict_container);
    Mock->Delete(rte_tel_data_add_dict_u64);
    DeleteMock(Mock);
}

DTEST_CASE_F(DPDK_TELEMETRY_EPOLL, TEST_KNET_TELEMETRY_EPOLL_DETAILS_CALLBACK_MP, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    struct rte_tel_data data = {0};
    Mock->Create(rte_memzone_lookup, MockRteMemzoneLookup);
    Mock->Create(KnetWaitAllSlavePorcessHandle, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetHandleTimeout, MockKnetHandleTimeout);
    Mock->Create(rte_tel_data_add_dict_string, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_tel_data_add_dict_int, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_tel_data_add_dict_u64, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_tel_data_add_dict_container, MockTelDataAddDictContainer);
    Mock->Create(FreeSockDetailsResource, MockFreeSockDetailsResource);
    int ret = KnetTelemetryEpollDetailsCallbackMp("/knet/stack/epoll_stat", "12345 0 1 0 1", (struct rte_tel_data *)&data);
    DT_ASSERT_EQUAL(ret, (int)KNET_OK);
 
    Mock->Delete(rte_memzone_lookup);
    Mock->Delete(KnetWaitAllSlavePorcessHandle);
    Mock->Delete(KnetHandleTimeout);
    Mock->Delete(rte_tel_data_add_dict_string);
    Mock->Delete(rte_tel_data_add_dict_int);
    Mock->Delete(rte_tel_data_add_dict_container);
    Mock->Delete(rte_tel_data_add_dict_u64);
    Mock->Delete(FreeSockDetailsResource);
    DeleteMock(Mock);
}