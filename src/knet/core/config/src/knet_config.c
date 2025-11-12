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

#include "cJSON.h"

#include "knet_log.h"
#include "knet_hw_scan.h"
#include "knet_queue_id.h"
#include "knet_config_setter.h"
#include "knet_utils.h"
#include "knet_rpc.h"
#include "knet_config.h"

#define MAX_CFG_SIZE 4096
#define SINGLE_MODE_QID 0
#define KNET_CFG_FILE "/etc/knet/knet_comm.conf"
#define KNET_MBUF_BATCH 2048

char *MODULE_NAME[CONF_MAX] = {"common", "interface", "hw_offload", "proto_stack", "dpdk", ""};

int MODULE_CONF_MIN[CONF_MAX] = {CONF_COMMON_MIN, CONF_INTERFACE_MIN, CONF_HW_MIN, CONF_DP_MIN,
    CONF_DPDK_MIN, CONF_INNER_MIN};
int MODULE_CONF_MAX[CONF_MAX] = {CONF_COMMON_MAX, CONF_INTERFACE_MAX, CONF_HW_MAX, CONF_DP_MAX,
    CONF_DPDK_MAX, CONF_INNER_MAX};

static int GetConfNum(void)
{
    int confNum = 0;
    for (int i = 0; i < CONF_MAX-1; ++i) {
        confNum += MODULE_CONF_MAX[i] - MODULE_CONF_MIN[i];
    }
    
    return confNum;
}

struct ConfKeyHandle {
    enum KnetConfKey key;
    char *name;
    union CfgValue value;
    FuncSetter setter;
    union KnetCfgValidateParam validateParam;
};

struct ConfKeyHandle g_commonConfHandler[CONF_COMMON_MAX - CONF_COMMON_MIN] = {
    {CONF_COMMON_MODE, "mode", {0}, IntSetter, {.intValue = {.min = 0, .max = 1}}},
    {CONF_COMMON_LOG_LEVEL, "log_level", {.strValue = "WARNING"}, LogLevelSetter, {}},
    {CONF_COMMON_CTRL_VCPU_ID, "ctrl_vcpu_id", {0}, IntSetter, {.intValue = {.min = 0, .max = 127}}},
};

struct ConfKeyHandle g_interfaceConfHandler[CONF_INTERFACE_MAX - CONF_INTERFACE_MIN] = {
    {CONF_INTERFACE_BDF_NUM,
        "bdf_num",
        {.strValue = "0000:06:00.0"},
        StringSetter,
        {.pattern = "^[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\\.[0-7]$"}},
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
};

struct ConfKeyHandle g_dpConfHandler[CONF_DP_MAX - CONF_DP_MIN] = {
    {CONF_DP_MAX_MBUF, "max_mbuf", {20480}, IntSetter, {.intValue = {.min = 8192, .max = INT32_MAX}}},
    // tx_queue_num、rx_queue_num与max_worker_num强一致，故内置
    {CONF_DP_MAX_WORKER_NUM, "max_worker_num", {1}, IntSetter, {.intValue = {.min = 1, .max = 32}}},
    {CONF_DP_MAX_ROUTE, "max_route", {1024}, IntSetter, {.intValue = {.min = 1, .max = 100000}}},
    // max_arp最小值8受限于DPDK的最小值RTE_HASH_BUCKET_ENTRIES
    {CONF_DP_MAX_ARP, "max_arp", {1024}, IntSetter, {.intValue = {.min = 8, .max = 8192}}},
    {CONF_DP_MAX_TCPCB, "max_tcpcb", {4096}, IntSetter, {.intValue = {.min = 32, .max = 1000000}}},
    {CONF_DP_MAX_UDPCB, "max_udpcb", {4096}, IntSetter, {.intValue = {.min = 32, .max = 1000000}}},
    {CONF_DP_TCP_SACK, "tcp_sack", {1}, IntSetter, {.intValue = {.min = 0, .max = 1}}},
    {CONF_DP_TCP_DACK, "tcp_dack", {1}, IntSetter, {.intValue = {.min = 0, .max = 1}}},
    {CONF_DP_MSL_TIME, "msl_time", {30}, IntSetter, {.intValue = {.min = 1, .max = 30}}},
    {CONF_DP_FIN_TIMEOUT, "fin_timeout", {600}, IntSetter, {.intValue = {.min = 1, .max = 600}}},
    {CONF_DP_MIN_PORT, "min_port", {49152}, IntSetter, {.intValue = {.min = 1, .max = 49152}}},
    {CONF_DP_MAX_PORT, "max_port", {65535}, IntSetter, {.intValue = {.min = 50000, .max = 65535}}},
    {CONF_DP_MAX_SENDBUF, "max_sendbuf", {10485760}, IntSetter, {.intValue = {.min = 8192, .max = INT32_MAX}}},
    {CONF_DP_DEF_SENDBUF, "def_sendbuf", {8192}, IntSetter, {.intValue = {.min = 8192, .max = INT32_MAX}}},
    {CONF_DP_MAX_RECVBUF, "max_recvbuf", {10485760}, IntSetter, {.intValue = {.min = 8192, .max = INT32_MAX}}},
    {CONF_DP_DEF_RECVBUF, "def_recvbuf", {8192}, IntSetter, {.intValue = {.min = 8192, .max = INT32_MAX}}},
    {CONF_DP_TCP_COOKIE, "tcp_cookie", {0}, IntSetter, {.intValue = {.min = 0, .max = 1}}},
    {CONF_DP_REASS_MAX, "reass_max", {1000}, IntSetter, {.intValue = {.min = 1, .max = 4096}}},
    {CONF_DP_REASS_TIMEOUT, "reass_timeout", {30}, IntSetter, {.intValue = {.min = 1, .max = 30}}},
};

