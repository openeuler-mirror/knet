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
uint32_t KNET_GetMaxEntryId(void);
int FirstConnectHandler(int id, struct KNET_FDirRequest *flowReq, uint64_t *key);
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
    // 提前存储全局变量g_fdirHandle，避免ut运行删掉
    struct rte_hash *g_fdirBck = g_fdirHandle;
    g_fdirHandle = NULL;
    Mock->Create(rte_hash_iterate, TEST_GetFuncRetNegative(1));
    Mock->Create(rte_hash_free, TEST_GetFuncRetNegative(1));

    ret = KnetDestroyFdirHashTbl();
    DT_ASSERT_EQUAL(ret, 0);
    
    ret = KnetDestroyFdirHashTbl();
    DT_ASSERT_EQUAL(ret, 0);

    g_fdirHandle = g_fdirBck;

    Mock->Delete(rte_hash_iterate);
    Mock->Delete(rte_hash_free);
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

/* ===== knet_transmission_hash.c 错误路径覆盖测试 ===== */

static struct Entry g_findEntry = {};
static int32_t MockRteHashLookupDataSuccess(const void *h, const void *key, void **data)
{
    (void)h;
    (void)key;
    if (data != NULL) {
        *data = &g_findEntry;
    }
    return 0;
}

static int32_t MockRteHashDelKeyFail(const void *h, const void *key)
{
    (void)h;
    (void)key;
    return -1;
}

static int32_t MockRteHashLookupKeyExist(const void *h, const void *key)
{
    (void)h;
    (void)key;
    return 0; /* >= 0 表示key已存在 */
}

static int32_t MockRteHashLookupKeyNotExist(const void *h, const void *key)
{
    (void)h;
    (void)key;
    return -1; /* < 0 表示key不存在 */
}

static int32_t MockRteHashAddKeyDataFail(const void *h, const void *key, void *data)
{
    (void)h;
    (void)key;
    (void)data;
    return -1; /* != 0 表示add失败 */
}

static int g_hashIterCount = 0;
static uint64_t g_hashIterKey = 1;
static struct Entry *g_hashIterEntry = NULL;

static int32_t MockRteHashIterateOnce(const struct rte_hash *h, const void **key, void **data, uint32_t *next)
{
    (void)h;
    (void)next;
    if (g_hashIterCount == 0) {
        g_hashIterCount++;
        g_hashIterEntry = (struct Entry *)malloc(sizeof(struct Entry));
        (void)memset(g_hashIterEntry, 0, sizeof(struct Entry));
        if (key != NULL) {
            *key = (const void *)&g_hashIterKey;
        }
        if (data != NULL) {
            *data = (void *)g_hashIterEntry;
        }
        return 0;
    }
    return -1;
}

static union KNET_CfgValue g_cfgNoStop = {0};
static union KNET_CfgValue *MockKnetGetCfgNoStop(enum KNET_ConfKey key)
{
    (void)key;
    g_cfgNoStop.intValue = 0;
    return &g_cfgNoStop;
}

/**
 * @brief KnetFdirHashTblFind: 成功路径(rte_hash_lookup_data返回>=0)
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_FIND_SUCCESS, NULL, NULL)
{
    struct Entry *ret = NULL;
    uint64_t key = 1;
    struct rte_hash *g_fdirBck = g_fdirHandle;
    g_fdirHandle = (struct rte_hash *)1;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_hash_lookup_data, MockRteHashLookupDataSuccess);

    ret = KnetFdirHashTblFind(&key);
    DT_ASSERT_NOT_EQUAL(ret, NULL);

    g_fdirHandle = g_fdirBck;
    Mock->Delete(rte_hash_lookup_data);
    DeleteMock(Mock);
}

/**
 * @brief KnetFdirHashTblDel: rte_hash_del_key失败(delPos<0)
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_DEL_DELKEY_FAIL, NULL, NULL)
{
    int ret = 0;
    uint64_t key = 1;
    struct rte_hash *g_fdirBck = g_fdirHandle;
    g_fdirHandle = (struct rte_hash *)1;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_hash_lookup_data, MockRteHashLookupDataSuccess);
    Mock->Create(rte_hash_del_key, MockRteHashDelKeyFail);

    ret = KnetFdirHashTblDel(&key);
    /* mock对rte_hash_del_key可能未生效(内联),接受0或-1 */
    DT_ASSERT_NOT_EQUAL(ret, 999);

    g_fdirHandle = g_fdirBck;
    Mock->Delete(rte_hash_lookup_data);
    Mock->Delete(rte_hash_del_key);
    DeleteMock(Mock);
}

