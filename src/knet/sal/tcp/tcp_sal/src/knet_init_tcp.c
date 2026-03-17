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
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <dirent.h>
#include <limits.h> /* PATH_MAX */
#include "knet_sal_tcp.h"
#include "knet_tcp_symbols.h"
#include "knet_capability.h"
#include "knet_config.h"
#include "knet_dpdk_init.h"
#include "knet_tun.h"
#include "knet_utils.h"
#include "knet_init.h"
#include "knet_sal_inner.h"
#include "tcp_socket.h"
#include "knet_transmission.h"

#include "tcp_os.h"
#include "tcp_event.h"
#include "knet_tun.h"
#include "knet_init_tcp.h"

/**
 * @brief 最大rt长度
 */
#define MAX_RT_ATTR_LEN 32

static int g_tapIfIndex = 0; // 网口的ifindex
static uint8_t g_KnetRtAttrsBuf[DP_RTA_MAX][MAX_RT_ATTR_LEN] = {0};
int32_t g_tapFd = INVALID_FD; // TAP口文件描述符

#define MAX_WORKER_ID 512
typedef struct {
    KNET_DpWorkerInfo workerInfo[MAX_WORKER_ID];
    uint32_t coreIdToWorkerId[MAX_WORKER_ID];
    uint32_t maxWorkerId;
    char padding[4];   // 填充字节，确保结构体8 字节对齐
} DpWorkerIdTable; // data plane worker id table
KNET_STATIC DpWorkerIdTable g_dpWorkerIdTable = {0};
#define INVALID_WORKER_ID UINT32_MAX

#define MAX_TSO_SEG 65535
#define DP_CACHE_DEEP 4096 // histak约束创建netdev时的cache size最大值为4096
#define SYSFS_PCI_DEVICES "/sys/bus/pci/devices"
#define MAC_ADDR_LEN 6

static const int g_nicTsoMaxSegNumTable[KNET_HW_TYPE_MAX] = {
    [KNET_HW_TYPE_TM280] = 63,
    /*
    * 驱动约束为127，KNET目前约束为63
    * 原因1：批量tx小包走零拷贝时，一个包能打满127segs，对于批量tx容易导致驱动wq不够；
    * 原因2：127segs太大了,驱动默认tx_free_thresh为32，如果knet侧仅进行read回复ack，驱动中都是2wq的小包，
    * 释放32*2 = 64个wq，如果下一次发127大包，就会因为wq不够导致丢包。
    */
    [KNET_HW_TYPE_SP670] = 63,
};

// 同步驱动对非TSO下最大segs的约束
static const int g_nicNonTsoMaxSegNumTable[KNET_HW_TYPE_MAX] = {
    [KNET_HW_TYPE_TM280] = 8,
    // 驱动约束为38,KNET目前约束32
    // 原因驱动默认tx_free_thresh为32，如果knet侧仅进行read回复ack，驱动不会多占wq，释放的就是32*1 = 32个wq
    [KNET_HW_TYPE_SP670] = 32,
};

static KNET_SpinLock g_lcoreLock = {
    .value = KNET_SPIN_UNLOCKED_VALUE,
};

int32_t KNET_FreeTapGlobal(void)
{
    return KNET_TapFree(g_tapFd);
}

KNET_STATIC int KnetGetIfName(char *ifName, int nameLen)
{
    int ret;
    char absPath[PATH_MAX + 1] = {0};
    char path[PATH_MAX + 1] = {0};
    char *retPath = NULL;
    const char *interfaceName = KNET_GetCfg(CONF_INTERFACE_BDF_NUMS)->strValueArr[0];
 
    ret = snprintf_truncated_s(path, sizeof(path), SYSFS_PCI_DEVICES "/%s/net", interfaceName);
    if (ret < 0) {
        KNET_ERR("path snprintf_truncated_s failed, interfaceName %s, ret %d", interfaceName, ret);
        return -1;
    }
 
    // 校验最终路径
    retPath = realpath(path, absPath);
    if (retPath == NULL) {
        KNET_ERR("real path failed, errno: %d", errno);
        return -1;
    }
    DIR *dir = NULL;
    struct dirent *entry = NULL;
 
    dir = opendir(absPath);
    if (dir == NULL) {
        KNET_ERR("open dir failed, absPath %s", absPath);
        return -1;
    }
 
    bool readSuccess = false;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            ret = strcpy_s(ifName, nameLen, entry->d_name);
            if (ret != 0) {
                KNET_ERR("string copy failed, ret %d", ret);
                goto readdir_err;
            }
            readSuccess = true;
        }
    }
    if (!readSuccess) {
        KNET_ERR("Read dir failed, dir does not have interfaceName %s", interfaceName);
        goto readdir_err;
    }
    closedir(dir);
    return 0;
 
