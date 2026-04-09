/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */


#include "securec.h"
#include "knet_transmission.h"
#include "knet_rpc.h"
#include "knet_dpdk_init.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>

#include "rte_timer.h"
#include "rte_ethdev.h"

#include "knet_log.h"
#include "knet_config.h"
#include "knet_offload.h"
#include "knet_atomic.h"

#include "common.h"
#include "mock.h"
#include "rte_hash.h"

#define MAX_ENTRIES 2048
#define DST_IPMASK 0xFFFFFFFF
#define DST_PORTMASK 0xFFFF
#define TCP_PROTO 6
#define FLOW_TABLE_LEN 2048

extern struct rte_hash *g_fdirHandle;
// 变更结构体需要同步到UT
struct Entry {
    uint64_t ip_port;
    struct Map {
        int clientId;
        uint32_t entryId; // 新增哈希表项id用于维测顺序输出
        uint16_t queueIdSize;
        uint16_t dPortMask;
        KNET_ATOMIC64_T count;
        uint16_t queueId[KNET_MAX_QUEUES_PER_PORT];
        struct rte_flow_action action[MAX_ACTION_NUM]; // 维测输出action信息
        struct rte_flow_item pattern[MAX_TRANS_PATTERN_NUM]; // 维测输出协议栈
        struct rte_flow *flow;
        struct rte_flow *arpFlow;
    } map;
};

static union KNET_CfgValue g_cfg = {.intValue = 1};
static union KNET_CfgValue *MockKnetGetCfg(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    g_cfg.intValue = 1;
    return &g_cfg;
}

struct rte_ring *MockRteRingLookupRetNULL(const char *name)
{
    static struct rte_ring *r = NULL;
    return r;
};

static struct rte_ring *MockRteRingCreateAndStartQueue(const char *name, unsigned int count,
    int socketId, unsigned int flags)
{
    static struct rte_ring r;
    return &r;
}

extern "C" {
int KnetCreateFdirHashTbl();
int KnetFdirHashTblAdd(struct Entry *newEntry);
extern struct Entry *KnetFdirHashTblFind(uint64_t *key);
int KnetFdirHashTblDel(uint64_t *key);
int KnetDestroyFdirHashTbl();
int ConnectHandler(int id, struct KNET_FDirRequest *flowReq, uint64_t *key);
int DisconnectHandler(int id, uint64_t *key);
int GenerateIpv4PortFlow(struct KNET_FDirRequest *flowReq, struct rte_flow **flow, struct KNET_FlowTeleInfo *flowTele); // 流规则下发
int GenerateFlow(struct KNET_FDirRequest *flowReq, struct rte_flow **flow, struct rte_flow **arpFlow);
int32_t FdirProcess(int id, struct KNET_FDirRequest *flow, uint64_t *key, uint32_t type);
int FdirDisconnectHandler(int id, struct KNET_RpcMessage *knetRpcRequest, struct KNET_RpcMessage *knetRpcResponse);
int KNET_FindFdirQue(uint32_t dstIp, uint16_t dstPort, uint16_t *queueId);
struct rte_ring *RteRingCreateAndStartQueue(uint16_t portId, uint16_t queueId);
int GetFlowQueue(int runMode, struct KNET_FDirRequest *flowReq);
KNET_STATIC void RteRingFree(uint16_t queueId);
uint32_t KNET_OutputFdirHashTbl(char *output);
uint32_t KnetOutPutPartial(char *output, uint32_t offset, uint32_t cnt, struct Entry *nextEntry);
int CtrFlowChange(uint16_t queueId, struct rte_flow *arpFlow);
int FDirRequestHandler(int id, struct KNET_RpcMessage *knetRpcRequest,
    struct KNET_RpcMessage *knetRpcResponse);
}

DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_CREATE_NORMAL, NULL, NULL)
{
    int ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_hash_create, TEST_GetFuncRetPositive(0));

    ret = KnetCreateFdirHashTbl();
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Create(rte_hash_create, TEST_GetFuncRetPositive(1));
    ret = KnetCreateFdirHashTbl();
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(rte_hash_create);
    DeleteMock(Mock);
}

DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_DEL_NORMAL, NULL, NULL)
{
    int ret = 0;
    uint64_t *key = 1;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_hash_lookup_data, TEST_GetFuncRetNegative(1));
    Mock->Create(rte_hash_del_key, TEST_GetFuncRetPositive(0));

    ret = KnetFdirHashTblDel(key);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(rte_hash_lookup_data);
    Mock->Create(rte_hash_lookup_data, TEST_GetFuncRetPositive(1));

    ret = KnetFdirHashTblDel(key);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(rte_hash_lookup_data);
    Mock->Delete(rte_hash_del_key);
    DeleteMock(Mock);
}

DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_ACC_NORMAL, NULL, NULL)
{
    int ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    Mock->Create(memcpy_s, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_RpcCall, TEST_GetFuncRetPositive(0));
    struct KNET_FDirRequest fdir = {0};
    fdir.proto = TCP_PROTO; // TCP
    ret = KNET_EventNotify(&fdir);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(memcpy_s);
    Mock->Delete(KNET_RpcCall);
    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_GENIPV4FLOW_NORMAL, NULL, NULL)
{
    int ret = 0;
    struct KNET_FDirRequest flowReq = {0};
    struct KNET_FlowTeleInfo flowTele = {0};
    flowReq.type = 0;
    flowReq.queueId[0] = 0;
    flowReq.queueIdSize = 1;
    flowReq.dstIp = 0;
    flowReq.dstIpMask = DST_IPMASK;
    flowReq.dstPort = 0;
    flowReq.dstPortMask = DST_PORTMASK;
    flowReq.proto = IPPROTO_TCP;
    struct rte_flow *flow = NULL;

    KTestMock *Mock = CreateMock();
    Mock->Create(rte_flow_validate, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_flow_create, TEST_GetFuncRetPositive(1));
    ret = GenerateIpv4PortFlow(&flowReq, &flow, &flowTele);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(rte_flow_validate);
    Mock->Delete(rte_flow_create);
    DeleteMock(Mock);
}

DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_INIT_NORMAL, NULL, NULL)
{
    int ret = 0;
    enum KNET_ProcType procType = KNET_PROC_TYPE_PRIMARY;
    ret = KNET_InitTrans(procType);
    DT_ASSERT_EQUAL(ret, 0);
    
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    Mock->Create(KnetCreateFdirHashTbl, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_RpcRegServer, TEST_GetFuncRetNegative(1));

    ret = KNET_InitTrans(procType);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_RpcRegServer);

    Mock->Create(KNET_RpcRegServer, TEST_GetFuncRetPositive(0));

    ret = KNET_InitTrans(procType);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KnetCreateFdirHashTbl);
    Mock->Delete(KNET_RpcRegServer);
    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_UNINIT_NORMAL, NULL, NULL)
{
    int ret = 0;
    enum KNET_ProcType procType = KNET_PROC_TYPE_PRIMARY;
    ret = KNET_UninitTrans(procType);
    DT_ASSERT_EQUAL(ret, 0);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    Mock->Create(KnetDestroyFdirHashTbl, TEST_GetFuncRetNegative(1));

    ret = KNET_UninitTrans(procType);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KnetDestroyFdirHashTbl);
    Mock->Create(KnetDestroyFdirHashTbl, TEST_GetFuncRetPositive(0));

    ret = KNET_UninitTrans(procType);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KnetDestroyFdirHashTbl);
    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_FIND_NORMAL, NULL, NULL)
{
    struct Entry *ret = NULL;
    struct Entry *oldEntry = NULL;
    uint64_t *key = 1;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_hash_lookup_data, TEST_GetFuncRetNegative(1));

    ret = KnetFdirHashTblFind(key);
    DT_ASSERT_EQUAL(ret, NULL);

    struct rte_hash *g_fdirBck = g_fdirHandle;
    g_fdirHandle = NULL;
    ret = KnetFdirHashTblFind(key);
    DT_ASSERT_EQUAL(ret, NULL);

    g_fdirHandle = g_fdirBck;
    Mock->Delete(rte_hash_lookup_data);
    DeleteMock(Mock);
}

DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_DES_NORMAL, NULL, NULL)
{
    int ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_hash_iterate, TEST_GetFuncRetPositive(1));
    Mock->Create(rte_hash_del_key, TEST_GetFuncRetNegative(1));

    ret = KnetDestroyFdirHashTbl();
    DT_ASSERT_EQUAL(ret, -1);
    
    struct rte_hash *g_fdirBck = g_fdirHandle;
    g_fdirHandle = NULL;
    ret = KnetDestroyFdirHashTbl();
    DT_ASSERT_EQUAL(ret, 0);

    g_fdirHandle = g_fdirBck;

    Mock->Delete(rte_hash_iterate);
    Mock->Delete(rte_hash_del_key);
    DeleteMock(Mock);
}

DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_ADD_NORMAL, NULL, NULL)
{
    int ret = 0;
    uint64_t key = 1;
    int id = 1;
    struct rte_flow *flow = 1;
    struct rte_flow *arpFlow = 1;
    struct rte_hash *g_fdirBck = g_fdirHandle;
    struct Entry *newEntry = (struct Entry *)malloc(sizeof(struct Entry));
    DT_ASSERT_NOT_EQUAL(newEntry, NULL);
    newEntry->ip_port = key;
    newEntry->map.flow = flow;
    newEntry->map.clientId = id;
    newEntry->map.arpFlow = arpFlow;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_hash_lookup, TEST_GetFuncRetNegative(1));
    Mock->Create(memset_s, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_hash_add_key_data, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_socket_id, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_ring_create, MockRteRingCreateAndStartQueue);
    Mock->Create(rte_eth_dev_rx_queue_start, TEST_GetFuncRetPositive(0));

    KnetCreateFdirHashTbl();
    ret = KnetFdirHashTblAdd(newEntry);
    DT_ASSERT_EQUAL(ret, 0);

    g_fdirHandle = NULL;
    ret = KnetFdirHashTblAdd(newEntry);
    DT_ASSERT_EQUAL(ret, -1);

    free(newEntry);
    Mock->Delete(rte_hash_lookup);
    Mock->Delete(memset_s);
    Mock->Delete(rte_hash_add_key_data);
    Mock->Delete(rte_ring_create);
    Mock->Delete(rte_socket_id);
    Mock->Delete(rte_eth_dev_rx_queue_start);
    g_fdirHandle = g_fdirBck;
    DeleteMock(Mock);
}

DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_Tx_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    int ret = 0;
    Mock->Create(rte_eth_tx_burst, TEST_GetFuncRetPositive(0));

    ret = KNET_TxBurst(0, NULL, 0, 0);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(rte_eth_tx_burst);
    DeleteMock(Mock);
}

DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_Rx_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    int ret = 0;
    Mock->Create(rte_eth_rx_burst, TEST_GetFuncRetPositive(0));

    ret = KNET_RxBurst(0, NULL, 0, 0);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(rte_eth_rx_burst);
    DeleteMock(Mock);
}

DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_CTLFLOWCHANGE_NORMAL, NULL, NULL)
{
    int ret = 0;
    uint32_t queueId = 1;
    struct rte_flow *arpFlow = 1;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_hash_iterate, TEST_GetFuncRetNegative(1));
    Mock->Create(KNET_DeleteFlowRule, TEST_GetFuncRetPositive(0));

    ret = CtrFlowChange(queueId, arpFlow);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Create(KNET_DeleteFlowRule, TEST_GetFuncRetNegative(1));

    ret = CtrFlowChange(queueId, arpFlow);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(rte_hash_iterate);
    Mock->Delete(KNET_DeleteFlowRule);
    DeleteMock(Mock);
}

DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_GENFLOW_NORMAL, NULL, NULL)
{
    int ret = 0;
    struct KNET_FDirRequest flowReq = {0};
    flowReq.type = 0;
    flowReq.queueId[0] = 0;
    flowReq.queueIdSize = 1;
    flowReq.dstIp = 0;
    flowReq.dstIpMask = DST_IPMASK;
    flowReq.dstPort = 0;
    flowReq.dstPortMask = DST_PORTMASK;
    flowReq.proto = 6; // TCP proto num 6.
    struct rte_flow *flow = NULL;
    struct rte_flow *arpFlow = NULL;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GenerateArpFlow, TEST_GetFuncRetPositive(0));
    Mock->Create(GenerateIpv4PortFlow, TEST_GetFuncRetPositive(0));

    ret = GenerateFlow(&flowReq, &flow, &arpFlow);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Create(GenerateIpv4PortFlow, TEST_GetFuncRetPositive(1));
    ret = GenerateFlow(&flowReq, &flow, &arpFlow);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KNET_GenerateArpFlow);
    Mock->Delete(GenerateIpv4PortFlow);
    DeleteMock(Mock);
}

DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_CONNECT_HANDLER_NORMAL, NULL, NULL)
{
    int ret = 0;
    struct KNET_FDirRequest flowReq = {0};
    flowReq.type = 0;
    flowReq.queueId[0] = 0;
    flowReq.queueIdSize = 1;
    flowReq.dstIp = 0;
    flowReq.dstIpMask = DST_IPMASK;
    flowReq.dstPort = 0;
    flowReq.dstPortMask = DST_PORTMASK;
    flowReq.proto = 6; // TCP proto num 6.
    uint64_t key = 1;
    int id = 1;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KnetFdirHashTblFind, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetFdirHashTblAdd, TEST_GetFuncRetPositive(0));
    Mock->Create(GenerateFlow, TEST_GetFuncRetPositive(0));

    Mock->Create(KnetFdirHashTblAdd, TEST_GetFuncRetNegative(1));
    ret = ConnectHandler(id, &flowReq, &key);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Create(GenerateFlow, TEST_GetFuncRetNegative(1));
    ret = ConnectHandler(id, &flowReq, &key);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KnetFdirHashTblFind);
    Mock->Delete(KnetFdirHashTblAdd);
    Mock->Delete(GenerateFlow);
    DeleteMock(Mock);
}

static struct Entry *MockFdirHashTblFindTest1()
{
    static struct Entry oldEntry = {0};
    KNET_HalAtomicSet64(&oldEntry.map.count, 1);
    oldEntry.map.clientId = 2; // 设置为2可以保证打桩后可以进入源代码不同的分支
    return &oldEntry;
}

static struct Entry *MockFdirHashTblFindTest2()
{
    static struct Entry oldEntry = {0};
    KNET_HalAtomicSet64(&oldEntry.map.count, 1);
    oldEntry.map.clientId = 1; // 保证可以进入源代码特定的分支
    oldEntry.map.arpFlow = NULL;
    return &oldEntry;
}

static struct Entry *MockFdirHashTblFindTest3(uint64_t *key)
{
    static struct Entry oldEntry = {0};
    oldEntry.map.queueIdSize = 1;
    oldEntry.map.queueId[0] = 1;
    return &oldEntry;
}

DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_DISCONNECT_HANDLER_NORMAL, NULL, NULL)
{
    int ret = 0;
    uint64_t key = 1;
    int id = 1;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KnetFdirHashTblFind, TEST_GetFuncRetPositive(0));

    ret = DisconnectHandler(id, &key);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KnetFdirHashTblFind);
    Mock->Create(KnetFdirHashTblFind, MockFdirHashTblFindTest1);
    ret = DisconnectHandler(id, &key);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KnetFdirHashTblFind);
    Mock->Create(KnetFdirHashTblFind, MockFdirHashTblFindTest2);
    Mock->Create(KnetFdirHashTblDel, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_DeleteFlowRule, TEST_GetFuncRetNegative(1));
    Mock->Create(KNET_HalAtomicRead64, TEST_GetFuncRetPositive(0));
    ret = DisconnectHandler(id, &key);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_DeleteFlowRule);
    Mock->Delete(KnetFdirHashTblDel);

    Mock->Create(KNET_DeleteFlowRule, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetFdirHashTblDel, TEST_GetFuncRetPositive(0));
    ret = DisconnectHandler(id, &key);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Create(KnetFdirHashTblDel, TEST_GetFuncRetNegative(1));
    ret = DisconnectHandler(id, &key);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KnetFdirHashTblFind);
    Mock->Delete(KNET_DeleteFlowRule);
    Mock->Delete(KnetFdirHashTblDel);
    Mock->Delete(KNET_HalAtomicRead64);
    DeleteMock(Mock);
}

DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_FDIR_PROCESS, NULL, NULL)
{
    int ret = 0;
    struct KNET_FDirRequest flowReq = {0};
    flowReq.type = 0;
    flowReq.queueId[0] = 0;
    flowReq.queueIdSize = 1;
    flowReq.dstIp = 0;
    flowReq.dstIpMask = DST_IPMASK;
    flowReq.dstPort = 0;
    flowReq.dstPortMask = DST_PORTMASK;
    flowReq.proto = 6; // TCP proto num 6.
    uint64_t key = 1;
    int id = 1;
    uint32_t type = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(ConnectHandler, TEST_GetFuncRetPositive(0));
    ret = FdirProcess(id, &flowReq, &key, type);
    DT_ASSERT_EQUAL(ret, 0);

    type = 1;
    Mock->Create(DisconnectHandler, TEST_GetFuncRetPositive(0));
    ret = FdirProcess(id, &flowReq, &key, type);
    DT_ASSERT_EQUAL(ret, 0);

    type = 2; // 2是错误的类型，可以进入源代码的错误分支
    ret = FdirProcess(id, &flowReq, &key, type);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(ConnectHandler);
    Mock->Delete(DisconnectHandler);
    DeleteMock(Mock);
}

DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_FDIR_REQUEST_HANDLER, NULL, NULL)
{
    int ret = 0;
    int id = 1;
    struct KNET_RpcMessage knetRpcRequest = {0};
    struct KNET_RpcMessage knetRpcResponse = {0};

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(FdirProcess, TEST_GetFuncRetPositive(0));
    ret = FDirRequestHandler(id, &knetRpcRequest, &knetRpcResponse);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(FdirProcess);

    Mock->Create(FdirProcess, TEST_GetFuncRetNegative(1));
    ret = FDirRequestHandler(id, &knetRpcRequest, &knetRpcResponse);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Create(FdirProcess, TEST_GetFuncRetPositive(0));
    Mock->Create(memcpy_s, TEST_GetFuncRetNegative(1));
    ret = FDirRequestHandler(id, &knetRpcRequest, &knetRpcResponse);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(FdirProcess);
    Mock->Delete(memcpy_s);
    DeleteMock(Mock);
}

DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_FDIR_DISCONNECT_HANDLER, NULL, NULL)
{
    int ret = 0;
    int id = 1;
    struct KNET_RpcMessage knetRpcRequest = {0};
    struct KNET_RpcMessage knetRpcResponse = {0};

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_hash_iterate, TEST_GetFuncRetNegative(1));
    ret = FdirDisconnectHandler(id, &knetRpcRequest, &knetRpcResponse);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(rte_hash_iterate);
    DeleteMock(Mock);
}

DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_FIND_FDIR_QUE, NULL, NULL)
{
    int ret = 0;
    uint32_t dstIp = 0;
    uint16_t dstPort = 0;
    uint16_t queueId[1] = {0};

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KnetFdirHashTblFind, TEST_GetFuncRetPositive(0));
    ret = KNET_FindFdirQue(dstIp, dstPort, queueId);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KnetFdirHashTblFind);

    Mock->Create(KnetFdirHashTblFind, MockFdirHashTblFindTest3);
    Mock->Create(KNET_IsNeedFlowRule, TEST_GetFuncRetPositive(1));
    ret = KNET_FindFdirQue(dstIp, dstPort, queueId);
    DT_ASSERT_EQUAL(ret, 1);

    Mock->Delete(KnetFdirHashTblFind);
    Mock->Delete(KNET_IsNeedFlowRule);
    DeleteMock(Mock);
}

DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_RTE_RING_CREATE_START_QUE, NULL, NULL)
{
    uint16_t portId;
    uint16_t queueId;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(snprintf_s, TEST_GetFuncRetNegative(1));
    struct rte_ring *cpdTapRing = RteRingCreateAndStartQueue(portId, queueId);
    DT_ASSERT_EQUAL(cpdTapRing, NULL);
    
    Mock->Create(snprintf_s, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_ring_lookup, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_ring_create, TEST_GetFuncRetPositive(0));
    cpdTapRing = RteRingCreateAndStartQueue(portId, queueId);
    DT_ASSERT_EQUAL(cpdTapRing, NULL);

    Mock->Create(rte_eth_dev_rx_queue_start, TEST_GetFuncRetNegative(1));
    Mock->Create(rte_ring_free, TEST_GetFuncRetPositive(0));
    cpdTapRing = RteRingCreateAndStartQueue(portId, queueId);
    DT_ASSERT_EQUAL(cpdTapRing, NULL);

    Mock->Delete(snprintf_s);
    Mock->Delete(rte_ring_lookup);
    Mock->Delete(rte_ring_create);
    Mock->Delete(rte_eth_dev_rx_queue_start);
    Mock->Delete(rte_ring_free);
    DeleteMock(Mock);
}

/**
 * @brief 仅在非共线程场景下获取流表的qid
 * 样例一
 * 输入：设置runMode值为KNET_RUN_MODE_MULTIPLE
 * 打桩：打桩KNET_GetCfg，判断为非共线程
 * 期望：获取流表，返回0

 * 样例二
 * 输入：在共线程场景下，提前结束函数
 * 打桩：打桩KNET_GetCfg，判断为共线程
 * 期望：提前结束，返回0
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_GET_FLOW_QUE, NULL, NULL)
{
    int ret = 0;
    int runMode = KNET_RUN_MODE_MULTIPLE;
    struct KNET_FDirRequest flowReq = {0};
    flowReq.type = 0;
    flowReq.queueId[0] = 0;
    flowReq.queueIdSize = 1;
    flowReq.dstIp = 0;
    flowReq.dstIpMask = DST_IPMASK;
    flowReq.dstPort = 0;
    flowReq.dstPortMask = DST_PORTMASK;
    flowReq.proto = 6; // TCP proto num 6.

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    
    ret = GetFlowQueue(runMode, &flowReq);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    ret = GetFlowQueue(runMode, &flowReq);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

/**
 * @brief 从进程终止后，关掉从进程队列，清理ring
 * 输入：设置队列id为0
 * 打桩：打桩rte_eth_dev_rx_queue_stop，正常终止进程
 * 打桩：打桩rte_ring_lookup，返回空指针
 * 期望：释放ring，无返回值
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_RTE_RING_FREE, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_eth_dev_rx_queue_stop, TEST_GetFuncRetPositive(0));
    Mock->Create(snprintf_s, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_ring_lookup, MockRteRingLookupRetNULL);

    RteRingFree(0);

    Mock->Delete(rte_eth_dev_rx_queue_stop);
    Mock->Delete(snprintf_s);
    Mock->Delete(rte_ring_lookup);
    DeleteMock(Mock);
}
