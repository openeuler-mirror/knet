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


#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "cJSON.h"

#include "knet_log.h"
#include "knet_utils.h"
#include "knet_config_hw_scan.h"
#include "knet_config_core_queue.h"
#include "knet_config_setter.h"
#include "knet_config_rpc.h"
#include "knet_config.h"

#define MAX_CFG_SIZE 8000
#define SINGLE_MODE_QID 0
#define KNET_CFG_FILE "/etc/knet/knet_comm.conf"
#define KNET_MBUF_BATCH 2048
#define INVALID_CONF_VALUE 0xFFFFFFFF
#define KERNEL_PORT_RANGE_FILE "/proc/sys/net/ipv4/ip_local_port_range"
#define MAX_FDIR_NUM 1024 // 同步驱动流表数
#define KERNEL_PORT_RANGE_LEN 2

char *g_moduleName[CONF_MAX] = {"common", "interface", "hw_offload", "proto_stack", "dpdk", ""};

int g_moduleConfMin[CONF_MAX] = {CONF_COMMON_MIN, CONF_INTERFACE_MIN, CONF_HW_MIN, CONF_TCP_MIN,
    CONF_DPDK_MIN, CONF_INNER_MIN};
int g_moduleConfMax[CONF_MAX] = {CONF_COMMON_MAX, CONF_INTERFACE_MAX, CONF_HW_MAX, CONF_TCP_MAX,
    CONF_DPDK_MAX, CONF_INNER_MAX};

struct ConfKeyHandle {
    enum KNET_ConfKey key;
    char *name;
    union KNET_CfgValue value;
    FuncSetter setter;
    union KnetCfgValidateParam validateParam;
};

struct ConfKeyHandle g_commonConfHandler[CONF_COMMON_MAX - CONF_COMMON_MIN] = {
    {CONF_COMMON_MODE, "mode", {0}, IntSetter, {.intValue = {.min = 0, .max = 1}}},
    {CONF_COMMON_LOG_LEVEL, "log_level", {.strValue = "WARNING"}, LogLevelSetter, {}},
    {CONF_COMMON_CTRL_VCPU_NUMS, "ctrl_vcpu_nums", {1}, IntSetter, {.intValue = {.min = 1, .max = MAX_VCPU_NUMS}}},
    {CONF_COMMON_CTRL_RING_PER_VCPU, "ctrl_ring_per_vcpu", {1}, CtrVcpuRingSetter,
        {.intValue = {.min = 1, .max = MAX_RING_PER_VCPU}}},
    {CONF_COMMON_CTRL_VCPU_IDS, "ctrl_vcpu_ids", {.intValueArr = {0}}, CtrlVcpuArraySetter,
        {.intValue = {.min = 0, .max = 127}}},
    {CONF_COMMON_ZERO_COPY, "zcopy_enable", {0}, IntSetter, {.intValue = {.min = 0, .max = 1}}},
    {CONF_COMMON_COTHREAD, "cothread_enable", {0}, IntSetter, {.intValue = {.min = 0, .max = 1}}},
};

struct ConfKeyHandle g_interfaceConfHandler[CONF_INTERFACE_MAX - CONF_INTERFACE_MIN] = {
    {CONF_INTERFACE_BOND_ENABLE, "user_bond_enable", {0}, IntSetter, {.intValue = {.min = 0, .max = 1}}},
    {CONF_INTERFACE_BOND_MODE, "user_bond_mode", {4}, IntSetter, {.intValue = {.min = 4, .max = 4}}},
    {CONF_INTERFACE_BDF_NUMS, "bdf_nums", {.strValueArr = {"0000:06:00.0", "\0"}}, BdfNumsSetter, {}},
    {CONF_INTERFACE_MAC, "mac", {.strValue = "52:54:00:2e:1b:a0"}, MacSetter, {}},
    {CONF_INTERFACE_IP, "ip", {.strValue = "192.168.1.6"}, IpSetter, {}},
    {CONF_INTERFACE_NETMASK, "netmask", {.strValue = "255.255.255.0"}, NetMaskSetter, {}},
    {CONF_INTERFACE_GATEWAY, "gateway", {.strValue = "0.0.0.0"}, IpSetter, {}},
    {CONF_INTERFACE_MTU, "mtu", {1500}, IntSetter, {.intValue = {.min = 256, .max = 9600}}},
};

struct ConfKeyHandle g_hwConfHandler[CONF_HW_MAX - CONF_HW_MIN] = {
    {CONF_HW_TSO, "tso", {0}, IntSetter, {.intValue = {.min = 0, .max = 1}}},
    {CONF_HW_LRO, "lro", {0}, IntSetter, {.intValue = {.min = 0, .max = 1}}},
    {CONF_HW_TCP_CHECKSUM, "tcp_checksum", {0}, IntSetter, {.intValue = {.min = 0, .max = 1}}},
    // IntSetter校验范围是[min, max], 此处实际为[0, 2]
    {CONF_HW_BIFUR_ENABLE, "bifur_enable", {0}, IntSetter, {.intValue = {.min = 0, .max = BIFUR_CFG_MAX - 1}}},
};

