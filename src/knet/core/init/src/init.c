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
#include <pthread.h>
#include <sched.h>
#include <signal.h>

#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>

#include <rte_timer.h>
#include "rte_ethdev.h"
#include "rte_eal_memconfig.h"

#include "securec.h"
#include "dp_init_api.h"
#include "dp_netdev_api.h"
#include "dp_worker_api.h"
#include "dp_cfg_api.h"
#include "dp_cpd_api.h"
#include "dp_tbm_api.h"
#include "dp_debug_api.h"

#include "knet_symbols.h"
#include "knet_config.h"
#include "knet_thread.h"
#include "knet_pkt.h"
#include "knet_sal_dp.h"
#include "knet_dp_hijack.h"
#include "knet_io_init.h"
#include "knet_hash_table.h"
#include "knet_lock.h"
#include "knet_tun.h"
#include "knet_capability.h"
#include "knet_dpdk_telemetry.h"
#include "knet_statistics.h"
#include "knet_pdump.h"
#include "knet_mem.h"
#include "init_stack.h"
#include "init.h"

#define MAX_TSO_SEG 65535
#define DP_CACHE_DEEP 4096 // histak约束创建netdev时的cache size最大值为4096

static uint8_t g_KnetRtAttrsBuf[DP_RTA_MAX][MAX_RT_ATTR_LEN] = {0};
static bool g_threadStop = false;
static KnetThreadInfo g_cpThread = {.lock = {KNET_SPIN_UNLOCKED_VALUE}, .threadID = 0, .isCreated = false};
static KnetThreadInfo g_multiPdumpThread = {.lock = {KNET_SPIN_UNLOCKED_VALUE}, .threadID = 0, .isCreated = false};
static KnetThreadInfo g_signalBlockMonitorThread = {
    .lock = {KNET_SPIN_UNLOCKED_VALUE}, .threadID = 0, .isCreated = false
};
static bool g_cfgInit = false;
static uint32_t g_tapId = 0; // TAP口ID
static int32_t g_tapFd = INVALID_FD; // TAP口文件描述符
static int g_tapIfIndex = 0; // 网口的ifindex

void KNET_AllThreadLock(void)
{
    KNET_SpinlockLock(&g_cpThread.lock);
    KNET_SpinlockLock(&g_multiPdumpThread.lock);
    KNET_SpinlockLock(&g_signalBlockMonitorThread.lock);

    uint32_t maxWorkerId = KNET_DpMaxWorkerIdGet();
    for (uint32_t workerId = 0; workerId < maxWorkerId; ++workerId) {
        DpWorkerInfo *dpWorkerInfo = KNET_DpWorkerInfoGet(workerId);
        KNET_SpinlockLock(&dpWorkerInfo->lock);
    }
}

void KNET_AllThreadUnlock(void)
{
    uint32_t maxWorkerId = KNET_DpMaxWorkerIdGet();
    for (uint32_t workerId = 0; workerId < maxWorkerId; ++workerId) {
        DpWorkerInfo *dpWorkerInfo = KNET_DpWorkerInfoGet(workerId);
        KNET_SpinlockUnlock(&dpWorkerInfo->lock);
    }

    KNET_SpinlockUnlock(&g_signalBlockMonitorThread.lock);
    KNET_SpinlockUnlock(&g_multiPdumpThread.lock);
    KNET_SpinlockUnlock(&g_cpThread.lock);
}

/**
 * @brief 手动关闭线程
 */
void KNET_SetDpdkAndStackThreadStop(void)
{
    g_threadStop = true;
}

