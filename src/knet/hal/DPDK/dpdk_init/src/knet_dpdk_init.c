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

#include "unistd.h"
#include "rte_pdump.h"
#include "rte_malloc.h"
#include "rte_eth_bond.h"

#include "dp_cfg_api.h"

#include "rte_ring.h"
#include "knet_pktpool.h"
#include "knet_thread.h"
#include "knet_capability.h"
#include "knet_telemetry.h"
#include "knet_transmission.h"
#include "knet_hash_table.h"
#include "knet_pdump.h"
#include "knet_dpdk_init_dev.h"
#include "knet_dpdk_init.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CPU_NUM 128
#define MEMORY_THRESHOLD_RATIO 0.75 // 为系统内存的75%时，启动进程时进行告警
#define MEMPOOL_THRESHOLD_RATIO 0.7
#define KNET_DPDK_PRIM_ARGC 17
#define KNET_DPDK_ARG_MAX_LEN 127
#define KNET_PKT_POOL_DEFAULT_CACHENUM 128
#define KNET_PKT_POOL_DEFAULT_CACHESIZE 512
#define KNET_PKT_POOL_DEFAULT_PRIVATE_SIZE 256
#define KNET_PKT_POOL_DEFAULT_HEADROOM_SIZE 128
#define KNET_PKT_POOL_DEFAULT_CREATE_ALG KNET_PKTPOOL_ALG_RING_MP_MC
#define DEFAULT_RING_SIZE 1024
#define MAX_RING_NUM 1024
#define SYSFS_PCI_DEVICES "/sys/bus/pci/devices"

typedef struct {
    KNET_DpdkNetdevCtx netdevCtx;
    uint32_t pktPoolId;
    char padding[4];   // 填充字节，确保结构体8 字节对齐
} DpdkDevInfo;

static DpdkDevInfo g_dpdkDevInfo = {
    .netdevCtx = {
        .portId = 0,
        .bondPortId = 0,
        .slavePortIds = {0},
    },
    .pktPoolId = KNET_PKTPOOL_INVALID_ID,
};

typedef struct {
    struct rte_ring *cpdRing[MAX_RING_NUM];
    int cpdRingNum;
} CpdRingInfo;

typedef struct {
    int32_t kernelForward;
    int32_t commonMode;
    int32_t isInit;
    char padding[4];
} CpdConfInfo;

const static struct rte_memzone *g_pdumpRequestMz = NULL;  // socket设计方案中pdump_request的共享内存
KNET_STATIC bool g_dpdkInited = false;
static CpdRingInfo g_delayCpdRxRingInfo = {
    .cpdRing = {NULL},
    .cpdRingNum = 0,
};

static CpdConfInfo g_delayCpdConfInfo = {
    .kernelForward = 0,
    .commonMode = 0,
    .isInit = 0,
};

static void DpdkMbufPoolUsageCheck(void)
{
    // 检查mbuf池使用情况
    KnetPktPoolCtrl *poolCtl = KnetPktGetPoolCtrl(g_dpdkDevInfo.pktPoolId);
    if (poolCtl == NULL) {
        KNET_ERR("Mbuf pool usage check failed, pool is invalid.");
        return;
    }
    uint32_t availCount = rte_mempool_avail_count(poolCtl->mempool);
    double mbufUsage = 1 - (double)availCount / (double)poolCtl->mempool->size;
    if (mbufUsage < 0) {
        mbufUsage = 0;
    }
    if (mbufUsage > MEMPOOL_THRESHOLD_RATIO) {
        KNET_WARN("Mbuf pool usage is too high: %lf", mbufUsage);
    }
}

