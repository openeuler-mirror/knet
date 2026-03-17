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
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <signal.h>

#include "rte_timer.h"
#include "rte_ethdev.h"
#include "rte_eal_memconfig.h"
#include "rte_cycles.h"

#include "securec.h"
#include "dp_init_api.h"
#include "dp_netdev_api.h"
#include "dp_worker_api.h"
#include "dp_cfg_api.h"
#include "dp_cpd_api.h"
#include "dp_tbm_api.h"
#include "dp_debug_api.h"
#include "dp_socket_api.h"

#include "knet_config.h"
#include "knet_thread.h"
#include "knet_pdump.h"
#include "knet_dpdk_init.h"
#include "knet_telemetry.h"
#include "knet_lock.h"
#include "knet_utils.h"
#include "knet_signal_tcp.h"
#include "knet_socketext_init.h"
#include "knet_sal_tcp.h"
#include "knet_init_tcp.h"
#include "knet_tcp_api_init.h"
#include "tcp_os.h"
#include "knet_bond.h"
#include "knet_init_tcp.h"
#include "knet_mem.h"
#include "knet_pkt.h"
#include "knet_transmission.h"
#include "knet_init.h"

typedef struct {
    KNET_SpinLock lock;
    uint64_t threadID;
    bool isCreated;
} KnetThreadInfo;

KNET_STATIC bool g_threadStop = false;
static KnetThreadInfo g_cpThread[MAX_VCPU_NUMS] = {
    {.lock = {KNET_SPIN_UNLOCKED_VALUE}, .threadID = 0, .isCreated = false}
};
static KnetThreadInfo g_multiPdumpThread = {.lock = {KNET_SPIN_UNLOCKED_VALUE}, .threadID = 0, .isCreated = false};
static KnetThreadInfo g_signalBlockMonitorThread = {
    .lock = {KNET_SPIN_UNLOCKED_VALUE}, .threadID = 0, .isCreated = false
};
static bool g_cfgInit = false;

#define DPDK_EAL_INTR_THREAD_NAME  "eal-intr-thread"
#define DPDK_RTE_MP_HAND_THREAD_NAME "rte_mp_handle"
#define KNET_SIGQUIT_WAIT (10 * 1000)
#define KNET_NO_KERNELFORWARD_FREQ 10000

// 定义为weak函数，默认返回fasle，即不进行daemon初始化
__attribute__((weak)) bool KNET_IsMpDaemonInit(void)
{
    return false;
}

void KNET_AllThreadLock(void)
{
    int ctrlVcpuNum = KNET_GetCfg(CONF_COMMON_CTRL_VCPU_NUMS)->intValue;
    for (int i = 0; i < ctrlVcpuNum; i++) {
        KNET_SpinlockLock(&g_cpThread[i].lock);
    }
    KNET_SpinlockLock(&g_multiPdumpThread.lock);
    KNET_SpinlockLock(&g_signalBlockMonitorThread.lock);

    uint32_t maxWorkerId = KNET_DpMaxWorkerIdGet();
    for (uint32_t workerId = 0; workerId < maxWorkerId; ++workerId) {
        KNET_DpWorkerInfo *dpWorkerInfo = KNET_DpWorkerInfoGet(workerId);
        KNET_SpinlockLock(&dpWorkerInfo->lock);
    }
}

void KNET_AllThreadUnlock(void)
{
    uint32_t maxWorkerId = KNET_DpMaxWorkerIdGet();
    for (uint32_t workerId = 0; workerId < maxWorkerId; ++workerId) {
        KNET_DpWorkerInfo *dpWorkerInfo = KNET_DpWorkerInfoGet(workerId);
        KNET_SpinlockUnlock(&dpWorkerInfo->lock);
    }

    KNET_SpinlockUnlock(&g_signalBlockMonitorThread.lock);
    KNET_SpinlockUnlock(&g_multiPdumpThread.lock);
    int ctrlVcpuNum = KNET_GetCfg(CONF_COMMON_CTRL_VCPU_NUMS)->intValue;
    for (int i = 0; i < ctrlVcpuNum; i++) {
        KNET_SpinlockUnlock(&g_cpThread[i].lock);
    }
}

/**
 * @ingroup knet_init
 * @brief 控制面参数结构体
 */
