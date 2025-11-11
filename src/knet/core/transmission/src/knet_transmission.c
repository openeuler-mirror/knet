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

#include "rte_ethdev.h"
#include "knet_types.h"
#include "knet_log.h"
#include "knet_pkt.h"
#include "knet_pktpool.h"
#include "knet_io_init.h"
#include "rte_hash.h"
#include "rte_errno.h"
#include "knet_rpc.h"
#include "knet_lock.h"
#include "knet_transmission.h"

static struct rte_hash *g_fdirHandle = NULL;

static bool g_ctlFlowFlag = false;
static KNET_SpinLock g_transLock[MAX_QUEUE_NUM] = {0};

int CreateFdirHashTbl(void);
int FdirHashTblAdd(struct Entry *newEntry);
struct Entry *FdirHashTblFind(uint64_t *key);
int FdirHashTblDel(uint64_t *key);
int DestroyFdirHashTbl(void);
int ConnectHandler(int id, struct FDirRequest *flowReq, uint64_t *key);
int DisconnectHandler(int id, uint64_t *key);

void KNET_SetFdirHandle(struct rte_hash* fdirHandle)
{
    g_fdirHandle = fdirHandle;
}

struct rte_hash* KNET_GetFdirHandle(void)
{
    return g_fdirHandle;
}

// 创建FdirHashTbl
int CreateFdirHashTbl(void)
{
    struct rte_hash_parameters hashParams = {0};

    if (g_fdirHandle != NULL) {
        KNET_ERR("Create Fdir hash table failed, because table already exist");
        return -1;
    }

    hashParams.name = "Flow_table";
    hashParams.entries = MAX_ENTRIES;
    hashParams.key_len = sizeof(uint64_t);
    hashParams.hash_func_init_val = 0;
    hashParams.socket_id = SOCKET_ID_ANY;

    g_fdirHandle = rte_hash_create(&hashParams);
    if (g_fdirHandle == NULL) {
        KNET_ERR("Create Fdir hash table failed, errno: %d", rte_errno);
        return -1;
    }
    return 0;
}

// 插入Fdir规则到hash表
int FdirHashTblAdd(struct Entry *newEntry)
{
    int32_t ret;

    if (g_fdirHandle == NULL) {
        KNET_ERR("Fdirhandle does not exist");
        return -1;
    }
    ret = rte_hash_lookup(g_fdirHandle, &(newEntry->ip_port));
    if (ret >= 0) {
        KNET_ERR("FdirHashTblAdd key already exist. ret: %d", ret);
        return -1;
    }
    
    ret = rte_hash_add_key_data(g_fdirHandle, &(newEntry->ip_port), newEntry);
    if (ret != 0) {
        KNET_ERR("FdirHashTblAdd entry failed. ret: %d", ret);
        return -1;
    }
    return 0;
}

struct Entry *FdirHashTblFind(uint64_t *key)
{
    int32_t ret;
    struct Entry *oldEntry = NULL;

    if (g_fdirHandle == NULL) {
        KNET_ERR("Fdirhandle does not exist");
        return NULL;
    }
    ret = rte_hash_lookup_data(g_fdirHandle, key, (void **) &oldEntry);
    if (ret < 0) {
        KNET_DEBUG("FdirHashTblFind key not exist. ret: %d", ret);
        return NULL;
    }

    return oldEntry;
}

// 删除hash表的Fdir规则
int FdirHashTblDel(uint64_t *key)
{
    int32_t ret;
    struct Entry *oldEntry = NULL;
    int32_t delPos;

    ret = rte_hash_lookup_data(g_fdirHandle, key, (void **) &oldEntry);
    if (ret < 0) {
        KNET_ERR("Delete Fdirhash table key not exist. ret: %d", ret);
        return -1;
    }

    delPos = rte_hash_del_key(g_fdirHandle, key);
    if (delPos < 0) {
        KNET_ERR("Delete Fdirhash table entry failed. delPos: %d.", delPos);
        return -1;
    }

    free(oldEntry);
    return 0;
}