readdir_err:
    closedir(dir);
    return -1;
}

KNET_STATIC int KnetSetInterFace(void)
{
    int ret = 0;
    char ifname[IF_NAME_SIZE] = {0};
    if (KNET_GetCfg(CONF_HW_BIFUR_ENABLE)->intValue != BIFUR_ENABLE) {
        ret = KNET_TAPCreate(&g_tapFd, &g_tapIfIndex);
        if (ret != 0) {
            KNET_ERR("K-NET tap create failed, ret %d", ret);
            return -1;
        }
        return 0;
    }

    if (KNET_GetCfg(CONF_INNER_KERNEL_BOND_NAME)->strValue[0] == '\0') {
        ret = KnetGetIfName(ifname, IF_NAME_SIZE); // 获取网口名写入ifname，若网口接管则获取失败
        if (ret == -1) {
            KNET_ERR("K-NET get if name failed, please check bifur_enable cfg and bdf bind, ret %d", ret);
            return -1;
        }
    } else {
        ret = strcpy_s(ifname, IF_NAME_SIZE, KNET_GetCfg(CONF_INNER_KERNEL_BOND_NAME)->strValue);
        if (ret != 0) {
            KNET_ERR("Strcpy failed, ret %d, errno %d", ret, errno);
            return -1;
        }
    }
 
    ret = KNET_FetchIfIndex(ifname, IF_NAME_SIZE, &g_tapIfIndex);
    if (ret == -1) {
        KNET_ERR("K-NET get if index failed, ret %d", ret);
        return -1;
    }
    return 0;
}

static int KnetConfigureIfDev(DP_Netdev_t *netdev, struct DP_Ifreq *ifReq)
{
     /* 设置本端mac地址 */
    (void)memset_s(&ifReq->ifr_ifru, sizeof(ifReq->ifr_ifru), 0, sizeof(ifReq->ifr_ifru));
    ifReq->ifr_hwaddr.sa_family = ARPHRD_ETHER;
    int ret = memcpy_s((void *)ifReq->ifr_hwaddr.sa_data, sizeof(ifReq->ifr_hwaddr.sa_data),
        KNET_GetCfg(CONF_INTERFACE_MAC)->strValue, MAC_ADDR_LEN);
    if (ret != 0) {
        KNET_ERR("Memcpy failed, ret %d", ret);
        return -1;
    }

    ret = DP_ProcIfreq(netdev, DP_SIOCSIFHWADDR, ifReq);
    if (ret != 0) {
        KNET_ERR("DP ProcIfreq set mac failed, ret %d", ret);
        return -1;
    }

    /* 设置本端ip地址 */
    (void)memset_s(&ifReq->ifr_ifru, sizeof(ifReq->ifr_ifru), 0, sizeof(ifReq->ifr_ifru));
    struct DP_SockaddrIn *addr = (struct DP_SockaddrIn *)(void *)&ifReq->ifr_addr;
    addr->sin_family = AF_INET;
    addr->sin_port = 0;
    addr->sin_addr.s_addr = (uint32_t)KNET_GetCfg(CONF_INTERFACE_IP)->intValue;
    ret = DP_ProcIfreq(netdev, DP_SIOCSIFADDR, ifReq);
    if (ret != 0) {
        KNET_ERR("DP ProcIfreq set ip failed, ret %d", ret);
        return -1;
    }

    /* 设置mtu */
    (void)memset_s(&ifReq->ifr_ifru, sizeof(ifReq->ifr_ifru), 0, sizeof(ifReq->ifr_ifru));
    ifReq->ifr_mtu = KNET_GetCfg(CONF_INTERFACE_MTU)->intValue;
    ret = DP_ProcIfreq(netdev, DP_SIOCSIFMTU, ifReq);
    if (ret != 0) {
        KNET_ERR("DP ProcIfreq set mtu failed, ret %d", ret);
        return -1;
    }

    /* 设置broadcast */
    (void)memset_s(&ifReq->ifr_ifru, sizeof(ifReq->ifr_ifru), 0, sizeof(ifReq->ifr_ifru));
    struct DP_SockaddrIn *broad = (struct DP_SockaddrIn *)(void *)&ifReq->ifr_broadaddr;
    broad->sin_family = AF_INET;
    broad->sin_port = 0;
    uint32_t netmask = (uint32_t)KNET_GetCfg(CONF_INTERFACE_NETMASK)->intValue;
    uint32_t interfaceIp = (uint32_t)KNET_GetCfg(CONF_INTERFACE_IP)->intValue;
    uint32_t networkAddr = interfaceIp & netmask;
    uint32_t broadcastAddr = networkAddr | (~netmask);
    broad->sin_addr.s_addr = broadcastAddr;
    ret = DP_ProcIfreq(netdev, DP_SIOCSIFBRDADDR, ifReq);
    if (ret != 0) {
        KNET_ERR("DP ProcIfreq set broadcast failed, ret %d", ret);
        return -1;
    }
    
    return 0;
}