typedef struct {
    uint32_t ctrlVcpuID; // 控制面绑核核号
    int cpdId;
} CtrlThreadArgs;

int KNET_PosixOpsApiInit(struct KNET_PosixApiOps *ops)
{
    (void)KNET_DpPosixOpsApiInit(ops);
    return 0;
}

KNET_STATIC void ProcessFromQueMap(uint32_t* queMap, uint32_t lcoreId, uint32_t workerId)
{
    KNET_QueIdMapPidTid_t* queIdMapPidTid = KNET_GetQueIdMapPidTidLcoreInfo();
    for (int i = 0; i < KNET_GetCfg(CONF_DPDK_QUEUE_NUM)->intValue; i++) {
        // 最大长度 128 个queue， 协议栈给的map是数组形式返回的{3, 7, 0, 0} 是表示queue 0、1、32、33、34 队列被workerId使用
        // 由于每次遍历只能用32位处理，因此i左移只能32位，模32放到32位空间里，和对应queMap[idx]做位运算，如果不为0表示队列i正被使用
        if (((1U << (i % UINT32_BIT_LEN)) & queMap[i / UINT32_BIT_LEN]) != 0) {
            KNET_SetQueIdMapPidTidLcoreInfo(i, queIdMapPidTid[i].pid, queIdMapPidTid[i].tid, lcoreId, workerId);
        }
    }
}

KNET_STATIC int EnsureAllQueueAlloc(void)
{
    KNET_QueIdMapPidTid_t* queIdMapPidTid = KNET_GetQueIdMapPidTidLcoreInfo();
    for (int i = 0; i < KNET_GetCfg(CONF_DPDK_QUEUE_NUM)->intValue; i++) {
        if (queIdMapPidTid[i].pid == 0) {
            return -1;
        }
    }
    return 0;
}

KNET_STATIC void ProcessTelemetryQueueMapWorker()
{
    static int processMode = -1;
    if (processMode == -1) {
        processMode = KNET_GetCfg(CONF_COMMON_MODE)->intValue;
    }
    if (processMode == KNET_RUN_MODE_MULTIPLE || EnsureAllQueueAlloc() != 0) {
        return;
    }
    uint32_t maxWorkerId = KNET_DpMaxWorkerIdGet();
    for (uint32_t workerId = 0; workerId < maxWorkerId; ++workerId) {
        uint32_t queMap[MAX_QUEUE_NUM / UINT32_BIT_LEN] = {0};
        KNET_DpWorkerInfo *KNET_dpWorkerInfo = KNET_DpWorkerInfoGet(workerId);
        DP_GetNetdevQueMap(workerId, KNET_GetIfIndex(), queMap, sizeof(queMap) / sizeof(queMap[0]));
        ProcessFromQueMap(queMap, KNET_dpWorkerInfo->lcoreId, KNET_dpWorkerInfo->workerId);
    }
}

void ProcessTelemetryShowStats(bool flag, KNET_TelemetryInfo *telemetryInfo, int queId)
{
    if (flag) {
        KNET_SpinlockLock(&g_multiPdumpThread.lock);
        ShowDpStats(telemetryInfo, queId);
        KNET_SpinlockUnlock(&g_multiPdumpThread.lock);
    }
}

void ProcessTelemetryPersist(bool flag, KNET_TelemetryPersistInfo *telemetryPersistInfo, pid_t pid)
{
    if (flag) {
        KNET_SpinlockLock(&g_multiPdumpThread.lock);
        if (telemetryPersistInfo->curPid == pid && telemetryPersistInfo->state == KNET_TELE_PERSIST_WAITSECOND) {
            PrepareAllDpStates(telemetryPersistInfo);
        }
        KNET_SpinlockUnlock(&g_multiPdumpThread.lock);
    }
}
/**
 * @brief 多进程中从进程抓包轮询线程函数
 */
