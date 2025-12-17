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
#include "knet_types.h"
#include "knet_log.h"
#include "knet_dpdk_init.h"
#include "rte_hash.h"
#include "rte_errno.h"
#include "knet_rpc.h"
#include "knet_lock.h"
#include "knet_transmission_hash.h"
#include "knet_transmission.h"

#define SINGLE_MODE_CLIENT_ID 0

enum AccConnectType {
    ACC_CONNECT = 0,
    ACC_DISCONNECT = 1
};

static bool g_ctlFlowFlag = false;
static KNET_SpinLock g_transLock[MAX_QUEUE_NUM] = {0};
static KNET_QueIdMapPidTid_t g_queIdMapPidTid[MAX_QUEUE_NUM] = {0};

int ConnectHandler(int id, struct KNET_FDirRequest *flowReq, uint64_t *key);
int DisconnectHandler(int id, uint64_t *key);

int32_t FdirProcess(int id, struct KNET_FDirRequest *flow, uint64_t *key, uint32_t type)
{
    int32_t ret = 0;
    if (type == ACC_CONNECT) { // 建链时
        ret = ConnectHandler(id, flow, key);
    } else if (type == ACC_DISCONNECT) { // 断链时
        ret = DisconnectHandler(id, key);
    } else {
        KNET_ERR("Type %u in %d is invalid", type, id);
        ret = -1;
    }

    return ret;
}

/**
 * @brief 仅在非共线程场景下获取流表的qid
 *
 * @param runMode [IN] 运行模式
 * @param flowReq [OUT] 获取流表的qid与queueIdSize
 * @return int 0: 成功, -1: 失败
 */
int GetFlowQueue(int runMode, struct KNET_FDirRequest *flowReq)
{
    if (KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue == 1) {
        return 0;
    }
    flowReq->queueIdSize = 0;
    if (runMode == KNET_RUN_MODE_MULTIPLE) {
        flowReq->queueId[0] = (uint32_t)KNET_GetCfg(CONF_INNER_QID)->intValue;
        flowReq->queueIdSize = 1;
        return 0;
    }

    // 单进程设置为rss的队列
    int queueNum = KNET_GetCfg(CONF_DPDK_QUEUE_NUM)->intValue;
    for (int i = 0; i < queueNum; i++) {
        flowReq->queueId[i] = i;
    }
    flowReq->queueIdSize = queueNum;
    return 0;
}

/**
 * 获取queId到进程/线程映射关系维测信息，改数据为Hash表，下标通过QueueId获取
*/
inline KNET_QueIdMapPidTid_t* KNET_GetQueIdMapPidTidLcoreInfo(void)
{
    return g_queIdMapPidTid;
}

/**
 * 获取queId到进程/线程映射关系维测信息，改数据为Hash表，下标通过QueueId获取
*/
inline int KNET_SetQueIdMapPidTidLcoreInfo(uint32_t queId, uint32_t pid, uint32_t tid, uint32_t lcoreId,
                                           uint32_t workerId)
{
    if (queId >= MAX_QUEUE_NUM) {
        KNET_ERR("Invalid queId %u, exceed max queue num %u", queId, MAX_QUEUE_NUM);
        return -1;
    }
    g_queIdMapPidTid[queId].pid = pid;
    g_queIdMapPidTid[queId].tid = tid;
    g_queIdMapPidTid[queId].lcoreId = lcoreId;
    g_queIdMapPidTid[queId].workerId = workerId;
    return 0;
}