struct ConfKeyHandle g_tcpConfHandler[CONF_TCP_MAX - CONF_TCP_MIN] = {
    {CONF_TCP_MAX_MBUF, "max_mbuf", {20480}, IntSetter, {.intValue = {.min = 8192, .max = (INT32_MAX/2)}}},
    {CONF_TCP_MAX_WORKER_NUM, "max_worker_num", {1}, IntSetter, {.intValue = {.min = 1, .max = 32}}},
    {CONF_TCP_MAX_ROUTE, "max_route", {1024}, IntSetter, {.intValue = {.min = 1, .max = 100000}}},
    // max_arp最小值8受限于DPDK的最小值RTE_HASH_BUCKET_ENTRIES
    {CONF_TCP_MAX_ARP, "max_arp", {1024}, IntSetter, {.intValue = {.min = 8, .max = 8192}}},
    {CONF_TCP_MAX_TCPCB, "max_tcpcb", {4096}, IntSetter, {.intValue = {.min = 32, .max = 1000000}}},
    {CONF_TCP_MAX_UDPCB, "max_udpcb", {4096}, IntSetter, {.intValue = {.min = 32, .max = 1000000}}},
    {CONF_TCP_TCP_SACK, "tcp_sack", {1}, IntSetter, {.intValue = {.min = 0, .max = 1}}},
    {CONF_TCP_TCP_DACK, "tcp_dack", {1}, IntSetter, {.intValue = {.min = 0, .max = 1}}},
    {CONF_TCP_MSL_TIME, "msl_time", {30}, IntSetter, {.intValue = {.min = 1, .max = 30}}},
    {CONF_TCP_FIN_TIMEOUT, "fin_timeout", {600}, IntSetter, {.intValue = {.min = 1, .max = 600}}},
    {CONF_TCP_MIN_PORT, "min_port", {49152}, IntSetter, {.intValue = {.min = 1, .max = 49152}}},
    {CONF_TCP_MAX_PORT, "max_port", {65535}, IntSetter, {.intValue = {.min = 50000, .max = 65535}}},
    {CONF_TCP_MAX_SENDBUF, "max_sendbuf", {10485760}, IntSetter, {.intValue = {.min = 8192, .max = INT32_MAX}}},
    {CONF_TCP_DEF_SENDBUF, "def_sendbuf", {8192}, IntSetter, {.intValue = {.min = 8192, .max = INT32_MAX}}},
    {CONF_TCP_MAX_RECVBUF, "max_recvbuf", {10485760}, IntSetter, {.intValue = {.min = 8192, .max = INT32_MAX}}},
    {CONF_TCP_DEF_RECVBUF, "def_recvbuf", {8192}, IntSetter, {.intValue = {.min = 8192, .max = INT32_MAX}}},
    {CONF_TCP_TCP_COOKIE, "tcp_cookie", {0}, IntSetter, {.intValue = {.min = 0, .max = 1}}},
    {CONF_TCP_REASS_MAX, "reass_max", {1000}, IntSetter, {.intValue = {.min = 1, .max = 4096}}},
    {CONF_TCP_REASS_TIMEOUT, "reass_timeout", {30}, IntSetter, {.intValue = {.min = 1, .max = 30}}},
    {CONF_TCP_SYNACK_RETRIES, "synack_retries", {5}, IntSetter, {.intValue = {.min = 1, .max = 255}}},
    {CONF_TCP_SGE_LEN, "zcopy_sge_len", {65535}, IntSetter, {.intValue = {.min = 0, .max = 524288}}},
    {CONF_TCP_SGE_NUM, "zcopy_sge_num", {8192}, IntSetter, {.intValue = {.min = 8192, .max = (INT32_MAX/2)}}},
    {CONF_TCP_EPOLL_DATA, "epoll_data", {.strValue = "0"}, Uint64Setter, {}},
};

struct ConfKeyHandle g_dpdkConfHandler[CONF_DPDK_MAX - CONF_DPDK_MIN] = {
    {CONF_DPDK_CORE_LIST_GLOBAL, "core_list_global", {.strValue = "1"}, CoreListGlobalSetter, {}},
    {CONF_DPDK_QUEUE_NUM, "queue_num", {1}, IntSetter, {.intValue = {.min = 1, .max = 64}}},
    {CONF_DPDK_TX_CACHE_SIZE, "tx_cache_size", {256}, IntSetter, {.intValue = {.min = 256, .max = 16384}}},
    {CONF_DPDK_RX_CACHE_SIZE, "rx_cache_size", {256}, IntSetter, {.intValue = {.min = 256, .max = 16384}}},
    {CONF_DPDK_SOCKET_MEM,
        "socket_mem",
        {.strValue = "--socket-mem=1024"},
        StringSetter,
        {.pattern = "^--socket-mem=[0-9]+(,[0-9]+)*$"}},
    {CONF_DPDK_SOCKET_LIM,
        "socket_limit",
        {.strValue = "--socket-limit=1024"},
        StringSetter,
        {.pattern = "^--socket-limit=[0-9]+(,[0-9]+)*$"}},
    {CONF_DPDK_EXTERNAL_DRIVER,
        "external_driver",
        {.strValue = "-dlibrte_net_sp600.so"},
        StringSetter,
        {.pattern = "^(-d[^/]+\\.so(\\.[0-9]+)*)?$"}},
    {CONF_DPDK_TELEMETRY, "telemetry", {1}, IntSetter, {.intValue = {.min = 0, .max = 1}}},
    {CONF_DPDK_HUGE_DIR,
        "huge_dir",
        {.strValue = ""},
        StringSetter,
        {.pattern = "^(--huge-dir=/([a-zA-Z0-9_/-]+)*)?"}},
    {CONF_DPDK_BASE_VIRTADDR,
        "base-virtaddr",
        {.strValue = ""},
        StringSetter,
        {.pattern = "^(--base-virtaddr=0x[0-9a-fA-F]+|)$"}},
};