KNET_STATIC void *MultiPdumpThreadFunc(void* args)
{
    /* 多进程模式 */
    const struct rte_memzone *pdumpRequestMz = rte_memzone_lookup(KNET_MULTI_PDUMP_MZ);
    if (pdumpRequestMz == NULL) {
        KNET_ERR("Not found available memzone for multi process dump packet");
    }

    /* 协议栈打点统计共享内存获取 */
    KNET_TelemetryInfo *telemetryInfo = NULL;
    KNET_TelemetryPersistInfo *telemetryPersistInfo = NULL;
    bool persistFlag = KNET_GetCfg(CONF_COMMON_MODE)->intValue == KNET_RUN_MODE_MULTIPLE;
    pid_t curPid = getpid();
    /* 开启配置 且 为多进程模式 */
    bool telemetryFlag = KNET_GetCfg(CONF_DPDK_TELEMETRY)->intValue == 1 &&
                         KNET_GetCfg(CONF_COMMON_MODE)->intValue == KNET_RUN_MODE_MULTIPLE;
    if (telemetryFlag) {
        const struct rte_memzone *tcpMz = rte_memzone_lookup(KNET_TELEMETRY_MZ_NAME);
        if (tcpMz == NULL || tcpMz->addr == NULL) {
            telemetryFlag = false;
            KNET_ERR("Subprocess couldn't allocate memory for tcp debug info");
        } else {
            telemetryInfo = tcpMz->addr;
        }
    }
    /* 多进程自动启动持久化 */
    if (persistFlag) {
        const struct rte_memzone *persistMz = rte_memzone_lookup(KNET_TELEMETRY_PERSIST_MZ_NAME);
        if (persistMz == NULL || persistMz->addr == NULL) {
            KNET_ERR("Subprocess couldn't allocate memory for persist mz");
        } else {
            telemetryPersistInfo = persistMz->addr;
        }
    }

    int queId = KNET_GetCfg(CONF_INNER_QID)->intValue;
    uint32_t usSleepGap = 100000;    // 100000表示100ms
    /* 从进程轮询是否需要开启抓包 */
    while (1) {
        ProcessTelemetryShowStats(telemetryFlag, telemetryInfo, queId);
        ProcessTelemetryPersist(persistFlag, telemetryPersistInfo, curPid);
        ProcessTelemetryQueueMapWorker();
        if (g_threadStop) {
            return NULL;
        }
        usleep(usSleepGap);

        if (pdumpRequestMz == NULL) {
            continue;
        }
       
        /* 标志位切换，开始注册或者删除cbs */
        KNET_SpinlockLock(&g_multiPdumpThread.lock);
        int ret = KNET_SetPdumpRxTxCbs(pdumpRequestMz);
        KNET_SpinlockUnlock(&g_multiPdumpThread.lock);
        if (ret != 0) {
            KNET_ERR("Pdump cbs failed to be set, ret %d", ret);
        }
    }
}

/**
 * @brief 控制面线程函数，CP (Control Plane)
 */
KNET_STATIC void *CpThreadFunc(void *args)
{
    CtrlThreadArgs *ctrlArgs = (CtrlThreadArgs *)args;
    uint16_t cpuset = ctrlArgs->ctrlVcpuID;
    uint64_t tid = KNET_ThreadId();
    KNET_SetThreadAffinity(tid, &cpuset, 1);
    int kernelForwardEnabled = KNET_GetCfg(CONF_HW_BIFUR_ENABLE)->intValue;

    while (1) {
        KNET_SpinlockLock(&g_cpThread[ctrlArgs->cpdId].lock);
        DP_CpdRunOnce(ctrlArgs->cpdId);
        KNET_SpinlockUnlock(&g_cpThread[ctrlArgs->cpdId].lock);
        if (g_threadStop) {
            return NULL;
        }
        if (kernelForwardEnabled != KERNEL_FORWARD_ENABLE) {
            usleep(KNET_NO_KERNELFORWARD_FREQ);
        }
    }
}