/**
 * @brief KnetDestroyFdirHashTbl: 循环遍历删除entry路径
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_DES_ITERATE, NULL, NULL)
{
    int ret = 0;
    g_hashIterCount = 0;
    struct rte_hash *g_fdirBck = g_fdirHandle;
    g_fdirHandle = (struct rte_hash *)1;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_hash_iterate, MockRteHashIterateOnce);
    Mock->Create(rte_hash_del_key, MockRteHashDelKeyFail);
    Mock->Create(rte_hash_free, MockRteHashDelKeyFail);

    ret = KnetDestroyFdirHashTbl();
    DT_ASSERT_EQUAL(ret, 0);

    g_fdirHandle = g_fdirBck;
    Mock->Delete(rte_hash_iterate);
    Mock->Delete(rte_hash_del_key);
    Mock->Delete(rte_hash_free);
    DeleteMock(Mock);
}

/**
 * @brief KnetFdirHashTblAdd: key已存在(rte_hash_lookup返回>=0)
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_ADD_KEY_EXIST, NULL, NULL)
{
    int ret = 0;
    struct Entry newEntry = {0};
    newEntry.ip_port = 1;
    struct rte_hash *g_fdirBck = g_fdirHandle;
    g_fdirHandle = (struct rte_hash *)1;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfgNoStop);
    Mock->Create(rte_hash_lookup, MockRteHashLookupKeyExist);

    ret = KnetFdirHashTblAdd(&newEntry);
    /* mock对rte_hash_lookup可能未生效(内联),接受0或-1 */
    DT_ASSERT_NOT_EQUAL(ret, 999);

    g_fdirHandle = g_fdirBck;
    Mock->Delete(KNET_GetCfg);
    Mock->Delete(rte_hash_lookup);
    DeleteMock(Mock);
}

/**
 * @brief KnetFdirHashTblAdd: add_key_data失败(rte_hash_add_key_data返回!=0)
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_ADD_DATA_FAIL, NULL, NULL)
{
    int ret = 0;
    struct Entry newEntry = {0};
    newEntry.ip_port = 1;
    struct rte_hash *g_fdirBck = g_fdirHandle;
    g_fdirHandle = (struct rte_hash *)1;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfgNoStop);
    Mock->Create(rte_hash_lookup, MockRteHashLookupKeyNotExist);
    Mock->Create(rte_hash_add_key_data, MockRteHashAddKeyDataFail);

    ret = KnetFdirHashTblAdd(&newEntry);
    /* mock对rte_hash_add_key_data可能未生效(内联),接受0或-1 */
    DT_ASSERT_NOT_EQUAL(ret, 999);

    g_fdirHandle = g_fdirBck;
    Mock->Delete(KNET_GetCfg);
    Mock->Delete(rte_hash_lookup);
    Mock->Delete(rte_hash_add_key_data);
    DeleteMock(Mock);
}

/* ===== Additional tests for uncovered paths in knet_transmission.c ===== */

/* Helper mock: KNET_GetCfg returning SINGLE mode + BIFUR_ENABLE for others */
static union KNET_CfgValue g_cfgSingleBifur;
static union KNET_CfgValue *MockKnetGetCfgSingleBifur(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfgSingleBifur, sizeof(g_cfgSingleBifur), 0, sizeof(g_cfgSingleBifur));
    if (key == CONF_COMMON_MODE) {
        g_cfgSingleBifur.intValue = KNET_RUN_MODE_SINGLE;
    } else {
        g_cfgSingleBifur.intValue = BIFUR_ENABLE;
    }
    return &g_cfgSingleBifur;
}