struct ConfKeyHandle g_innerConfHandler[CONF_INNER_MAX - CONF_INNER_MIN] = {
    {CONF_INNER_QID, "queue_id", {0}, NULL, {.intValue = {.min = 0, .max = 31}}},
    {CONF_INNER_CORE_LIST, "core_list", {.strValue = "1"}, NULL, {}},
    {CONF_INNER_PROC_TYPE, "proc_type", {0}, NULL, {.intValue = {.min = 0, .max = 1}}},
    {CONF_INNER_QUEUE_NUM, "inner_queue_num", {1}, NULL, {.intValue = {.min = 1, .max = 64}}},
    // 1表示不下区间流表，仅在单进程+cothread时更新设置为区间步长
    {CONF_INNER_PORT_STEP, "port_step", {1}, NULL, {.intValue = {.min = 1, .max = 4096}}},
    // 0表示TM280 1表示SP
    {CONF_INNER_HW_TYPE, "hw_type", {0}, NULL, {.intValue = {.min = 0, .max = 1}}},
    // 0表示不需要停止队列，1表示需要停止队列
    {CONF_INNER_NEED_STOP_QUEUE, "need_stop_queue", {0}, NULL,
        {.intValue = {.min = 0, .max = 1}}},
    // 内核bond口名称，如果没检测到为"\0"
    {CONF_INNER_KERNEL_BOND_NAME, "bond_name", {.strValue = "\0"}, NULL, {}},
};

struct ConfKeyHandle *g_confHandleMap[CONF_MAX] = {
    g_commonConfHandler,
    g_interfaceConfHandler,
    g_hwConfHandler,
    g_tcpConfHandler,
    g_dpdkConfHandler,
    g_innerConfHandler,
};

KNET_STATIC char *g_primaryCfg = NULL; // 用于校验配置文件一致性, 主进程留存启动时配置文件

static int g_needFlowRule = -1; // 用于判断当前cfg配置是否需要下发流表 0代表不需要，1代表需要

KNET_STATIC char *GetKnetCfgContent(const char *fileName)
{
    char *cfgContent = NULL;
    FILE *file = fopen(fileName, "r");
    if (file == NULL) {
        KNET_ERR("Open K-NET conf file failed, errno %d", errno);
        return cfgContent;
    }
    cfgContent = (char *)malloc(MAX_CFG_SIZE);
    if (cfgContent == NULL) {
        KNET_ERR("Malloc conf size %d failed, errno %d", MAX_CFG_SIZE, errno);
        goto END;
    }
    (void)memset_s(cfgContent, MAX_CFG_SIZE, 0, MAX_CFG_SIZE);
    size_t readSize = fread(cfgContent, 1, MAX_CFG_SIZE, file);
    if (readSize >= MAX_CFG_SIZE) {
        free(cfgContent);
        cfgContent = NULL;
        KNET_ERR("Fread failed, read larger than %d, errno %d", MAX_CFG_SIZE, errno);
        goto END;
    }

    if (ferror(file) != 0) {
        // 读取过程发生错误
        free(cfgContent);
        cfgContent = NULL;
        KNET_ERR("Error reading file, errno %d", errno);
        goto END;
    }

END:
    (void)fclose(file);

    return cfgContent;
}

/*
 * @brief 配置配置项的值，模块内接口，且性能敏感，不检查参数有效性。建议使用前确认参数有效性。
 */
void SetCfgValue(enum KNET_ConfKey key, const union KNET_CfgValue *value)
{
    g_confHandleMap[key >> MAX_CONF_NUM_PER_INDEX_BITS][key & CONF_INDEX_LOWER_MASK].value = *value;
}

KNET_STATIC void DelKnetCfgContent(char *cfgCtx)
{
    free(cfgCtx);
}
static int GetCfgValue(enum KNET_ConfModule module, cJSON *json)
{
    // 内部配置项，不需要从外部获取。
    if (g_moduleName[module] == NULL || g_moduleName[module][0] == '\0') {
        return 0;
    }

    cJSON *subJson = cJSON_GetObjectItem(json, g_moduleName[module]);
    if (subJson == NULL) {
        KNET_ERR("cJSON get sub json %s failed", g_moduleName[module]);
        return -1;
    }

    int ret = 0;
    struct ConfKeyHandle *confItem = NULL;
    for (int key = 0; key < g_moduleConfMax[module] - g_moduleConfMin[module]; ++key) {
        confItem = &g_confHandleMap[module][key];
        // 内部配置项，不需要从外部获取。
        if (confItem->setter == NULL) {
            continue;
        }

        cJSON *jsonValue = cJSON_GetObjectItem(subJson, confItem->name);
        ret = confItem->setter(jsonValue, &confItem->value, &confItem->validateParam);
        if (ret != 0) {
            KNET_ERR("cJSON get value %s:%s failed, err:%d", g_moduleName[module], confItem->name, ret);
            break;
        }
    }

    if (ret != 0) {
        return -1;
    }

    return 0;
}

/**
 * @brief 设置区间流表的区间步长，仅在单进程+cothread时需要更新，其他场景为默认值1：不下区间流表
 * 计算公式为：[可用端口数*2/流表项数， 可用端口数/worker数/2]，分别计算边界是2的几次幂次，计算得到幂次的中点n。在计算2的n次幂作为步长
 * 并需要同步更新下发给协议栈的可用端口的边界值SetCfgPort()，使双端的低n位为0
 */
