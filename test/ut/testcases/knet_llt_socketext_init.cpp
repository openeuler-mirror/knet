/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: knet_socketext_init 单元测试, 覆盖 ShowDpStats/GetFdCountMp/PrepareAllDpStates/GetSockDetailsMp/
 *             GetNetStatMp/GetEpollStatMp 的各分支
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "securec.h"
#include "rte_malloc.h"
#include "dp_debug_api.h"
#include "dp_socket_api.h"

#include "knet_log.h"
#include "tcp_fd.h"

#include "common.h"
#include "mock.h"

extern "C" {
#include "knet_telemetry.h"
#include "knet_socketext_init.h"

void GetFdCountMp(KNET_TelemetryInfo *telemetryInfo, int queId);
KNET_SocketState *GetNetStatMp(KNET_TelemetryInfo *telemetryInfo, int queId);
int GetSockDetailsMp(int fd, KNET_SocketDetails *socketDetails);
EpollTelemetryContext *GetEpollStatMp(KNET_TelemetryInfo *telemetryInfo, int queId);
int KNET_MaintainQueue2TidPidMp(uint32_t queId);
int DP_SocketCountGet(int fdType);
int KNET_FdMaxGet(void);
}

/* rte_malloc 打桩: 返回 calloc 内存, 覆盖 GetNetStatMp/GetEpollStatMp 成功路径 */
static void *MockRteMallocOk(const char *type, size_t size, unsigned align)
{
    (void)type;
    (void)align;
    if (size == 0) {
        return NULL;
    }
    return calloc(1, size);
}

/**
 * @brief ShowDpStats 空入参测试, 覆盖 NULL 早返回分支
 */
DTEST_CASE_F(SOCKETEXT, TEST_SHOWDPSTATS_NULL_INPUT, NULL, NULL)
{
    ShowDpStats(NULL, 0);
}

/**
 * @brief ShowDpStats msgReady=0, 覆盖不进入 switch 的分支
 */
DTEST_CASE_F(SOCKETEXT, TEST_SHOWDPSTATS_NOT_READY, NULL, NULL)
{
    KNET_TelemetryInfo telemetryInfo = {0};
    telemetryInfo.msgReady[0] = 0;
    ShowDpStats(&telemetryInfo, 0);
    DT_ASSERT_EQUAL(telemetryInfo.msgReady[0], 0);
}

/**
 * @brief ShowDpStats KNET_TELEMETRY_STATISTIC 分支
 */
DTEST_CASE_F(SOCKETEXT, TEST_SHOWDPSTATS_STATISTIC, NULL, NULL)
{
    KNET_TelemetryInfo telemetryInfo = {0};
    telemetryInfo.msgReady[0] = 1;
    telemetryInfo.telemetryType = KNET_TELEMETRY_STATISTIC;

    KTestMock *Mock = CreateMock();
    Mock->Create(DP_ShowStatistics, TEST_GetFuncRetPositive(0));
    ShowDpStats(&telemetryInfo, 0);
    DT_ASSERT_EQUAL(telemetryInfo.msgReady[0], 0);
    Mock->Delete(DP_ShowStatistics);
    DeleteMock(Mock);
}

/**
 * @brief ShowDpStats KNET_TELEMETRY_UPDATE_QUE_INFO 分支
 */
DTEST_CASE_F(SOCKETEXT, TEST_SHOWDPSTATS_UPDATE_QUE, NULL, NULL)
{
    KNET_TelemetryInfo telemetryInfo = {0};
    telemetryInfo.msgReady[0] = 1;
    telemetryInfo.telemetryType = KNET_TELEMETRY_UPDATE_QUE_INFO;

    KTestMock *Mock = CreateMock();
    Mock->Create(KNET_MaintainQueue2TidPidMp, TEST_GetFuncRetPositive(0));
    ShowDpStats(&telemetryInfo, 0);
    DT_ASSERT_EQUAL(telemetryInfo.msgReady[0], 0);
    Mock->Delete(KNET_MaintainQueue2TidPidMp);
    DeleteMock(Mock);
}

/**
 * @brief ShowDpStats KNET_TELEMETRY_GET_FD_COUNT 分支, 同时覆盖 GetFdCountMp
 */
