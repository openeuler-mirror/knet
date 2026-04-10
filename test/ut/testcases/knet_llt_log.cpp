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
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include "securec.h"
#include "knet_log.h"

#include "knet_config.h"
#include "knet_log.h"

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

static KNET_LogLevel g_logLevel = KNET_LOG_DEFAULT;
static union KNET_CfgValue g_cfg = {.intValue = 1};
static union KNET_CfgValue *MockKnetGetCfg(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    g_cfg.intValue = 1;
    return &g_cfg;
}

DTEST_CASE_F(LOG, TEST_LOG_SET_NORMAL, NULL, NULL)
{
    KNET_LogLevel logLevel = (KNET_LogLevel)(KNET_LOG_MAX + 1);
    KNET_LogLevel logLevelBak = g_logLevel;

    KNET_LogLevelSet(logLevel);
    g_logLevel = logLevelBak;
}

DTEST_CASE_F(LOG, TEST_LOG_FIX_HOOK, NULL, NULL)
{
    const char* format = 0;
    int ret = 0;
    
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(vsprintf_s, TEST_GetFuncRetPositive(0));
    KNET_LogFixLenOutputHook(format);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(vsprintf_s);
    DeleteMock(Mock);
}

DTEST_CASE_F(LOG, TEST_LOG_LOG_NORMAL, NULL, NULL)
{
    const char *function = 0;
    int line = 0;
    int level = 0;
    const char *format = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    KNET_Log(function, line, level, format);

    DeleteMock(Mock);
}

DTEST_CASE_F(LOG, TEST_LOG_CONFIG_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_LogLevelSet, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);

    KNET_LogLevelSetByStr(KNET_GetCfg(CONF_COMMON_LOG_LEVEL)->strValue);

    Mock->Delete(KNET_GetCfg);
    Mock->Delete(KNET_LogLevelSet);
    DeleteMock(Mock);
}

DTEST_CASE_F(LOG, TEST_LOG_INIT_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(openlog, TEST_GetFuncRetPositive(0));
    Mock->Create(setlogmask, TEST_GetFuncRetPositive(0));

    KNET_LogInit();

    Mock->Delete(openlog);
    Mock->Delete(setlogmask);
    DeleteMock(Mock);
}

DTEST_CASE_F(LOG, TEST_LOG_UNINIT_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(closelog, TEST_GetFuncRetPositive(0));

    KNET_LogUninit();

    Mock->Delete(closelog);
    DeleteMock(Mock);
}

DTEST_CASE_F(LOG, TEST_LOG_LOGNORMAL, NULL, NULL)
{
    const char *format = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    KNET_LogNormal(format);

    DeleteMock(Mock);
}