void SetPortStepSize(void)
{
    union KNET_CfgValue portStep = {0};
    int minPort = KNET_GetCfg(CONF_TCP_MIN_PORT)->intValue;
    int maxPort = KNET_GetCfg(CONF_TCP_MAX_PORT)->intValue;
    unsigned int portSize = (unsigned)(maxPort - minPort + 1);
    unsigned int stepLeft = portSize * 2 / MAX_FDIR_NUM;
    unsigned int stepLeftPower = 0;
    while (stepLeft > 1) {
        stepLeft >>= 1;
        stepLeftPower++;
    }

    unsigned int stepRight = (portSize / (unsigned)KNET_GetCfg(CONF_TCP_MAX_WORKER_NUM)->intValue) / 2;
    unsigned int stepRightPower = 0;
    while (stepRight > 1) {
        stepRight >>= 1;
        stepRightPower++;
    }

    unsigned int powerMid = (stepLeftPower + stepRightPower) / 2;
    union KnetCfgValidateParam portStepItem =
        g_confHandleMap[CONF_INNER][CONF_INNER_PORT_STEP-CONF_INNER_MIN].validateParam;
    unsigned int portStepItemMax = (unsigned)portStepItem.intValue.max;
    unsigned int maxStepPower = 0;
    while (portStepItemMax > 1) {
        portStepItemMax >>= 1;
        maxStepPower++;
    }
    if (maxStepPower < powerMid) {
        portStep.intValue = portStepItem.intValue.max;
    } else {
        portStep.intValue = 1 << powerMid;
    }
    SetCfgValue(CONF_INNER_PORT_STEP, &portStep);
    return;
}
 
/**
 * @brief 获取到步长后更新给协议栈的port边界值
 *
 */
void SetCfgDpNewPort(void)
{
    unsigned int minPort = (unsigned)KNET_GetCfg(CONF_TCP_MIN_PORT)->intValue;
    unsigned int portStep = (unsigned)KNET_GetCfg(CONF_INNER_PORT_STEP)->intValue;
    int flag = 0;
    unsigned int minPortLeftBoundary = minPort & ~(portStep - 1);
    if (minPort > minPortLeftBoundary) {
        flag = 1;
        union KNET_CfgValue minPortCfg = {0};
        minPortCfg.intValue = (int)(minPortLeftBoundary + portStep); // 此值不会超过minport的最大值49152
        SetCfgValue(CONF_TCP_MIN_PORT, &minPortCfg);
    }
 
    unsigned int maxPort = (unsigned)KNET_GetCfg(CONF_TCP_MAX_PORT)->intValue;
    unsigned int maxPortLeftBoundary = maxPort & ~(portStep - 1);
    if (maxPort > maxPortLeftBoundary) {
        flag = 1;
        union KNET_CfgValue maxPortCfg = {0};
        maxPortCfg.intValue = (int)maxPortLeftBoundary; // 此值小于maxPort可以直接转换
        SetCfgValue(CONF_TCP_MAX_PORT, &maxPortCfg);
    }
    if (flag == 1) {
        KNET_INFO("Update port boundary, min_port %d, max_port %d", KNET_GetCfg(CONF_TCP_MIN_PORT)->intValue,
            KNET_GetCfg(CONF_TCP_MAX_PORT)->intValue);
    }
}

/**
 * @brief 确保内核和knet端口不会相互冲突
 *
 * @return int 0: 成功，-1：失败
 */
int CheckLocalPort(void)
{
    int kernelRange[KERNEL_PORT_RANGE_LEN] = {0}; // 存储内核port范围
    FILE *f = fopen(KERNEL_PORT_RANGE_FILE, "r");
    if (f == NULL) {
        KNET_ERR("K-NET open local port range failed, errno %d", errno);
        return -1;
    }
    int ret = fscanf_s(f, "%d %d", &kernelRange[0], &kernelRange[1]);
    if (ret != KERNEL_PORT_RANGE_LEN) {
        KNET_ERR("K-NET fscanf local port range failed, errno %d", errno);
        goto out;
    }
    int minPort = KNET_GetCfg(CONF_TCP_MIN_PORT)->intValue;
    int maxPort = KNET_GetCfg(CONF_TCP_MAX_PORT)->intValue;
    if (!(kernelRange[1] <= minPort || kernelRange[0] >= maxPort)) {
        KNET_ERR("K-NET port range [%d, %d], which may conflict with Kernel local port range [%d, %d]."
            "Please modify knet_comm.conf or kernel port range!",
            minPort, maxPort, kernelRange[0], kernelRange[1]);
        goto out;
    }

    (void)fclose(f);
    return 0;
 
out:
    (void)fclose(f);
    return -1;
}

static int SetSingleModeLocalCfgValue(void)
{  // 处于单进程模式时core_list设置为core_list_global
    union KNET_CfgValue qId = {0};
    qId.intValue = SINGLE_MODE_QID;

    SetCfgValue(CONF_INNER_QID, &qId);
    SetCfgValue(CONF_INNER_QUEUE_NUM, KNET_GetCfg(CONF_DPDK_QUEUE_NUM));
    // 不开启共线程时，core_list设置为core_list_global，开启为默认值
    if (KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue == 0) {
        SetCfgValue(CONF_INNER_CORE_LIST, KNET_GetCfg(CONF_DPDK_CORE_LIST_GLOBAL));
    } else { // single + cothread 下主动建链才会下区间流表，在初始化时进行校验
        SetPortStepSize();
        SetCfgDpNewPort();
        return CheckLocalPort();
    }
    return 0;
}

