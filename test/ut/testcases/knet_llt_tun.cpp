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

#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <linux/if_arp.h>
#include <linux/if_tun.h>
#include <linux/if.h>

#include "knet_config.h"
#include "knet_log.h"
#include "knet_tun.h"
#include "common.h"
#include "mock.h"

#define MAC_LEN 6

DTEST_CASE_F(TUN, TEST_KNET_TAP_CREATE, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    char ifname[IF_NAME_SIZE] = { 0 };
    uint8_t macAddr[MAC_LEN] = { 0 };
    uint16_t mtu = 0;
    uint32_t ipAddr = 0;
    int32_t fd;
    int tapIfIndex;
    int ret = 0;

    Mock->Create(ioctl, TEST_GetFuncRetPositive(0));
    Mock->Create(open, TEST_GetFuncRetPositive(0));
    Mock->Create(memcpy_s, TEST_GetFuncRetPositive(0));
    Mock->Create(socket, TEST_GetFuncRetPositive(0));
    Mock->Create(snprintf_truncated_s, TEST_GetFuncRetPositive(0));
    Mock->Create(strcpy_s, TEST_GetFuncRetPositive(0));
    Mock->Create(fcntl, TEST_GetFuncRetPositive(0));

    ret = KNET_TAPCreate(&fd, &tapIfIndex);
    DT_ASSERT_EQUAL(ret, 0);

    ret = KNET_FetchIfIndex(ifname, IF_NAME_SIZE, &tapIfIndex);
    DT_ASSERT_EQUAL(ret, 0);

    KNET_TapFree(fd);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(strcpy_s);
    Mock->Delete(snprintf_truncated_s);
    Mock->Delete(socket);
    Mock->Delete(memcpy_s);
    Mock->Delete(ioctl);
    Mock->Delete(open);
    Mock->Delete(fcntl);
    DeleteMock(Mock);
}

/**
 * @brief KNET_TapFree 传入 INVALID_FD, 覆盖非法 fd 早返回分支
 */
DTEST_CASE_F(TUN, TEST_KNET_TAP_FREE_INVALID_FD, NULL, NULL)
{
    int32_t ret = KNET_TapFree(INVALID_FD);
    DT_ASSERT_EQUAL(ret, -1);
}

/**
 * @brief KNET_FetchIfIndex socket 失败
 */
DTEST_CASE_F(TUN, TEST_FETCH_IFINDEX_SOCKET_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(socket, TEST_GetFuncRetNegative(1));

    char ifname[IF_NAME_SIZE] = "testtap";
    int ifIndex = 0;
    int32_t ret = KNET_FetchIfIndex(ifname, IF_NAME_SIZE, &ifIndex);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(socket);
    DeleteMock(Mock);
}

/**
 * @brief KNET_FetchIfIndex strcpy_s 失败
 */
DTEST_CASE_F(TUN, TEST_FETCH_IFINDEX_STRCPY_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(socket, TEST_GetFuncRetPositive(0));
    Mock->Create(strcpy_s, TEST_GetFuncRetNegative(1));

    char ifname[IF_NAME_SIZE] = "testtap";
    int ifIndex = 0;
    int32_t ret = KNET_FetchIfIndex(ifname, IF_NAME_SIZE, &ifIndex);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(strcpy_s);
    Mock->Delete(socket);
    DeleteMock(Mock);
}

/**
 * @brief KNET_FetchIfIndex ioctl 失败
 */
DTEST_CASE_F(TUN, TEST_FETCH_IFINDEX_IOCTL_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(socket, TEST_GetFuncRetPositive(0));
    Mock->Create(strcpy_s, TEST_GetFuncRetPositive(0));
    Mock->Create(ioctl, TEST_GetFuncRetNegative(1));

    char ifname[IF_NAME_SIZE] = "testtap";
    int ifIndex = 0;
    int32_t ret = KNET_FetchIfIndex(ifname, IF_NAME_SIZE, &ifIndex);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(ioctl);
    Mock->Delete(strcpy_s);
    Mock->Delete(socket);
    DeleteMock(Mock);
}

/**
 * @brief KNET_FetchIfIndex 全部成功, 覆盖成功路径返回 0
 */
DTEST_CASE_F(TUN, TEST_FETCH_IFINDEX_SUCCESS, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(socket, TEST_GetFuncRetPositive(0));
    Mock->Create(strcpy_s, TEST_GetFuncRetPositive(0));
    Mock->Create(ioctl, TEST_GetFuncRetPositive(0));

    char ifname[IF_NAME_SIZE] = "testtap";
    int ifIndex = 0;
    int32_t ret = KNET_FetchIfIndex(ifname, IF_NAME_SIZE, &ifIndex);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(ioctl);
    Mock->Delete(strcpy_s);
    Mock->Delete(socket);
    DeleteMock(Mock);
}

/**
 * @brief KNET_TAPCreate snprintf_truncated_s 失败
 */
DTEST_CASE_F(TUN, TEST_TAP_CREATE_SNPRINTF_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(snprintf_truncated_s, TEST_GetFuncRetNegative(1));

    int32_t fd = 0;
    int tapIfIndex = 0;
    int ret = KNET_TAPCreate(&fd, &tapIfIndex);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(snprintf_truncated_s);
    DeleteMock(Mock);
}

/**
 * @brief KNET_TAPCreate open 失败导致 CreateInitTap 失败
 */
DTEST_CASE_F(TUN, TEST_TAP_CREATE_OPEN_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(snprintf_truncated_s, TEST_GetFuncRetPositive(0));
    Mock->Create(open, TEST_GetFuncRetNegative(1));

    int32_t fd = 0;
    int tapIfIndex = 0;
    int ret = KNET_TAPCreate(&fd, &tapIfIndex);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(open);
    Mock->Delete(snprintf_truncated_s);
    DeleteMock(Mock);
}

/**
 * @brief KNET_TAPCreate TapAlloc 成功但 TapSetTapInfo 中 socket 失败
 */
DTEST_CASE_F(TUN, TEST_TAP_CREATE_SOCKET_FAIL_IN_SETTAPINFO, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(snprintf_truncated_s, TEST_GetFuncRetPositive(0));
    Mock->Create(open, TEST_GetFuncRetPositive(0));
    Mock->Create(strcpy_s, TEST_GetFuncRetPositive(0));
    Mock->Create(ioctl, TEST_GetFuncRetPositive(0));
    Mock->Create(fcntl, TEST_GetFuncRetPositive(0));
    Mock->Create(memcpy_s, TEST_GetFuncRetPositive(0));
    Mock->Create(socket, TEST_GetFuncRetNegative(1));

    int32_t fd = 0;
    int tapIfIndex = 0;
    int ret = KNET_TAPCreate(&fd, &tapIfIndex);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(socket);
    Mock->Delete(memcpy_s);
    Mock->Delete(fcntl);
    Mock->Delete(ioctl);
    Mock->Delete(strcpy_s);
    Mock->Delete(open);
    Mock->Delete(snprintf_truncated_s);
    DeleteMock(Mock);
}