KNET_STATIC int LcoreMainloop(void *arg)
{
    unsigned lcoreId = rte_lcore_id();
    KNET_DpWorkerInfo *workerInfo = (KNET_DpWorkerInfo *)arg;

    KNET_INFO("WorkerId %u starting mainloop on core %u", workerInfo->workerId, lcoreId);

    uint64_t hz = rte_get_timer_hz();
    // 计算90ms对应的周期数 (hz * 90 / 1000), 确保lacp协商报文发送频率满足最低100ms一次的要求
    uint64_t interval = (hz * 9) / 100;  // 等价于 hz * 0.09
    uint64_t lastCycle = rte_get_timer_cycles();
    /* 多进程更新0队列的数据，单进程worker的queue和workerId暂时相等 */
    if (KNET_GetCfg(CONF_COMMON_MODE)->intValue == KNET_RUN_MODE_MULTIPLE) {
        (void)KNET_SetQueIdMapPidTidLcoreInfo(workerInfo->workerId, getpid(), syscall(SYS_gettid), workerInfo->lcoreId,
                                              workerInfo->workerId);
    } else {
        uint32_t queMap[MAX_QUEUE_NUM / UINT32_BIT_LEN] = {0};
        DP_GetNetdevQueMap(workerInfo->workerId, KNET_GetIfIndex(), queMap, sizeof(queMap) / sizeof(queMap[0]));
        for (int i = 0; i < KNET_GetCfg(CONF_DPDK_QUEUE_NUM)->intValue; i++) {
            if (((1U << (i % UINT32_BIT_LEN)) & queMap[i / UINT32_BIT_LEN]) != 0) {
                KNET_SetQueIdMapPidTidLcoreInfo(i, getpid(), syscall(SYS_gettid), lcoreId, workerInfo->workerId);
            }
        }
    }

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

        uint64_t currentCycle = rte_get_timer_cycles();
        if (currentCycle - lastCycle >= interval) {
            // 调用LACP报文发送接口
            KNET_BondSendLacpPkt();
            lastCycle = currentCycle;
        }
    }
    /* >8 End of main loop. */
}