void* KNET_GetDelayRxRing(int cpdRingId)
{
    if (g_delayCpdConfInfo.isInit == 0) {
        g_delayCpdConfInfo.kernelForward = KNET_GetCfg(CONF_HW_BIFUR_ENABLE)->intValue;
        g_delayCpdConfInfo.commonMode = KNET_GetCfg(CONF_COMMON_MODE)->intValue;
        g_delayCpdConfInfo.isInit = 1;
    }

    if (g_delayCpdConfInfo.kernelForward == BIFUR_FORBID || g_delayCpdConfInfo.kernelForward == BIFUR_ENABLE) {
        return NULL;
    }

    // 多进程模式下只支持单控制线程单队列，ringId以inner qid为准。
    if (g_delayCpdConfInfo.commonMode == KNET_RUN_MODE_MULTIPLE) {
        cpdRingId = KNET_GetCfg(CONF_INNER_QID)->intValue;
    }

    // 单进程才需要校验cpdRingId的范围
    if ((cpdRingId >= g_delayCpdRxRingInfo.cpdRingNum || cpdRingId < 0) &&
        g_delayCpdConfInfo.commonMode == KNET_RUN_MODE_SINGLE) {
        return NULL;
    }

    // cpdRing最大支持1024，超过多进程的队列数，不会产生溢出风险
    if (g_delayCpdRxRingInfo.cpdRing[cpdRingId] != NULL) {
        return g_delayCpdRxRingInfo.cpdRing[cpdRingId];
    }
    char name[MAX_CPD_NAME_LEN] = {0};
    int ret = snprintf_s(name, MAX_CPD_NAME_LEN, MAX_CPD_NAME_LEN - 1, "cpdtaprx%d", cpdRingId);
    if (ret < 0) {
        KNET_ERR("Ring name %s get error", name);
        return NULL;
    }

    g_delayCpdRxRingInfo.cpdRing[cpdRingId] = rte_ring_lookup(name);
    if (g_delayCpdRxRingInfo.cpdRing[cpdRingId] == NULL) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_WARN, "Cpd lookup ring failed %s", name);
    }
    return (void*)g_delayCpdRxRingInfo.cpdRing[cpdRingId];
}

static void PktPoolCfgInit(KNET_PktPoolCfg *pktPoolCfg)
{
    (void)memcpy_s(pktPoolCfg->name, KNET_PKTPOOL_NAME_LEN, KNET_PKT_POOL_NAME, strlen(KNET_PKT_POOL_NAME));
    pktPoolCfg->bufNum = (uint32_t)KNET_GetCfg(CONF_TCP_MAX_MBUF)->intValue;
    pktPoolCfg->cacheNum = KNET_PKT_POOL_DEFAULT_CACHENUM;
    pktPoolCfg->cacheSize = KNET_PKT_POOL_DEFAULT_CACHESIZE;
    pktPoolCfg->privDataSize = KNET_PKT_POOL_DEFAULT_PRIVATE_SIZE;
    pktPoolCfg->headroomSize = KNET_PKT_POOL_DEFAULT_HEADROOM_SIZE;
    pktPoolCfg->dataroomSize = KNET_PKT_POOL_DEFAULT_DATAROOM_SIZE;
    pktPoolCfg->createAlg = KNET_PKT_POOL_DEFAULT_CREATE_ALG;
    pktPoolCfg->numaId = (int32_t)rte_socket_id();
    pktPoolCfg->init = NULL;
}

static int32_t MbufMempoolCreate(void)
{
    KNET_PktPoolCfg pktPoolCfg = { 0 };
    PktPoolCfgInit(&pktPoolCfg);
    uint32_t ret = KNET_PktPoolCreate(&pktPoolCfg, &g_dpdkDevInfo.pktPoolId);
    if (ret != KNET_OK) {
        KNET_ERR("K-NET pkt pool create failed");
        return -1;
    }

    return 0;
}

static void MbufMempoolDestroy(void)
{
    KNET_PktPoolDestroy(g_dpdkDevInfo.pktPoolId);
    g_dpdkDevInfo.pktPoolId = KNET_PKTPOOL_INVALID_ID;
}

uint32_t KnetGetPktPoolId(void)
{
    return g_dpdkDevInfo.pktPoolId;
}

KNET_DpdkNetdevCtx *KNET_GetNetDevCtx(void)
{
    return &g_dpdkDevInfo.netdevCtx;
}

