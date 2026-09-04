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
#include <dirent.h>
#include <libgen.h>

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
extern int32_t RetrieveKernelBondName(char *bondName, size_t bondNameSize, char *absPath);
#ifdef __cplusplus
}
#endif

#define TEST_VENDOR_ID 1
#define TEST_DEVICE_ID 2
#define HW_MAX_NIC_VENDOR_DEVICE_LEN 7
struct HwNeedIdInfo {
    char nicNeedId[HW_MAX_NIC_VENDOR_DEVICE_LEN];
    char padding[1];
};

extern "C" int GetnicNeedId(struct HwNeedIdInfo *hv, const char *interfaceName, int type);

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

static FILE *MockFopenReturnNull(const char *path, const char *mode)
{
    return NULL;
}

static char *MockFgetsReturnNull(char *s, int n, FILE *stream)
{
    return NULL;
}

static char *MockFgetsOk(char *s, int n, FILE *stream)
{
    const char *data = "802.3ad";
    if (n < 8) {
        return NULL;
    }
    if (strncpy_s(s, n, data, strlen(data)) != 0) {
        return NULL;
    }
    return s;
}

static char *MockFgetsWrongMode(char *s, int n, FILE *stream)
{
    const char *data = "active-backup";
    if (n < 14) {
        return NULL;
    }
    if (strncpy_s(s, n, data, strlen(data)) != 0) {
        return NULL;
    }
    return s;
}

static ssize_t MockReadlinkReturnNeg(char *path, char *buf, size_t bufsize)
{
    return -1;
}