struct ConfKeyHandle g_dpdkConfHandler[CONF_DPDK_MAX - CONF_DPDK_MIN] = {
    {CONF_DPDK_CORE_LIST_GLOBAL, "core_list_global", {.strValue = "1"}, CoreListGlobalSetter, {}},
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
};

struct ConfKeyHandle g_innerConfHandler[CONF_INNER_MAX - CONF_INNER_MIN] = {
    {CONF_INNER_QID, "queue_id", {0}, NULL, {.intValue = {.min = 0, .max = 31}}},
    {CONF_INNER_CORE_LIST, "core_list", {.strValue = "1"}, NULL, {}},
    {CONF_INNER_PROC_TYPE, "proc_type", {0}, NULL, {.intValue = {.min = 0, .max = 1}}},
    {CONF_INNER_TX_QUEUE_NUM, "tx_queue_num", {1}, IntSetter, {.intValue = {.min = 1, .max = 32}}},
    {CONF_INNER_RX_QUEUE_NUM, "rx_queue_num", {1}, IntSetter, {.intValue = {.min = 1, .max = 32}}},
};

struct ConfKeyHandle *g_confHandleMap[CONF_MAX] = {
    g_commonConfHandler,
    g_interfaceConfHandler,
    g_hwConfHandler,
    g_dpConfHandler,
    g_dpdkConfHandler,
    g_innerConfHandler,
};

static char *GetKnetCfgContent(const char *fileName)
{
    char *cfgContent = NULL;
    size_t readSize;
    FILE *file = NULL;

    file = fopen(fileName, "r");
    if (file == NULL) {
        KNET_ERR("Open K-NET conf file failed, errno:%d", errno);
        return cfgContent;
    }
    cfgContent = (char *)malloc(MAX_CFG_SIZE);
    if (cfgContent == NULL) {
        KNET_ERR("Malloc failed, errno:%d", errno);
        goto END;
    }
    (void)memset_s(cfgContent, MAX_CFG_SIZE, 0, MAX_CFG_SIZE);
    readSize = fread(cfgContent, 1, MAX_CFG_SIZE, file);
    if (readSize >= MAX_CFG_SIZE) {
        free(cfgContent);
        cfgContent = NULL;
        KNET_ERR("Fread failed, read larger than %d, errno:%d", MAX_CFG_SIZE, errno);
        goto END;
    }

END:
    (void)fclose(file);

    return cfgContent;
}

static void SetCfgValue(enum KnetConfKey key, union CfgValue value)
{
    for (int i = 0; i < CONF_MAX; ++i) {
        if (key >= MODULE_CONF_MIN[i] && key < MODULE_CONF_MAX[i]) {
            g_confHandleMap[i][key - MODULE_CONF_MIN[i]].value = value;
            return;
        }
    }
}