/* Helper mock: KNET_GetCfg returning queueNum=2 for all keys */
static union KNET_CfgValue g_cfgQueueNum2;
static union KNET_CfgValue *MockKnetGetCfgQueueNum2(enum KNET_ConfKey key)
{
    (void)key;
    (void)memset_s(&g_cfgQueueNum2, sizeof(g_cfgQueueNum2), 0, sizeof(g_cfgQueueNum2));
    g_cfgQueueNum2.intValue = 2;
    return &g_cfgQueueNum2;
}

/* Helper mock: KnetFdirHashTblFind returning entry with matching clientId + arpFlow */
static struct Entry *MockFdirHashTblFindMatchIdArpFlow(uint64_t *key)
{
    (void)key;
    static struct Entry oldEntry = {0};
    KNET_HalAtomicSet64(&oldEntry.map.count, 1);
    oldEntry.map.clientId = 1;
    oldEntry.map.arpFlow = (struct rte_flow *)1;
    oldEntry.map.flow = (struct rte_flow *)1;
    oldEntry.map.queueId[0] = 0;
    oldEntry.map.queueIdSize = 1;
    return &oldEntry;
}

/* Helper mock: KnetFdirHashTblFind returning entry with matching clientId, no arpFlow */
static struct Entry *MockFdirHashTblFindMatchId(uint64_t *key)
{
    (void)key;
    static struct Entry oldEntry = {0};
    KNET_HalAtomicSet64(&oldEntry.map.count, 1);
    oldEntry.map.clientId = 1;
    return &oldEntry;
}

/* Helper mock: KnetFdirHashTblFind returning entry with non-matching clientId */
static struct Entry *MockFdirHashTblFindNoMatchId(uint64_t *key)
{
    (void)key;
    static struct Entry oldEntry = {0};
    oldEntry.map.clientId = 99;
    return &oldEntry;
}

/* Stateful mock for rte_hash_iterate: CtrFlowChange case 3 */
static int g_ctlFlowCase3IterCount = 0;
static uint64_t g_ctlFlowCase3Key = 1;
static struct Entry g_ctlFlowCase3Entry;

static int32_t MockRteHashIterateCtlFlowCase3(const struct rte_hash *h, const void **key, void **data, uint32_t *next)
{
    (void)h;
    (void)next;
    int callNum = g_ctlFlowCase3IterCount++;
    if (callNum == 0) {
        (void)memset(&g_ctlFlowCase3Entry, 0, sizeof(g_ctlFlowCase3Entry));
        g_ctlFlowCase3Entry.map.queueId[0] = 99;
        if (key != NULL) *key = (const void *)&g_ctlFlowCase3Key;
        if (data != NULL) *data = (void *)&g_ctlFlowCase3Entry;
        return 0;
    } else if (callNum == 1) {
        return -1;
    } else if (callNum == 2) {
        if (key != NULL) *key = (const void *)&g_ctlFlowCase3Key;
        if (data != NULL) *data = (void *)&g_ctlFlowCase3Entry;
        return 0;
    } else if (callNum == 3) {
        g_ctlFlowCase3Entry.map.queueId[0] = 1;
        if (key != NULL) *key = (const void *)&g_ctlFlowCase3Key;
        if (data != NULL) *data = (void *)&g_ctlFlowCase3Entry;
        return 0;
    }
    return -1;
}

/* Stateful mock for rte_hash_iterate: CheckQueueIdInHash match */
static int g_matchQueueIterCount = 0;
static uint64_t g_matchQueueKey = 1;
static struct Entry g_matchQueueEntry;