// 销毁FdirHash表
int DestroyFdirHashTbl(void)
{
    int32_t delPos;
    uint32_t iter = 0;
    uint64_t *key = NULL;
    struct Entry *nextEntry = NULL;
    
    while (rte_hash_iterate(g_fdirHandle, (const void **) &key, (void **) &nextEntry, &iter) >= 0) {
        delPos = rte_hash_del_key(g_fdirHandle, key);
        if (delPos < 0) {
            KNET_ERR("Delete Fdirhash table entry failed. delPos:%d", delPos);
            return -1;
        }

        free(nextEntry);
    }

    rte_hash_free(g_fdirHandle);
    g_fdirHandle = NULL;
    return 0;
}

int KNET_EventNotify(uint32_t ip, uint32_t port, int32_t proto, uint32_t type)
{
    struct FDirRequest fr = {0};

    fr.type = type;
    if (ip == 0) {
        fr.dstIp = htonl((uint32_t)KNET_GetCfg(CONF_INTERFACE_IP).intValue);
    } else {
        fr.dstIp = ntohl(ip);
    }
    fr.dstIpMask = DST_IPMASK;
    fr.dstPort = port;
    fr.dstPortMask = DST_PORTMASK;
    fr.proto = proto;
    fr.queueId = (uint32_t)KNET_GetCfg(CONF_INNER_QID).intValue;

    int ret;
    struct KnetRpcMessage req = {0};
    struct KnetRpcMessage res = {0};

    ret = memcpy_s(req.data, RPC_MESSAGE_SIZE, &fr, sizeof(struct FDirRequest));
    if (ret != 0) {
        KNET_ERR("Memcpy failed, ret %d", ret);
        return -1;
    }
    req.len = sizeof(struct FDirRequest);

    ret = KNET_RpcClient(KNET_MOD_FDIR, &req, &res);
    if (ret != 0) {
        KNET_ERR("Rpc client failed, ret %d", ret);
        return -1;
    }

    return *(int *)res.data;
}

int KNET_GenerateIpv4PortFlow(struct FDirRequest *flowReq, struct rte_flow **flow) // 流规则下发
{
    struct KnetFlowCfg cfg = {0};

    cfg.flowEnable = 1;
    cfg.rxQueueId = flowReq->queueId;
    cfg.dstIp = flowReq->dstIp;
    cfg.dstIpMask = flowReq->dstIpMask;
    cfg.dstPort = flowReq->dstPort;
    cfg.dstPortMask = flowReq->dstPortMask;

    int32_t proto = flowReq->proto;
    if (proto == IPPROTO_TCP) {
        return KnetGenerateIpv4TcpPortFlow(&cfg, flow);
    } else if (proto == IPPROTO_UDP) {
        return KnetGenerateIpv4UdpPortFlow(&cfg, flow);
    } else {
        KNET_ERR("Invalid proto :%d ", proto);
        return -1;
    }
    return 0;
}

// ARP流表在队列中转移
int KnetCtrFlowChange(uint32_t queueId, struct rte_flow *arpFlow)
{
    int32_t ret;
    uint32_t iter = 0;
    uint64_t *key = NULL;
    struct Entry *nextEntry = NULL;

    // case 1: hash表中剩余的entry有相同的队列
    while (rte_hash_iterate(g_fdirHandle, (const void **) &key, (void **) &nextEntry, &iter) >= 0) {
        if (nextEntry->map.queueId == queueId) {
            KNET_INFO("Ctl flow not change");
            nextEntry->map.arpFlow = arpFlow;
            return 0;
        }
    }
    // case 2和case 3需要删除控制流表
    ret = KNET_DeleteFlowRule(arpFlow);
    if (ret != 0) {
        KNET_ERR("Delete arp flow rule failed.");
        return -1;
    }
    // case 2: hash表为空
    iter = 0;
    if (rte_hash_iterate(g_fdirHandle, (const void **) &key, (void **) &nextEntry, &iter) < 0) {
        KNET_INFO("Fdir hash table is enmpty");
        g_ctlFlowFlag = false;
        return 0;
    }
    // case 3: 获取hash表中第一个entry并重新下Arp流表
    iter = 0;
    ret = rte_hash_iterate(g_fdirHandle, (const void **) &key, (void **) &nextEntry, &iter);
    if (ret < 0) {
        KNET_ERR("rte_hash_iterate failed, ret: %d", ret);
        return -1;
    }
    ret = KnetGenerateCtlArpFlow(nextEntry->map.queueId, &nextEntry->map.arpFlow);
    if (ret != 0) {
        KNET_ERR("Change arp flow failed.");
        return -1;
    }
    KNET_INFO("Change arp flow success.");
    return 0;
}

