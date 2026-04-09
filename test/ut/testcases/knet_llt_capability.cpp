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
#include <string.h>

#include "securec.h"
#include "common.h"
#include "mock.h"
#include "knet_capability.h"

struct KnetCapInfo {
    uint8_t capEffMap;
    char padding[7];   // 填充字节，确保结构体8 字节对齐
};

extern struct KnetCapInfo g_knetCapInfo;

DTEST_CASE_F(KNET_CAPABILITY, TEST_CAPABILITY_GET_CLEAR_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(geteuid, TEST_GetFuncRetPositive(1));

    KNET_GetCap(0x1f);
    DT_ASSERT_EQUAL(g_knetCapInfo.capEffMap, 0x1f);

    KNET_ClearCap(0x1f);
    DT_ASSERT_EQUAL(g_knetCapInfo.capEffMap, 0x0);

    Mock->Delete(geteuid);
    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_CAPABILITY, TEST_CAPABILITY_GET_INVALID, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(geteuid, TEST_GetFuncRetPositive(1));
    KNET_GetCap(0x1f);
    DT_ASSERT_EQUAL(g_knetCapInfo.capEffMap, 0x1f);
    
    KNET_GetCap(0xff);
    DT_ASSERT_EQUAL(g_knetCapInfo.capEffMap, 0x1f);

    KNET_ClearCap(0xff);
    DT_ASSERT_EQUAL(g_knetCapInfo.capEffMap, 0x1f);

    KNET_ClearCap(0x1f);
    DT_ASSERT_EQUAL(g_knetCapInfo.capEffMap, 0x0);

    Mock->Delete(geteuid);
    DeleteMock(Mock);
}
