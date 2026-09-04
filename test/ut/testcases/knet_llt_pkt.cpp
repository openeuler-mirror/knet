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
#include "knet_config.h"

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

static union KNET_CfgValue g_pktCfg = {0};
static union KNET_CfgValue *MockKnetGetCfgPkt(enum KNET_ConfKey key)
{
    (void)key;
    return &g_pktCfg;
}

/**
 * @brief KNET_PktFree 非 NULL mbuf, refcnt_update 返回非 0, 早返回
 */
DTEST_CASE_F(MBUF, TEST_PKT_FREE_REFCNT_NONZERO, NULL, NULL)
{
    struct rte_mbuf mbuf = {0}; // refcnt=0, update(-1) wraps to 65535 (non-zero)

    KNET_PktFree(&mbuf);
}

/**
 * @brief KNET_PktFree 非 NULL mbuf, refcnt=1, 覆盖正常释放到 freeCache 路径
 */
DTEST_CASE_F(MBUF, TEST_PKT_FREE_NORMAL_PATH, NULL, NULL)
{
    struct rte_mbuf mbuf = {0};
    mbuf.refcnt = 1; // refcnt=1 → update(-1) returns 0 → 继续释放流程

    g_pktCfg.intValue = 0;
    KTestMock *Mock = CreateMock();
    Mock->Create(syscall, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_GetCfg, MockKnetGetCfgPkt);
    Mock->Create(KNET_SpinlockLock, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_SpinlockUnlock, TEST_GetFuncRetPositive(0));

    KNET_PktFree(&mbuf);

    Mock->Delete(KNET_SpinlockLock);
    Mock->Delete(KNET_SpinlockUnlock);
    Mock->Delete(syscall);
    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

/**
 * @brief KNET_MbufAttachExtBuf cothread 路径 (intValue==1, 直接 refcnt++)
 */
DTEST_CASE_F(MBUF, TEST_MBUF_ATTACH_EXTBUF_COTHREAD, NULL, NULL)
{
    struct rte_mbuf mbuf = {0};
    struct rte_mbuf_ext_shared_info shinfo = {0};
    char addr[16] = {0};
    mbuf.refcnt = 1;
    shinfo.free_cb = (rte_mbuf_extbuf_free_callback_t)1;

    g_pktCfg.intValue = 1;
    KTestMock *Mock = CreateMock();
    Mock->Create(KNET_GetCfg, MockKnetGetCfgPkt);

    KNET_MbufAttachExtBuf(&mbuf, addr, 0, sizeof(addr), &shinfo);
    DT_ASSERT_EQUAL(shinfo.refcnt, 1);

    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

/**
 * @brief KNET_MbufAttachExtBuf 非 cothread 路径 (intValue==0, 调 rte_mbuf_ext_refcnt_update)
 */
DTEST_CASE_F(MBUF, TEST_MBUF_ATTACH_EXTBUF_NORMAL, NULL, NULL)
{
    struct rte_mbuf mbuf = {0};
    struct rte_mbuf_ext_shared_info shinfo = {0};
    char addr[16] = {0};
    mbuf.refcnt = 1;
    shinfo.free_cb = (rte_mbuf_extbuf_free_callback_t)1;

    g_pktCfg.intValue = 0;
    KTestMock *Mock = CreateMock();
    Mock->Create(KNET_GetCfg, MockKnetGetCfgPkt);

    KNET_MbufAttachExtBuf(&mbuf, addr, 0, sizeof(addr), &shinfo);
    DT_ASSERT_EQUAL(shinfo.refcnt, 1); // rte_mbuf_ext_refcnt_update(shinfo, 1): 0+1=1

    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}