int KNET_GenerateFlow(struct FDirRequest *flowReq, struct rte_flow **flow, struct rte_flow **arpFlow)
{
    int32_t ret;
    if (g_ctlFlowFlag == false) { // 当前没有控制流表
        ret = KnetGenerateCtlArpFlow(flowReq->queueId, arpFlow);
        if (ret != 0) {
            KNET_ERR("Generate ctl arp flow failed.");
            return -1;
        }
        g_ctlFlowFlag = true;
    }
    ret = KNET_GenerateIpv4PortFlow(flowReq, flow);
    if (ret != 0) {
        KNET_ERR("GenerateIpv4TcpFlow failed.");
        return -1;
    }
    return 0;
}

int ConnectHandler(int id, struct FDirRequest *flowReq, uint64_t *key)
{
    struct Entry *oldEntry = NULL;
    int32_t ret;

    oldEntry = FdirHashTblFind(key);
    if (oldEntry == NULL) {
        struct rte_flow *flow = NULL;
        struct rte_flow *arpFlow = NULL;

        oldEntry = (struct Entry *)malloc(sizeof(struct Entry));
        if (oldEntry == NULL) {
            KNET_ERR("ConnectHandler malloc failed.");
            return -1;
        }
        oldEntry->ip_port = *key;
        oldEntry->map.clientId = id;
        oldEntry->map.count = 1;
        oldEntry->map.queueId = flowReq->queueId;

        ret = FdirHashTblAdd(oldEntry);
        if (ret != 0) {
            KNET_ERR("FdirHashTblAdd failed. key: %lu", *key);
            free(oldEntry);
            return -1;
        }

        ret = KNET_GenerateFlow(flowReq, &flow, &arpFlow);
        if (ret != 0) {
            KNET_ERR("Generate flow failed.");
            ret = FdirHashTblDel(key);
            if (ret != 0) {
                free(oldEntry);
            }
            return -1;
        }
        oldEntry->map.flow = flow;
        oldEntry->map.arpFlow = arpFlow;
    } else if (oldEntry->map.clientId == id) {
        oldEntry->map.count++;
    } else {
        KNET_ERR("Ip port:%lu already exist, but clientId: %d not match", *key, id);
        return -1;
    }
    return 0;
}

int DisconnectHandler(int id, uint64_t *key)
{
    struct Entry *oldEntry = NULL;
    int32_t ret;

    oldEntry = FdirHashTblFind(key);
    if (oldEntry == NULL) {
        KNET_ERR("Disconnet failed, not find ip_port");
        return -1;
    }
    if (oldEntry->map.clientId != id) {
        KNET_ERR("Disconnet failed, clientId not match");
        return -1;
    }
    oldEntry->map.count--;
    if (oldEntry->map.count == 0) {
        ret = KNET_DeleteFlowRule(oldEntry->map.flow);
        if (ret != 0) {
            KNET_ERR("Delete flow rule failed");
            return -1;
        }
        if (oldEntry->map.arpFlow != NULL) { // 如果当前的控制流表存在该entry中，则需要调用KnetCtrFlowChange
            uint32_t arpFlowQueueID = oldEntry->map.queueId;
            struct rte_flow *arpFlow = oldEntry->map.arpFlow;
            ret = FdirHashTblDel(key);
            if (ret != 0) {
                KNET_ERR("Delete Fdirhash table failed. ret: %d", ret);
                return -1;
            }
            ret = KnetCtrFlowChange(arpFlowQueueID, arpFlow);
            if (ret != 0) {
                KNET_ERR("CtrFlow change failed");
                return -1;
            }
        } else {
            ret = FdirHashTblDel(key);
            if (ret != 0) {
                KNET_ERR("Delete Fdirhash table failed. ret: %d", ret);
                return -1;
            }
        }
    }
    return 0;
}