void *SignalBlockMonitorThreadFunc(void* args)
{
    struct SignalTriggerTimes last = {0};
    struct SignalTriggerTimes cur = {0};
    struct SignalTriggerTimes *signalTriggerTimes = KNET_DpSignalTriggerTimesGet();
 
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
 
KNET_STATIC int32_t KnetCreateSignalBlockMonitorThread(void)
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

KNET_STATIC int32_t CreateCpThread(void)
{
    int ctrlVcpuNum = KNET_GetCfg(CONF_COMMON_CTRL_VCPU_NUMS)->intValue;
    const int *ctrlVcpuArr = KNET_GetCfg(CONF_COMMON_CTRL_VCPU_IDS)->intValueArr;
    static CtrlThreadArgs ctrlArgs[MAX_VCPU_NUMS];
    if (ctrlVcpuNum > MAX_VCPU_NUMS) {
        KNET_ERR("Ctrl vcpu num is larger than max cpu num");
        return -1;
    }

    for (int i = 0; i < ctrlVcpuNum; i++) {
        ctrlArgs[i].ctrlVcpuID = (uint32_t)ctrlVcpuArr[i];
        ctrlArgs[i].cpdId = i;
        int32_t ret = KNET_CreateThread(&g_cpThread[i].threadID, CpThreadFunc, (void *)&ctrlArgs[i]);
        if (ret != 0) {
            KNET_ERR("K-NET create control thread failed, ret %d", ret);
            return -1;
        }
        char name[MAX_CPD_NAME_LEN] = {0};
        ret = snprintf_s(name, MAX_CPD_NAME_LEN, MAX_CPD_NAME_LEN - 1, "KnetCpThread%d", i);
        if (ret < 0) {
            KNET_ERR("Cpd thread name get error, ret %d", ret);
            return -1;
        }
        ret = KNET_ThreadNameSet(g_cpThread[i].threadID, name);
        if (ret != 0) {
            KNET_ERR("K-NET set control thread name failed, ret %d", ret);
            return -1;
        }
        g_cpThread[i].isCreated = true;
    }

    return 0;
}

KNET_STATIC int32_t CreateMultiPdumpThread(void)
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

KNET_STATIC int32_t StartDpThread(void)
{
    // 开启共线程无需启动数据面线程
    if (KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue == 1) {
        return 0;
    }

    int32_t ret = 0;
    uint32_t maxWorkerId = KNET_DpMaxWorkerIdGet();
    for (uint32_t workerId = 0; workerId < maxWorkerId; ++workerId) {
        KNET_DpWorkerInfo *KNET_dpWorkerInfo = KNET_DpWorkerInfoGet(workerId);
        /* 将协议栈数据面线程挂到eal线程上启动 */
        ret = rte_eal_remote_launch(LcoreMainloop, (void *)KNET_dpWorkerInfo, KNET_dpWorkerInfo->lcoreId);
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
KNET_STATIC int32_t DpdkStackInit(void)
{
    /* 注册tcp协议栈钩子 */
    int32_t ret = (int32_t)KNET_SAL_Init();
    if (ret != 0) {
        KNET_ERR("K-NET init sal failed, ret %d", ret);
        return -1;
    }

    if (KNET_GetCfg(CONF_COMMON_MODE)->intValue == KNET_RUN_MODE_SINGLE) {
        ret = KNET_InitDpdk(KNET_PROC_TYPE_PRIMARY, KNET_RUN_MODE_SINGLE);
    } else {
        ret = KNET_InitDpdk(KNET_PROC_TYPE_SECONDARY, KNET_RUN_MODE_MULTIPLE);
    }
    if (ret < 0) {
        KNET_ERR("K-NET init dpdk failed, ret %d", ret);
        return -1;
    }
    KNET_INFO("K-NET init dpdk success");

    /* 初始化tcp协议栈 */
    ret = KNET_InitDp();
    if (ret < 0) {
        KNET_ERR("K-NET init tcp failed, ret %d", ret);
        return -1;
    }
    KNET_INFO("K-NET init tcp success");

    ret = KnetCreateSignalBlockMonitorThread();
    if (ret != 0) {
        KNET_ERR("K-NET create signal block monitor thread failed, ret %d", ret);
        return -1;
    }

    /* 需要一个抓包线程检查共享内存，拉起抓包线程 */
    ret = CreateMultiPdumpThread();
    if (ret != 0) {
        KNET_ERR("K-NET init multidump failed, ret %d", ret);
        return -1;
    }

    /* 拉起控制面线程 */
    ret = CreateCpThread();
    if (ret != 0) {
        KNET_ERR("K-NET start cp thread failed, ret %d", ret);
        return -1;
    }

    /* 拉起数据面线程 */
    ret = StartDpThread();
    if (ret != 0) {
        KNET_ERR("K-NET start dp thread failed, ret %d", ret);
        return -1;
    }

    ret = KNET_TelemetryStartPersistThread(KNET_GetCfg(CONF_INNER_PROC_TYPE)->intValue,
                                           KNET_GetCfg(CONF_COMMON_MODE)->intValue);
    if (ret != 0) {
        return -1; // 函数内部打印日志
    }

    KNET_SetDpInited();

    return 0;
}

static bool IsDpdkCtrlThread(void)
{
    // dpdk初始化时会置这个全局变量为当前线程tid,-1说明非dpdk线程
    if (per_lcore__thread_id != -1) {
        return true;
    }

    return false;
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

    static bool inited = false;
    if (inited) {  // 先判断一下，若已初始化直接退出，节省后续加解锁的开销
        return 0;
    }

    static bool inprogress = false;
    static uint64_t tid = 0;
    /* 如果是同一个线程重入，且初始化inprogress，则退出 */
    if (inprogress && tid == (uint64_t)syscall(__NR_gettid)) {
        KNET_INFO("Same thread re enter, return");
        return 0;
    } else if (IsDpdkCtrlThread()) {
        /* dpdk控制线程必须直接走os（bond场景：dpdk主线程需要等待eal-intr-thread线程的结果，需要intr线程在主线之前初始化完成） */
        KNET_INFO("Dpdk ctrl thread enter, return and go os");
        return 0;
    } else if (inprogress && tid != (uint64_t)syscall(__NR_gettid)) {
        KNET_INFO("Last_tid %llu in progress, cur_tid %llu wait", tid, syscall(__NR_gettid));
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

    int32_t ret = DpdkStackInit();
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
void ConfigInit(void)
{
    KNET_LogInit();

    int32_t ret = KNET_InitCfg(KNET_PROC_TYPE_SECONDARY);
    if (ret != 0) {
        KNET_ERR("K-NET init cfg failed");
        g_cfgInit = false;
        return;
    }

    KNET_LogLevelSetByStr(KNET_GetCfg(CONF_COMMON_LOG_LEVEL)->strValue);
    KNET_INFO("K-NET start success");
    if (KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue == 1) {
        KNET_INFO("K-NET start cothread mode");
    }
    g_cfgInit = true;
}

KNET_STATIC int JoinDpdkAndStackThread(void)
{
    // 等待knet线程结束
    int failedFlag = 0;
    int ret = 0;

    int ctrlVcpuNum = KNET_GetCfg(CONF_COMMON_CTRL_VCPU_NUMS)->intValue;
    for (int i = 0; i < ctrlVcpuNum; i++) {
        if (g_cpThread[i].isCreated) {
            ret = KNET_JoinThread(g_cpThread[i].threadID, NULL);
            if (ret != 0) {
                failedFlag = -1;
                KNET_ERR("K-NET cp thread %d join failed, ret %d", i, ret);
            }
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
        KNET_DpWorkerInfo *KNET_dpWorkerInfo = KNET_DpWorkerInfoGet(workerId);
        ret = rte_eal_wait_lcore(KNET_dpWorkerInfo->lcoreId);
        if (ret < 0) {
            failedFlag = -1;
            KNET_ERR("K-NET dp thread join failed, ret %d", ret);
        }
    }

    return failedFlag;
}

/**
 * @brief 手动关闭线程
 */
void KNET_SetDpdkAndStackThreadStop(void)
{
    g_threadStop = true;
}

void Uninit(void)
{
    if (KNET_DpSignalIsInSigHandler()) {
        KNET_LogLevelSet(KNET_LOG_EMERG);  /* 在信号流程中退出不能打印syslog，将LOG级别设为0 */
        KNET_MemSetFlagInSignalQuiting();
        if (KNET_DpIsForkedParent()) {
            KNET_DpExit();
            usleep(10 * 1000); // 通过延时保证数据面线程已经将RST报文发送出去，10 * 1000表示10ms
            g_threadStop = true;
            KNET_TelemetrySetPersistThreadExit();
            usleep(200 * 1000); // 通过延时保证数据面和控制面线程已退出，200 * 1000表示200ms
            (void)KNET_FreeTapGlobal();
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
    if (KNET_DpIsForkedParent()) {
        KNET_INFO("All hijack fds close");
        KNET_DpExit();
        usleep(10 * 1000); // 通过延时保证数据面线程已经将RST报文发送出去，10 * 1000表示10ms

        g_threadStop = true;        // 置此标志位，控制面和数据面线程才会退出,抓包线程也会退出
        KNET_TelemetrySetPersistThreadExit();
        usleep(10 * 1000); // 通过延时保证数据面和控制面线程已退出，10 * 1000表示10ms
        int ret = JoinDpdkAndStackThread();
        if (ret != 0) {
            KNET_ERR("K-NET join thread failed");
        }
        
        if (!g_tcpInited) {
            goto END;
        }

        if (KNET_GetCfg(CONF_COMMON_MODE)->intValue == KNET_RUN_MODE_SINGLE) {
            ret = KNET_UninitDpdk(KNET_PROC_TYPE_PRIMARY, KNET_RUN_MODE_SINGLE);
        } else {
            ret = KNET_UninitDpdk(KNET_PROC_TYPE_SECONDARY, KNET_RUN_MODE_MULTIPLE);
        }
    }

END:
    KNET_UninitDp();
    KNET_UninitCfg();
}

#ifdef KNET_TEST
#define KNET_INIT_API
#define KNET_UNINIT_API
#else
#define KNET_INIT_API __attribute__((constructor))
#define KNET_UNINIT_API __attribute__((destructor))
#endif

KNET_STATIC KNET_INIT_API void KnetInit(void)
{
    // 如果KNET_IsMpDaemonInit函数返回值为true，即为daemon在执行，便不执行默认的构造函数
    if (KNET_IsMpDaemonInit()) {
        return;
    }

    KNET_INFO("K-NET start");
    ConfigInit();
    KNET_DpSignalRegAll();
}

KNET_STATIC KNET_UNINIT_API void KnetUninit(void)
{
    // 如果KNET_IsMpDaemonInit函数返回值为true，即为daemon在执行，便不执行默认的析构函数
    if (KNET_IsMpDaemonInit()) {
        return;
    }

    Uninit();
    KNET_INFO("K-NET stop");
}
