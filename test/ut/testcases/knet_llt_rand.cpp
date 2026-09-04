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
#include <rte_cycles.h>
#include <rte_timer.h>

#include "knet_lock.h"
#include "knet_log.h"
#include "knet_atomic.h"
#include "knet_rand.h"
#include "securec.h"
#include "common.h"
#include "mock.h"

#define RAND_DATA_LENGTH 8

static int64_t MockReadFunc()
{
    return -1;
}

DTEST_CASE_F(RAND, TEST_KNET_GET_RAND_NUM_NORMAL, NULL, NULL)
{
    uint8_t data[RAND_DATA_LENGTH] = {0};
    uint32_t len = RAND_DATA_LENGTH;
    int64_t ret = 0;

    ret = KNET_RandInit();
    DT_ASSERT_EQUAL(ret, 0);

    ret = KNET_GetRandomNum(data, len);
    DT_ASSERT_EQUAL(1, ret <= (int64_t)len);

    KNET_RandUninit();
}

DTEST_CASE_F(RAND, TEST_KNET_GET_RAND_NUM_OPEN_ABNORMAL, NULL, NULL)
{
    uint8_t data[RAND_DATA_LENGTH] = {0};
    uint32_t len = RAND_DATA_LENGTH;
    int64_t ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(open, TEST_GetFuncRetNegative(1));

    ret = KNET_GetRandomNum(data, len);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(open);
    DeleteMock(Mock);
}

DTEST_CASE_F(RAND, TEST_KNET_GET_RAND_NUM_READ_ABNORMAL, NULL, NULL)
{
    uint8_t data[RAND_DATA_LENGTH] = {0};
    uint32_t len = RAND_DATA_LENGTH;
    int64_t ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(read, MockReadFunc);

    ret = KNET_GetRandomNum(data, len);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(read);
    DeleteMock(Mock);
}

/**
 * @brief KNET_RandInit: open失败(g_randFd<0)
 */
DTEST_CASE_F(RAND, TEST_KNET_RAND_INIT_OPEN_FAIL, NULL, NULL)
{
    int64_t ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(open, TEST_GetFuncRetNegative(1));

    ret = KNET_RandInit();
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(open);
    DeleteMock(Mock);
}

/**
 * @brief KNET_RandInit: 已初始化(g_randFd>=0),直接返回0
 */
DTEST_CASE_F(RAND, TEST_KNET_RAND_INIT_ALREADY, NULL, NULL)
{
    int64_t ret = 0;

    ret = KNET_RandInit();
    DT_ASSERT_EQUAL(ret, 0);

    ret = KNET_RandInit();
    DT_ASSERT_EQUAL(ret, 0);

    KNET_RandUninit();
}

/**
 * @brief KNET_GetRandomNum: read失败(先正常init再mock read失败)
 */
DTEST_CASE_F(RAND, TEST_KNET_GET_RAND_NUM_READ_FAIL_AFTER_INIT, NULL, NULL)
{
    uint8_t data[RAND_DATA_LENGTH] = {0};
    uint32_t len = RAND_DATA_LENGTH;
    int64_t ret = 0;

    ret = KNET_RandInit();
    DT_ASSERT_EQUAL(ret, 0);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(read, MockReadFunc);

    ret = KNET_GetRandomNum(data, len);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(read);
    DeleteMock(Mock);
    KNET_RandUninit();
}

/**
 * @brief KNET_GetRandomNum: 参数len为0,read返回0(bytesRead=0正常路径)
 */
DTEST_CASE_F(RAND, TEST_KNET_GET_RAND_NUM_ZERO_LEN, NULL, NULL)
{
    uint8_t data[RAND_DATA_LENGTH] = {0};
    int64_t ret = 0;

    ret = KNET_RandInit();
    DT_ASSERT_EQUAL(ret, 0);

    ret = KNET_GetRandomNum(data, 0);
    DT_ASSERT_EQUAL(ret, 0);

    KNET_RandUninit();
}