DTEST_CASE_F(SOCKETEXT, TEST_SHOWDPSTATS_GET_FD_COUNT, NULL, NULL)
{
    KNET_TelemetryInfo telemetryInfo = {0};
    telemetryInfo.msgReady[0] = 1;
    telemetryInfo.telemetryType = KNET_TELEMETRY_GET_FD_COUNT;

    ShowDpStats(&telemetryInfo, 0);
    DT_ASSERT_EQUAL(telemetryInfo.msgReady[0], 0);
    DT_ASSERT_NOT_EQUAL(telemetryInfo.message[0][0], '\0');
}

/**
 * @brief ShowDpStats KNET_TELEMETRY_GET_NET_STAT 分支, rte_malloc 返回 NULL, 覆盖错误路径
 */
DTEST_CASE_F(SOCKETEXT, TEST_SHOWDPSTATS_GET_NET_STAT_NULL, NULL, NULL)
{
    KNET_TelemetryInfo telemetryInfo = {0};
    telemetryInfo.msgReady[0] = 1;
    telemetryInfo.telemetryType = KNET_TELEMETRY_GET_NET_STAT;

    KTestMock *Mock = CreateMock();
    Mock->Create(rte_malloc, TEST_GetFuncRetPositive(0)); // 返回 NULL
    ShowDpStats(&telemetryInfo, 0);
    DT_ASSERT_EQUAL(telemetryInfo.msgReady[0], 0);
    DT_ASSERT_EQUAL(telemetryInfo.socketStates, NULL);
    Mock->Delete(rte_malloc);
    DeleteMock(Mock);
}

/**
 * @brief ShowDpStats KNET_TELEMETRY_GET_NET_STAT 分支, rte_malloc 打桩返回有效内存, 覆盖成功路径
 */
DTEST_CASE_F(SOCKETEXT, TEST_SHOWDPSTATS_GET_NET_STAT_OK, NULL, NULL)
{
    KNET_TelemetryInfo telemetryInfo = {0};
    telemetryInfo.msgReady[0] = 1;
    telemetryInfo.telemetryType = KNET_TELEMETRY_GET_NET_STAT;

    KTestMock *Mock = CreateMock();
    Mock->Create(rte_malloc, MockRteMallocOk);
    ShowDpStats(&telemetryInfo, 0);
    DT_ASSERT_EQUAL(telemetryInfo.msgReady[0], 0);
    DT_ASSERT_NOT_EQUAL(telemetryInfo.socketStates, NULL);
    if (telemetryInfo.socketStates != NULL) {
        DT_ASSERT_EQUAL(telemetryInfo.socketStates[0].isLast, true);
        free(telemetryInfo.socketStates);
        telemetryInfo.socketStates = NULL;
    }
    Mock->Delete(rte_malloc);
    DeleteMock(Mock);
}

/**
 * @brief ShowDpStats KNET_TELEMETRY_GET_SOCKET_INFO 分支, 同时覆盖 GetSockDetailsMp 非 hijack 路径
 */
DTEST_CASE_F(SOCKETEXT, TEST_SHOWDPSTATS_GET_SOCKET_INFO, NULL, NULL)
{
    KNET_TelemetryInfo telemetryInfo = {0};
    telemetryInfo.msgReady[0] = 1;
    telemetryInfo.telemetryType = KNET_TELEMETRY_GET_SOCKET_INFO;

    ShowDpStats(&telemetryInfo, 0);
    DT_ASSERT_EQUAL(telemetryInfo.msgReady[0], 0);
    DT_ASSERT_EQUAL(telemetryInfo.socketDetails.isReady, false);
}

/**
 * @brief ShowDpStats KNET_TELEMETRY_GET_EPOLL_STAT 分支, rte_malloc 返回 NULL, 覆盖错误路径
 */