static int FindDpdkCore(void)
{
    // 开启共线程业务绑核未知，默认dpdk绑核在控制核心
    if (KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue == 1) {
        return KNET_GetCfg(CONF_COMMON_CTRL_VCPU_IDS)->intValueArr[0];
    }

    uint64_t serviceTid = KNET_ThreadId();
    uint16_t cpus[MAX_CPU_NUM] = {0};
    uint32_t cpuNums = MAX_CPU_NUM;

    int32_t ret = KNET_GetThreadAffinity(serviceTid, cpus, &cpuNums);
    if (ret != 0) {
        KNET_ERR("Service cpu get failed, ret %d", ret);
        return -1;
    }
    
    uint16_t core = 0;
    int dpdkCore = -1;
    for (uint32_t i = 0; i < cpuNums; ++i) {
        core = cpus[i];
        if (KNET_FindCoreInList(core) == -1) {
            dpdkCore = (int)core;
            break;
        }
    }
    return dpdkCore;
}

int GenerateDpdkCoreList(int dpdkCore, int procType, char* coreList, char* mainLcore)
{
    int32_t ret = 0;
    const char* coreListStr = KNET_GetCfg(CONF_INNER_CORE_LIST)->strValue;
    if (coreListStr == NULL) {
        KNET_ERR("Get core list failed");
        return -1;
    }

    int runMode = KNET_GetCfg(CONF_COMMON_MODE)->intValue;
    // 主进程/开启共线程后，不需要创建线程，只需要将dpdkCore添加到coreList中
    bool enableCothread = (KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue == 1);
    if ((runMode == KNET_RUN_MODE_MULTIPLE && procType == KNET_PROC_TYPE_PRIMARY) || enableCothread) {
        ret = sprintf_s(coreList, MAX_STRVALUE_NUM, "%d", dpdkCore);
        if (ret < 0) {
            KNET_ERR("Sprintf core list failed, ret %d", ret);
            return -1;
        }
        return 0;
    }
    // 单进程或者从进程
    ret = sprintf_s(coreList, MAX_STRVALUE_NUM, "%s,%d", coreListStr, dpdkCore);
    if (ret < 0) {
        KNET_ERR("Sprintf core list failed, ret %d", ret);
        return -1;
    }
    ret = sprintf_s(mainLcore, MAX_STRVALUE_NUM, "--main-lcore=%d", dpdkCore);
    if (ret < 0) {
        KNET_ERR("Sprintf main lcore failed, ret %d", ret);
        return -1;
    }

    return 0;
}

/**
 * @note 示例：argv的拼接后的内容格式等同如下所示
 * char *argv[8] = {
 *     KNET_LOG_MODULE_NAME,
 *     "-c3",
 *     "-n2",
 *     "--file-prefix=knet",
 *     "--proc-type=primary",
 *     "--socket-mem=1024",
 *     "-a0000:06:00.0",
 *     "" 或 "-dlibrte_net_hns3.so"
 * };
 */
static int32_t DpdkArgvAssign(char **argv, int procType)
{
    int dpdkCore = FindDpdkCore();
    if (dpdkCore == -1) {
        KNET_ERR("Dpdk core not found");
        return -1;
    }

    char coreList[MAX_STRVALUE_NUM] = {0};
    char mainLcore[MAX_STRVALUE_NUM] = {0};

    int32_t ret = GenerateDpdkCoreList(dpdkCore, procType, coreList, mainLcore);
    if (ret != 0) {
        KNET_ERR("Generate core list or main Lcore failed");
        return -1;
    }

    const char* dpdkArgs[KNET_DPDK_PRIM_ARGC] = {
        KNET_LOG_MODULE_NAME,
        "-l",
        coreList,
        "-n2",
        "--file-prefix=knet",
        KNET_GetCfg(CONF_DPDK_TELEMETRY)->intValue == 1 ? "--telemetry" : "--no-telemetry",
        (procType == KNET_PROC_TYPE_PRIMARY) ? "--proc-type=primary" : "--proc-type=secondary",
        mainLcore,
        KNET_GetCfg(CONF_DPDK_SOCKET_MEM)->strValue,
        KNET_GetCfg(CONF_DPDK_SOCKET_LIM)->strValue,
        KNET_GetCfg(CONF_DPDK_EXTERNAL_DRIVER)->strValue,
        KNET_GetCfg(CONF_DPDK_HUGE_DIR)->strValue,
        KNET_GetCfg(CONF_DPDK_BASE_VIRTADDR)->strValue,
        "-a",
        KNET_GetCfg(CONF_INTERFACE_BDF_NUMS)->strValueArr[0],
        // 只有bond场景下才需要添加其余网卡
        "-a",
        KNET_GetCfg(CONF_INTERFACE_BDF_NUMS)->strValueArr[1]
    };

    uint32_t dpdkArgNum = 0;
    // 非使能bond场景下，rte_eal_init参数只需要"-a"第一张网卡的BDF号
    dpdkArgNum = (KNET_GetCfg(CONF_INTERFACE_BOND_ENABLE)->intValue == 1) ?
        KNET_DPDK_PRIM_ARGC : (KNET_DPDK_PRIM_ARGC - 1 - 1);

    for (uint32_t i = 0; i < dpdkArgNum; ++i) {
        const char *dpdkArg = dpdkArgs[i];
        int32_t ret = memcpy_s((void *)argv[i], KNET_DPDK_ARG_MAX_LEN, (void *)dpdkArg, strlen(dpdkArgs[i]));
        if (ret != 0) {
            KNET_ERR("Memcpy dpdkArg \"%s\" failed, ret %d, ARG_MAX_LEN %u, dpdkArg_len %u",
                dpdkArg, ret, KNET_DPDK_ARG_MAX_LEN, strlen(dpdkArg));
            return -1;
        }
        KNET_INFO("Argv[%u] \"%s\", len %u", i, argv[i] == NULL ? "NULL" : argv[i], strlen(argv[i]));
    }
    return 0;
}