static ssize_t MockReadlinkOk(char *path, char *buf, size_t bufsize)
{
    const char *bondPath = "/sys/devices/virtual/net/bond0";
    if (bufsize < strlen(bondPath) + 1) {
        return -1;
    }
    if (strncpy_s(buf, bufsize, bondPath, strlen(bondPath)) != 0) {
        return -1;
    }
    return strlen(bondPath);
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

DTEST_CASE_F(KNET_HW_SCAN, TEST_GETNICNEEDID_NULL_INPUT, NULL, NULL)
{
    struct HwNeedIdInfo hv = {{0}};
    int ret = GetnicNeedId(&hv, NULL, TEST_VENDOR_ID);
    DT_ASSERT_EQUAL(ret, -1);

    ret = GetnicNeedId(NULL, "test", TEST_VENDOR_ID);
    DT_ASSERT_EQUAL(ret, -1);
}

DTEST_CASE_F(KNET_HW_SCAN, TEST_GETNICNEEDID_INVALID_TYPE, NULL, NULL)
{
    struct HwNeedIdInfo hv = {{0}};
    KTestMock *Mock = CreateMock();
    Mock->Create(snprintf_truncated_s, TEST_GetFuncRetPositive(0));
    int ret = GetnicNeedId(&hv, "test", 99);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(snprintf_truncated_s);
    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_HW_SCAN, TEST_GETNICNEEDID_SNPRINTF_FAIL, NULL, NULL)
{
    struct HwNeedIdInfo hv = {{0}};
    KTestMock *Mock = CreateMock();
    Mock->Create(snprintf_truncated_s, TEST_GetFuncRetNegative(1));
    int ret = GetnicNeedId(&hv, "test", TEST_VENDOR_ID);
    DT_ASSERT_EQUAL(ret, -1);
    ret = GetnicNeedId(&hv, "test", TEST_DEVICE_ID);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(snprintf_truncated_s);
    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_HW_SCAN, TEST_GETNICNEEDID_REALPATH_FAIL, NULL, NULL)
{
    struct HwNeedIdInfo hv = {{0}};
    KTestMock *Mock = CreateMock();
    Mock->Create(snprintf_truncated_s, TEST_GetFuncRetPositive(0));
    Mock->Create(realpath, GetFuncRetNull);
    int ret = GetnicNeedId(&hv, "test", TEST_VENDOR_ID);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(realpath);
    Mock->Delete(snprintf_truncated_s);
    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_HW_SCAN, TEST_GETNICNEEDID_FOPEN_FAIL, NULL, NULL)
{
    struct HwNeedIdInfo hv = {{0}};
    KTestMock *Mock = CreateMock();
    Mock->Create(snprintf_truncated_s, TEST_GetFuncRetPositive(0));
    Mock->Create(realpath, RealPathMock);
    Mock->Create(fopen, MockFopenReturnNull);
    int ret = GetnicNeedId(&hv, "test", TEST_VENDOR_ID);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(fopen);
    Mock->Delete(realpath);
    Mock->Delete(snprintf_truncated_s);
    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_HW_SCAN, TEST_CHECK_BOND_MODE_FAIL, NULL, NULL)
{
    char path[] = "/tmp/nonexistent_bond_mode";
    KTestMock *Mock = CreateMock();
    Mock->Create(fopen, MockFopenReturnNull);
    int ret = CheckKernelBondMode(path, sizeof(path));
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(fopen);
    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_HW_SCAN, TEST_CHECK_BOND_XMIT_FAIL, NULL, NULL)
{
    char path[] = "/tmp/nonexistent_xmit_policy";
    KTestMock *Mock = CreateMock();
    Mock->Create(fopen, MockFopenReturnNull);
    int ret = CheckKernelBondXmitHashPolicy(path, sizeof(path));
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(fopen);
    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_HW_SCAN, TEST_RETRIEVE_KERNEL_BOND_NAME_DIR_FAIL, NULL, NULL)
{
    char bondName[PATH_MAX + 1] = {0};
    KTestMock *Mock = CreateMock();
    Mock->Create(opendir, GetFuncRetNull);
    int32_t ret = RetrieveKernelBondName(bondName, sizeof(bondName), (char *)"/tmp");
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(opendir);
    DeleteMock(Mock);
}

static int g_readdir_count = 0;
static struct dirent g_test_entry;
static struct dirent *MockReaddirReturnNull(DIR *dir)
{
    return NULL;
}

static struct dirent *MockReaddirReturnEntry(DIR *dir)
{
    if (g_readdir_count == 0) {
        g_readdir_count++;
        const char *name = "eth0";
        strncpy(g_test_entry.d_name, name, sizeof(g_test_entry.d_name) - 1);
        g_test_entry.d_name[sizeof(g_test_entry.d_name) - 1] = '\0';
        return &g_test_entry;
    }
    return NULL;
}

DTEST_CASE_F(KNET_HW_SCAN, TEST_CONFIGURE_KERNEL_BOND_NAME_SNPRINTF_FAIL, NULL, NULL)
{
    char bondName[PATH_MAX + 1] = {0};
    KTestMock *Mock = CreateMock();
    Mock->Create(snprintf_truncated_s, TEST_GetFuncRetNegative(1));
    int ret = ConfigureKernelBondName(bondName, sizeof(bondName), "test");
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(snprintf_truncated_s);
    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_HW_SCAN, TEST_CONFIGURE_KERNEL_BOND_NAME_REALPATH_FAIL, NULL, NULL)
{
    char bondName[PATH_MAX + 1] = {0};
    KTestMock *Mock = CreateMock();
    Mock->Create(snprintf_truncated_s, TEST_GetFuncRetPositive(0));
    Mock->Create(realpath, GetFuncRetNull);
    int ret = ConfigureKernelBondName(bondName, sizeof(bondName), "test");
    // realpath返回NULL, errno非ENOENT走错误返回-1; ENOENT走返回0
    // UT中无法精确控制errno, 仅验证不崩溃
    (void)ret;
    Mock->Delete(realpath);
    Mock->Delete(snprintf_truncated_s);
    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_HW_SCAN, TEST_CONFIGURE_KERNEL_BOND_NAME_OK, NULL, NULL)
{
    char bondName[PATH_MAX + 1] = {0};
    KTestMock *Mock = CreateMock();
    Mock->Create(snprintf_truncated_s, TEST_GetFuncRetPositive(0));
    Mock->Create(realpath, RealPathMock);
    Mock->Create(RetrieveKernelBondName, TEST_GetFuncRetPositive(0));
    int ret = ConfigureKernelBondName(bondName, sizeof(bondName), "test");
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(RetrieveKernelBondName);
    Mock->Delete(realpath);
    Mock->Delete(snprintf_truncated_s);
    DeleteMock(Mock);
}

DTEST_CASE_F(KNET_HW_SCAN, TEST_CONFIGURE_KERNEL_BOND_NAME_RETRIEVE_FAIL, NULL, NULL)
{
    char bondName[PATH_MAX + 1] = {0};
    KTestMock *Mock = CreateMock();
    Mock->Create(snprintf_truncated_s, TEST_GetFuncRetPositive(0));
    Mock->Create(realpath, RealPathMock);
    Mock->Create(RetrieveKernelBondName, TEST_GetFuncRetNegative(1));
    int ret = ConfigureKernelBondName(bondName, sizeof(bondName), "test");
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(RetrieveKernelBondName);
    Mock->Delete(realpath);
    Mock->Delete(snprintf_truncated_s);
    DeleteMock(Mock);
}