DTEST_CASE_F(SOCKETEXT, TEST_SHOWDPSTATS_GET_EPOLL_STAT_NULL, NULL, NULL)
{
    KNET_TelemetryInfo telemetryInfo = {0};
    telemetryInfo.msgReady[0] = 1;
    telemetryInfo.telemetryType = KNET_TELEMETRY_GET_EPOLL_STAT;

    KTestMock *Mock = CreateMock();
    Mock->Create(rte_malloc, TEST_GetFuncRetPositive(0)); // 返回 NULL
    ShowDpStats(&telemetryInfo, 0);
    DT_ASSERT_EQUAL(telemetryInfo.msgReady[0], 0);
    DT_ASSERT_EQUAL(telemetryInfo.epollDetailCtx, NULL);
    Mock->Delete(rte_malloc);
    DeleteMock(Mock);
}

/**
 * @brief ShowDpStats KNET_TELEMETRY_GET_EPOLL_STAT 分支, rte_malloc 打桩返回有效内存, 覆盖成功路径
 */
DTEST_CASE_F(SOCKETEXT, TEST_SHOWDPSTATS_GET_EPOLL_STAT_OK, NULL, NULL)
{
    KNET_TelemetryInfo telemetryInfo = {0};
    telemetryInfo.msgReady[0] = 1;
    telemetryInfo.telemetryType = KNET_TELEMETRY_GET_EPOLL_STAT;

    KTestMock *Mock = CreateMock();
    Mock->Create(rte_malloc, MockRteMallocOk);
    ShowDpStats(&telemetryInfo, 0);
    DT_ASSERT_EQUAL(telemetryInfo.msgReady[0], 0);
    DT_ASSERT_NOT_EQUAL(telemetryInfo.epollDetailCtx, NULL);
    if (telemetryInfo.epollDetailCtx != NULL) {
        DT_ASSERT_EQUAL(telemetryInfo.epollDetailCtx[0].isLast, true);
        free(telemetryInfo.epollDetailCtx);
        telemetryInfo.epollDetailCtx = NULL;
    }
    Mock->Delete(rte_malloc);
    DeleteMock(Mock);
}

/**
 * @brief ShowDpStats default 分支 (非法 telemetryType)
 */
DTEST_CASE_F(SOCKETEXT, TEST_SHOWDPSTATS_DEFAULT, NULL, NULL)
{
    KNET_TelemetryInfo telemetryInfo = {0};
    telemetryInfo.msgReady[0] = 1;
    telemetryInfo.telemetryType = (KNET_TelemetryType)99;

    ShowDpStats(&telemetryInfo, 0);
    DT_ASSERT_EQUAL(telemetryInfo.msgReady[0], 0);
}

/**
 * @brief GetFdCountMp 直接调用测试
 */
DTEST_CASE_F(SOCKETEXT, TEST_GETFDcountMP_DIRECT, NULL, NULL)
{
    KNET_TelemetryInfo telemetryInfo = {0};
    telemetryInfo.statType = 0;
    GetFdCountMp(&telemetryInfo, 0);
    DT_ASSERT_NOT_EQUAL(telemetryInfo.message[0][0], '\0');
}

/**
 * @brief PrepareAllDpStates 空入参测试
 */
DTEST_CASE_F(SOCKETEXT, TEST_PREPAREALLDPSTATES_NULL, NULL, NULL)
{
    PrepareAllDpStates(NULL);
}

/**
 * @brief PrepareAllDpStates state=ERROR, 覆盖循环内早返回分支
 */
DTEST_CASE_F(SOCKETEXT, TEST_PREPAREALLDPSTATES_ERROR, NULL, NULL)
{
    KNET_TelemetryPersistInfo telemetryInfo = {0};
    telemetryInfo.state = KNET_TELE_PERSIST_ERROR;

    KTestMock *Mock = CreateMock();
    Mock->Create(DP_ShowStatistics, TEST_GetFuncRetPositive(0));
    PrepareAllDpStates(&telemetryInfo);
    DT_ASSERT_EQUAL(telemetryInfo.state, KNET_TELE_PERSIST_ERROR);
    Mock->Delete(DP_ShowStatistics);
    DeleteMock(Mock);
}

/**
 * @brief PrepareAllDpStates 正常流程, 覆盖完整循环 + 设置 MSGREADY
 */
