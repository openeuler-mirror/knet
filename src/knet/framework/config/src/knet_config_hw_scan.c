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
#include <dirent.h>
#include <libgen.h>

#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/if.h>

#include "knet_log.h"
#include "knet_config.h"
#include "knet_config_hw_scan.h"

#define SYSFS_PCI_DEVICES "/sys/bus/pci/devices"
#define SYSFS_DEVICES_VIRTUAL_NET "/sys/devices/virtual/net"
#define BOND_CONFIG_LEN 32

static char *g_nicList[] = {
    "0x19e5",       // Huawei
};

static char *g_hisdkDeviceIdList[] = {
    "0x0222", // SP670 SP580设备device id
    "0x375f", // SP670 SP580 VF设备device id
};

static char *g_hnsDeviceIdList[] = {
    "0xa222", // TM280偶数bdf
    "0xa221", // TM280奇数bdf
    "0xa22f", // TM280 VF device id
};

/**
 * @brief Nic vendor与device id最大长度
 */
#define HW_MAX_NIC_VENDOR_DEVICE_LEN 7
/**
 * @brief 硬件vendor或者devide id信息
 */
typedef struct {
    char nicNeedId[HW_MAX_NIC_VENDOR_DEVICE_LEN];
    char padding[1];   // 填充字节，确保结构体8 字节对齐
} HwNeedIdInfo;

#define VENDOR_ID 1
#define DEVICE_ID 2

#ifdef KNET_TEST
static bool InList(const char *element, char *list[], int len)
{
    return true;
}
#else
static bool InList(const char *element, char *list[], int len)
{
    for (int i = 0; i < len; i++) {
        if (strcmp(list[i], element) == 0) {
            return true;
        }
    }

    return false;
}
#endif

int GetnicNeedId(HwNeedIdInfo *hv, const char *interfaceName, int type)
{
    if (hv == NULL || interfaceName == NULL) {
        return -1;
    }
    char path[PATH_MAX + 1] = {0};
    int ret = -1;
    if (type == VENDOR_ID) {
        ret = snprintf_truncated_s(path, sizeof(path), SYSFS_PCI_DEVICES "/%s/vendor", interfaceName);
        if (ret < 0) {
            KNET_ERR("Path snprintf truncated vendor path failed, interfaceName %s, ret %d", interfaceName, ret);
            return -1;
        }
    } else if (type == DEVICE_ID) {
        ret = snprintf_truncated_s(path, sizeof(path), SYSFS_PCI_DEVICES "/%s/device", interfaceName);
        if (ret < 0) {
            KNET_ERR("Path snprintf truncated device path failed, interfaceName %s, ret %d", interfaceName, ret);
            return -1;
        }
    } else {
        KNET_ERR("Invalid type %d", type);
        return -1;
    }

    // 校验最终路径
    char absPath[PATH_MAX + 1] = {0};
    char *retPath = realpath(path, absPath);
    if (retPath == NULL) {
        KNET_ERR("Real path failed, errno: %d", errno);
        return -1;
    }
    FILE *file = fopen(absPath, "r");
    if (file == NULL) {
        KNET_ERR("Pci devices file open failed, errno:%d", errno);
        return -1;
    }

    if (fgets(hv->nicNeedId, sizeof(hv->nicNeedId), file) != NULL) {
        (void)fclose(file);
        return 0;
    }

    KNET_ERR("Get vendor or device id failed");
    (void)fclose(file);
    return -1;
}

int KnetCheckCompatibleNic(void)
{
    // 获取网卡vendor信息
    HwNeedIdInfo hwInfo = {{0}};
    int ret = 0;
    // 非bond模式下，只检查一个网卡
    if (KNET_GetCfg(CONF_INTERFACE_BOND_ENABLE)->intValue == 0) {
        ret = GetnicNeedId(&hwInfo, KNET_GetCfg(CONF_INTERFACE_BDF_NUMS)->strValueArr[0], VENDOR_ID);
    } else {
        if (GetnicNeedId(&hwInfo, KNET_GetCfg(CONF_INTERFACE_BDF_NUMS)->strValueArr[0], VENDOR_ID) != 0 ||
            GetnicNeedId(&hwInfo, KNET_GetCfg(CONF_INTERFACE_BDF_NUMS)->strValueArr[1], VENDOR_ID) != 0) {
            ret = -1;
        }
    }
    if (ret != 0) {
        KNET_ERR("Get nic device id failed");
        return -1;
    }
    // 检查网卡是否在兼容列表里
    if (!InList(hwInfo.nicNeedId, g_nicList, sizeof(g_nicList) / sizeof(char *))) {
        KNET_ERR("Detected incompatible nic");
        return -1;
    }

    return 0;
}