KNET_STATIC int SetMultiModeLocalCfgValue(enum KNET_ProcType procType)
{
    union KNET_CfgValue queue_num = {0};

    if (procType == KNET_PROC_TYPE_PRIMARY) {  // 主进程不分配queue id
        /* 多进程主进程的队列个数同worker个数保持一致 */
        queue_num.intValue = KNET_GetCfg(CONF_TCP_MAX_WORKER_NUM)->intValue;
        /* 多进程不支持单核多队列，修改内置值和MAX_WORKER_NUM一致，等价于queue_num配置项不生效 */
        SetCfgValue(CONF_INNER_QUEUE_NUM, &queue_num);
        return 0;
    }
    // 以下皆为从进程执行
    queue_num.intValue = 1;

    /* 从进程目前只支持单核单队列，需修改从进程的配置值 */
    SetCfgValue(CONF_INNER_QUEUE_NUM, &queue_num);
    SetCfgValue(CONF_TCP_MAX_WORKER_NUM, &queue_num);

    int queueId = KnetGetQueueIdFromPrimary(); // 每个进程分配一个core
    if (queueId == -1) {
        KNET_ERR("Get Queue Id from primary failed, queueId %d", queueId);
        return -1;
    }
    union KNET_CfgValue qId = {0};
    qId.intValue = queueId;
    SetCfgValue(CONF_INNER_QID, &qId);

    if (KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue == 0) {
        int core = KnetGetCoreByQueueId(queueId);
        if (core == -1) {
            KnetFreeQueueIdFromPrimary(queueId);
            KNET_ERR("Get core failed in secondary, queueId %d, core %d", queueId, core);
            return -1;
        }

        union KNET_CfgValue coreList = {0};
        int ret = sprintf_s(coreList.strValue, MAX_STRVALUE_NUM, "%d", core);
        if (ret < 0) {
            KnetFreeQueueIdFromPrimary(queueId);
            KNET_ERR("Sprintf core %d to core list failed in secondary", core);
            return -1;
        }
        SetCfgValue(CONF_INNER_CORE_LIST, &coreList);
    }

    return 0;
}

KNET_STATIC int KnetHwOffloadCheck(void)
{
    if ((KNET_GetCfg(CONF_HW_LRO)->intValue == 0) && (KNET_GetCfg(CONF_HW_TSO)->intValue == 0)) {
        return 0;
    }

    if (KNET_GetCfg(CONF_HW_TCP_CHECKSUM)->intValue == 0) {
        KNET_ERR("Lro or tso enable without checksum enable");
        return -1;
    }

    return 0;
}

static int MbufCheck(void)
{
    int32_t txCacheSize = KNET_GetCfg(CONF_DPDK_TX_CACHE_SIZE)->intValue;
    int32_t rxCacheSize = KNET_GetCfg(CONF_DPDK_RX_CACHE_SIZE)->intValue;
    int32_t workerNum = KNET_GetCfg(CONF_TCP_MAX_WORKER_NUM)->intValue;
    int32_t tcpCb = KNET_GetCfg(CONF_TCP_MAX_TCPCB)->intValue;
    // 计算公式：(tx_cache_size + rx_cache_size) * max_worker_num + max_tcpcb * 4 + 2048, 其中2048为系统预留
    int32_t minBuf = workerNum * txCacheSize + workerNum * rxCacheSize + tcpCb * 4 + KNET_MBUF_BATCH;
    int32_t bufNum = KNET_GetCfg(CONF_TCP_MAX_MBUF)->intValue;
    if (bufNum < minBuf) {
        KNET_ERR("Please set max_mbuf bigger than %d", minBuf);
        return -1;
    }

    return 0;
}

int CheckCoreNum(enum KNET_ProcType procType)
{
    // 开启共线程无需进行core数量检查
    if (KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue == 1) {
        return 0;
    }

    int coreNum = KnetGetCoreNum();
    if (procType == KNET_PROC_TYPE_SECONDARY) {
        coreNum = 1;
    }
    int32_t workerNum = KNET_GetCfg(CONF_TCP_MAX_WORKER_NUM)->intValue;
    if (workerNum != coreNum) {
        KNET_ERR("KNET conf max_worker_num %d not equal to core num %d", workerNum, coreNum);
        return -1;
    }

    return 0;
}

int CheckQueueNum(void)
{
    /* 只有单进程检查queue_num合法性 */
    int32_t workerNum = KNET_GetCfg(CONF_TCP_MAX_WORKER_NUM)->intValue;
    int32_t queueNum = KNET_GetCfg(CONF_DPDK_QUEUE_NUM)->intValue;
    if (KNET_GetCfg(CONF_COMMON_MODE)->intValue == KNET_RUN_MODE_SINGLE) {
        if (workerNum > queueNum) {
            KNET_ERR("KNET conf max_worker_num %d must be less than or equal to queue_num %d", workerNum, queueNum);
            return -1;
        }
        // 如果单进程中，不开流分叉就不能下rss流表，需要检查：worker == 1（直接使用rss功能）或者queue / worker == 1 无余数（下单queue流表）
        // SP670 开流量分叉的情况下, 对上述场景不承诺商用, K-NET做屏蔽
        if (KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue == 1) {
            int32_t queuePerWorker = queueNum / workerNum;
            if (!(workerNum == 1 || (queuePerWorker == 1 && queueNum % workerNum == 0))) {
                KNET_ERR("KNET conf queue_num %d but worker_num %d, need rss flow table, "
                    "which is not supported in cothread.", queueNum, workerNum);
                return -1;
            }
        }
    }
    return 0;
}