int KNET_EventNotify(struct KNET_FDirRequest *fdir)
{
    int runMode = KNET_GetCfg(CONF_COMMON_MODE)->intValue;
    if (fdir->type == ACC_CONNECT) {
        GetFlowQueue(runMode, fdir);
    }
    int ret = 0;
    // 单进程时：开流分叉或共线程需要下流表
    if (runMode == KNET_RUN_MODE_SINGLE && (KNET_GetCfg(CONF_HW_BIFUR_ENABLE)->intValue == BIFUR_ENABLE ||
        KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue == 1)) {
        uint64_t ip_port = (((uint64_t)fdir->dstIp << 16) | fdir->dstPort); //  低16位为端口号，高48位为IP
        ret = FdirProcess(SINGLE_MODE_CLIENT_ID, fdir, &ip_port, fdir->type);
        if (ret != 0) {
            KNET_ERR("FdirProcess failed in single mode.");
            return -1;
        }
        return 0;
    }

    struct KNET_RpcMessage req = {0};
    // 多进程单队列，仅传输1个queueid
    size_t fdirRpcSize = sizeof(struct KNET_FDirRequest) - sizeof(uint16_t) * (KNET_MAX_QUEUES_PER_PORT - 1);
    ret = memcpy_s(req.fixedLenData, RPC_MESSAGE_SIZE, fdir, fdirRpcSize);
    if (ret != 0) {
        KNET_ERR("Memcpy failed, ret %d", ret);
        return -1;
    }
    req.dataLen = fdirRpcSize;
    req.dataType = RPC_MSG_DATA_TYPE_FIXED_LEN;

    struct KNET_RpcMessage res = {0};
    ret = KNET_RpcCall(KNET_RPC_MOD_FDIR, &req, &res);
    if (ret != 0) {
        KNET_ERR("Rpc call failed, ret %d, mod %d, dataLen %d, dataTyep %d", \
            ret, KNET_RPC_MOD_FDIR, req.dataLen, req.dataType);
        return -1;
    }
    return *(int *)res.fixedLenData;
}

int GenerateIpv4PortFlow(struct KNET_FDirRequest *flowReq, struct rte_flow **flow) // 流规则下发
{
    struct KNET_FlowCfg cfg = {0};

    cfg.flowEnable = 1;
    // 目前支持多进程单队列，单进程：单队列、多队列，Knet生成的流表下发
    cfg.rxQueueIdSize = flowReq->queueIdSize;
    for (int i = 0; i < cfg.rxQueueIdSize; i++) {
        cfg.rxQueueId[i] = flowReq->queueId[i];
    }
    cfg.dstIp = flowReq->dstIp;
    cfg.dstIpMask = flowReq->dstIpMask;
    cfg.dstPort = flowReq->dstPort;
    cfg.dstPortMask = flowReq->dstPortMask;
    cfg.proto = flowReq->proto;

    return KNET_GenerateIpv4Flow(KNET_GetNetDevCtx()->xmitPortId, &cfg, flow);
}

static bool CheckQueueIdInHash(uint16_t queueId, struct rte_flow *arpFlow)
{
    uint32_t iter = 0;
    uint64_t *key = NULL;
    struct Entry *nextEntry = NULL;
    while (rte_hash_iterate(KnetGetFdirHandle(), (const void **) &key, (void **) &nextEntry, &iter) >= 0) {
        if (nextEntry->map.queueId[0] == queueId) {
            KNET_INFO("Ctl flow not change");
            nextEntry->map.arpFlow = arpFlow;
            return true;
        }
    }
    return false;
}

KNET_STATIC bool IsHashEmpty(void)
{
    uint32_t iter = 0;
    uint64_t *key = NULL;
    struct Entry *nextEntry = NULL;
    if (rte_hash_iterate(KnetGetFdirHandle(), (const void **) &key, (void **) &nextEntry, &iter) < 0) {
        KNET_INFO("Fdir hash table is empty");
        g_ctlFlowFlag = false;
        return true;
    }
    return false;
}

// ARP流表在队列中转移
int CtrFlowChange(uint16_t queueId, struct rte_flow *arpFlow)
{
    uint32_t iter = 0;
    uint64_t *key = NULL;
    struct Entry *nextEntry = NULL;

    // case 1: hash表中剩余的entry有相同的队列
    if (CheckQueueIdInHash(queueId, arpFlow)) {
        return 0;
    }

    int32_t ret = 0;
    // case 2和case 3需要删除控制流表
    ret = KNET_DeleteFlowRule(KNET_GetNetDevCtx()->xmitPortId, arpFlow);
    if (ret != 0) {
        KNET_ERR("Delete arp flow rule failed.");
        return -1;
    }
    // case 2: hash表为空
    if (IsHashEmpty()) {
        return 0;
    }
    // case 3: 获取hash表中第一个entry并重新下Arp流表
    ret = rte_hash_iterate(KnetGetFdirHandle(), (const void **) &key, (void **) &nextEntry, &iter);
    if (ret < 0) {
        KNET_ERR("rte_hash_iterate failed, ret %d", ret);
        return -1;
    }
    ret = KNET_GenerateArpFlow(KNET_GetNetDevCtx()->xmitPortId, nextEntry->map.queueId[0], &nextEntry->map.arpFlow);
    if (ret != 0) {
        KNET_ERR("Change arp flow failed.");
        return -1;
    }
    KNET_INFO("Change arp flow success.");
    return 0;
}