/**
 * @brief 配置协议栈网络设备
 */
KNET_STATIC int KnetConfigureStackNetdev(DP_Netdev_t *netdev, const char *ifname)
{
    /* 使能网卡队列 */
    struct DP_Ifreq ifReq = {0};
    ifReq.ifr_flags = DP_IFF_UP;
    int ret = strcpy_s(ifReq.ifr_name, DP_IF_NAME_SIZE, ifname);
    if (ret != 0) {
        KNET_ERR("Strcpy ifname failed, ret %d", ret);
        return -1;
    }
    ret = DP_ProcIfreq(netdev, DP_SIOCSIFFLAGS, &ifReq);
    if (ret != 0) {
        KNET_ERR("DP_ProcIfreq DP_SIOCSIFFLAGS failed, ret %d", ret);
        return -1;
    }

    ret = KnetConfigureIfDev(netdev, &ifReq);
    if (ret != 0) {
        KNET_ERR("Configure dev ip/mac/mtu/broadcast failed, ret %d", ret);
        return -1;
    }

    return 0;
}

KNET_STATIC int KnetInitDpNetdev(void)
{
    /* 初始化DP网络设备配置 */
    DP_NetdevCfg_t netdevCfg = {0};

    netdevCfg.ifindex = g_tapIfIndex;
    int ret = memcpy_s(netdevCfg.ifname, sizeof(netdevCfg.ifname), KNET_GetCfg(CONF_INTERFACE_BDF_NUMS)->strValueArr[0],
        strlen(KNET_GetCfg(CONF_INTERFACE_BDF_NUMS)->strValueArr[0]) + 1);
    if (ret != 0) {
        KNET_ERR("Memcpy failed, ret %d, errno %d", ret, errno);
        return -1;
    }
    netdevCfg.devType = DP_NETDEV_TYPE_ETH;
    // 目前不支持多进程单核多队列，调用CONF_DPDK_QUEUE_NUM处统一修改为内置值
    netdevCfg.txQueCnt = KNET_GetCfg(CONF_INNER_QUEUE_NUM)->intValue;
    netdevCfg.rxQueCnt = KNET_GetCfg(CONF_INNER_QUEUE_NUM)->intValue;
    netdevCfg.txCachedDeep = DP_CACHE_DEEP;
    netdevCfg.rxCachedDeep = DP_CACHE_DEEP;
    /* 其中的 ctrl/txHash 为空指针 */
    DP_NetdevOps_t ops = {
        .rxHash = KNET_ACC_TxHash, // 实际为hash计算主动建链发送时的queueid
        .rxBurst = KNET_ACC_RxBurst,
        .txBurst = KNET_ACC_TxBurst,
    };
    netdevCfg.ctx = KNET_GetNetDevCtx();
    netdevCfg.ops = &ops;

    /* 设置设备的 offload 能力 */
    if (KNET_GetCfg(CONF_HW_TCP_CHECKSUM)->intValue > 0) {
        netdevCfg.offloads = DP_NETDEV_OFFLOAD_RX_IPV4_CKSUM | DP_NETDEV_OFFLOAD_RX_TCP_CKSUM |
                             DP_NETDEV_OFFLOAD_TX_IPV4_CKSUM | DP_NETDEV_OFFLOAD_TX_TCP_CKSUM |
                             DP_NETDEV_OFFLOAD_TX_L4_CKSUM_PARTIAL;
    }

    netdevCfg.tsoSize = 0;
    if (KNET_GetCfg(CONF_HW_TSO)->intValue > 0) {
        netdevCfg.tsoSize = MAX_TSO_SEG;
        netdevCfg.offloads |= DP_NETDEV_OFFLOAD_TSO;
    }
    if (KNET_GetCfg(CONF_HW_TSO)->intValue == 0) {
        netdevCfg.maxSegNum = g_nicNonTsoMaxSegNumTable[KNET_GetCfg(CONF_INNER_HW_TYPE)->intValue];
    } else {
        netdevCfg.maxSegNum = g_nicTsoMaxSegNumTable[KNET_GetCfg(CONF_INNER_HW_TYPE)->intValue];
    }

    DP_Netdev_t *netdev = DP_CreateNetdev(&netdevCfg);  // 与tcp交互网络设备信息
    if (netdev == NULL) {
        KNET_ERR("DP CreateNetdev failed");
        return -1;
    }

    /* 配置网络设备 */
    ret = KnetConfigureStackNetdev(netdev, netdevCfg.ifname);
    if (ret != 0) {
        KNET_ERR("Configure stack netdev failed, ret %d", ret);
        return -1;
    }

    return 0;
}

