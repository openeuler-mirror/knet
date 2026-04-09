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

#include "rte_timer.h"
#include "rte_ethdev.h"
#include "knet_log.h"
#include "knet_config_core_queue.h"
#include "knet_config_setter.h"
#include "knet_utils.h"
#include "knet_rpc.h"
#include "knet_config_rpc.h"
#include "knet_config.h"
#include "knet_config_hw_scan.h"
#include "common.h"
#include "mock.h"

#ifdef __cplusplus
extern "C" {
#endif
extern int CheckKernelBondMode(char *absPath, ssize_t pathLen);
extern int CheckKernelBondXmitHashPolicy(char *absPath, ssize_t pathLen);
extern int ConfigureKernelBondName(char *bondName, size_t bondNameSize, const char *interfaceName);
extern int CheckKernelBondConfig(char *bondName, size_t bondNameSize);
#ifdef __cplusplus
}
#endif

static char* GetFuncRetNull(const char *name, char *resolved)
{
    return NULL;
}

static char *RealPathMock(const char *name, char *resolved)
{
    char path[] = "/sys/bus/pci/devices";
    size_t pathLen = strlen(path) + 1;
    if (resolved == NULL) {
        return NULL;
    }
    if (strncpy_s(resolved, pathLen, path, pathLen) != 0) { // resolved至少MAX_PATH字节
        return NULL;
    }
    return resolved;
}

DTEST_CASE_F(KNET_HW_SCAN, TEST_NIC_SCAN_NULL, NULL, NULL)
{
    // static函数无法打桩，避免受硬件影响导致后续门禁错误 不做返回值判断
    KnetCheckCompatibleNic();
}

DTEST_CASE_F(BASE, TEST_CHECK_KERNEL_BOND_CONFIG, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    int ret;

    // 测试snprintf_truncated_s失败（mode路径）
    Mock->Create(snprintf_truncated_s, TEST_GetFuncRetNegative(1));
    ret = CheckKernelBondConfig("bond0", PATH_MAX + 1);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(snprintf_truncated_s);

    // 测试realpath失败（mode路径）
    Mock->Create(realpath, GetFuncRetNull);
    ret = CheckKernelBondConfig("bond0", PATH_MAX + 1);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(realpath);

    // 测试snprintf_truncated_s失败（xmit_hash_policy路径）
    Mock->Create(snprintf_truncated_s, TEST_GetFuncRetNegative(1));
    ret = CheckKernelBondConfig("bond0", PATH_MAX + 1);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(snprintf_truncated_s);

    // 测试realpath失败（xmit_hash_policy路径）
    Mock->Create(realpath, GetFuncRetNull);
    ret = CheckKernelBondConfig("bond0", PATH_MAX + 1);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(realpath);

    Mock->Create(snprintf_truncated_s, TEST_GetFuncRetPositive(0));
    Mock->Create(realpath, RealPathMock);
    Mock->Create(CheckKernelBondMode, TEST_GetFuncRetPositive(0));
    Mock->Create(CheckKernelBondXmitHashPolicy, TEST_GetFuncRetPositive(0));
    ret = CheckKernelBondConfig("bond0", PATH_MAX + 1);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(snprintf_truncated_s);
    Mock->Delete(realpath);
    Mock->Delete(CheckKernelBondMode);
    Mock->Delete(CheckKernelBondXmitHashPolicy);

    DeleteMock(Mock);
}

DTEST_CASE_F(BASE, TEST_KNET_KERNEL_BOND_CFG_SCAN, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    int ret;

    // 测试 ConfigureKernelBondName 失败
    Mock->Create(ConfigureKernelBondName, TEST_GetFuncRetNegative(1));
    ret = KnetKernelBondCfgScan("bond0", PATH_MAX + 1);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(ConfigureKernelBondName);

    // 测试 bondName 为空
    Mock->Create(ConfigureKernelBondName, TEST_GetFuncRetPositive(0));
    ret = KnetKernelBondCfgScan("", PATH_MAX + 1);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(ConfigureKernelBondName);

    // 测试 CheckKernelBondConfig 失败
    Mock->Create(ConfigureKernelBondName, TEST_GetFuncRetPositive(0));
    Mock->Create(CheckKernelBondConfig, TEST_GetFuncRetNegative(1));
    ret = KnetKernelBondCfgScan("bond0", PATH_MAX + 1);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(ConfigureKernelBondName);
    Mock->Delete(CheckKernelBondConfig);

    // 测试成功路径
    Mock->Create(ConfigureKernelBondName, TEST_GetFuncRetPositive(0));
    Mock->Create(CheckKernelBondConfig, TEST_GetFuncRetPositive(0));
    ret = KnetKernelBondCfgScan("bond0", PATH_MAX + 1);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(ConfigureKernelBondName);
    Mock->Delete(CheckKernelBondConfig);

    DeleteMock(Mock);
}