int32_t RteEalInit(int32_t argc, char **argv)
{
    uint32_t cpuNums = MAX_CPU_NUM;
    uint16_t cpus[MAX_CPU_NUM] = {0};

    /* 获取 业务线程绑核 cpu */
    uint64_t serviceTid = KNET_ThreadId();
    int32_t ret = KNET_GetThreadAffinity(serviceTid, cpus, &cpuNums);
    if (ret < 0) {
        KNET_ERR("Service cpu get failed, ret %d", ret);
        return -1;
    }
    KNET_GetCap(KNET_CAP_SYS_RAWIO | KNET_CAP_DAC_READ_SEARCH |
        KNET_CAP_IPC_LOCK | KNET_CAP_SYS_ADMIN | KNET_CAP_NET_RAW | KNET_CAP_DAC_OVERRIDE);
    ret = rte_eal_init(argc, argv);
    KNET_ClearCap(KNET_CAP_SYS_RAWIO | KNET_CAP_DAC_READ_SEARCH |
        KNET_CAP_IPC_LOCK | KNET_CAP_SYS_ADMIN | KNET_CAP_NET_RAW | KNET_CAP_DAC_OVERRIDE);
    if (ret < 0) {
        KNET_ERR("Rte eal init failed, ret %d", ret);
        return -1;
    }

    /* 重新绑定 业务线程cpu  */
    ret = KNET_SetThreadAffinity(serviceTid, cpus, cpuNums);
    if (ret < 0) {
        KNET_ERR("Service cpu set failed, ret %d", ret);
        return -1;
    }

    return 0;
}

static int32_t DpdkEalInit(int procType, int processMode)
{
    char *argv[KNET_DPDK_PRIM_ARGC] = {0};
    char argvArr[KNET_DPDK_PRIM_ARGC][KNET_DPDK_ARG_MAX_LEN + 1] = {0};  // +1因为要预留截断符

    for (uint32_t i = 0; i < KNET_DPDK_PRIM_ARGC; ++i) {
        argv[i] = argvArr[i];
    }

    int32_t ret = DpdkArgvAssign(argv, procType);
    if (ret != 0) {
        KNET_ERR("K-NET dpdk argv assign failed in procType %d, ret %d", procType, ret);
        return -1;
    }

    int32_t argc = KNET_DPDK_PRIM_ARGC;
    ret = RteEalInit(argc, argv);
    if (ret < 0) {
        KNET_ERR("The rte eal init failed in procType %d, ret %d", procType, ret);
        return -1;
    }

    return 0;
}