static void DelKnetCfgContent(char *cfgCtx)
{
    free(cfgCtx);
}
static int GetCfgValue(enum ConfModule module, cJSON *json)
{
    int ret = 0;

    // 内部配置项，不需要从外部获取。
    if (MODULE_NAME[module] == NULL || MODULE_NAME[module][0] == '\0') {
        return 0;
    }

    cJSON *subJson = cJSON_GetObjectItem(json, MODULE_NAME[module]);
    if (subJson == NULL) {
        KNET_ERR("cJson get sub json %s failed", MODULE_NAME[module]);
        return -1;
    }

    struct ConfKeyHandle *confItem = NULL;
    for (int key = 0; key < MODULE_CONF_MAX[module] - MODULE_CONF_MIN[module]; ++key) {
        confItem = &g_confHandleMap[module][key];
        // 内部配置项，不需要从外部获取。
        if (confItem->setter == NULL) {
            continue;
        }

        cJSON *jsonValue = cJSON_GetObjectItem(subJson, confItem->name);
        ret = confItem->setter(jsonValue, &confItem->value, &confItem->validateParam);
        if (ret != 0) {
            KNET_ERR("cJson get vaule %s:%s failed, err:%d", MODULE_NAME[module], confItem->name, ret);
            break;
        }
    }

    if (ret != 0) {
        return -1;
    }

    return 0;
}

static int SetSingleModeLocalCfgValue(void)
{  // 处于单进程模式时core_list设置为core_list_global
    union CfgValue qId;
    qId.intValue = SINGLE_MODE_QID;

    SetCfgValue(CONF_INNER_QID, qId);
    SetCfgValue(CONF_INNER_CORE_LIST, KNET_GetCfg(CONF_DPDK_CORE_LIST_GLOBAL));
    return 0;
}

static int SetMultiModeLocalCfgValue(enum KnetProcType procType)
{
    union CfgValue qId;
    union CfgValue coreList;
    int ret;
    int queueId;  // 每个进程分配一个core

    if (procType == KNET_PROC_TYPE_PRIMARY) {  // 主进程不分配queue id
        return 0;
    }

    queueId = KNET_GetQueueId(procType);
    if (queueId == -1) {
        KNET_ERR("Get Queue Id from primary failed, queueId %d", queueId);
        return -1;
    }

    qId.intValue = queueId;

    int core = KNET_GetCore(queueId);
    if (core == -1) {
        KNET_FreeQueueId(procType, queueId);
        KNET_ERR("Get core failed");
        return -1;
    }

    ret = sprintf_s(coreList.strValue, MAX_STRVALUE_NUM, "%d", core);
    if (ret < 0) {
        KNET_FreeQueueId(procType, queueId);
        KNET_ERR("Sprintf failed, ret %d", ret);
        return -1;
    }

    SetCfgValue(CONF_INNER_QID, qId);
    SetCfgValue(CONF_INNER_CORE_LIST, coreList);

    return 0;
}

static int KnetHwOffloadCheck(void)
{
    if ((KNET_GetCfg(CONF_HW_LRO).intValue == 0) && (KNET_GetCfg(CONF_HW_TSO).intValue == 0)) {
        return 0;
    }

    if (KNET_GetCfg(CONF_HW_TCP_CHECKSUM).intValue == 0) {
        KNET_ERR("Lro or tso enable without checksum enable");
        return -1;
    }

    return 0;
}

static int KnetMbufCheck(void)
{
    int32_t txCacheSize = KNET_GetCfg(CONF_DPDK_TX_CACHE_SIZE).intValue;
    int32_t rxCacheSize = KNET_GetCfg(CONF_DPDK_RX_CACHE_SIZE).intValue;
    int32_t workerNum = KNET_GetCfg(CONF_DP_MAX_WORKER_NUM).intValue;
    int32_t tcpCb = KNET_GetCfg(CONF_DP_MAX_TCPCB).intValue;
    // 计算公式：(tx_cache_size + rx_cache_size) * max_worker_num + max_tcpcb * 4 + 2048, 其中2048为系统预留
    int32_t minBuf = workerNum * txCacheSize + workerNum * rxCacheSize + tcpCb * 4 + KNET_MBUF_BATCH;
    int32_t bufNum = KNET_GetCfg(CONF_DP_MAX_MBUF).intValue;
    if (bufNum < minBuf) {
        KNET_ERR("Please set max_mbuf bigger than %d", minBuf);
        return -1;
    }

    return 0;
}

int CheckQueueNum(enum KnetProcType procType)
{
    int coreNum = KNET_GetCoreNum();

    if (procType == KNET_PROC_TYPE_SECONDARY) {
        union CfgValue queue_num;
        queue_num.intValue = 1;

        SetCfgValue(CONF_INNER_TX_QUEUE_NUM, queue_num);
        SetCfgValue(CONF_INNER_RX_QUEUE_NUM, queue_num);
        SetCfgValue(CONF_DP_MAX_WORKER_NUM, queue_num);
        coreNum = 1;
    }

    int32_t workerNum = KNET_GetCfg(CONF_DP_MAX_WORKER_NUM).intValue;
    if (workerNum != coreNum) {
        KNET_ERR("KNET conf max_worker_num and coreNum must be the same");
        return -1;
    }

    return 0;
}