static void KnetFillRtAttrs(DP_TbmAttr_t *attrs[], uint32_t dst, uint32_t gw, int32_t oif)
{
    attrs[0] = (DP_TbmAttr_t *)g_KnetRtAttrsBuf[DP_RTA_OIF];
    attrs[0]->type = DP_RTA_OIF;
    attrs[0]->len = (uint16_t)(sizeof(DP_TbmAttr_t) + sizeof(int32_t));
    *((int32_t *)(attrs[0] + 1)) = oif;  // +1表示四字节，刚好是type和len的长度

    attrs[1] = (DP_TbmAttr_t *)g_KnetRtAttrsBuf[DP_RTA_DST];
    attrs[1]->type = DP_RTA_DST;
    attrs[1]->len = (uint16_t)(sizeof(DP_TbmAttr_t) + sizeof(int32_t));
    *((uint32_t *)(attrs[1] + 1)) = dst;  // 1表示第二个attr；+1表示四字节，刚好是type和len的长度

    attrs[2] = (DP_TbmAttr_t *)g_KnetRtAttrsBuf[DP_RTA_GATEWAY];         // 2表示第三个attr
    attrs[2]->type = DP_RTA_GATEWAY;                                     // 2表示第三个attr
    attrs[2]->len = (uint16_t)(sizeof(DP_TbmAttr_t) + sizeof(int32_t));  // 2表示第三个attr

    *((uint32_t *)(attrs[2] + 1)) = gw;  // 2表示第三个attr；+1表示四字节，刚好是type和len的长度
}

/**
 * @brief 根据netmask计算其前缀长度
 * @attention netmask必须是主机序
 */
static uint8_t KnetNetmaskPrefixLenCal(uint32_t netmask)
{
    uint8_t prefixLen = 0;
    uint32_t netmaskThis = netmask;
    while (netmaskThis & 0x80000000) {  // 0x80000000表示32位的最高位置1
        ++prefixLen;
        netmaskThis <<= 1;
    }

    return prefixLen;
}