static void DpdkResourceCheck(void)
{
    // 检查dpdk堆内存使用情况
    struct rte_malloc_socket_stats sockStats = {0};
    for (uint32_t i = 0; i < rte_socket_count(); i++) {
        int sock = rte_socket_id_by_idx(i);
        int ret = rte_malloc_get_socket_stats(sock, &sockStats);
        if (ret != 0) {
            KNET_WARN("Get socket %u/%d stats failed, ret %d", i, sock, ret);
            continue;
        }
        KNET_INFO("Socket %d: total_size: %zu, total_used: %zu, total_free: %zu, greatest_free_size:%zu",
            sock, sockStats.heap_totalsz_bytes, sockStats.heap_allocsz_bytes, sockStats.heap_freesz_bytes,
            sockStats.greatest_free_size);

        double memUsage = (double)sockStats.heap_allocsz_bytes / (double)sockStats.heap_totalsz_bytes;
        if (memUsage > MEMORY_THRESHOLD_RATIO) {
            KNET_WARN("Socket %d, Memory usage is too high %lf", sock, memUsage);
        }
    }

    DpdkMbufPoolUsageCheck();
}

int32_t DpdkDfxInit(int procType, int processMode)
{
    int32_t ret = 0;
    /* 在主进程或者单进程中调用 */
    if (procType == KNET_PROC_TYPE_PRIMARY) {
        g_pdumpRequestMz = KNET_MultiPdumpInit();
        if (g_pdumpRequestMz == NULL) {
            KNET_ERR("K-NET multi pkt dump init failed");
            return -1;
        }
    }

    /* 在主进程或者单进程中调用 */
    if (procType == KNET_PROC_TYPE_PRIMARY && KNET_GetCfg(CONF_DPDK_TELEMETRY)->intValue == 1) {
        ret = KNET_InitDpdkTelemetry();
        if (ret < 0) {
            KNET_ERR("K-NET init dpdk telemetry failed, ret %d", ret);
            return -1;
        }
    }
    return 0;
}

// 内部接口,调用前入参已确保可靠
static int CheckDpdkDevBind(const char *interfaceName)
{
    int32_t ret;
    char absPath[PATH_MAX + 1] = {0};
    char path[PATH_MAX + 1] = {0};
    char *retPath = NULL;
 
    ret = snprintf_truncated_s(path, sizeof(path), SYSFS_PCI_DEVICES "/%s/net", interfaceName);
    if (ret < 0) {
        KNET_ERR("Path snprintf_truncated_s failed, interfaceName %s, ret %d", interfaceName, ret);
        return -1;
    }
 
    // 校验路径 判断是否Bind
    retPath = realpath(path, absPath);
    if (retPath == NULL) {
        KNET_INFO("The dpdk-devbind %s", interfaceName);
        return 0;
    }
 
    return -1;
}

KNET_STATIC int32_t BondSlavesInit(int procType)
{
    if (procType == KNET_PROC_TYPE_SECONDARY) {
        return 0;
    }

    if (KnetGetDpdkPortIdAndInit(KNET_GetCfg(CONF_INTERFACE_BDF_NUMS)->strValueArr[0],
        &g_dpdkDevInfo.netdevCtx.slavePortIds[0], procType) != 0 ||
        KnetGetDpdkPortIdAndInit(KNET_GetCfg(CONF_INTERFACE_BDF_NUMS)->strValueArr[1],
        &g_dpdkDevInfo.netdevCtx.slavePortIds[1], procType) != 0) {
        return -1;
    }

    return 0;
}

KNET_STATIC int32_t BondInit(int procType)
{
    if (procType == KNET_PROC_TYPE_SECONDARY) {
        return 0;
    }

    int bondPortID = KNET_BondCreate(g_dpdkDevInfo.netdevCtx.slavePortIds, KNET_BOND_SLAVE_NUM);
    if (bondPortID < 0) {
        KNET_ERR("K-NET create dpdk bond failed");
        return -1;
    }
    g_dpdkDevInfo.netdevCtx.bondPortId = bondPortID;

    // 适配tm280网卡,下发配置前需要关闭从端口
    if (rte_eth_dev_stop(g_dpdkDevInfo.netdevCtx.slavePortIds[0]) != 0 ||
        rte_eth_dev_stop(g_dpdkDevInfo.netdevCtx.slavePortIds[1]) != 0) {
        KNET_ERR("Stop slave ports before init bond port failed");
        return -1;
    }

    if (KnetInitDpdkPort(bondPortID, procType, DPDK_PORT_BOND) != 0) {
        KNET_ERR("K-NET init dpdk bond port failed");
        return -1;
    }
    
    return KNET_BondWaitSlavesReady(bondPortID);
}