int SetLocalCfgValue(enum KnetProcType procType)
{
    int ret;

    union CfgValue processType;
    processType.intValue = procType;
    SetCfgValue(CONF_INNER_PROC_TYPE, processType);

    ret = KNET_RegConfigRpcHandler(procType);
    if (ret != 0) {
        KNET_ERR("Regist config rpc handler faild, ret %d", ret);
        return -1;
    }

    ret = CheckQueueNum(procType);
    if (ret != 0) {
        KNET_ERR("Check queue num faild, ret %d", ret);
        return -1;
    }

    int mode = KNET_GetCfg(CONF_COMMON_MODE).intValue;
    if (mode == KNET_RUN_MODE_SINGLE) {
        ret = SetSingleModeLocalCfgValue();
        if (ret != 0) {
            KNET_ERR("Get single mode local config failed, ret %d", ret);
            return -1;
        }
    } else {
        ret = SetMultiModeLocalCfgValue(procType);
        if (ret != 0) {
            KNET_ERR("Get multiple mode local config failed, ret %d", ret);
            return -1;
        }
    }

    return 0;
}

static int LoadCfgFromRpc(void)
{
    int ret;
    struct KnetRpcMessage req = {0};
    struct KnetRpcMessage res = {0};

    struct ConfigRequest confReq = {0};
    confReq.type = CONF_REQ_TYPE_LOAD_CONF;

    ret = memcpy_s(req.data, RPC_MESSAGE_SIZE, (char *)&confReq, sizeof(struct ConfigRequest));
    if (ret != 0) {
        KNET_ERR("Memcpy faild, ret %d", ret);
        return -1;
    }
    req.len = sizeof(struct ConfigRequest);

    ret = KNET_RpcClient(KNET_MOD_CONF, &req, &res);
    if (ret != 0) {
        KNET_ERR("Rpc client load conf failed, ret %d", ret);
        return -1;
    }

    if (res.ret != 0) {
        KNET_ERR("Get conf failed, ret %d", res.ret);
        return -1;
    }

    int confNum = GetConfNum();
    union CfgValue *knetConfArr = NULL;
    knetConfArr = (union CfgValue *)res.data;
    int index = 0;
    for (int i = 0; i < CONF_MAX; ++i) {
        for (int j = 0; j < MODULE_CONF_MAX[i] - MODULE_CONF_MIN[i] && index < confNum; ++j) {
            if ((knetConfArr[index].intValue != g_confHandleMap[i][j].value.intValue) ||
                (strcmp(knetConfArr[index].strValue, g_confHandleMap[i][j].value.strValue) != 0)) {
                KNET_ERR("K-NET conf %s differs from knet_mp_daemon conf. Please reset the conf!",
                    g_confHandleMap[i][j].name);
                return -1;
            }
            index++;
        }
    }

    return 0;
}

static int SendConfRpcHandler(int clientId, struct KnetRpcMessage *knetRpcRequest,
    struct KnetRpcMessage *knetRpcResponse)
{
    int ret;
    int index = 0;
    int confNum = GetConfNum();
    for (int i = 0; i < CONF_MAX ; ++i) {
        for (int j = 0; j < MODULE_CONF_MAX[i] - MODULE_CONF_MIN[i] && index < confNum; ++j) {
            ret = memcpy_s((void *)&knetRpcResponse->data[index * sizeof(union CfgValue)],
                sizeof(union CfgValue), (const void *)&g_confHandleMap[i][j].value,
                sizeof(g_confHandleMap[i][j].value));
            if (ret != 0) {
                KNET_ERR("knet_mp_daemon memcpy conf faild, ret %d", ret);
                return -1;
            }

            index++;
        }
    }

    return 0;
}