/**
 * @brief 获取硬件是否支持流量分叉，并与配置校验。流量分叉配置有以下约束：
 * 1、user_bond_enable不能为 1, 不支持用户态bond + 流量分叉
 * 2、不支持tm280网卡
 * @return int 0 : 配置合法
 *         int -1 : 配置非法
 */
int IsHwFlowFuncEnable(void)
{
    int nicFunEnable =  KnetIsEnableNicFlowFun();
    if (nicFunEnable == -1) {
        KNET_ERR("Get nic function enable failed");
        return -1;
    } else {
        union KNET_CfgValue nicFunEnableCfg = {0};
        nicFunEnableCfg.intValue = nicFunEnable;
        SetCfgValue(CONF_INNER_HW_TYPE, &nicFunEnableCfg);
    }

    if (KNET_GetCfg(CONF_HW_BIFUR_ENABLE)->intValue != BIFUR_ENABLE) {
        return 0;
    }

    if (KNET_GetCfg(CONF_INNER_HW_TYPE)->intValue == KNET_HW_TYPE_TM280) {
        KNET_ERR("KNET conf bifur_enable enable but nic function not support");
        return -1;
    }

    if ((KNET_GetCfg(CONF_INTERFACE_BOND_ENABLE)->intValue == 1)) {
        KNET_ERR("K-NET conf bifur_enable enable, user_bond_enable cannot be enabled");
        return -1;
    }

    return 0;
}

KNET_STATIC int IsNeedStopQueue(void)
{
    // 未开内核转发场景无需停止队列，直接返回false
    if (KNET_GetCfg(CONF_HW_BIFUR_ENABLE)->intValue != KERNEL_FORWARD_ENABLE) {
        return KNET_NOT_STOP_QUEUE;
    }
    // 非TM280网卡，单进程+共线程+内核转发+多worker时需要停止队列
    if (KNET_GetCfg(CONF_COMMON_MODE)->intValue == KNET_RUN_MODE_SINGLE &&
        KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue == 1 &&
        KNET_GetCfg(CONF_TCP_MAX_WORKER_NUM)->intValue > 1) {
        KNET_INFO("In single mode, need to stop queue when kernel forward");
        return KNET_STOP_QUEUE;
    }
    if (KNET_GetCfg(CONF_COMMON_MODE)->intValue == KNET_RUN_MODE_MULTIPLE) {
        KNET_INFO("In multiple mode, need to stop queue when kernel forward");
        return KNET_STOP_QUEUE;
    }
    return KNET_NOT_STOP_QUEUE;
}

KNET_STATIC int SetInnerCfgValue(enum KNET_ProcType procType)
{
    union KNET_CfgValue processType = {0};
    processType.intValue = procType;
    SetCfgValue(CONF_INNER_PROC_TYPE, &processType);
    
    int ret = 0;
    ret = IsHwFlowFuncEnable();
    if (ret != 0) {
        return -1; // 在函数内部打印日志
    }

    union KNET_CfgValue needStopQueue = {0};
    needStopQueue.intValue = IsNeedStopQueue();
    SetCfgValue(CONF_INNER_NEED_STOP_QUEUE, &needStopQueue);

    if (KNET_GetCfg(CONF_INTERFACE_BOND_ENABLE)->intValue == 0 &&
        KNET_GetCfg(CONF_HW_BIFUR_ENABLE)->intValue == BIFUR_ENABLE) {
        union KNET_CfgValue bondName = {0};
        bondName.strValue[0] = '\0';
        ret = KnetKernelBondCfgScan(bondName.strValue, MAX_STRVALUE_NUM);
        if (ret != 0) {
            return -1; // 日志在函数内处理
        }
        if (bondName.strValue[0] != '\0') {
            SetCfgValue(CONF_INNER_KERNEL_BOND_NAME, &bondName);
        }
    }

    int mode = KNET_GetCfg(CONF_COMMON_MODE)->intValue;
    if (mode == KNET_RUN_MODE_SINGLE) {
        ret = SetSingleModeLocalCfgValue();
        if (ret != 0) {
            KNET_ERR("Get single mode local config failed, ret %d", ret);
            return -1;
        }
    } else {
        ret = KnetRegConfigRpcHandler(procType);
        if (ret != 0) {
            KNET_ERR("Register config rpc handler failed, ret %d", ret);
            return -1;
        }
        ret = SetMultiModeLocalCfgValue(procType);
        if (ret != 0) {
            KNET_ERR("Get multiple mode local config failed, ret %d", ret);
            return -1;
        }
    }

    return 0;
}