static void XmitPortIdSet(int procType)
{
    /* 从进程bond场景，打桩bond port id。 todo：后续bondPortId从主进程获取  */
    if (procType == KNET_PROC_TYPE_SECONDARY && KNET_GetCfg(CONF_INTERFACE_BOND_ENABLE)->intValue == 1) {
        g_dpdkDevInfo.netdevCtx.bondPortId = KNET_SECONDARY_BOND_PORT_ID;
        g_dpdkDevInfo.netdevCtx.xmitPortId = g_dpdkDevInfo.netdevCtx.bondPortId;
        KNET_INFO("Set secondary bondPortId %hu", g_dpdkDevInfo.netdevCtx.bondPortId);
        return;
    }

    if (KNET_GetCfg(CONF_INTERFACE_BOND_ENABLE)->intValue == 1) {
        g_dpdkDevInfo.netdevCtx.xmitPortId = g_dpdkDevInfo.netdevCtx.bondPortId;
        return;
    }
    g_dpdkDevInfo.netdevCtx.xmitPortId = g_dpdkDevInfo.netdevCtx.portId;
}

KNET_STATIC int DelayCpdRingCreate(int ringId)
{
    char name[MAX_CPD_NAME_LEN] = {0};
    int ret = snprintf_s(name, MAX_CPD_NAME_LEN, MAX_CPD_NAME_LEN - 1, "cpdtaprx%d", ringId);
    if (ret < 0) {
        KNET_ERR("Cpd ring %d name get error, ret %d", ringId, ret);
        return -1;
    }
    if (rte_ring_create(name, DEFAULT_RING_SIZE, rte_socket_id(), 0) == NULL) {
        KNET_ERR("Cpd ring %d creation failed, errno %d", ringId, rte_errno);
        return -1;
    }

    return 0;
}

int DelayCpdRingInit(int procType, int processMode)
{
    if (KNET_GetCfg(CONF_HW_BIFUR_ENABLE)->intValue != KERNEL_FORWARD_ENABLE) {
        return 0;
    }

    int ret = 0;
    // 需要停队列时，单进程与多进程主进程均为KNET_PROC_TYPE_PRIMARY，进行停队列操作
    if (KNET_GetCfg(CONF_INNER_NEED_STOP_QUEUE)->intValue == KNET_STOP_QUEUE
        && procType == KNET_PROC_TYPE_PRIMARY) {
        for (int qid = 0; qid < KNET_GetCfg(CONF_TCP_MAX_WORKER_NUM)->intValue; qid++) {
            ret = rte_eth_dev_rx_queue_stop(g_dpdkDevInfo.netdevCtx.portId, qid);
            if (ret < 0 && ret != -ENOTSUP) {  // 忽略不支持的错误
                KNET_ERR("Warning: Failed to stop RX queue %u: %s", qid, rte_strerror(-ret));
                return -1;
            }
        }
    }

    if (processMode != KNET_RUN_MODE_SINGLE) {
        return 0;
    }

    int ringNum = KNET_GetCfg(CONF_COMMON_CTRL_VCPU_NUMS)->intValue *
        KNET_GetCfg(CONF_COMMON_CTRL_RING_PER_VCPU)->intValue;
    for (int i = 0; i < ringNum; i++) {
        if (DelayCpdRingCreate(i) != 0) {
            return -1;
        }
        g_delayCpdRxRingInfo.cpdRingNum += 1;
    }
    return 0;
}