int KnetIsEnableNicFlowFun(void)
{
    // 获取网卡device信息
    HwNeedIdInfo hwInfo = {{0}};
    int ret = 0;
    // 非bond模式下，只检查一个网卡
    if (KNET_GetCfg(CONF_INTERFACE_BOND_ENABLE)->intValue == 0) {
        ret = GetnicNeedId(&hwInfo, KNET_GetCfg(CONF_INTERFACE_BDF_NUMS)->strValueArr[0], DEVICE_ID);
    } else {
        if (GetnicNeedId(&hwInfo, KNET_GetCfg(CONF_INTERFACE_BDF_NUMS)->strValueArr[0], DEVICE_ID) != 0 ||
            GetnicNeedId(&hwInfo, KNET_GetCfg(CONF_INTERFACE_BDF_NUMS)->strValueArr[1], DEVICE_ID) != 0) {
            ret = -1;
        }
    }
    if (ret != 0) {
        KNET_ERR("Nic get device id failed");
        return -1;
    }

    if (InList(hwInfo.nicNeedId, g_hisdkDeviceIdList, sizeof(g_hisdkDeviceIdList) / sizeof(char *))) {
        return KNET_HW_TYPE_SP670; // SP设备支持rss流表与流分叉
    } else if (InList(hwInfo.nicNeedId, g_hnsDeviceIdList, sizeof(g_hnsDeviceIdList) / sizeof(char *))) {
        return KNET_HW_TYPE_TM280; // 板载网卡不支持rss流表与流分叉
    }
    KNET_ERR("Unknown nic device id %s", hwInfo.nicNeedId);
    return -1;
}

KNET_STATIC int32_t RetrieveKernelBondName(char *bondName, size_t bondNameSize, char *absPath)
{
    DIR *dir = opendir(absPath);
    if (dir == NULL) {
        KNET_ERR("Failed to open dir %s", absPath); // absPath是绝对路径，无注入问题
        return -1;
    }

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        // 构造 master 符号链接路径
        char masterLink[PATH_MAX + 1] = {0};
        int ret = snprintf_truncated_s(masterLink, PATH_MAX + 1, "%s/%s/master", absPath, entry->d_name);
        if (ret < 0) {
            KNET_ERR("Failed to construct bond master link path, ret %d", ret);
            goto END;
        }
        // 读取 master 链接, readlink对指定路径不存在不做处理，对其他问题返回-1
        char realpathBuf[PATH_MAX + 1] = {0};
        ssize_t len = readlink(masterLink, realpathBuf, sizeof(realpathBuf) - 1);
        if (len == -1 && errno != ENOENT) { // 除了master路径不存在的场景，其他场景都打印错误日志
            KNET_ERR("Readlink failed, ret %d, errno %d", ret, errno);
            goto END;
        }
        if (len != -1 && len < PATH_MAX + 1) { // master存在，给bondName赋值，然后返回;master不存在说明没配bond，knet不感知
            realpathBuf[len] = '\0'; // readlink 不自动加 '\0'
            ret = strncpy_s(bondName, bondNameSize, basename(realpathBuf), strlen(basename(realpathBuf)));
            if (ret != 0) {
                KNET_ERR("Strncpy bond name failed, ret %d", ret);
                goto END;
            }
        }
        closedir(dir);
        return 0;
    }
END:
    closedir(dir);
    return -1;
}

KNET_STATIC int ConfigureKernelBondName(char *bondName, size_t bondNameSize, const char *interfaceName)
{
    char absPath[PATH_MAX + 1] = {0};
    char path[PATH_MAX + 1] = {0};
 
    int32_t ret = snprintf_truncated_s(path, sizeof(path), SYSFS_PCI_DEVICES "/%s/net", interfaceName);
    if (ret < 0) {
        KNET_ERR("Path snprintf_truncated_s failed, interfaceName %s, ret %d", interfaceName, ret);
        return -1;
    }
    char *retPath = realpath(path, absPath);
    if (retPath == NULL && errno != ENOENT) { // 如果devbind, 且bifur_enable为1, 此处会报错。这种场景忽略，拦截在后续处理
        KNET_ERR("Dpdk devbind %s, errno %d", interfaceName, errno);
        return -1;
    } else if (retPath == NULL && errno == ENOENT) {
        KNET_WARN("Dpdk devbind %s, but bifur_enable is set %d", bondName, BIFUR_ENABLE);
        return 0;
    }

    ret = RetrieveKernelBondName(bondName, bondNameSize, absPath);
    if (ret != 0) {
        KNET_ERR("Get kernel bond name failed, %d", ret);
        return -1;
    }

    return 0;
}