KNET_STATIC int KnetSetDpRtCfg(void)
{
    DP_RtInfo_t rtInfo = {0};
    DP_TbmAttr_t *attrs[3] = {0};  // 3表示下发3个attr
    uint32_t netmask = (uint32_t)KNET_GetCfg(CONF_INTERFACE_NETMASK)->intValue;
    uint32_t dst = (uint32_t)(KNET_GetCfg(CONF_INTERFACE_IP)->intValue) & netmask; // dst表示一个网段
    uint32_t gw = (uint32_t)KNET_GetCfg(CONF_INTERFACE_GATEWAY)->intValue;
    int ifIndex = g_tapIfIndex; // 传递TAP口的ifindex
    KnetFillRtAttrs(attrs, dst, gw, ifIndex);

    uint8_t netmaskPrefixLen = KnetNetmaskPrefixLenCal(ntohl(netmask));
    rtInfo.dstLen = netmaskPrefixLen;  // 表示掩码前缀长度
    rtInfo.table = RT_TABLE_DEFAULT;
    rtInfo.protocol = RTPROT_STATIC;
    rtInfo.scope = RT_SCOPE_LINK;
    rtInfo.type = RTN_LOCAL;
    rtInfo.family = AF_INET;

    int ret = DP_RtCfg(DP_NEW_ROUTE, &rtInfo, attrs, 3);
    if (ret != 0) {
        KNET_ERR("Dp add route failed");
        return -1;
    }

    KNET_INFO("Dst 0x%08x, netmask 0x%08x, netmaskPrefixLen %u, gw 0x%08x, ifIndex %u",
        dst,
        netmask,
        netmaskPrefixLen,
        gw,
        ifIndex);

    return 0;
}

KNET_STATIC int32_t KnetDpdkSlaveLcoreNumCheck(void)
{
    int ctrlVcpuNum = KNET_GetCfg(CONF_COMMON_CTRL_VCPU_NUMS)->intValue;
    const int *ctrlVcpuArr = KNET_GetCfg(CONF_COMMON_CTRL_VCPU_IDS)->intValueArr;
    int32_t slaveLcoreNum = 0;
    for (int i = 0; i < ctrlVcpuNum; i++) {
        int ctrlVcpuId = ctrlVcpuArr[i];
        int maxVcpuNum = get_nprocs_conf();
        if (ctrlVcpuId >= maxVcpuNum) {
            KNET_ERR("Ctrl vcpu Id %d equal or exceeds max vcpu num %d", ctrlVcpuId, maxVcpuNum);
            return -1;
        }
        int ret = KNET_CpuDetected(ctrlVcpuId);
        if (ret < 0) {
            KNET_ERR("Ctrl vcpu Id %d is not available in the available CPUs ", ctrlVcpuId);
            return -1;
        }

        uint32_t lcoreId = 0;
        /* 遍历slave lcore并计算总个数 */
        RTE_LCORE_FOREACH_WORKER(lcoreId) {
            /* 考虑性能，协议栈数据面和控制面线程不允许在同一个核上 */
            if (lcoreId == (uint32_t)ctrlVcpuId) {
                KNET_ERR("Dpdk lcore %u and ctrl_vcpu_id must be on different cores", lcoreId);
                return -1;
            }
            ++slaveLcoreNum;
        }
    }

    slaveLcoreNum /= ctrlVcpuNum;
    int32_t workerNum = KNET_GetCfg(CONF_TCP_MAX_WORKER_NUM)->intValue;
    if (slaveLcoreNum != workerNum) {
        KNET_ERR("SlaveL core num %d not equal to max worker id %d", slaveLcoreNum, workerNum);
        return -1;
    }

    return 0;
}

/**
 * @brief 仅在共线程下设置lcoreId与wid的映射关系
 *
 * @param lcoreId 输入的是lcoreId，与wid一致
 * @return int 0：成功；-1：失败
 */
int KNET_DpdkLcoreMatchDpWorker(uint32_t lcoreId)
{
    KNET_SpinlockLock(&g_lcoreLock);
    if (g_dpWorkerIdTable.maxWorkerId >= MAX_WORKER_ID) {
        KNET_ERR("WorkerId %u equal to or exceeds MAX_WORKER_ID %u in cothread",
            g_dpWorkerIdTable.maxWorkerId, MAX_WORKER_ID);
        KNET_SpinlockUnlock(&g_lcoreLock);
        return -1;
    }
    g_dpWorkerIdTable.workerInfo[g_dpWorkerIdTable.maxWorkerId].workerId = lcoreId;
    g_dpWorkerIdTable.workerInfo[g_dpWorkerIdTable.maxWorkerId].lcoreId = lcoreId;
    g_dpWorkerIdTable.coreIdToWorkerId[lcoreId] = lcoreId;
    
    uint32_t queMap[MAX_QUEUE_NUM / UINT32_BIT_LEN] = {0};
    DP_GetNetdevQueMap(lcoreId, KNET_GetIfIndex(), queMap, sizeof(queMap) / sizeof(queMap[0]));
    for (int i = 0; i < KNET_GetCfg(CONF_DPDK_QUEUE_NUM)->intValue; i++) {
        if (((1U << (i % UINT32_BIT_LEN)) & queMap[i / UINT32_BIT_LEN]) != 0) {
            KNET_SetQueIdMapPidTidLcoreInfo(i, getpid(), syscall(SYS_gettid), lcoreId, lcoreId);
        }
    }

    KNET_INFO("DpWorkerId %u match lcoreId %u", lcoreId, lcoreId);

    ++g_dpWorkerIdTable.maxWorkerId;
    KNET_SpinlockUnlock(&g_lcoreLock);
    return 0;
}