int GenerateFlow(struct KNET_FDirRequest *flowReq, struct rte_flow **flow, struct rte_flow **arpFlow)
{
    int32_t ret = 0;

    bool needArpFlow = false;
    /* 存下modeFLag, 数据路径避免每次GetCfg返回长union，从函数栈空间拷贝长数据到现场 */
    static int modeFLag = INVALID_CONF_INT_VALUE;
    if (modeFLag == INVALID_CONF_INT_VALUE) {
        modeFLag = KNET_GetCfg(CONF_COMMON_MODE)->intValue;
    }
    /* 存下bifurEnableFlag, 数据路径避免每次GetCfg返回长union，从函数栈空间拷贝长数据到现场 */
    static int bifurEnableFlag = INVALID_CONF_INT_VALUE;
    if (bifurEnableFlag == INVALID_CONF_INT_VALUE) {
        bifurEnableFlag = KNET_GetCfg(CONF_HW_BIFUR_ENABLE)->intValue;
    }

    if (modeFLag == KNET_RUN_MODE_MULTIPLE && bifurEnableFlag != BIFUR_ENABLE) {
        needArpFlow = true;
    }

    if (g_ctlFlowFlag == false && needArpFlow) { // 当前没有控制流表 且 多进程没开流分叉
        // 仅多进程会下发，所以取queue 0没问题
        if (flowReq->queueIdSize != 1) {
            KNET_ERR("Queue id size %d is invalid", flowReq->queueIdSize);
            return -1;
        }
        ret = KNET_GenerateArpFlow(KNET_GetNetDevCtx()->xmitPortId, flowReq->queueId[0], arpFlow);
        if (ret != 0) {
            KNET_ERR("Generate ctl arp flow failed. port %hu, queue %hu", \
                KNET_GetNetDevCtx()->xmitPortId, flowReq->queueId[0]);
            return -1;
        }
        g_ctlFlowFlag = true;
    }
    ret = GenerateIpv4PortFlow(flowReq, flow);
    if (ret != 0) {
        KNET_ERR("GenerateIpv4TcpFlow failed. dstIp %u, dstIpMask %u, dstPort %u, dstPortMask %u, proto %d", \
            flowReq->dstIp, flowReq->dstIpMask, flowReq->dstPort, flowReq->dstPortMask, flowReq->proto);
        return -1;
    }
    return 0;
}

int FirstConnectHandler(int id, struct KNET_FDirRequest *flowReq, uint64_t *key)
{
    struct rte_flow *flow = NULL;
    struct rte_flow *arpFlow = NULL;

    struct Entry *oldEntry = (struct Entry *)malloc(sizeof(struct Entry));
    if (oldEntry == NULL) {
        KNET_ERR("FirstConnectHandler malloc failed.");
        return -1;
    }
    (void)memset_s(oldEntry, sizeof(struct Entry), 0, sizeof(struct Entry));
    oldEntry->ip_port = *key;
    oldEntry->map.clientId = id;
    KNET_HalAtomicSet64(&oldEntry->map.count, 1);
    // 每个hash表维护流表的queue信息
    if (flowReq->queueIdSize > KNET_MAX_QUEUES_PER_PORT) {
        KNET_ERR("FirstConnectHandler queueId size is invalid, larger than max queue per port.");
        free(oldEntry);
        return -1;
    }
    oldEntry->map.queueIdSize = flowReq->queueIdSize;
    for (int i = 0; i < flowReq->queueIdSize; i++) {
        oldEntry->map.queueId[i] = flowReq->queueId[i];
    }

    int32_t ret = 0;
    ret = KnetFdirHashTblAdd(oldEntry);
    if (ret != 0) {
        KNET_ERR("FdirHashTblAdd failed. key %lu", *key);
        free(oldEntry);
        return -1;
    }

    ret = GenerateFlow(flowReq, &flow, &arpFlow);
    if (ret != 0) {
        KNET_ERR("Generate flow failed.");
        ret = KnetFdirHashTblDel(key);
        if (ret != 0) {
            free(oldEntry);
        }
        return -1;
    }
    oldEntry->map.flow = flow;
    oldEntry->map.arpFlow = arpFlow;
    oldEntry->map.dPortMask = flowReq->dstPortMask;
    return 0;
}