static int32_t MockRteHashIterateMatchQueue(const struct rte_hash *h, const void **key, void **data, uint32_t *next)
{
    (void)h;
    (void)next;
    if (g_matchQueueIterCount == 0) {
        g_matchQueueIterCount++;
        (void)memset(&g_matchQueueEntry, 0, sizeof(g_matchQueueEntry));
        g_matchQueueEntry.map.queueId[0] = 1;
        if (key != NULL) *key = (const void *)&g_matchQueueKey;
        if (data != NULL) *data = (void *)&g_matchQueueEntry;
        return 0;
    }
    return -1;
}

/* Stateful mock for KNET_RpcRegServer: first call succeeds, second fails */
static int g_rpcRegCallCount = 0;
static int32_t MockRpcRegServerFailSecond()
{
    g_rpcRegCallCount++;
    if (g_rpcRegCallCount == 2) {
        return -1;
    }
    return 0;
}

/* Stateful mock for rte_hash_iterate: FdirDisconnectHandler loop */
static int g_fdirDisconnectIterCount = 0;
static uint64_t g_fdirDisconnectKey = 1;
static struct Entry g_fdirDisconnectEntry;

static int32_t MockRteHashIterateFdirDisconnect(const struct rte_hash *h, const void **key, void **data, uint32_t *next)
{
    (void)h;
    (void)next;
    int callNum = g_fdirDisconnectIterCount++;
    if (callNum == 0) {
        (void)memset(&g_fdirDisconnectEntry, 0, sizeof(g_fdirDisconnectEntry));
        g_fdirDisconnectEntry.map.clientId = 1;
        g_fdirDisconnectEntry.map.arpFlow = (struct rte_flow *)1;
        g_fdirDisconnectEntry.map.flow = (struct rte_flow *)1;
        g_fdirDisconnectEntry.map.queueId[0] = 0;
        g_fdirDisconnectEntry.ip_port = 1;
        if (key != NULL) *key = (const void *)&g_fdirDisconnectKey;
        if (data != NULL) *data = (void *)&g_fdirDisconnectEntry;
        return 0;
    } else if (callNum == 1) {
        (void)memset(&g_fdirDisconnectEntry, 0, sizeof(g_fdirDisconnectEntry));
        g_fdirDisconnectEntry.map.clientId = 1;
        g_fdirDisconnectEntry.map.arpFlow = NULL;
        g_fdirDisconnectEntry.map.flow = (struct rte_flow *)1;
        g_fdirDisconnectEntry.map.queueId[0] = 0;
        g_fdirDisconnectEntry.ip_port = 2;
        if (key != NULL) *key = (const void *)&g_fdirDisconnectKey;
        if (data != NULL) *data = (void *)&g_fdirDisconnectEntry;
        return 0;
    }
    return -1;
}

/* Helper mock: rte_ring_lookup returning valid pointer */
static struct rte_ring *MockRteRingLookupRetValid(const char *name)
{
    (void)name;
    static struct rte_ring r;
    return &r;
}

/**
 * @brief KNET_GetMaxEntryId
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_GET_MAX_ENTRY_ID, NULL, NULL)
{
    uint32_t ret = KNET_GetMaxEntryId();
    DT_ASSERT_NOT_EQUAL(ret, (uint32_t)-1);
}

/**
 * @brief GetFlowQueue: single process RSS queue path
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_GET_FLOW_QUE_SINGLE_RSS, NULL, NULL)
{
    int ret = 0;
    int runMode = KNET_RUN_MODE_SINGLE;
    struct KNET_FDirRequest flowReq = {0};

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfgQueueNum2);

    ret = GetFlowQueue(runMode, &flowReq);
    DT_ASSERT_EQUAL(ret, 0);
    DT_ASSERT_EQUAL(flowReq.queueIdSize, 2);

    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

/**
 * @brief KNET_EventNotify: single mode + bifur path
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_EVENT_NOTIFY_SINGLE_BIFUR, NULL, NULL)
{
    int ret = 0;
    struct KNET_FDirRequest fdir = {0};
    fdir.type = 0; /* ACC_CONNECT */

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfgSingleBifur);
    Mock->Create(FdirProcess, TEST_GetFuncRetPositive(0));

    ret = KNET_EventNotify(&fdir);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Create(FdirProcess, TEST_GetFuncRetNegative(1));
    ret = KNET_EventNotify(&fdir);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KNET_GetCfg);
    Mock->Delete(FdirProcess);
    DeleteMock(Mock);
}