DTEST_CASE_F(SOCKETEXT, TEST_PREPAREALLDPSTATS_NORMAL, NULL, NULL)
{
    KNET_TelemetryPersistInfo telemetryInfo = {0};
    telemetryInfo.state = KNET_TELE_PERSIST_INTI;

    KTestMock *Mock = CreateMock();
    Mock->Create(DP_ShowStatistics, TEST_GetFuncRetPositive(0));
    PrepareAllDpStates(&telemetryInfo);
    DT_ASSERT_EQUAL(telemetryInfo.state, KNET_TELE_PERSIST_MSGREADY);
    Mock->Delete(DP_ShowStatistics);
    DeleteMock(Mock);
}

/**
 * @brief GetSockDetailsMp 非 hijack fd, 覆盖错误返回路径
 */
DTEST_CASE_F(SOCKETEXT, TEST_GETSOCKDETAILS_NOT_HIJACK, NULL, NULL)
{
    KNET_SocketDetails socketDetails = {0};
    int ret = GetSockDetailsMp(0, &socketDetails);
    DT_ASSERT_EQUAL(ret, -1);
    DT_ASSERT_EQUAL(socketDetails.isReady, false);
}

/**
 * @brief GetSockDetailsMp hijack fd, DP_GetSocketDetails 返回 0, 覆盖成功路径
 */
DTEST_CASE_F(SOCKETEXT, TEST_GETSOCKDETAILS_HIJACK_OK, NULL, NULL)
{
    KNET_SocketDetails socketDetails = {0};

    KTestMock *Mock = CreateMock();
    Mock->Create(KNET_IsFdHijack, TEST_GetFuncRetPositive(1));
    Mock->Create(DP_GetSocketDetails, TEST_GetFuncRetPositive(0));
    int ret = GetSockDetailsMp(0, &socketDetails);
    DT_ASSERT_EQUAL(ret, KNET_OK);
    DT_ASSERT_EQUAL(socketDetails.isReady, true);
    Mock->Delete(DP_GetSocketDetails);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}

/**
 * @brief GetSockDetailsMp hijack fd, DP_GetSocketDetails 返回 -1, 覆盖失败路径
 */
DTEST_CASE_F(SOCKETEXT, TEST_GETSOCKDETAILS_HIJACK_FAIL, NULL, NULL)
{
    KNET_SocketDetails socketDetails = {0};

    KTestMock *Mock = CreateMock();
    Mock->Create(KNET_IsFdHijack, TEST_GetFuncRetPositive(1));
    Mock->Create(DP_GetSocketDetails, TEST_GetFuncRetNegative(1));
    int ret = GetSockDetailsMp(0, &socketDetails);
    DT_ASSERT_EQUAL(ret, -1);
    DT_ASSERT_EQUAL(socketDetails.isReady, false);
    Mock->Delete(DP_GetSocketDetails);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}

/**
 * @brief GetNetStatMp 直接调用, rte_malloc 打桩返回有效内存, 覆盖成功路径
 */
DTEST_CASE_F(SOCKETEXT, TEST_GETNETSTAT_OK, NULL, NULL)
{
    KNET_TelemetryInfo telemetryInfo = {0};

    KTestMock *Mock = CreateMock();
    Mock->Create(rte_malloc, MockRteMallocOk);
    KNET_SocketState *ret = GetNetStatMp(&telemetryInfo, 0);
    DT_ASSERT_NOT_EQUAL(ret, NULL);
    if (ret != NULL) {
        DT_ASSERT_EQUAL(ret[0].isLast, true);
        free(ret);
    }
    Mock->Delete(rte_malloc);
    DeleteMock(Mock);
}

/**
 * @brief GetEpollStatMp 直接调用, rte_malloc 打桩返回有效内存, 覆盖成功路径
 */
DTEST_CASE_F(SOCKETEXT, TEST_GETEPOLSTAT_OK, NULL, NULL)
{
    KNET_TelemetryInfo telemetryInfo = {0};

    KTestMock *Mock = CreateMock();
    Mock->Create(rte_malloc, MockRteMallocOk);
    EpollTelemetryContext *ret = GetEpollStatMp(&telemetryInfo, 0);
    DT_ASSERT_NOT_EQUAL(ret, NULL);
    if (ret != NULL) {
        DT_ASSERT_EQUAL(ret[0].isLast, true);
        free(ret);
    }
    Mock->Delete(rte_malloc);
    DeleteMock(Mock);
}