int ConnectHandler(int id, struct KNET_FDirRequest *flowReq, uint64_t *key)
{
    struct Entry *oldEntry = KnetFdirHashTblFind(key);
    if (oldEntry == NULL) {
        int ret = FirstConnectHandler(id, flowReq, key);
        if (ret != 0) {
            KNET_ERR("FirstConnectHandler failed.");
            return -1;
        }
        return 0;
    }
    
    if (oldEntry->map.clientId == id) {
        KNET_HalAtomicAdd64(&oldEntry->map.count, 1);
        return 0;
    }
    KNET_ERR("Ip port %lu already exist, but clientId %d not match", *key, id);
    return -1;
}

int DisconnectCleanup(struct Entry *oldEntry, uint64_t *key)
{
    int32_t ret = 0;
    uint16_t arpFlowQueueID = oldEntry->map.queueId[0];
    struct rte_flow *tcpFlow = oldEntry->map.flow;
    struct rte_flow *arpFlow = oldEntry->map.arpFlow;
    uint64_t ip_port = oldEntry->ip_port;
    uint16_t queueIdSize = oldEntry->map.queueIdSize;
    int clientId = oldEntry->map.clientId;
    
    // 删除哈希表条目, 先删哈希表防止查表耗时导致单进程+流量分叉+iperf场景下的流规则并发问题
    ret = KnetFdirHashTblDel(key);
    if (ret != 0) {
        KNET_ERR("Delete Fdirhash table failed. ret %d, key %lu", ret, *key);
        return -1;
    }

    // 删除tcpFlow
    ret = KNET_DeleteFlowRule(KNET_GetNetDevCtx()->xmitPortId, tcpFlow);
    if (ret != 0) {
        KNET_ERR("Delete port %hu flow rule failed, entry ip_port %lu, queueIdSize %hu, clientId %d",
            KNET_GetNetDevCtx()->xmitPortId, ip_port, queueIdSize, clientId);
        return -1;
    }

    // 处理arpFlow
    if (arpFlow != NULL) {
        ret = CtrFlowChange(arpFlowQueueID, arpFlow);
        if (ret != 0) {
            KNET_ERR("QueueId %hu ctrFlow change failed", arpFlowQueueID);
            return -1;
        }
    }

    return 0;
}
/**
 * @brief 正常请求断链处理
 */
int DisconnectHandler(int id, uint64_t *key)
{
    struct Entry *oldEntry = KnetFdirHashTblFind(key);
    if (oldEntry == NULL) {
        KNET_ERR("Disconnect failed, not find ip_port");
        return -1;
    }
    if (oldEntry->map.clientId != id) {
        KNET_ERR("Disconnect failed, clientId not match");
        return -1;
    }
    KNET_HalAtomicSub64(&oldEntry->map.count, 1);
    if (KNET_HalAtomicRead64(&oldEntry->map.count) == 0) {
        int ret = DisconnectCleanup(oldEntry, key);
        if (ret != 0) {
            KNET_ERR("Disconnect cleanup failed");
            return -1;
        }
    }
    return 0;
}

// 主进程对Fdir请求的处理
KNET_STATIC int FDirRequestHandler(int id, struct KNET_RpcMessage *knetRpcRequest,
    struct KNET_RpcMessage *knetRpcResponse)
{
    struct KNET_FDirRequest *fr = (struct KNET_FDirRequest *)(knetRpcRequest->fixedLenData);
    uint64_t ip_port = (((uint64_t)fr->dstIp << 16) | fr->dstPort); //  低16位为端口号，高48位为IP

