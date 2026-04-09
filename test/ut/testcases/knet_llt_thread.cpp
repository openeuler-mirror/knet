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

#define _GNU_SOURCE

#include <pthread.h>
#include <sched.h>

#include "knet_log.h"
#include "knet_thread.h"

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

void *ThreadFunc(void *args)
{
    printf("Thread_Func\n");
}

DTEST_CASE_F(THREAD, TEST_THREAD_CREATE_NORMAL, NULL, NULL)
{
    int32_t ret = 0;
    uint64_t *thread = 1;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(pthread_create, TEST_GetFuncRetPositive(0));
    uint64_t threadID = 0;

    ret = KNET_CreateThread(&threadID, ThreadFunc, NULL);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(pthread_create);
    DeleteMock(Mock);
}

DTEST_CASE_F(THREAD, TEST_THREAD_SET_NORMAL, NULL, NULL)
{
    int32_t ret = 0;
    uint64_t threadId = 0;
    uint16_t cpus[1];
    cpus[0] = 1;
    uint32_t len = 1;

    ret = KNET_SetThreadAffinity(threadId, NULL, len);
    DT_ASSERT_EQUAL(ret, -1);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(pthread_setaffinity_np, TEST_GetFuncRetPositive(0));
    ret = KNET_SetThreadAffinity(threadId, cpus, len);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(pthread_setaffinity_np);
    DeleteMock(Mock);
}

DTEST_CASE_F(THREAD, TEST_THREAD_GET_NORMAL, NULL, NULL)
{
    int32_t ret = 0;
    uint64_t threadId = 0;
    uint16_t cpus[1];
    cpus[0] = 1;
    uint32_t len[1];
    len[0] = 1;

    ret = KNET_GetThreadAffinity(threadId, NULL, len);
    DT_ASSERT_EQUAL(ret, -1);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(pthread_getaffinity_np, TEST_GetFuncRetPositive(0));
    ret = KNET_GetThreadAffinity(threadId, cpus, len);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(pthread_getaffinity_np);
    DeleteMock(Mock);
}

DTEST_CASE_F(THREAD, TEST_THREAD_JOIN_NORMAL, NULL, NULL)
{
    int32_t res = 0;
    uint64_t threadId = 0;
    void **ret = { 0 };

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(pthread_join, TEST_GetFuncRetPositive(0));
    res = KNET_JoinThread(threadId, ret);
    DT_ASSERT_EQUAL(res, 0);

    Mock->Delete(pthread_join);
    DeleteMock(Mock);
}

DTEST_CASE_F(THREAD, TEST_THREAD_ThreadNameSet_NORMAL, NULL, NULL)
{
    int32_t res = 0;
    uint64_t threadId = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(pthread_setname_np, TEST_GetFuncRetPositive(0));
    res = KNET_ThreadNameSet(threadId, "KnetCpThread");
    DT_ASSERT_EQUAL(res, 0);

    Mock->Delete(pthread_setname_np);
    DeleteMock(Mock);
}

DTEST_CASE_F(THREAD, TEST_THREAD_KNET_ThreadId_NORMAL, NULL, NULL)
{
    int32_t res = 0;
    uint64_t threadId = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(pthread_self, TEST_GetFuncRetPositive(0));
    res = KNET_ThreadId();
    DT_ASSERT_EQUAL(res, 0);

    Mock->Delete(pthread_self);
    DeleteMock(Mock);
}