int32_t DpdkPortInitNoBifur(void)
{
    int32_t ret = 0;
    // 使用sp网卡流量分叉vf方案，若K-NET bifur_enable = 0且在模板3 需要校验是否bind配置文件中的bdf网口
    // 否则在物理机驱动会分配一个vf，等同于bind，逻辑错误
    if (KNET_GetCfg(CONF_HW_BIFUR_ENABLE)->intValue != BIFUR_ENABLE) {
        if (KNET_GetCfg(CONF_INTERFACE_BOND_ENABLE)->intValue == 0) {
            ret = CheckDpdkDevBind(KNET_GetCfg(CONF_INTERFACE_BDF_NUMS)->strValueArr[0]);
        } else {
            if (CheckDpdkDevBind(KNET_GetCfg(CONF_INTERFACE_BDF_NUMS)->strValueArr[0]) != 0 ||
                CheckDpdkDevBind(KNET_GetCfg(CONF_INTERFACE_BDF_NUMS)->strValueArr[1]) != 0) {
                ret = -1;
            }
        }

        if (ret != 0) {
            KNET_ERR("bifur_enable is not set 1 and bdf is not bind, ret %d", ret);
            return -1;
        }
    }

    return 0;
}

int32_t DpdkPortInit(int procType)
{
    int32_t ret = DpdkPortInitNoBifur();
    if (ret != 0) {
        return -1; // 日志在函数内部打印
    }
    
    // bond_enable配置项为0时,只需初始化bdf_num1端口;为1时,初始化两个bdf_num端口以及bond端口
    if (KNET_GetCfg(CONF_INTERFACE_BOND_ENABLE)->intValue == 0) {
        ret = KnetGetDpdkPortIdAndInit(KNET_GetCfg(CONF_INTERFACE_BDF_NUMS)->strValueArr[0],
            &g_dpdkDevInfo.netdevCtx.portId, procType);
        if (ret < 0) {
            return -1; // 日志在函数内部打印
        }
        return 0;
    }
    ret = BondSlavesInit(procType);
    if (ret < 0) {
        KNET_ERR("K-NET bond init slaves failed");
        return -1;
    }

    ret = BondInit(procType);
    if (ret < 0) {
        KNET_ERR("K-NET bond init failed");
        return -1;
    }

    return 0;
}

int32_t InitDpdkPart(int procType, int processMode)
{
    /* DPDK args与rte_eal初始化 */
    int32_t ret = DpdkEalInit(procType, processMode);
    if (ret < 0) {
        KNET_ERR("K-NET dpdk components init failed");
        return -1;
    }

    ret = DpdkDfxInit(procType, processMode);
    if (ret < 0) {
        KNET_ERR("K-NET dpdk dfx init failed");
        return -1;
    }

    /* KNET模块初始化（mbuf和内存管理依赖此初始化） */
    uint32_t uRet = KNET_PktModInit();
    if (uRet != 0) {
        KNET_ERR("K-NET pkt mod init failed");
        return -1;
    }

    ret = MbufMempoolCreate();
    if (ret < 0) {
        KNET_ERR("K-NET mbuf mempool create failed");
        return -1;
    }

    ret = DpdkPortInit(procType);
    if (ret < 0) {
        KNET_ERR("K-NET dpdk port init failed");
        return -1;
    }

    return 0;
}

int32_t KNET_InitDpdk(int procType, int processMode)
{
    int32_t ret = InitDpdkPart(procType, processMode);
    if (ret < 0) {
        return -1; // 日志在函数内部处理
    }

    // 在单进程与从进程中进行KNET_HashTblInit
    if (processMode == KNET_RUN_MODE_SINGLE || procType == KNET_PROC_TYPE_SECONDARY) {
        ret = KNET_HashTblInit();
        if (ret < 0) {
            KNET_ERR("K-NET init hashtabl failed, ret %d", ret);
            return -1;
        }
    }

    ret = DelayCpdRingInit(procType, processMode);
    if (ret < 0) {
        KNET_ERR("K-NET delay Cpd init failed");
        return -1;
    }

    DpdkResourceCheck();

    XmitPortIdSet(procType);

    g_dpdkInited = true;

    ret = KNET_InitTrans(procType);
    if (ret != 0) {
        KNET_ERR("K-NET init Trans failed");
        return -1;
    }

    return 0;
}