KNET_STATIC int CheckKernelBondMode(char *absPath, ssize_t pathLen)
{
    char mode[BOND_CONFIG_LEN] = {0};
    FILE *fp = NULL;

    fp = fopen(absPath, "r");
    if (fp == NULL) {
        KNET_ERR("Failed to open kernel bond mode, errno %d, path %s", errno, absPath);
        return -1;
    }

    if (fgets(mode, sizeof(mode), fp) == NULL) { // fgets确保mode以\0结尾
        KNET_ERR("Failed to read bonding mode, error %d , path %s", errno, absPath);
        (void)fclose(fp);
        return -1;
    }
    (void)fclose(fp);

     // 验证配置是否符合要求
    if (strstr(mode, "802.3ad") == NULL) {  // 802.3ad动态链路聚合
        KNET_ERR("Invalid bonding mode, expected 802.3ad, check file %s", absPath);
        return -1;
    }

    return 0;
}

KNET_STATIC int CheckKernelBondXmitHashPolicy(char *absPath, ssize_t pathLen)
{
    char policy[BOND_CONFIG_LEN] = {0};
    FILE *fp = NULL;

    fp = fopen(absPath, "r");
    if (fp == NULL) {
        KNET_ERR("Failed to open kernel bond xmit_hash_policy, errno %d, path %s", errno, absPath);
        return -1;
    }
    if (fgets(policy, sizeof(policy), fp) == NULL) { // fgets确保policy以\0结尾
        KNET_ERR("Failed to read bonding xmit_hash_policy, error %d , path %s", errno, absPath);
        (void)fclose(fp);
        return -1;
    }
    (void)fclose(fp);

    if (strstr(policy, "layer3+4") == NULL) {
        KNET_ERR("Invalid xmit_hash_policy, expected layer3+4, check file %s", absPath);
        return -1;
    }

    return 0;
}

KNET_STATIC int CheckKernelBondConfig(char *bondName, size_t bondNameSize)
{
    char absPath[PATH_MAX + 1] = {0};
    char path[PATH_MAX + 1] = {0};

    // 拼接mode路径
    int ret = snprintf_truncated_s(path, sizeof(path), SYSFS_DEVICES_VIRTUAL_NET "/%s/bonding/mode", bondName);
    if (ret < 0) {
        KNET_ERR("Kernel bond mode path snprintf_truncated_s failed, ret %d", ret);
        return -1;
    }
    char *modePath = realpath(path, absPath);
    if (modePath == NULL) {
        KNET_ERR("Get bond mode real path failed, errno: %d", errno);
        return -1;
    }

    // 读取mode配置
    ret = CheckKernelBondMode(absPath, PATH_MAX + 1);
    if (ret < 0) {
        KNET_ERR("Failed to check kernel bond mode, ret %d", ret);
        return -1;
    }

    // 拼接xmit_hash_policy路径
    ret = snprintf_truncated_s(path, sizeof(path), SYSFS_DEVICES_VIRTUAL_NET "/%s/bonding/xmit_hash_policy", bondName);
    if (ret < 0) {
        KNET_ERR("Kernel bond xmit_hash_policy path snprintf_truncated_s failed, ret %d", ret);
        return -1;
    }
    char *xmitHashPolicyPath = realpath(path, absPath);
    if (xmitHashPolicyPath == NULL) {
        KNET_ERR("Get bond xmit_hash_policy real path failed, errno: %d", errno);
        return -1;
    }
    // 读取xmit_hash_policy配置
    ret = CheckKernelBondXmitHashPolicy(absPath, PATH_MAX + 1);
    if (ret < 0) {
        KNET_ERR("Failed to check kernel bond xmit_hash_policy, ret %d", ret);
        return -1;
    }
    return 0;
}

int KnetKernelBondCfgScan(char *bondName, size_t bondLen)
{
    const char *interfaceName = KNET_GetCfg(CONF_INTERFACE_BDF_NUMS)->strValueArr[0];
    // 根据bdf查是否组了bond, 如果调用有失败，则返回-1
    int ret = ConfigureKernelBondName(bondName, bondLen, interfaceName);
    if (ret != 0) {
        KNET_ERR("Configure bond name in kernel failed, ret %d", ret);
        return -1;
    }
    if (bondName[0] == '\0') {
        return 0;
    }

    // 检测bondName配置，K-NET对外的配置中 mode仅支持4 xmit_hash_policy 仅支持layer3+4
    ret = CheckKernelBondConfig(bondName, bondLen);
    if (ret != 0) {
        KNET_ERR("Check kernel bond configure in kernel failed, ret %d", ret);
        return -1;
    }
    return 0;
}