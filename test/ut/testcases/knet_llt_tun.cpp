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
    Mock->Delete(fcntl);
    DeleteMock(Mock);
}