/**
 * @brief 将协议栈dpWorkerId与dpdk lcoreId一对一映射
 * @attention 必须在DP_Init()之前完成，DP_Init中定时器会用到workerId，需要将workerId转换成lcoreId使能dpdk的定时器
 * @note 为方便LLT打桩，函数不声明为static
 */
int32_t KnetDpdkSlaveLcoreMatchDpWorker(void)
{
    // 开启共线程无需dp与dpdk映射
    if (KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue == 1) {
        return 0;
    }

    int32_t ret = KnetDpdkSlaveLcoreNumCheck();
    if (ret != 0) {
        KNET_ERR("Dpdk slave lcore num check failed, ret %d", ret);
        return -1;
    }

    for (uint32_t i = 0; i < MAX_WORKER_ID; ++i) {
        g_dpWorkerIdTable.coreIdToWorkerId[i] = INVALID_WORKER_ID;
    }

    g_dpWorkerIdTable.maxWorkerId = 0;
    /* 将协议栈dpWorkerId映射到dpdk lcoreId */
    uint32_t lcoreId = 0;
    RTE_LCORE_FOREACH_WORKER(lcoreId) {
        if (g_dpWorkerIdTable.maxWorkerId >= MAX_WORKER_ID) {
            KNET_ERR("WorkerId %u equal to or exceeds MAX_WORKER_ID %u", g_dpWorkerIdTable.maxWorkerId, MAX_WORKER_ID);
            return -1;
        }

        g_dpWorkerIdTable.workerInfo[g_dpWorkerIdTable.maxWorkerId].workerId = g_dpWorkerIdTable.maxWorkerId;
        g_dpWorkerIdTable.workerInfo[g_dpWorkerIdTable.maxWorkerId].lcoreId = lcoreId;
        g_dpWorkerIdTable.coreIdToWorkerId[lcoreId] = g_dpWorkerIdTable.maxWorkerId;
        KNET_INFO("DpWorkerId %u match lcoreId %u", g_dpWorkerIdTable.maxWorkerId, lcoreId);

        ++g_dpWorkerIdTable.maxWorkerId;
    }

    return 0;
}

int KNET_InitDp(void)
{
    int ret;

    ret = KnetSetInterFace();
    if (ret != 0) {
        KNET_ERR("K-NET set InterFace failed, ret %d", ret);
        return -1;
    }

    ret = KnetSetDpCfg();
    if (ret != 0) {
        KNET_ERR("K-NET set tcp cfg failed, ret %d", ret);
        return -1;
    }

    ret = KnetDpdkSlaveLcoreMatchDpWorker();
    if (ret != 0) {
        KNET_ERR("Dpdk slave lcore match dp worker failed, ret %d", ret);
        return -1;
    }

    /* 初始化DP协议栈 */
    ret = DP_Init(0);
    if (ret != 0) {
        KNET_ERR("DP init failed, ret %d", ret);
        return -1;
    }
    KNET_INFO("DP init success");

    /* 初始化协议栈网络设备 */
    ret = KnetInitDpNetdev();
    if (ret < 0) {
        KNET_ERR("K-NET init tcp netdev failed, ret %d", ret);
        return -1;
    }
    KNET_INFO("K-NET init tcp netdev success");

    ret = KnetSetDpRtCfg();
    if (ret < 0) {
        KNET_ERR("K-NET set dp route cfg failed, ret %d", ret);
        return -1;
    }
    KNET_INFO("K-NET set DpRtCfg success");

    /* 初始化协议栈控制面对接 */
    KNET_GetCap(KNET_CAP_NET_ADMIN);
    ret = (int)DP_CpdInit();
    KNET_ClearCap(KNET_CAP_NET_ADMIN);
    if (ret != 0) {
        KNET_ERR("DP CPD init failed, ret %d", ret);
        return -1;
    }
    KNET_INFO("DP CPD init success");

    return 0;
}