KNET_STATIC int LoadCfgFromRpc(void)
{
    // request 函数内alloc/free
    // response 由KNET_RpcCall alloc, free在函数内
    struct KNET_RpcMessage req = {0};
    req.variableLenData = calloc(1, sizeof(struct ConfigRequest));
    if (req.variableLenData == NULL) {
        KNET_ERR("Calloc ConfigRequest fail");
        return -1;
    }
    req.dataType = RPC_MSG_DATA_TYPE_VARIABLE_LEN;
    req.dataLen = sizeof(struct ConfigRequest);
    ((struct ConfigRequest *)req.variableLenData)->type = CONF_REQ_TYPE_LOAD_CONF;

    char *cfgCtx = GetKnetCfgContent(KNET_CFG_FILE);
    if (cfgCtx == NULL) {
        KNET_ERR("Get cfg content failed");
        free(req.variableLenData);
        return -1;
    }

    struct KNET_RpcMessage res = {0};
    int ret = KNET_RpcCall(KNET_RPC_MOD_CONF, &req, &res);
    if (ret != 0 || res.ret != 0) {
        KNET_ERR("Rpc call load conf failed, ret %d, res.ret %d", ret, res.ret);
        free(req.variableLenData);
        DelKnetCfgContent(cfgCtx);
        return -1;
    }

    ret = memcmp(res.variableLenData, cfgCtx, MAX_CFG_SIZE);
    if (ret != 0) {
        KNET_ERR("K-NET conf differs from knet_mp_daemon conf. Please check the conf file");
        goto END;
    }

END:
    free(req.variableLenData);
    free(res.variableLenData);
    DelKnetCfgContent(cfgCtx);
    return ret;
}

KNET_STATIC int SendConfRpcHandler(int clientId, struct KNET_RpcMessage *knetRpcRequest,
    struct KNET_RpcMessage *knetRpcResponse)
{
    // free由RpcHandleRequest执行 函数内仅处理异常场景free
    knetRpcResponse->variableLenData = calloc(1, MAX_CFG_SIZE);
    if (knetRpcResponse->variableLenData == NULL) {
        KNET_ERR("Config calloc rpc response msg fail");
        return -1;
    }

    knetRpcResponse->dataType = RPC_MSG_DATA_TYPE_VARIABLE_LEN;
    knetRpcResponse->dataLen = MAX_CFG_SIZE;

    if (g_primaryCfg == NULL) {
        KNET_ERR("knet_mp_daemon fail to get backup conf");
        goto END;
    }

    (void)memcpy_s(knetRpcResponse->variableLenData, MAX_CFG_SIZE, g_primaryCfg, MAX_CFG_SIZE);

    return 0;
END:
    free(knetRpcResponse->variableLenData);
    return -1;
}

static int LoadCfgFromFile(enum KNET_ProcType outterProcType)
{
    char *cfgCtx = NULL;
    
    cfgCtx = GetKnetCfgContent(KNET_CFG_FILE);
    if (cfgCtx == NULL) {
        KNET_ERR("Get cfg content failed");
        return -1;
    }

    int ret = 0;
    cJSON *json = cJSON_Parse(cfgCtx);
    if (json == NULL) {
        KNET_ERR("K-NET config cJSON parse failed");
        ret = -1;
        goto END;
    }

    for (int i = 0; i < CONF_MAX; ++i) {
        ret = GetCfgValue(i, json);
        if (ret != 0) {
            KNET_ERR("Get cfg value failed");
            goto END;
        }
    }
    // 主进程留存配置文件，g_primaryCfg在进程退出时释放
    if (outterProcType == KNET_PROC_TYPE_PRIMARY && KNET_GetCfg(CONF_COMMON_MODE)->intValue == KNET_RUN_MODE_MULTIPLE) {
        g_primaryCfg = calloc(1, MAX_CFG_SIZE);
        if (g_primaryCfg == NULL) {
            KNET_ERR("Primary fail to calloc the backup conf file");
            ret = -1;
            goto END;
        }
        (void)memcpy_s(g_primaryCfg, MAX_CFG_SIZE, cfgCtx, MAX_CFG_SIZE);
    }
END:
    DelKnetCfgContent(cfgCtx);
    if (json != NULL) {
        cJSON_Delete(json);
    }

    return ret;
}

int ConfigRequestHandler(int clientId, struct KNET_RpcMessage *knetRpcRequest, struct KNET_RpcMessage *knetRpcResponse)
{
    struct ConfigRequest *req = NULL;
    if (knetRpcRequest->dataType == RPC_MSG_DATA_TYPE_VARIABLE_LEN) {
        req = (struct ConfigRequest *)knetRpcRequest->variableLenData;
    } else if (knetRpcRequest->dataType == RPC_MSG_DATA_TYPE_FIXED_LEN) {
        req = (struct ConfigRequest *)knetRpcRequest->fixedLenData;
    } else {
        KNET_ERR("ConfigRequest data type is invalid, client %d", clientId);
        return -1;
    }

    int ret = 0;
    if (req->type == CONF_REQ_TYPE_GET) {
        ret = KnetGetRequestHandler(clientId, knetRpcRequest, knetRpcResponse);
        if (ret != 0) {
            KNET_ERR("Get queue id request failed, clientId %d, type %d, ret %d", clientId, req->type, ret);
            return -1;
        }
    } else if (req->type == CONF_REQ_TYPE_FREE) {
        ret = KnetFreeRequestHandler(clientId, req->queueId);
        if (ret != 0) {
            KNET_ERR("Free queue id request failed, clientId %d, type %d, ret %d", clientId, req->type, ret);
            return -1;
        }
    } else if (req->type == CONF_REQ_TYPE_LOAD_CONF) {
        ret = SendConfRpcHandler(clientId, knetRpcRequest, knetRpcResponse);
        if (ret != 0) {
            KNET_ERR("Send conf request failed, clientId %d, type %d, ret %d", clientId, req->type, ret);
            return -1;
        }
    }

    knetRpcResponse->ret = ret;
    return 0;
}