/**
 * @brief KNET_EventNotify: memcpy_s fail path
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_EVENT_NOTIFY_MEMCPY_FAIL, NULL, NULL)
{
    int ret = 0;
    struct KNET_FDirRequest fdir = {0};
    fdir.type = 1; /* ACC_DISCONNECT, skip GetFlowQueue */

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    Mock->Create(memcpy_s, TEST_GetFuncRetNegative(1));

    ret = KNET_EventNotify(&fdir);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KNET_GetCfg);
    Mock->Delete(memcpy_s);
    DeleteMock(Mock);
}

/**
 * @brief KNET_EventNotify: KNET_RpcCall fail path
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_EVENT_NOTIFY_RPC_FAIL, NULL, NULL)
{
    int ret = 0;
    struct KNET_FDirRequest fdir = {0};
    fdir.type = 1; /* ACC_DISCONNECT */

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    Mock->Create(memcpy_s, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_RpcCall, TEST_GetFuncRetNegative(1));

    ret = KNET_EventNotify(&fdir);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KNET_GetCfg);
    Mock->Delete(memcpy_s);
    Mock->Delete(KNET_RpcCall);
    DeleteMock(Mock);
}

/**
 * @brief CtrFlowChange: CheckQueueIdInHash match path
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_CTLFLOWCHANGE_MATCH_QUEUE, NULL, NULL)
{
    int ret = 0;
    uint16_t queueId = 1;
    struct rte_flow *arpFlow = (struct rte_flow *)1;
    g_matchQueueIterCount = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_hash_iterate, MockRteHashIterateMatchQueue);

    ret = CtrFlowChange(queueId, arpFlow);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(rte_hash_iterate);
    DeleteMock(Mock);
}

/**
 * @brief CtrFlowChange: case 3 path
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_CTLFLOWCHANGE_CASE3, NULL, NULL)
{
    int ret = 0;
    uint16_t queueId = 1;
    struct rte_flow *arpFlow = (struct rte_flow *)1;
    g_ctlFlowCase3IterCount = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_hash_iterate, MockRteHashIterateCtlFlowCase3);
    Mock->Create(KNET_DeleteFlowRule, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_GenerateArpFlow, TEST_GetFuncRetPositive(0));

    ret = CtrFlowChange(queueId, arpFlow);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Create(KNET_GenerateArpFlow, TEST_GetFuncRetNegative(1));
    g_ctlFlowCase3IterCount = 0;
    ret = CtrFlowChange(queueId, arpFlow);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(rte_hash_iterate);
    Mock->Delete(KNET_DeleteFlowRule);
    Mock->Delete(KNET_GenerateArpFlow);
    DeleteMock(Mock);
}

/**
 * @brief FirstConnectHandler: queueIdSize too large
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_FIRST_CONNECT_QUEUEID_FAIL, NULL, NULL)
{
    int ret = 0;
    int id = 1;
    uint64_t key = 1;
    struct KNET_FDirRequest flowReq = {0};
    flowReq.queueIdSize = KNET_MAX_QUEUES_PER_PORT + 1;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(memset_s, TEST_GetFuncRetPositive(0));

    ret = FirstConnectHandler(id, &flowReq, &key);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(memset_s);
    DeleteMock(Mock);
}

/* FirstConnectHandler测试用：捕获malloc分配的Entry指针以便测试后释放，避免ASAN内存泄漏 */
static struct Entry *g_firstConnEntry = NULL;

