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

#include "tcp_fd.h"
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
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>

#include "securec.h"
#include "knet_lock.h"
#include "knet_config.h"
#include "common.h"
#include "mock.h"

#define DTEST_OSFD 1
#define DTEST_TID 2

DTEST_CASE_F(FRAME_FD, TEST_FRAME_ISVAL_NORMAL, Init, Deinit)
{
    int osfd = 0;
    bool ret;

    // 先调用 KNET_SetFdStateAndType 设置状态
    KNET_SetFdStateAndType(KNET_FD_STATE_HIJACK, osfd, 1, KNET_FD_TYPE_SOCKET);

    // 验证 KNET_IsFdValid 返回 true
    ret = KNET_IsFdValid(osfd);
    DT_ASSERT_EQUAL(ret, true);

    // 验证 KNET_IsFdHijack 返回 true
    ret = KNET_IsFdHijack(osfd);
    DT_ASSERT_EQUAL(ret, true);

    // 验证 KNET_GetFdType 返回 KNET_FD_TYPE_SOCKET
    enum KNET_FdType type = KNET_GetFdType(osfd);
    DT_ASSERT_EQUAL(type, KNET_FD_TYPE_SOCKET);
}

DTEST_CASE_F(FRAME_FD, TEST_FRAME_OSTODP_NORMAL, Init, Deinit)
{
    int osfd = 0;
    int ret = 0;

    // 先调用 KNET_SetFdStateAndType 设置 dpFd
    KNET_SetFdStateAndType(KNET_FD_STATE_HIJACK, osfd, 1, KNET_FD_TYPE_SOCKET);

    // 验证 KNET_OsFdToDpFd 返回正确的 dpFd
    ret = KNET_OsFdToDpFd(osfd);
    DT_ASSERT_EQUAL(ret, 1);

    // 验证 KNET_GetFdType 返回 KNET_FD_TYPE_SOCKET
    enum KNET_FdType type = KNET_GetFdType(osfd);
    DT_ASSERT_EQUAL(type, KNET_FD_TYPE_SOCKET);
}

DTEST_CASE_F(FRAME_FD, TEST_FRAME_ISFD_NORMAL, Init, Deinit)
{
    int osfd = 0;
    bool ret;

    // 先调用 KNET_SetFdStateAndType 设置状态为 KNET_FD_STATE_HIJACK
    KNET_SetFdStateAndType(KNET_FD_STATE_HIJACK, osfd, 1, KNET_FD_TYPE_SOCKET);

    // 验证 KNET_IsFdHijack 返回 true
    ret = KNET_IsFdHijack(osfd);
    DT_ASSERT_EQUAL(ret, true);

    // 验证 KNET_GetFdType 返回 KNET_FD_TYPE_SOCKET
    enum KNET_FdType type = KNET_GetFdType(osfd);
    DT_ASSERT_EQUAL(type, KNET_FD_TYPE_SOCKET);

    // 再调用 KNET_ResetFdState 重置状态
    KNET_ResetFdState(osfd);

    // 验证 KNET_IsFdHijack 返回 false
    ret = KNET_IsFdHijack(osfd);
    DT_ASSERT_EQUAL(ret, false);

    // 验证 KNET_GetFdType 返回 KNET_FD_TYPE_INVALID
    type = KNET_GetFdType(osfd);
    DT_ASSERT_EQUAL(type, KNET_FD_TYPE_INVALID);
}

DTEST_CASE_F(FRAME_FD, TEST_FRAME_INIT_ABNORMAL, Init, Deinit)
{
    int osfd = 0;
    int ret;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    /* getrlimit Failed */
    Mock->Create(getrlimit, TEST_GetFuncRetNegative(1));
    KNET_FdDeinit();
    ret = KNET_FdInit();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(getrlimit);

    /* malloc Failed */
    Mock->Create(calloc, TEST_GetFuncRetPositive(0));
    KNET_FdDeinit();
    ret = KNET_FdInit();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(calloc);

    KNET_FdInit();
    DeleteMock(Mock);
}

DTEST_CASE_F(FRAME_FD, TEST_FRAME_COMBINED, Init, Deinit)
{
    int osfd = 0;
    int ret;
    bool retBool;
    uint64_t retTid;

    KNET_SetEstablishedFdState(osfd);
    KNET_EpHasOsFdSet(osfd);
    KNET_SetEpollFdTid(osfd, DTEST_OSFD);

    ret = KNET_GetEstablishedFdState(osfd);
    DT_ASSERT_EQUAL(ret, KNET_ESTABLISHED_FD);

    retBool = KNET_EpfdHasOsfdGet(osfd);
    DT_ASSERT_EQUAL(retBool, true);

    retTid = KNET_GetEpollFdTid(osfd);
    DT_ASSERT_EQUAL(retTid, DTEST_OSFD);

    KNET_SetEpollFdTid(osfd, DTEST_TID);

    retTid = KNET_GetEpollFdTid(osfd);
    DT_ASSERT_EQUAL(retTid, DTEST_TID);
}