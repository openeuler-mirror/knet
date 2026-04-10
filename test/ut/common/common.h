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

#ifndef UT_TEST_COMMON_H_
#define UT_TEST_COMMON_H_

#include "gtest/gtest.h"

class TestBase : public testing::Test {
public:
    virtual void SetUp(void);
    virtual void TearDown(void);
};

#define DTEST_CASE_F(a, b, c, d) TEST_F(TestBase, a##_##b)
#define DT_ASSERT_NOT_EQUAL(a, b) EXPECT_NE(((uintptr_t)a), ((uintptr_t)b))
#define DT_ASSERT_EQUAL(a, b) EXPECT_EQ((uintptr_t)a, (uintptr_t)b)

extern void *g_sysLibcHandle;

#endif