    int32_t ret = FdirProcess(id, fr, &ip_port, fr->type);
    if (ret != 0) {
        KNET_ERR("FdirProcess id %d ip_port %llx  type %d failed", id, ip_port, fr->type);
        return -1;
    }

    ret = memcpy_s(knetRpcResponse->fixedLenData, RPC_MESSAGE_SIZE, (char *)&ret, sizeof(int));
    if (ret != 0) {
        KNET_ERR("Memcpy failed");
        return -1;
    }
    knetRpcResponse->dataLen = sizeof(int);
    knetRpcResponse->dataType = RPC_MSG_DATA_TYPE_FIXED_LEN;
    return ret;
}

// 从进程终止后，关掉从进程队列，清理ring
KNET_STATIC void RteRingFree(uint16_t queueId)
{
    KNET_DpdkNetdevCtx* netdevCtx = KNET_GetNetDevCtx();
    int32_t ret = rte_eth_dev_rx_queue_stop(netdevCtx->portId, queueId);
    if (ret != 0) {  // 忽略不支持的错误
        KNET_ERR("Failed to stop RX queue, ret %d, port %hu, queue %hu", ret, netdevCtx->portId, queueId);
    }
    char name[MAX_CPD_NAME_LEN] = {0};
    ret = snprintf_s(name, MAX_CPD_NAME_LEN, MAX_CPD_NAME_LEN - 1, "cpdtaprx%hu", queueId);
    if (ret < 0) {
        KNET_ERR("Ring name %s get error, ret %d", name, ret);
    }
    struct rte_ring *cpdTapRing = rte_ring_lookup(name);
    if (cpdTapRing == NULL) {
        KNET_WARN("Cpd tap %s does not exist", name);
    }
    rte_ring_free(cpdTapRing);
}

// 从进程异常断链处理、单进程+流分叉进行流表删除（因为没有arp流表，也不会删除）
KNET_STATIC int FdirDisconnectHandler(int id, struct KNET_RpcMessage *knetRpcRequest,
    struct KNET_RpcMessage *knetRpcResponse)
{
    if (KnetGetFdirHandle() == NULL) {
        KNET_WARN("Fdirhandle does not exist in disconnnect handler");
        return 0;
    }
    int32_t ret;
    int32_t  disconnectFlag = 0;
    uint32_t iter = 0;
    uint64_t *key = NULL;
    struct Entry *nextEntry = NULL;

    while (rte_hash_iterate(KnetGetFdirHandle(), (const void **) &key, (void **) &nextEntry, &iter) >= 0) {
        if (id != nextEntry->map.clientId) {
            continue;
        }
        ret = KNET_DeleteFlowRule(KNET_GetNetDevCtx()->xmitPortId, nextEntry->map.flow);
        if (ret != 0) {
            KNET_ERR("Delete flow rule failed");
            disconnectFlag = -1;
        }
        if (nextEntry->map.arpFlow != NULL) { // 如果当前的控制流表存在该entry中，则需要调用CtrFlowChange
            uint32_t arpFlowQueueID = nextEntry->map.queueId[0];
            struct rte_flow *arpFlow = nextEntry->map.arpFlow;
            ret = KnetFdirHashTblDel(key);
            if (ret != 0) {
                KNET_ERR("Delete FdirHash table failed");
                disconnectFlag = -1;
            }
            ret = CtrFlowChange(arpFlowQueueID, arpFlow);
            if (ret != 0) {
                KNET_ERR("CtrFlow change failed");
                disconnectFlag = -1;
            }
        } else {
            ret = KnetFdirHashTblDel(key);
            if (ret != 0) {
                KNET_ERR("Delete FdirHash table failed");
                disconnectFlag = -1;
            }
        }
        /* 最后断链退出时为内核转发停掉队列并删除ring */
        if (nextEntry != NULL && KNET_GetCfg(CONF_INNER_NEED_STOP_QUEUE)->intValue == KNET_STOP_QUEUE) {
            RteRingFree(nextEntry->map.queueId[0]);
        }
    }
    return disconnectFlag;
}

int KNET_TxBurst(uint16_t queId, struct rte_mbuf **rteMbuf, int cnt, uint32_t portId)
{
    KNET_SpinlockLock(&g_transLock[queId]);
    int ret = rte_eth_tx_burst(portId, queId, rteMbuf, cnt);
    KNET_SpinlockUnlock(&g_transLock[queId]);

    return ret;
}

