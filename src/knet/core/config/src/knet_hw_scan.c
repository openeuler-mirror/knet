/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <limits.h> /* PATH_MAX */
#include <stdio.h>
#include <regex.h>

#include "knet_log.h"
#include "knet_config.h"
#include "knet_hw_scan.h"

#define SYSFS_PCI_DEVICES "/sys/bus/pci/devices"

static char *g_nicList[] = {
    "0x19e5",
};

/**
 * @brief Nic vendor最大长度
 */
#define HW_MAX_NIC_VENDOR_LEN 7
/**
 * @brief 硬件vendor信息
 */
typedef struct {
    char nicVendorId[HW_MAX_NIC_VENDOR_LEN];
    char padding[1];   // 填充字节，确保结构体8 字节对齐
}HwVendorInfo;

#ifdef KNET_SDV_TEST
static bool InNicList(const char *element, char *nicList[], int len)
{
    return true;
}
#else
static bool InNicList(const char *element, char *nicList[], int len)
{
    for (int i = 0; i < len; i++) {
        if (strcmp(nicList[i], element) == 0) {
            return true;
        }
    }

    return false;
}
#endif

int GetNicVendorId(HwVendorInfo *hv)
{
    if (hv == NULL) {
        return -1;
    }

    char absPath[PATH_MAX + 1] = {0};
    char path[PATH_MAX + 1] = {0};
    char *retPath = NULL;
    const char *interfaceName = KNET_GetCfg(CONF_INTERFACE_BDF_NUM).strValue;

    int ret = snprintf_truncated_s(path, sizeof(path), SYSFS_PCI_DEVICES "/%s/vendor", interfaceName);
    if (ret < 0) {
        KNET_ERR("Path snprintf truncated failed, interfaceName %s, ret %d", interfaceName, ret);
        return -1;
    }

    // 校验最终路径
    retPath = realpath(path, absPath);
    if (retPath == NULL) {
        KNET_ERR("Real path failed, errno: %d", errno);
        return -1;
    }
    FILE *file = NULL;
    file = fopen(absPath, "r");
    if (file == NULL) {
        KNET_ERR("Pci devices file open failed, errno:%d", errno);
        return -1;
    }

    if (fgets(hv->nicVendorId, sizeof(hv->nicVendorId), file) != NULL) {
        (void)fclose(file);
        return 0;
    }

    (void)fclose(file);
    return -1;
}

int KNET_CheckCompatibleNic(void)
{
    int ret;
    // 获取网卡信息
    HwVendorInfo hwInfo = {{0}};

    ret = GetNicVendorId(&hwInfo);
    if (ret != 0) {
        KNET_ERR("Nic check failed");
        return -1;
    }
    // 检查网卡是否在兼容列表里
    if (!InNicList(hwInfo.nicVendorId, g_nicList, sizeof(g_nicList) / sizeof(char *))) {
        KNET_ERR("Detected incompatible nic");
        return -1;
    }

    return 0;
}