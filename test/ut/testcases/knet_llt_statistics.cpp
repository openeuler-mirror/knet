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


#include "dp_debug_api.h"
#include "dp_show_api.h"

#include "knet_log.h"
#include "knet_sal_func.h"

#include "securec.h"
#include "common.h"
#include "mock.h"
#include "rte_hash.h"

static int g_pktStatistics = 0;
extern "C" {
#include "knet_telemetry.h"
uint32_t KNET_ACC_Debug(uint32_t flag, char *output, uint32_t len);
}

static void FuncRet2(const char *function, int line, int level, const char *format, ...)
{
    g_pktStatistics++;
}

static uint32_t FuncRet1(DP_DebugShowHook hook)
{
    g_pktStatistics++;
}

DTEST_CASE_F(STATISTICS, TEST_PKT_STATISTICS_DEBUG, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(DP_DebugShowHookReg, FuncRet1);
    Mock->Create(KNET_Log, FuncRet2);

    g_pktStatistics = 0;
    KnetRegDebug();
    DT_ASSERT_EQUAL(g_pktStatistics, 1);

    g_pktStatistics = 0;

    KNET_ACC_Debug(KNET_STAT_OUTPUT_TO_LOG, "12345", 6); // 6 is string length
    DT_ASSERT_EQUAL(g_pktStatistics, 1);
    g_pktStatistics = 0;
    KNET_ACC_Debug(KNET_STAT_OUTPUT_TO_SCREEN, "12345", 6); // 6 is string length
    DT_ASSERT_EQUAL(g_pktStatistics, 0);
    g_pktStatistics = 0;

    Mock->Delete(DP_DebugShowHookReg);
    Mock->Delete(KNET_Log);

    DeleteMock(Mock);
}
