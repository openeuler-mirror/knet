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

#include "securec.h"
#include "knet_lock.h"
#include "common.h"
#include "mock.h"
#include "securec.h"
#include "knet_types.h"
#include "knet_log.h"
#include "knet_pktpool.h"
#include "knet_mock.h"
#include "knet_pkt.h"

extern "C" {
bool KnetIsCurrentMainThread(void);
}

DTEST_CASE_F(MBUF, TEST_PKT_ALLOC_NORMAL, NULL, NULL)
{
    void *ret;

    ret = KNET_PktAlloc(0);
    DT_ASSERT_EQUAL(ret, NULL);
}

DTEST_CASE_F(MBUF, TEST_PKT_FREE_NORMAL, NULL, NULL)
{
    struct rte_mbuf *mbuf = NULL;

    KNET_PktFree(mbuf);
}

DTEST_CASE_F(MBUF, TEST_PKT_BATCH_FREE_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_mempool_generic_put, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_mempool_put_bulk, TEST_GetFuncRetPositive(0));

    KNET_PktBatchFree();

    Mock->Delete(rte_mempool_generic_put);
    Mock->Delete(rte_mempool_put_bulk);
    DeleteMock(Mock);
}

/**
 * 打桩：打桩syscall, getpid, 返回相同id
 * 期望：主线程的tid和进程的pid相同，返回true
 */
DTEST_CASE_F(MBUF, TEST_IS_CURRENT_MAIN_THREAD_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(syscall, TEST_GetFuncRetPositive(0));
    Mock->Create(getpid, TEST_GetFuncRetPositive(0));

    bool ret = KnetIsCurrentMainThread();
    DT_ASSERT_EQUAL(ret, true);

    Mock->Delete(syscall);
    Mock->Delete(getpid);
    DeleteMock(Mock);
}