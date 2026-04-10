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
#include <regex.h>

#include "securec.h"
#include "knet_lock.h"

#include "common.h"
#include "mock.h"

#include "knet_types.h"
#include "knet_log.h"
#include "knet_utils.h"

DTEST_CASE_F(BASE, TEST_BASE_PARSE_MAC_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    int ret = 0;

    uint8_t outputMac;

    ret = KNET_ParseMac(NULL, NULL);
    DT_ASSERT_EQUAL(ret, -1);

    ret = KNET_ParseMac("00:1A:2B:3C:4D:5E", NULL);
    DT_ASSERT_EQUAL(ret, -1);

    ret = KNET_ParseMac(":", &outputMac);
    DT_ASSERT_EQUAL(ret, -1);

    ret = KNET_ParseMac("000:1A:2B:3C:4D:5E", &outputMac);
    DT_ASSERT_EQUAL(ret, -1);

    ret = KNET_ParseMac("00:1A:2B:3C:4D:5E:6F", &outputMac);
    DT_ASSERT_EQUAL(ret, -1);

    ret = KNET_ParseMac("GG:1A:2B:3C:4D:5E", &outputMac);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Create(sscanf_s, TEST_GetFuncRetPositive(0));
    ret = KNET_ParseMac("00:1A:2B:3C:4D:5E", &outputMac);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(sscanf_s);

    ret = KNET_ParseMac("00.1A.2B.3C.4D.5E", &outputMac);
    DT_ASSERT_EQUAL(ret, -1);

    ret = KNET_ParseMac("00:1A:2B:3c:4d:5e", &outputMac);
    DT_ASSERT_EQUAL(ret, 0);

    DeleteMock(Mock);
}

DTEST_CASE_F(BASE, TEST_REG_MATCH_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    bool ret;

    ret = KNET_RegMatch("^[0-9]+$", "12345");
    DT_ASSERT_EQUAL(ret, true);

    ret = KNET_RegMatch("^[a-zA-Z]+$", "123abc");
    DT_ASSERT_EQUAL(ret, false);

    ret = KNET_RegMatch(NULL, "test_string");
    DT_ASSERT_EQUAL(ret, false);

    ret = KNET_RegMatch("test_pattern", NULL);
    DT_ASSERT_EQUAL(ret, false);

    ret = KNET_RegMatch("([a-z]*", "test");
    DT_ASSERT_EQUAL(ret, false);

    Mock->Create(regcomp, TEST_GetFuncRetPositive(1)); // 模拟 regcomp 返回错误码 1
    ret = KNET_RegMatch("pattern", "string");
    DT_ASSERT_EQUAL(ret, false);
    Mock->Delete(regcomp);

    Mock->Create(regexec, TEST_GetFuncRetPositive(1)); // REG_NOMATCH
    ret = KNET_RegMatch("^[A-Z]+$", "abc123");
    DT_ASSERT_EQUAL(ret, false);
    Mock->Delete(regexec);

    Mock->Create(regexec, TEST_GetFuncRetPositive(2)); // 错误码 REG_BADPAT 2
    ret = KNET_RegMatch("^[a-z]+$", "ABC123");
    DT_ASSERT_EQUAL(ret, false);
    Mock->Delete(regexec);

    DeleteMock(Mock);
}

DTEST_CASE_F(MAIN_DAEMON, TEST_KNET_TRANS_STR_TO_NUM, NULL, NULL)
{
    uint32_t num;
    int32_t ret = 0;

    // 测试num为NULL
    ret = KNET_TransStrToNum("123", NULL);
    DT_ASSERT_EQUAL(ret, -1);

    // 测试str为NULL
    ret = KNET_TransStrToNum(NULL, &num);
    DT_ASSERT_EQUAL(ret, -1);

    // 测试str包含非数字字符
    ret = KNET_TransStrToNum("123a", &num);
    DT_ASSERT_EQUAL(ret, -1);

    // 测试str为空字符串
    ret = KNET_TransStrToNum("", &num);
    DT_ASSERT_EQUAL(ret, 0);

    // 测试str表示的数字超出long范围
    ret = KNET_TransStrToNum("9223372036854775808", &num); // 超出long范围
    DT_ASSERT_EQUAL(ret, -1);

    // 测试str表示的数字小于0
    ret = KNET_TransStrToNum("-1", &num);
    DT_ASSERT_EQUAL(ret, -1);

    // 测试str表示的数字大于UINT32_MAX
    ret = KNET_TransStrToNum("4294967296", &num); // 大于UINT32_MAX
    DT_ASSERT_EQUAL(ret, -1);

    // 测试正常转换
    ret = KNET_TransStrToNum("123", &num);
    DT_ASSERT_EQUAL(ret, 0);

    ret = KNET_TransStrToNum("0", &num);
    DT_ASSERT_EQUAL(ret, 0);
    DT_ASSERT_EQUAL(num, 0);

    ret = KNET_TransStrToNum("4294967295", &num); // UINT32_MAX
    DT_ASSERT_EQUAL(ret, 0);
}

DTEST_CASE_F(BASE, TEST_CPU_DETECTED, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    int ret;

    // 测试snprintf_s失败
    Mock->Create(snprintf_s, TEST_GetFuncRetNegative(1)); // 模拟 snprintf_s 返回负数
    ret = KNET_CpuDetected(0);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(snprintf_s);

    // 测试access失败
    Mock->Create(access, TEST_GetFuncRetPositive(1)); // 模拟 access 返回非0
    ret = KNET_CpuDetected(0);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(access);

    // 测试正常情况
    ret = KNET_CpuDetected(0);
    DT_ASSERT_EQUAL(ret, 0);

    DeleteMock(Mock);
}