int KNET_RxBurst(uint16_t queId, struct rte_mbuf **rxBuf, int cnt, uint32_t portId)
{
    return rte_eth_rx_burst(portId, queId, rxBuf, cnt);
}

int KNET_FindFdirQue(uint32_t dstIp, uint16_t dstPort, uint16_t *queueId)
{
    if (KNET_IsNeedFlowRule() == 0) { // 0代表无需下发流表, 直接返回-1
        KNET_DEBUG("K-NET disabled rss flow rule in tx hash");
        return -1;
    }
    uint64_t ip_port = (((uint64_t)dstIp << 16) | dstPort);
    struct Entry *oldEntry = KnetFdirHashTblFind(&ip_port);
    if (oldEntry == NULL) {
        KNET_WARN("No flow hash table entry found, dstIp %u, dstPort %hu. Maybe use nic rss function.", dstIp, dstPort);
        return -1;
    }
    for (int i = 0; i < oldEntry->map.queueIdSize; i++) {
        queueId[i] = oldEntry->map.queueId[i];
    }
 
    return oldEntry->map.queueIdSize;
}

KNET_STATIC int CheckPreTrans(enum KNET_ProcType procType)
{
    int runMode = KNET_GetCfg(CONF_COMMON_MODE)->intValue;
    int bifureEnable = KNET_GetCfg(CONF_HW_BIFUR_ENABLE)->intValue;
    int cothreadEnable = KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue;
    // 未开流分叉与共线程的单进程，或者多进程（开不开流分叉）主进程都需要执行
    if (procType != KNET_PROC_TYPE_PRIMARY || (runMode == KNET_RUN_MODE_SINGLE && bifureEnable != BIFUR_ENABLE &&
        cothreadEnable == 0)) {
        return 0;
    }
    return -1;
}

int KNET_InitTrans(enum KNET_ProcType procType)
{
    if (CheckPreTrans(procType) == 0) {
        return 0;
    }

    int ret;
    g_ctlFlowFlag = false;
    ret = KnetCreateFdirHashTbl();
    if (ret != 0) {
        KNET_ERR("Create fdir hash table failed");
        return -1;
    }
    if (KNET_GetCfg(CONF_COMMON_MODE)->intValue == KNET_RUN_MODE_MULTIPLE) {
        ret = KNET_RpcRegServer(KNET_RPC_EVENT_REQUEST, KNET_RPC_MOD_FDIR, FDirRequestHandler);
        if (ret != 0) {
            KNET_ERR("Register fdir request handler failed");
            return -1;
        }
        ret = KNET_RpcRegServer(KNET_RPC_EVENT_DISCONNECT, KNET_RPC_MOD_FDIR, FdirDisconnectHandler);
        if (ret != 0) {
            KNET_ERR("Register fdir disconnect handler failed");
            return -1;
        }
    }
    return 0;
}

int KNET_UninitTrans(enum KNET_ProcType procType)
{
    if (CheckPreTrans(procType) == 0) {
        return 0;
    }

    int flag = 0;
    int ret = 0;
    if (KNET_GetCfg(CONF_COMMON_MODE)->intValue == KNET_RUN_MODE_SINGLE) {
        ret = FdirDisconnectHandler(SINGLE_MODE_CLIENT_ID, NULL, NULL);
        if (ret != 0) {
            KNET_ERR("FdirDisconnectHandler failed");
            flag = 1;
        }
    }

    g_ctlFlowFlag = false;
    ret = KnetDestroyFdirHashTbl();
    if (ret != 0) {
        KNET_ERR("Destroy fdir hash table failed");
        flag = 1;
    }

    if (KNET_GetCfg(CONF_COMMON_MODE)->intValue == KNET_RUN_MODE_MULTIPLE) {
        KNET_RpcDesServer(KNET_RPC_EVENT_REQUEST, KNET_RPC_MOD_FDIR);
        KNET_RpcDesServer(KNET_RPC_EVENT_DISCONNECT, KNET_RPC_MOD_FDIR);
    }

    return flag == 0 ? 0 : -1;
}