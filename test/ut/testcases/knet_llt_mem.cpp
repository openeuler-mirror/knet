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
#include <rte_malloc.h>

#include "common.h"
#include "mock.h"
#include "knet_log.h"
#include "knet_config.h"
#include "knet_mem.h"

extern "C" {
extern void KNET_SetRunMode(int8_t mode);
extern __thread bool g_isInSignalQuit;
}
void *KnetRteMalloc(const char *type, size_t size, unsigned align)
{
    return malloc(size);
}

DTEST_CASE_F(MEM, TEST_MEM_ALLOC_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_malloc, KnetRteMalloc);

    KNET_SetRunMode(KNET_RUN_MODE_SINGLE);

    size_t sz = 1;
    void *mem = KNET_MemAlloc(sz);
    DT_ASSERT_NOT_EQUAL(mem, NULL);

    free(mem);

    Mock->Delete(rte_malloc);
    DeleteMock(Mock);
}

DTEST_CASE_F(MEM, TEST_MEM_FREE_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_free, TEST_GetFuncRetPositive(0));

    KNET_SetRunMode(KNET_RUN_MODE_SINGLE);

    void *mem = NULL;
    KNET_MemFree(mem);

    Mock->Delete(rte_free);
    DeleteMock(Mock);
}

DTEST_CASE_F(MEM, TEST_MEM_ALLOC_INVALID, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_malloc, KnetRteMalloc);

    KNET_SetRunMode(KNET_RUN_MODE_INVALID);

    size_t sz = 1;
    void *mem = KNET_MemAlloc(sz);
    DT_ASSERT_NOT_EQUAL(mem, NULL);

    free(mem);

    Mock->Delete(rte_malloc);
    DeleteMock(Mock);
}

DTEST_CASE_F(MEM, TEST_MEM_FREE_INVALID, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_free, TEST_GetFuncRetPositive(0));

    KNET_SetRunMode(KNET_RUN_MODE_INVALID);

    void *mem = NULL;
    KNET_MemFree(mem);

    Mock->Delete(rte_free);
    DeleteMock(Mock);
}

DTEST_CASE_F(MEM, TEST_MEM_FREE_SIGNALQUIT, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_free, TEST_GetFuncRetPositive(0));

    KNET_SetRunMode(KNET_RUN_MODE_MULTIPLE);
    KNET_MemSetFlagInSignalQuiting();

    void *mem = NULL;
    KNET_MemFree(mem);

    Mock->Delete(rte_free);
    DeleteMock(Mock);
}