int CtrlVcpuCheck(void)
{
    int ctrlVcpuNum = KNET_GetCfg(CONF_COMMON_CTRL_VCPU_NUMS)->intValue;
    const int *ctrlVcpuArr = KNET_GetCfg(CONF_COMMON_CTRL_VCPU_IDS)->intValueArr;
    for (int i = 0; i < ctrlVcpuNum; i++) {
        int ctrlVcpuId = ctrlVcpuArr[i];

        for (int j = i + 1; j < ctrlVcpuNum; j++) {
            if (ctrlVcpuId == ctrlVcpuArr[j]) {
                KNET_ERR("Ctrl vcpus should not be same");
                return -1;
            }
        }
        int ret = KNET_CpuDetected(ctrlVcpuId);
        if (ret < 0) {
            KNET_ERR("Ctrl vcpu %d is not a available core", ctrlVcpuId);
            return -1;
        }
        // 不开启共线程时检查ctrl cpu
        if (KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue == 0) {
            int idx = KNET_FindCoreInList(ctrlVcpuId);
            if (idx >= 0) {
                KNET_ERR("Ctrl vcpu %d should not in core list, which is used by worker", ctrlVcpuId);
                return -1;
            }
        }
    }
    return 0;
}

int CheckCfgValid(void)
{
    int ret = CheckQueueNum();
    if (ret != 0) {
        KNET_ERR("Check queue num failed, ret %d", ret);
        return -1;
    }

    ret = CtrlVcpuCheck();
    if (ret != 0) {
        KNET_ERR("Ctrl vcpu check failed");
        return -1;
    }

    ret = KnetCheckCompatibleNic();
    if (ret != 0) {
        KNET_ERR("Incompatible NIC, check the NIC");
        return -1;
    }

    ret = KnetHwOffloadCheck();
    if (ret != 0) {
        KNET_ERR("Lro and tso need to enable checksum");
        return -1;
    }

    ret = MbufCheck();
    if (ret != 0) {
        KNET_ERR("K-NET max_mbuf is not enough");
        return -1;
    }
    return 0;
}

KNET_STATIC void SetCfgFlowRule(void)
{
    // 单进程且未开流分叉
    if (KNET_GetCfg(CONF_HW_BIFUR_ENABLE)->intValue != BIFUR_ENABLE  &&
        KNET_GetCfg(CONF_COMMON_MODE)->intValue == KNET_RUN_MODE_SINGLE) {
        if (KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue == 0 ||
            KNET_GetCfg(CONF_TCP_MAX_WORKER_NUM)->intValue == 1) { // 没开共线程或者是单worker
            g_needFlowRule = 0;
            KNET_INFO("K-NET disabled rss flow rule");
            return;
        }
    }

    g_needFlowRule = 1;
}

int KNET_InitCfg(enum KNET_ProcType outterProcType)
{
    // 主进程和从进程都需要先读配置文件
    int ret = LoadCfgFromFile(outterProcType);
    if (ret != 0) {
        KNET_ERR("Load knet cfg failed, ret %d", ret);
        return -1;
    }

    enum KNET_ProcType procType = outterProcType;
    if (procType == KNET_PROC_TYPE_SECONDARY && KNET_GetCfg(CONF_COMMON_MODE)->intValue == KNET_RUN_MODE_SINGLE) {
        procType = KNET_PROC_TYPE_PRIMARY;
    }

    // 如果是多进程的从进程，需要rpc通信
    if (procType == KNET_PROC_TYPE_SECONDARY) {
        ret = LoadCfgFromRpc();
        if (ret != 0) {
            KNET_ERR("K-NET config from main Process failed, ret %d", ret);
            return -1;
        }
    } else if (KNET_GetCfg(CONF_COMMON_MODE)->intValue == KNET_RUN_MODE_MULTIPLE) {
        ret = KNET_RpcRegServer(KNET_RPC_EVENT_REQUEST, KNET_RPC_MOD_CONF, ConfigRequestHandler);
        if (ret != 0) {
            KNET_ERR("Register handler faild, ret %d", ret);
            return -1;
        }
    }

    ret = SetInnerCfgValue(procType);
    if (ret != 0) {
        KNET_ERR("Set local cfg failed, ret %d", ret);
        return -1;
    }

    ret = CheckCoreNum(procType);
    if (ret != 0) {
        KNET_ERR("Check core num failed, ret %d", ret);
        return -1;
    }

    ret = CheckCfgValid();
    if (ret != 0) {
        return -1; // 在函数内部打印日志
    }

    SetCfgFlowRule();

    return 0;
}

/*
 * @brief 获取配置项的值，模块内接口，且性能敏感，不检查参数有效性。建议使用前确认参数有效性。
 */
const union KNET_CfgValue *KNET_GetCfg(enum KNET_ConfKey key)
{
    return &(g_confHandleMap[key >> MAX_CONF_NUM_PER_INDEX_BITS][key & CONF_INDEX_LOWER_MASK].value);
}

void KNET_UninitCfg(void)
{
    free(g_primaryCfg);
    g_primaryCfg = NULL;
}

int KNET_IsNeedFlowRule(void)
{
    return g_needFlowRule;
}