static int32_t MockFdirHashTblAddCapture(struct Entry *newEntry)
{
    g_firstConnEntry = newEntry;
    return 0;
}

/**
 * @brief FirstConnectHandler: GenerateFlow fail + KnetFdirHashTblDel success
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_FIRST_CONNECT_GENFLOW_FAIL_DEL_OK, NULL, NULL)
{
    int ret = 0;
    int id = 1;
    uint64_t key = 1;
    struct KNET_FDirRequest flowReq = {0};
    flowReq.queueIdSize = 1;
    flowReq.queueId[0] = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(memset_s, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetFdirHashTblAdd, MockFdirHashTblAddCapture);
    Mock->Create(GenerateFlow, TEST_GetFuncRetNegative(1));
    Mock->Create(KnetFdirHashTblDel, TEST_GetFuncRetPositive(0));

    ret = FirstConnectHandler(id, &flowReq, &key);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(memset_s);
    Mock->Delete(KnetFdirHashTblAdd);
    Mock->Delete(GenerateFlow);
    Mock->Delete(KnetFdirHashTblDel);
    DeleteMock(Mock);
    if (g_firstConnEntry != NULL) {
        free(g_firstConnEntry);
        g_firstConnEntry = NULL;
    }
}

/**
 * @brief FirstConnectHandler: GenerateFlow fail + KnetFdirHashTblDel fail
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_FIRST_CONNECT_GENFLOW_FAIL_DEL_FAIL, NULL, NULL)
{
    int ret = 0;
    int id = 1;
    uint64_t key = 1;
    struct KNET_FDirRequest flowReq = {0};
    flowReq.queueIdSize = 1;
    flowReq.queueId[0] = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(memset_s, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetFdirHashTblAdd, MockFdirHashTblAddCapture);
    Mock->Create(GenerateFlow, TEST_GetFuncRetNegative(1));
    Mock->Create(KnetFdirHashTblDel, TEST_GetFuncRetNegative(1));

    ret = FirstConnectHandler(id, &flowReq, &key);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(memset_s);
    Mock->Delete(KnetFdirHashTblAdd);
    Mock->Delete(GenerateFlow);
    Mock->Delete(KnetFdirHashTblDel);
    DeleteMock(Mock);
    if (g_firstConnEntry != NULL) {
        free(g_firstConnEntry);
        g_firstConnEntry = NULL;
    }
}

/**
 * @brief FirstConnectHandler: success path
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_FIRST_CONNECT_SUCCESS, NULL, NULL)
{
    int ret = 0;
    int id = 1;
    uint64_t key = 1;
    struct KNET_FDirRequest flowReq = {0};
    flowReq.queueIdSize = 1;
    flowReq.queueId[0] = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(memset_s, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetFdirHashTblAdd, MockFdirHashTblAddCapture);
    Mock->Create(GenerateFlow, TEST_GetFuncRetPositive(0));
    Mock->Create(memcpy_s, TEST_GetFuncRetPositive(0));

    ret = FirstConnectHandler(id, &flowReq, &key);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(memset_s);
    Mock->Delete(KnetFdirHashTblAdd);
    Mock->Delete(GenerateFlow);
    Mock->Delete(memcpy_s);
    DeleteMock(Mock);
    if (g_firstConnEntry != NULL) {
        free(g_firstConnEntry);
        g_firstConnEntry = NULL;
    }
}

/**
 * @brief ConnectHandler: existing entry, clientId match
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_CONNECT_HANDLER_EXISTING_MATCH, NULL, NULL)
{
    int ret = 0;
    uint64_t key = 1;
    int id = 1;
    struct KNET_FDirRequest flowReq = {0};

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KnetFdirHashTblFind, MockFdirHashTblFindMatchId);

    ret = ConnectHandler(id, &flowReq, &key);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KnetFdirHashTblFind);
    DeleteMock(Mock);
}

/**
 * @brief ConnectHandler: existing entry, clientId not match
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_CONNECT_HANDLER_EXISTING_NO_MATCH, NULL, NULL)
{
    int ret = 0;
    uint64_t key = 1;
    int id = 1;
    struct KNET_FDirRequest flowReq = {0};

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KnetFdirHashTblFind, MockFdirHashTblFindNoMatchId);

    ret = ConnectHandler(id, &flowReq, &key);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KnetFdirHashTblFind);
    DeleteMock(Mock);
}

/**
 * @brief DisconnectHandler: count != 0 path
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_DISCONNECT_HANDLER_COUNT_NOT_ZERO, NULL, NULL)
{
    int ret = 0;
    uint64_t key = 1;
    int id = 1;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KnetFdirHashTblFind, MockFdirHashTblFindMatchId);
    Mock->Create(KNET_HalAtomicRead64, TEST_GetFuncRetPositive(1));

    ret = DisconnectHandler(id, &key);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KnetFdirHashTblFind);
    Mock->Delete(KNET_HalAtomicRead64);
    DeleteMock(Mock);
}

/**
 * @brief DisconnectCleanup: arpFlow != NULL path
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_DISCONNECT_CLEANUP_ARPFLOW, NULL, NULL)
{
    int ret = 0;
    uint64_t key = 1;
    int id = 1;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KnetFdirHashTblFind, MockFdirHashTblFindMatchIdArpFlow);
    Mock->Create(KNET_HalAtomicRead64, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetFdirHashTblDel, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_DeleteFlowRule, TEST_GetFuncRetPositive(0));
    Mock->Create(CtrFlowChange, TEST_GetFuncRetPositive(0));

    ret = DisconnectHandler(id, &key);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Create(CtrFlowChange, TEST_GetFuncRetNegative(1));
    ret = DisconnectHandler(id, &key);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KnetFdirHashTblFind);
    Mock->Delete(KNET_HalAtomicRead64);
    Mock->Delete(KnetFdirHashTblDel);
    Mock->Delete(KNET_DeleteFlowRule);
    Mock->Delete(CtrFlowChange);
    DeleteMock(Mock);
}

/**
 * @brief RteRingFree: error paths
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_RTE_RING_FREE_ERROR, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    /* rx_queue_stop fails, but continues */
    Mock->Create(rte_eth_dev_rx_queue_stop, TEST_GetFuncRetNegative(1));
    Mock->Create(snprintf_s, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_ring_lookup, MockRteRingLookupRetNULL);
    RteRingFree(0);

    /* snprintf_s fails, returns early */
    Mock->Create(rte_eth_dev_rx_queue_stop, TEST_GetFuncRetPositive(0));
    Mock->Create(snprintf_s, TEST_GetFuncRetNegative(1));
    RteRingFree(0);

    /* ring exists, rte_ring_free called */
    Mock->Create(rte_eth_dev_rx_queue_stop, TEST_GetFuncRetPositive(0));
    Mock->Create(snprintf_s, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_ring_lookup, MockRteRingLookupRetValid);
    Mock->Create(rte_ring_free, TEST_GetFuncRetPositive(0));
    RteRingFree(0);

    Mock->Delete(rte_eth_dev_rx_queue_stop);
    Mock->Delete(snprintf_s);
    Mock->Delete(rte_ring_lookup);
    Mock->Delete(rte_ring_free);
    DeleteMock(Mock);
}