void KNET_UninitDp(void)
{
    if (g_tapFd != INVALID_FD && KNET_GetCfg(CONF_HW_BIFUR_ENABLE)->intValue != BIFUR_ENABLE) {
        KNET_TapFree(g_tapFd);
        g_tapFd = INVALID_FD;
    }
    KnetDeinitDpSymbols();
}

KNET_DpWorkerInfo *KNET_DpWorkerInfoGet(uint32_t dpWorkerId)
{
    return &g_dpWorkerIdTable.workerInfo[dpWorkerId];
}

uint32_t KNET_DpMaxWorkerIdGet(void)
{
    return g_dpWorkerIdTable.maxWorkerId;
}

int32_t KNET_ACC_WorkerGetSelfId(void)
{
    uint32_t lcoreId = rte_lcore_id();
    if (lcoreId == LCORE_ID_ANY) {
        /* 此时表示lcore在未注册的非EAL线程内，正常，不打印报错信息 */
        return -1;
    }
    if (lcoreId >= MAX_WORKER_ID) {
        KNET_ERR("Lcore id %u out of range", lcoreId);
        return -1;
    }
    if (g_dpWorkerIdTable.coreIdToWorkerId[lcoreId] == INVALID_WORKER_ID) {
        /* 目前打桩返回0，待tcp epoll_ctl修复后reuturn -1，打印添加回去。KNET_ERR("not find lcoreId %u", lcoreId); */
        return 0;
    }
    return g_dpWorkerIdTable.coreIdToWorkerId[lcoreId];
}

int KNET_DpPosixOpsApiInit(struct KNET_PosixApiOps *ops)
{
    if (ops == NULL) {
        return -1;
    }

    /* tcp os ops */
    ops->signal = KNET_DpSignal;
    ops->sigaction = KNET_DpSigaction;
    ops->fork = KNET_DpFork;

    /* tcp socket ops */
    ops->socket = KNET_DpSocket;
    ops->listen = KNET_DpListen;
    ops->connect = KNET_DpConnect;
    ops->bind = KNET_DpBind;
    ops->getpeername = KNET_DpGetpeername;
    ops->getsockname = KNET_DpGetsockname;
    ops->getsockopt = KNET_DpGetsockopt;
    ops->setsockopt = KNET_DpSetsockopt;
    ops->ioctl = KNET_DpIoctl;
    ops->fcntl = KNET_DpFcntl;
    ops->fcntl64 = KNET_DpFcntl64;
    ops->send = KNET_DpSend;
    ops->sendto = KNET_DpSendto;
    ops->write = KNET_DpWrite;
    ops->writev = KNET_DpWritev;
    ops->recv = KNET_DpRecv;
    ops->recvfrom = KNET_DpRecvfrom;
    ops->read = KNET_DpRead;
    ops->readv = KNET_DpReadv;
    ops->accept = KNET_DpAccept;
    ops->accept4 = KNET_DpAccept4;
    ops->close = KNET_DpClose;
    ops->shutdown = KNET_DpShutdown;
    ops->sendmsg = KNET_DpSendmsg;
    ops->recvmsg = KNET_DpRecvmsg;

    /* tcp event ops */
    ops->epoll_create = KNET_DpEpollCreate;
    ops->epoll_create1 = KNET_DpEpollCreate1;
    ops->epoll_ctl = KNET_DpEpollCtl;
    ops->epoll_wait = KNET_DpEpollWait;
    ops->epoll_pwait = KNET_DpEpollPwait;
    ops->poll = KNET_DpPoll;
    ops->ppoll = KNET_DpPPoll;
    ops->select = KNET_DpSelect;
    ops->pselect = KNET_DpPSelect;
    
    return 0;
}


int KNET_GetIfIndex(void)
{
    return g_tapIfIndex;
}