int FreeDelayCpdRing(int procType, int processMode)
{
    if (KNET_GetCfg(CONF_HW_BIFUR_ENABLE)->intValue != KERNEL_FORWARD_ENABLE) {
        return 0;
    }
    if (g_dpdkInited == false) {
        KNET_ERR("Dpdk not inited, no need to free ring");
        return -1;
    }

    if (processMode == KNET_RUN_MODE_MULTIPLE) {
        return 0;
    }

    /* 单进程释放转发内核的队列 */
    int cpdRingNums = KNET_GetCfg(CONF_COMMON_CTRL_VCPU_NUMS)->intValue *
        KNET_GetCfg(CONF_COMMON_CTRL_RING_PER_VCPU)->intValue;
    for (int i = 0; i < cpdRingNums; i++) {
        char name[MAX_CPD_NAME_LEN] = {0};
        int ret = snprintf_s(name, MAX_CPD_NAME_LEN, MAX_CPD_NAME_LEN - 1, "cpdtaprx%d", i);
        if (ret < 0) {
            KNET_ERR("Ring name %s get error", name);
            return -1;
        }
        struct rte_ring *cpdTapRing = rte_ring_lookup(name);
        if (cpdTapRing == NULL) {
            KNET_ERR("Cpd tap %s does not exist", name);
            return -1;
        }
        rte_ring_free(cpdTapRing);
    }
    return 0;
}

int32_t UninitDpdkDfx(int procType, int processMode)
{
    if (procType == KNET_PROC_TYPE_PRIMARY && KNET_GetCfg(CONF_DPDK_TELEMETRY)->intValue == 1) {
        KNET_UninitDpdkTelemetry();
    }

    int32_t ret = 0;
    if (procType == KNET_PROC_TYPE_PRIMARY && g_dpdkInited == true) {
        ret = KNET_MultiPdumpUninit(g_pdumpRequestMz);
        if (ret < 0) {
            KNET_ERR("rte dump clean up failed, ret %d", ret);
        }
        g_pdumpRequestMz = NULL;
    }
    return ret;
}

int32_t UninitDpdkPort(int procType)
{
    int32_t ret = 0;
    if (KNET_GetCfg(CONF_INTERFACE_BOND_ENABLE)->intValue == 0) {
        ret = KnetUninitUnbondDpdkPort(g_dpdkDevInfo.netdevCtx.portId, procType);
        if (ret != 0) {
            KNET_ERR("K-NET uninit unbonded dpdk port failed, ret %d", ret);
        }
        g_dpdkDevInfo.netdevCtx.portId = 0;
        return ret;
    }
    ret = KNET_BondUninit(procType);
    if (ret != 0) {
        KNET_ERR("K-NET uninit bond port failed, ret %d", ret);
    }
    g_dpdkDevInfo.netdevCtx.bondPortId = 0;
    g_dpdkDevInfo.netdevCtx.slavePortIds[0] = 0;
    g_dpdkDevInfo.netdevCtx.slavePortIds[1] = 0;
    return ret;
}

int32_t KNET_UninitDpdk(int procType, int processMode)
{
    int32_t flag = 0;
    int32_t ret = 0;
    if (UninitDpdkPort(procType) != 0) {
        KNET_ERR("K-NET uninit dpdk port failed");
        flag = 1;
    }

    if (processMode == KNET_RUN_MODE_SINGLE || procType == KNET_PROC_TYPE_SECONDARY)  {
        KNET_PktBatchFree(); // 必须要在数据面线程退出之后才能释放
        KNET_HashTblDeinit(); // 父进程退出之后释放资源
    }
    MbufMempoolDestroy();

    ret = UninitDpdkDfx(procType, processMode);
    if (ret != 0) {
        KNET_ERR("K-NET dpdk dfx uninit failed");
        flag = 1;
    }

    ret = KNET_UninitTrans(procType);
    if (ret != 0) {
        flag = 1;
        KNET_WARN("K-NET uninit trans failed");
    }

    ret = FreeDelayCpdRing(procType, processMode);
    if (ret != 0) {
        flag = 1;
        KNET_ERR("Delay cpd handle resource uninit failed");
    }

    ret = rte_eal_cleanup();
    if (ret != 0) {
        KNET_ERR("rte eal clean up failed, ret %d", ret);
        flag = 1;
    }

    return flag == 0 ? 0 : -1;
}
#ifdef __cplusplus
}
#endif /* __cpluscplus */