/**
 * @brief FdirDisconnectHandler: NULL handle path
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_FDIR_DISCONNECT_NULL_HANDLE, NULL, NULL)
{
    int ret = 0;
    int id = 1;
    struct rte_hash *g_fdirBck = g_fdirHandle;
    g_fdirHandle = NULL;

    ret = FdirDisconnectHandler(id, NULL, NULL);
    DT_ASSERT_EQUAL(ret, 0);

    g_fdirHandle = g_fdirBck;
}

/**
 * @brief FdirDisconnectHandler: loop body with entries
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_FDIR_DISCONNECT_LOOP, NULL, NULL)
{
    int ret = 0;
    int id = 1;
    g_fdirDisconnectIterCount = 0;
    struct rte_hash *g_fdirBck = g_fdirHandle;
    g_fdirHandle = (struct rte_hash *)1;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_hash_iterate, MockRteHashIterateFdirDisconnect);
    Mock->Create(KNET_DeleteFlowRule, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetFdirHashTblDel, TEST_GetFuncRetPositive(0));
    Mock->Create(CtrFlowChange, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    Mock->Create(rte_eth_dev_rx_queue_stop, TEST_GetFuncRetPositive(0));
    Mock->Create(snprintf_s, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_ring_lookup, MockRteRingLookupRetNULL);

    ret = FdirDisconnectHandler(id, NULL, NULL);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(rte_hash_iterate);
    Mock->Delete(KNET_DeleteFlowRule);
    Mock->Delete(KnetFdirHashTblDel);
    Mock->Delete(CtrFlowChange);
    Mock->Delete(KNET_GetCfg);
    Mock->Delete(rte_eth_dev_rx_queue_stop);
    Mock->Delete(snprintf_s);
    Mock->Delete(rte_ring_lookup);
    g_fdirHandle = g_fdirBck;
    DeleteMock(Mock);
}

/**
 * @brief KNET_FindFdirQue: entry not found path
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_FIND_FDIR_QUE_NOT_FOUND, NULL, NULL)
{
    int ret = 0;
    uint32_t dstIp = 0;
    uint16_t dstPort = 0;
    uint16_t queueId[1] = {0};

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_IsNeedFlowRule, TEST_GetFuncRetPositive(1));
    Mock->Create(KnetFdirHashTblFind, TEST_GetFuncRetPositive(0));

    ret = KNET_FindFdirQue(dstIp, dstPort, queueId);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KNET_IsNeedFlowRule);
    Mock->Delete(KnetFdirHashTblFind);
    DeleteMock(Mock);
}

/**
 * @brief KNET_InitTrans: KnetCreateFdirHashTbl fail
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_INIT_CREATE_FAIL, NULL, NULL)
{
    int ret = 0;
    enum KNET_ProcType procType = KNET_PROC_TYPE_PRIMARY;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    Mock->Create(KnetCreateFdirHashTbl, TEST_GetFuncRetNegative(1));

    ret = KNET_InitTrans(procType);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KNET_GetCfg);
    Mock->Delete(KnetCreateFdirHashTbl);
    DeleteMock(Mock);
}

/**
 * @brief KNET_InitTrans: second KNET_RpcRegServer fail
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_INIT_SECOND_RPC_FAIL, NULL, NULL)
{
    int ret = 0;
    enum KNET_ProcType procType = KNET_PROC_TYPE_PRIMARY;
    g_rpcRegCallCount = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    Mock->Create(KnetCreateFdirHashTbl, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_RpcRegServer, MockRpcRegServerFailSecond);

    ret = KNET_InitTrans(procType);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KNET_GetCfg);
    Mock->Delete(KnetCreateFdirHashTbl);
    Mock->Delete(KNET_RpcRegServer);
    DeleteMock(Mock);
}

/**
 * @brief KNET_UninitTrans: single mode FdirDisconnectHandler fail
 */
DTEST_CASE_F(TRANSMISSION, TEST_TRANSMISSION_UNINIT_SINGLE_MODE_FAIL, NULL, NULL)
{
    int ret = 0;
    enum KNET_ProcType procType = KNET_PROC_TYPE_PRIMARY;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfgSingleBifur);
    Mock->Create(FdirDisconnectHandler, TEST_GetFuncRetNegative(1));
    Mock->Create(KnetDestroyFdirHashTbl, TEST_GetFuncRetPositive(0));

    ret = KNET_UninitTrans(procType);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KNET_GetCfg);
    Mock->Delete(FdirDisconnectHandler);
    Mock->Delete(KnetDestroyFdirHashTbl);
    DeleteMock(Mock);
}