static int KnetInitDpNetdev(void)
{
    /* 初始化DP网络设备配置 */
    DP_NetdevCfg_t netdevCfg = {0};

    netdevCfg.ifindex = g_tapIfIndex;
    int ret = memcpy_s(netdevCfg.ifname, sizeof(netdevCfg.ifname), KNET_GetCfg(CONF_INTERFACE_BDF_NUM).strValue,
        strlen(KNET_GetCfg(CONF_INTERFACE_BDF_NUM).strValue));
    if (ret != 0) {
        KNET_ERR("Memcpy failed, ret %d, errno %d", ret, errno);
        return -1;
    }
    netdevCfg.devType = DP_NETDEV_TYPE_ETH;
    netdevCfg.txQueCnt = KNET_GetCfg(CONF_INNER_TX_QUEUE_NUM).intValue;
    netdevCfg.rxQueCnt = KNET_GetCfg(CONF_INNER_RX_QUEUE_NUM).intValue;
    netdevCfg.txCachedDeep = DP_CACHE_DEEP;
    netdevCfg.rxCachedDeep = DP_CACHE_DEEP;
    /* 其中的 ctrl/txHash/rxHash 为空指针 */
    DP_NetdevOps_t ops = {
        .rxBurst = KNET_ACC_RxBurst,
        .txBurst = KNET_ACC_TxBurst,
    };
    netdevCfg.ctx = KNET_GetNetDevCtx();
    netdevCfg.ops = &ops;

    /* 设置设备的 offload 能力 */
    if (KNET_GetCfg(CONF_HW_TCP_CHECKSUM).intValue > 0) {
        netdevCfg.offloads = DP_NETDEV_OFFLOAD_RX_IPV4_CKSUM | DP_NETDEV_OFFLOAD_RX_TCP_CKSUM |
                             DP_NETDEV_OFFLOAD_TX_IPV4_CKSUM | DP_NETDEV_OFFLOAD_TX_TCP_CKSUM |
                             DP_NETDEV_OFFLOAD_TX_L4_CKSUM_PARTIAL;
    }

    netdevCfg.tsoSize = 0;
    if (KNET_GetCfg(CONF_HW_TSO).intValue > 0) {
        netdevCfg.tsoSize = MAX_TSO_SEG;
        netdevCfg.offloads |= DP_NETDEV_OFFLOAD_TSO;
    }

    DP_Netdev_t *netdev = DP_CreateNetdev(&netdevCfg);  // 与dp交互网络设备信息
    if (netdev == NULL) {
        KNET_ERR("DP CreateNetdev failed");
        return -1;
    }

    /* 配置网络设备 */
    ret = KNET_ConfigureStackNetdev(netdev, netdevCfg.ifname);
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

static int KnetSetDpRtCfg(void)
{
    DP_RtInfo_t rtInfo = {0};
    DP_TbmAttr_t *attrs[3] = {0};  // 3表示下发3个attr
    uint32_t netmask = (uint32_t)KNET_GetCfg(CONF_INTERFACE_NETMASK).intValue;
    uint32_t dst = (uint32_t)(KNET_GetCfg(CONF_INTERFACE_IP).intValue) & netmask; // dst表示一个网段
    uint32_t gw = (uint32_t)KNET_GetCfg(CONF_INTERFACE_GATEWAY).intValue;
    int ifIndex = g_tapIfIndex; // 传递TAP口的ifindex
    KnetFillRtAttrs(attrs, dst, gw, ifIndex);

    uint8_t netmaskPrefixLen = KnetNetmaskPrefixLenCal(ntohl(netmask));
    rtInfo.dstLen = netmaskPrefixLen;  // 表示掩码前缀长度
    rtInfo.table = RT_TABLE_DEFAULT;
    rtInfo.protocol = RTPROT_STATIC;
    rtInfo.scope = RT_SCOPE_LINK;
    rtInfo.type = RTN_LOCAL;

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

static int KnetInitDp(void)
{
    int ret;
    int r;

    ret = KNET_TAPCreate(g_tapId, &g_tapFd, &g_tapIfIndex);
    if (ret != 0) {
        KNET_ERR("K-NET tap create failed, ret %d", ret);
        return -1;
    }

    ret = KNET_SetDpCfg();
    if (ret != 0) {
        KNET_ERR("K-NET set dp cfg failed, ret %d", ret);
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
        KNET_ERR("K-NET init dp netdev failed, ret %d", ret);
        return -1;
    }
    KNET_INFO("K-NET init dp netdev success");

    ret = KnetSetDpRtCfg();
    if (ret < 0) {
        KNET_ERR("K-NET set dp route cfg failed, ret %d", ret);
        return -1;
    }
    KNET_INFO("K-NET set DpRtCfg success");

    /* 初始化协议栈控制面对接 */
    KNET_GetCap(KNET_CAP_NET_ADMIN);
    r = (int)DP_CpdInit();
    KNET_ClearCap(KNET_CAP_NET_ADMIN);
    if (r != 0) {
        KNET_ERR("DP CPD init failed, ret %d", r);
        return -1;
    }
    KNET_INFO("DP CPD init success");

    return 0;
}

static void ShowHiastackStats(TelemetryInfo *telemetryInfo, int queId)
{
    /* telemetryInfo 未申请到共享内存，跳过处理，错误日志已经输出，没必要刷屏刷日志 */
    if (telemetryInfo == NULL) {
        return;
    }

    if (telemetryInfo->msgReady[queId] == 1) {
        DP_ShowStatistics(telemetryInfo->statType, -1, KNET_STAT_OUTPUT_TO_TELEMETRY);
        /* 调用后触发 KNET_ACC_Debug */
        telemetryInfo->msgReady[queId] = 0;
    }
}

/**
 * @brief 多进程中从进程抓包轮询线程函数
 */
void *MultiPdumpThreadFunc(void* args)
{
    uint32_t usSleepGap = 100000;    // 100000表示100ms
    /* 多进程模式 */
    const struct rte_memzone *pdumpRequestMz = rte_memzone_lookup(MZ_KNET_MULTI_PDUMP);
    if (pdumpRequestMz == NULL) {
        KNET_ERR("Not found available memzone for multi process dump packet");
    }
    struct PdumpRequest *pdumpRequest = NULL;
    uint8_t oldOp = 1;  // 默认DISABLE
    if (pdumpRequestMz != NULL) {
        pdumpRequest = pdumpRequestMz->addr;
        oldOp = pdumpRequest->op;
    }

    /* 协议栈打点统计共享内存获取 */
    TelemetryInfo *telemetryInfo = NULL;
    /* 开启配置 且 为多进程模式 */
    bool telemetryFlag = KNET_GetCfg(CONF_DPDK_TELEMETRY).intValue == 1 &&
                         KNET_GetCfg(CONF_COMMON_MODE).intValue == KNET_RUN_MODE_MULTIPLE;
    if (telemetryFlag) {
        const struct rte_memzone *dpMz = rte_memzone_lookup(KNET_TELEMETRY_MZ_NAME);
        if (dpMz == NULL || dpMz->addr == NULL) {
            telemetryFlag = false;
            KNET_ERR("Subprocess couldn't allocate memory for dp debug info");
        } else {
            telemetryInfo = dpMz->addr;
        }
    }
    int queId = KNET_GetCfg(CONF_INNER_QID).intValue;

    /* 从进程轮询是否需要开启抓包 */
    while (1) {
        /* 从进程判断是否需要执行打点统计 */
        if (telemetryFlag) {
            KNET_SpinlockLock(&g_multiPdumpThread.lock);
            ShowHiastackStats(telemetryInfo, queId);
            KNET_SpinlockUnlock(&g_multiPdumpThread.lock);
        }

        if (g_threadStop) {
            return NULL;
        }
        usleep(usSleepGap);

        if (pdumpRequestMz == NULL) {
            continue;
        }
        /* 抓包侧改标志，从进程检测 */
        if (pdumpRequest->op == oldOp) {
            continue;
        }
        /* 标志位切换，开始注册或者删除cbs */
        KNET_SpinlockLock(&g_multiPdumpThread.lock);
        int ret = KNET_SetPdumpRxTxCbs(pdumpRequest);
        KNET_SpinlockUnlock(&g_multiPdumpThread.lock);
        if (ret != 0) {
            KNET_ERR("Pdump cbs failed to be set, ret %d", ret);
        }
        oldOp = pdumpRequest->op;
    }
}

/**
 * @brief 控制面线程函数，CP (Control Plane)
 */
void *Cp_Thread_Func(void *args)
{
    CtrlThreadArgs *ctrlArgs = (CtrlThreadArgs *)args;
    uint16_t cpuset = ctrlArgs->ctrlVcpuID;
    uint64_t tid = KNET_ThreadId();
    KNET_SetThreadAffinity(tid, &cpuset, 1);

    while (1) {
        KNET_SpinlockLock(&g_cpThread.lock);
        DP_CpdRunOnce();
        KNET_SpinlockUnlock(&g_cpThread.lock);
        if (g_threadStop) {
            return NULL;
        }
        usleep(10000);  // 10000表示10ms, 管控面不需要调度太频繁，释放部分算力给业务线程使用
    }
}

static int LcoreMainloop(void *arg)
{
    unsigned lcoreId = rte_lcore_id();
    DpWorkerInfo *workerInfo = (DpWorkerInfo *)arg;

    KNET_INFO("WorkerId %u starting mainloop on core %u", workerInfo->workerId, lcoreId);

    /* Main loop. 8< */
    while (1) {
        KNET_SpinlockLock(&workerInfo->lock);

        DP_RunWorkerOnce(workerInfo->workerId);

        /*
         * Call the timer handler on each core: as we don't need a
         * very precise timer, so only call rte_timer_manage()
         * every ~10ms. In a real application, this will enhance
         * performances as reading the HPET timer is not efficient.
         */
        if (unlikely(g_threadStop)) {
            KNET_SpinlockUnlock(&workerInfo->lock);
            KNET_WARN("WorkerId %u stop mainloop on core %u", workerInfo->workerId, lcoreId);
            return 0;
        }

        KNET_SpinlockUnlock(&workerInfo->lock);
    }
    /* >8 End of main loop. */
}

static int32_t KnetCreateCpThread(void)
{
    static CtrlThreadArgs ctrlArgs = {0};
    ctrlArgs.ctrlVcpuID = (uint32_t)KNET_GetCfg(CONF_COMMON_CTRL_VCPU_ID).intValue;
    int32_t ret = KNET_CreateThread(&g_cpThread.threadID, Cp_Thread_Func, (void *)&ctrlArgs);
    if (ret != 0) {
        KNET_ERR("K-NET create control thread failed, ret %d", ret);
        return -1;
    }
    g_cpThread.isCreated = true;

    ret = KNET_ThreadNameSet(g_cpThread.threadID, "KnetCpThread");
    if (ret != 0) {
        KNET_ERR("K-NET set control thread name failed, ret %d", ret);
        return -1;
    }
    return 0;
}

void *SignalBlockMonitorThreadFunc(void* args)
{
    struct SignalTriggerTimes last = {0};
    struct SignalTriggerTimes cur = {0};
    struct SignalTriggerTimes *signalTriggerTimes = KNET_SignalTriggerTimesGet();

    while (1) {
        KNET_SpinlockLock(&signalTriggerTimes->lock);
        last.knetSignalEnterCnt = signalTriggerTimes->knetSignalEnterCnt;
        last.knetSignalExitCnt = signalTriggerTimes->knetSignalExitCnt;
        KNET_SpinlockUnlock(&signalTriggerTimes->lock);

        sleep(2); // 每2s检查信号处理是否死锁
        
        KNET_SpinlockLock(&signalTriggerTimes->lock);
        cur.knetSignalEnterCnt = signalTriggerTimes->knetSignalEnterCnt;
        cur.knetSignalExitCnt = signalTriggerTimes->knetSignalExitCnt;
        KNET_SpinlockUnlock(&signalTriggerTimes->lock);

        /* 经过2s，信号进入退出次数无变化，且进入次数大于退出次数，说明在信号流程中阻塞超过2s */
        if (cur.knetSignalEnterCnt == last.knetSignalEnterCnt && cur.knetSignalExitCnt == last.knetSignalExitCnt
            && cur.knetSignalEnterCnt > cur.knetSignalExitCnt) {
            (void)raise(SIGKILL);
        }
    }
}

static int32_t KnetCreateSignalBlockMonitorThread(void)
{
    int32_t ret = KNET_CreateThread(&g_signalBlockMonitorThread.threadID, SignalBlockMonitorThreadFunc, NULL);
    if (ret != 0) {
        KNET_ERR("K-NET create signal block monitor thread failed, ret %d", ret);
        return -1;
    }
    g_signalBlockMonitorThread.isCreated = true;

    ret = KNET_ThreadNameSet(g_signalBlockMonitorThread.threadID, "KnetSignalMon");
    if (ret != 0) {
        KNET_ERR("K-NET set signal block monitor thread name failed, ret %d, tid %llu",
            ret, g_signalBlockMonitorThread.threadID);
        return -1;
    }
    return 0;
}

static int32_t KnetCreateMultiPdumpThread(void)
{
    int32_t ret = KNET_CreateThread(&g_multiPdumpThread.threadID, MultiPdumpThreadFunc, NULL);
    if (ret != 0) {
        KNET_ERR("K-NET create multidump thread failed, ret %d", ret);
        return -1;
    }
    g_multiPdumpThread.isCreated = true;

    ret = KNET_ThreadNameSet(g_multiPdumpThread.threadID, "KnetPdumpThread");
    if (ret != 0) {
        KNET_ERR("K-NET set multidump thread name failed, ret %d", ret);
        return -1;
    }
    return 0;
}

static int32_t KnetStartDpThread(void)
{
    int32_t ret;
    uint32_t maxWorkerId = KNET_DpMaxWorkerIdGet();
    for (uint32_t workerId = 0; workerId < maxWorkerId; ++workerId) {
        const DpWorkerInfo *dpWorkerInfo = KNET_DpWorkerInfoGet(workerId);
        /* 将协议栈数据面线程挂到eal线程上启动 */
        ret = rte_eal_remote_launch(LcoreMainloop, (void *)dpWorkerInfo, dpWorkerInfo->lcoreId);
        if (ret != 0) {
            KNET_ERR("Rte eal remote launch failed, ret %d", ret);
            return -1;
        }
    }

    return 0;
}

/**
 * @brief dpdk和协议栈资源初始化
 */
static int32_t KnetDpdkStackInit(void)
{
    /* 注册dp协议栈钩子 */
    int32_t ret = (int32_t)KNET_SAL_Init();
    if (ret != 0) {
        KNET_ERR("K-NET init sal failed, ret %d", ret);
        return -1;
    }

    if (KNET_GetCfg(CONF_COMMON_MODE).intValue == KNET_RUN_MODE_SINGLE) {
        ret = KNET_InitDpdk(KNET_PROC_TYPE_PRIMARY, KNET_RUN_MODE_SINGLE);
    } else {
        ret = KNET_InitDpdk(KNET_PROC_TYPE_SECONDARY, KNET_RUN_MODE_MULTIPLE);
    }
    if (ret < 0) {
        KNET_ERR("K-NET init dpdk failed, ret %d", ret);
        return -1;
    }
    KNET_INFO("K-NET init dpdk success");

    ret = KNET_HashTblInit();
    if (ret < 0) {
        KNET_ERR("K-NET init hashtabl failed, ret %d", ret);
        return -1;
    }
    /* 初始化dp协议栈 */
    ret = KnetInitDp();
    if (ret < 0) {
        KNET_ERR("K-NET init dp failed, ret %d", ret);
        return -1;
    }
    KNET_INFO("K-NET init dp success");

    ret = KnetCreateSignalBlockMonitorThread();
    if (ret != 0) {
        KNET_ERR("K-NET create signal block monitor thread failed, ret %d", ret);
        return -1;
    }

    /* 需要一个抓包线程检查共享内存，拉起抓包线程 */
    ret = KnetCreateMultiPdumpThread();
    if (ret != 0) {
        KNET_ERR("K-NET init multidump failed, ret %d", ret);
        return -1;
    }

    /* 拉起控制面线程 */
    ret = KnetCreateCpThread();
    if (ret != 0) {
        KNET_ERR("K-NET start cp thread failed, ret %d", ret);
        return -1;
    }

    /* 拉起数据面线程 */
    ret = KnetStartDpThread();
    if (ret != 0) {
        KNET_ERR("K-NET start dp thread failed, ret %d", ret);
        return -1;
    }

    SetDpInited();

    return 0;
}

/**
 * @attention 与dpdk资源相关的初始化必须放在此函数中，否则daemonize fork会使得dpdk资源提前被初始化
 */
int KNET_TrafficResourcesInit(void)
{
    /**
     * @attention: 1.这里加自旋锁的话，dpdk初始化的子线程会用到epoll_create，又会进来，然后加锁，
     *               如果dpdk主线程在初始化过程中需要等待子线程的结果，就会死锁（目前dpdk版本无此问题）
     *             2.方案变动时须考虑用户多线程并发场景
     */
    static KNET_SpinLock lock = {
        .value = KNET_SPIN_UNLOCKED_VALUE,
    };

    static bool inprogress = false;
    static uint64_t tid = 0;
    static bool inited = false;
    if (inited) {  // 先判断一下，若已初始化直接退出，节省后续加解锁的开销
        return 0;
    }

    /* 如果是同一个线程重入，且初始化inprogress，则退出 */
    if (inprogress && tid == (uint64_t)syscall(__NR_gettid)) {
        KNET_INFO("Same thread re enter, return");
        return 0;
    } else if (inprogress && tid != (uint64_t)syscall(__NR_gettid)) {
        KNET_INFO("Last_tid %llu inprogress, cur_tid %llu wait", tid, syscall(__NR_gettid));
    }

    KNET_SpinlockLock(&lock);
    if (inited) {
        KNET_SpinlockUnlock(&lock);
        return 0;
    }

    if (!g_cfgInit) {
        KNET_SpinlockUnlock(&lock);
        KNET_ERR("K-NET cfg init failed");
        return -1;
    }

    inprogress = true;
    tid = (uint64_t)syscall(__NR_gettid);

    int32_t ret = KnetDpdkStackInit();
    if (ret != 0) {
        KNET_SpinlockUnlock(&lock);
        KNET_ERR("Dpdk stack init failed, ret %d", ret);
        return -1;
    }

    inited = true;
    KNET_SpinlockUnlock(&lock);

    KNET_INFO("K-NET traffic resources init success");
    return 0;
}

/**
 * @attention: 初始化有依赖顺序，勿随意修改顺序
 */
void KNET_ConfigInit(void)
{
    int32_t ret;
    KNET_LogInit();

    ret = KNET_InitCfg(KNET_PROC_TYPE_SECONDARY);
    if (ret != 0) {
        KNET_ERR("K-NET init cfg failed");
        g_cfgInit = false;
        return;
    }

    KNET_LogLevelConfigure();

    KNET_INFO("K-NET start success");
    g_cfgInit = true;
}

int KNET_JoinDpdkAndStackThread(void)
{
    // 等待knet线程结束
    int failedFlag = 0;
    int ret = 0;

    if (g_cpThread.isCreated) {
        ret = KNET_JoinThread(g_cpThread.threadID, NULL);
        if (ret != 0) {
            failedFlag = -1;
            KNET_ERR("K-NET cp thread join failed, ret %d", ret);
        }
    }

    if (g_multiPdumpThread.isCreated) {
        ret = KNET_JoinThread(g_multiPdumpThread.threadID, NULL);
        if (ret != 0) {
            failedFlag = -1;
            KNET_ERR("K-NET multidump thread join failed, ret %d", ret);
        }
    }

    // 等待dpdk线程结束
    uint32_t maxWorkerId = KNET_DpMaxWorkerIdGet();
    for (uint32_t workerId = 0; workerId < maxWorkerId; ++workerId) {
        const DpWorkerInfo *dpWorkerInfo = KNET_DpWorkerInfoGet(workerId);
        ret = rte_eal_wait_lcore(dpWorkerInfo->lcoreId);
        if (ret < 0) {
            failedFlag = -1;
            KNET_ERR("K-NET dp thread join failed, ret %d", ret);
        }
    }

    return failedFlag;
}

void KNET_Uninit(void)
{
    if (KNET_IsInSignal()) {
        KNET_LogLevelSet(KNET_LOG_EMERG); // 在信号流程中退出不能打印syslog，将LOG级别设为0
        KNET_MemSetFlagInSignalQuiting();
        if (KNET_IsForkedParent()) {
            KNET_DpExit();
            usleep(10 * 1000); // 通过延时保证数据面线程已经将RST报文发送出去，10 * 1000表示10ms
            g_threadStop = true;
            usleep(200 * 1000); // 通过延时保证数据面和控制面线程已退出，200 * 1000表示200ms
            (void)KNET_TapFree((g_tapFd));
            KNET_PktBatchFree();
            return;
        }
    }

    /**
     * @attention
     * 进程正常退出、exit函数会调用到destructor函数，若存在无法调用到destructor函数的场景，会无法发送RST报文，需再针对性分析和适配
     *            不可行场景：例如进程未sigaction注册SIGINT信号，则ctrl+c时无法调用到destructor函数
     * @note 目前redis场景，存在dbg fork，子进程退出会调用destructor，为避免dpdk资源重复释放，采用只让父进程close hijack
     * fd的方案 HijackFds主要是epollfd、tcp udp
     * sockfd，截获信号后关闭fd是为了协议栈能通过DP_Close发送RST报文，告知对端本端已经退出
     */
    if (KNET_IsForkedParent()) {
        KNET_INFO("All hijack fds close");
        KNET_DpExit();
        usleep(10 * 1000); // 通过延时保证数据面线程已经将RST报文发送出去，10 * 1000表示10ms
        if (KNET_GetCfg(CONF_INNER_PROC_TYPE).intValue == KNET_PROC_TYPE_PRIMARY) {
            KNET_UninitDpdkTelemetry();
        }

        if (g_tapFd != INVALID_FD) {
            KNET_TapFree(g_tapFd);
        }
    }
    g_threadStop = true;        // 置此标志位，控制面和数据面线程才会退出,抓包线程也会退出

    int ret = 0;
    if (KNET_IsForkedParent()) {
        usleep(10 * 1000); // 通过延时保证数据面和控制面线程已退出，10 * 1000表示10ms

        ret = KNET_JoinDpdkAndStackThread();
        if (ret != 0) {
            KNET_ERR("K-NET join thread failed");
        }

        KNET_PktBatchFree(); // 必须要在数据面线程退出之后才能释放
        KNET_HashTblDeinit(); // 父进程退出之后释放资源

        ret = rte_eal_cleanup();
        if (ret != 0) {
            KNET_ERR("rte eal cleanup failed, ret %d", ret);
        }
    }

    KnetDeinitDpSymbols();
}

#ifdef KNET_TEST
#define KNET_INIT_API
#define KNET_UNINIT_API
#else
#define KNET_INIT_API __attribute__((constructor))
#define KNET_UNINIT_API __attribute__((destructor))
#endif

static KNET_INIT_API void KnetInit(void)
{
    KNET_INFO("K-NET start");
    KNET_ConfigInit();
    KNET_SigactionReg();
}

static KNET_UNINIT_API void KnetUnint(void)
{
    KNET_Uninit();
    KNET_INFO("K-NET stop");
}