static int LoadCfgFromFile(void)
{
    int ret;
    char *cfgCtx = NULL;
    
    cfgCtx = GetKnetCfgContent(KNET_CFG_FILE);
    if (cfgCtx == NULL) {
        KNET_ERR("Get cfg content failed");
        return -1;
    }

    cJSON *json = cJSON_Parse(cfgCtx);
    if (json == NULL) {
        KNET_ERR("K-NET config cJson parse failed");
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
    // 为内置的tx rx queue num赋值
    SetCfgValue(CONF_INNER_TX_QUEUE_NUM, KNET_GetCfg(CONF_DP_MAX_WORKER_NUM));
    SetCfgValue(CONF_INNER_RX_QUEUE_NUM, KNET_GetCfg(CONF_DP_MAX_WORKER_NUM));

END:
    DelKnetCfgContent(cfgCtx);
    if (json != NULL) {
        cJSON_Delete(json);
    }

    return ret;
}

int ConfigRequestHandler(int clientId, struct KnetRpcMessage *knetRpcRequest, struct KnetRpcMessage *knetRpcResponse)
{
    int ret = 0;

    struct ConfigRequest *req = (struct ConfigRequest *)knetRpcRequest->data;
    if (req->type == CONF_REQ_TYPE_GET) {
        ret = GetRequestHandler(clientId, knetRpcResponse);
        if (ret != 0) {
            KNET_ERR("Get queue id request faild, ret %d", ret);
            return -1;
        }
    } else if (req->type == CONF_REQ_TYPE_FREE) {
        ret = FreeRequestHandler(clientId, req->queueId);
        if (ret != 0) {
            KNET_ERR("Free queue id request faild, ret %d", ret);
            return -1;
        }
    } else if (req->type == CONF_REQ_TYPE_LOAD_CONF) {
        ret = SendConfRpcHandler(clientId, knetRpcRequest, knetRpcResponse);
        if (ret != 0) {
            KNET_ERR("Send conf request faild, ret %d", ret);
            return -1;
        }
    }

    knetRpcResponse->ret = ret;
    return 0;
}

int KNET_InitCfg(enum KnetProcType outterProcType)
{
    enum KnetProcType procType = outterProcType;
    int ret;

    // 主进程和从进程都需要先读配置文件
    ret = LoadCfgFromFile();
    if (ret != 0) {
        KNET_ERR("Load knet cfg failed, ret %d", ret);
        return -1;
    }

    if (procType == KNET_PROC_TYPE_SECONDARY && KNET_GetCfg(CONF_COMMON_MODE).intValue == KNET_RUN_MODE_SINGLE) {
        procType = KNET_PROC_TYPE_PRIMARY;
    }

    // 如果是多进程的从进程，需要rpc通信
    if (procType == KNET_PROC_TYPE_SECONDARY) {
        ret = LoadCfgFromRpc();
        if (ret != 0) {
            KNET_ERR("K-NET config from main Process failed, ret %d", ret);
            return -1;
        }
    } else if (KNET_GetCfg(CONF_COMMON_MODE).intValue == KNET_RUN_MODE_MULTIPLE) {
        ret = KNET_RegServer(KNET_CONNECT_EVENT_REQUEST, KNET_MOD_CONF, ConfigRequestHandler);
        if (ret != 0) {
            KNET_ERR("Register handler faild, ret %d", ret);
            return -1;
        }
    }

    ret = SetLocalCfgValue(procType);
    if (ret != 0) {
        KNET_ERR("Set local cfg failed, ret %d", ret);
        return -1;
    }

    ret = KNET_CheckCompatibleNic();
    if (ret != 0) {
        KNET_ERR("Incompatible NIC, check the NIC");
        return -1;
    }

    ret = KnetHwOffloadCheck();
    if (ret != 0) {
        KNET_ERR("Lro and tso neet to enable checksum");
        return -1;
    }

    ret = KnetMbufCheck();
    if (ret != 0) {
        KNET_ERR("Knet max_mbuf is not enough");
        return -1;
    }

    return 0;
}

int KNET_CtrlVcpuCheck(void)
{
    int ctrlVcpuId = KNET_GetCfg(CONF_COMMON_CTRL_VCPU_ID).intValue;
    int ret = KNET_CpuDetected(ctrlVcpuId);
    if (ret < 0) {
        KNET_ERR("Ctrl vcpu Id is not a available core");
        return -1;
    }
    int idx = KNET_FindCoreInList(ctrlVcpuId);
    if (idx >= 0) {
        KNET_ERR("Ctrl vcpu Id should not in core list");
        return -1;
    }
    return 0;
}

union CfgValue KNET_GetCfg(enum KnetConfKey key)
{
    for (int i = 0; i < CONF_MAX; ++i) {
        if (key >= MODULE_CONF_MIN[i] && key < MODULE_CONF_MAX[i]) {
            return g_confHandleMap[i][key - MODULE_CONF_MIN[i]].value;
        }
    }

    union CfgValue value = {0};
    value.intValue = INVALID_CONF_VALUE;

    return value;
}