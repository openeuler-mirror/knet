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

#include <gtest/gtest.h>
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
#include "mock.h"
#include "common.h"
#include "knet_atomic.h"
#include "rte_config.h"
#include "rte_atomic.h"

DTEST_CASE_F(BASE, TEST_ATOMIC_TEST_SET_64_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    int ret = 0;
    KNET_ATOMIC64_T value = {0};

    Mock->Create(rte_atomic64_test_and_set, TEST_GetFuncRetPositive(1));
    ret = KNET_HalAtomicTestSet64(&value);
    DT_ASSERT_EQUAL(ret, 1);
    Mock->Delete(rte_atomic64_test_and_set);

    DeleteMock(Mock);
}

DTEST_CASE_F(BASE, TEST_ATOMIC_READ_64_NORMAL, NULL, NULL)
{
    uint64_t ret = 0;
    KNET_ATOMIC64_T value = {0};

    ret = KNET_HalAtomicRead64(&value);
    DT_ASSERT_EQUAL(ret, value.count);
}

DTEST_CASE_F(BASE, TEST_ATOMIC_SET_64_NORMAL, NULL, NULL)
{
    KNET_ATOMIC64_T value;
    uint64_t newVal;

    KNET_HalAtomicSet64(&value, newVal);
}

DTEST_CASE_F(BASE, TEST_ATOMIC_ADD_64_NORMAL, NULL, NULL)
{
    KNET_ATOMIC64_T value;
    uint64_t newVal;

    KNET_HalAtomicAdd64(&value, newVal);
}

DTEST_CASE_F(BASE, TEST_ATOMIC_SUB_64_NORMAL, NULL, NULL)
{
    KNET_ATOMIC64_T value;
    uint64_t newVal;

    KNET_HalAtomicSub64(&value, newVal);
}