// 主进程对Fdir请求的处理
static int FdirRequestHandler(int id, struct KnetRpcMessage *knetRpcRequest, struct KnetRpcMessage *knetRpcResponse)
{
    int32_t ret;
    struct FDirRequest *fr = NULL;

    fr = (struct FDirRequest *)(knetRpcRequest->data);
    uint32_t type = fr->type;
    uint32_t dstIp = fr->dstIp;
    uint16_t dstPort = fr->dstPort;
    uint64_t ip_port = 0;

    ip_port = (((uint64_t)dstIp << 16) | dstPort); //  低16位为端口号，高48位为IP
    
    if (type == ACC_CONNECT) { // 建链时
        ret = ConnectHandler(id, fr, &ip_port);
    } else if (type == ACC_DISCONNECT) { // 断链时
        ret = DisconnectHandler(id, &ip_port);
    } else {
        KNET_ERR("Type:%d is invalid", type);
        ret = -1;
    }

    ret = memcpy_s(knetRpcResponse->data, RPC_MESSAGE_SIZE, (char *)&ret, sizeof(int));
    if (ret != 0) {
        KNET_ERR("Memcpy failed");
        return -1;
    }
    knetRpcResponse->len = sizeof(int);
    return ret;
}

// 从进程断链处理
static int FdirDisconnectHandler(int id, struct KnetRpcMessage *knetRpcRequest, struct KnetRpcMessage *knetRpcResponse)
{
    int32_t ret;
    int32_t  disconnectFlag = 0;
    uint32_t iter = 0;
    uint64_t *key = NULL;
    struct Entry *nextEntry = NULL;

    while (rte_hash_iterate(g_fdirHandle, (const void **) &key, (void **) &nextEntry, &iter) >= 0) {
        if (id != nextEntry->map.clientId) {
            continue;
        }
        ret = KNET_DeleteFlowRule(nextEntry->map.flow);
        if (ret != 0) {
            KNET_ERR("Delete flow rule failed");
            disconnectFlag = -1;
        }
        if (nextEntry->map.arpFlow != NULL) { // 如果当前的控制流表存在该entry中，则需要调用KnetCtrFlowChange
            uint32_t arpFlowQueueID = nextEntry->map.queueId;
            struct rte_flow *arpFlow = nextEntry->map.arpFlow;
            ret = FdirHashTblDel(key);
            if (ret != 0) {
                KNET_ERR("Delete FdirHash table failed");
                disconnectFlag = -1;
            }
            ret = KnetCtrFlowChange(arpFlowQueueID, arpFlow);
            if (ret != 0) {
                KNET_ERR("CtrFlow change failed");
                disconnectFlag = -1;
            }
        } else {
            ret = FdirHashTblDel(key);
            if (ret != 0) {
                KNET_ERR("Delete FdirHash table failed");
                disconnectFlag = -1;
            }
        }
    }
    return disconnectFlag;
}

int KNET_TxBurst(uint16_t queId, struct rte_mbuf **rteMbuf, int cnt, uint32_t portId)
{
    int ret;
    KNET_SpinlockLock(&g_transLock[queId]);
    ret = rte_eth_tx_burst(portId, queId, rteMbuf, cnt);
    KNET_SpinlockUnlock(&g_transLock[queId]);

    return ret;
}

int KNET_RxBurst(uint16_t queId, struct rte_mbuf **rxBuf, int cnt, uint32_t portId)
{
    return rte_eth_rx_burst(portId, queId, rxBuf, cnt);
}

int KNET_InitTrans(enum KnetProcType procType)
{
    int ret;
    
    g_ctlFlowFlag = false;
    ret = CreateFdirHashTbl();
    if (ret != 0) {
        KNET_ERR("Create fdir hash table failed");
        return -1;
    }
    ret = KNET_RegServer(KNET_CONNECT_EVENT_REQUEST, KNET_MOD_FDIR, FdirRequestHandler);
    if (ret != 0) {
        KNET_ERR("Register fdir request handler failed");
        return -1;
    }
    ret = KNET_RegServer(KNET_CONNECT_EVENT_DISCONNECT, KNET_MOD_FDIR, FdirDisconnectHandler);
    if (ret != 0) {
        KNET_ERR("Register fdir disconnect handler failed");
        return -1;
    }
    return 0;
}

int KNET_UninitTrans(enum KnetProcType procType)
{
    int ret;

    g_ctlFlowFlag = false;
    ret = DestroyFdirHashTbl();
    if (ret != 0) {
        KNET_ERR("Destroy fdir hash table failed");
        return -1;
    }

    KNET_DesServer(KNET_CONNECT_EVENT_REQUEST, KNET_MOD_FDIR);
    KNET_DesServer(KNET_CONNECT_EVENT_DISCONNECT, KNET_MOD_FDIR);

    return 0;
}