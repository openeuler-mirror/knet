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

#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/sysinfo.h>

#include "rte_ethdev.h"
#include "rte_pdump.h"
#include "rte_lcore.h"
#include "rte_ring.h"
#include "rte_flow.h"
#include "rte_ring.h"
#include "rte_mempool.h"
#include "rte_memzone.h"
#include "rte_mbuf_core.h"
#include "rte_bpf.h"
#include "rte_malloc.h"

#include "dp_cfg_api.h"

#include "knet_mock.h"
#include "knet_lock.h"
#include "knet_types.h"
#include "knet_log.h"
#include "knet_pktpool.h"
#include "knet_hash_table.h"
#include "knet_lock.h"
#include "knet_dpdk_init.h"
#include "knet_pdump.h"
#include "knet_pkt.h"
#include "knet_thread.h"
#include "knet_offload.h"
#include "knet_utils.h"
#include "knet_init_tcp.h"
#include "knet_lock.h"
#include "knet_dpdk_init_dev.h"
#include "securec.h"
#include "common.h"
#include "mock.h"

#define L2_ARP_HEADER_LEN 42
#define L2_ETH_HEADER_LEN 14
#define GENERAL_PKT_LEN 74
extern  "C" {
#include "knet_telemetry.h"
extern bool g_dpdkInited;
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

struct PdumpRequest {
    uint16_t ver;                       /* 抓包版本，当前支持V1 */
    uint16_t op;                        /* 开关标志位 */
    uint32_t flags;                     /* RX/TX方向的报文 */
    char device[RTE_DEV_NAME_MAX_LEN];  /* 设备名称 */
    uint16_t queue;                     /* 队列号，若抓包从进程指定RTE_PDUMP_ALL_QUEUES，则抓取所有队列 */
    const struct rte_bpf_prm *prm;      /* 过滤条件 */
    KNET_SpinLock sharedLock;           /* 多进程抓包同步锁 */
};

#define MAX_WORKER_ID 512
typedef struct {
    KNET_DpWorkerInfo workerInfo[MAX_WORKER_ID];
    uint32_t coreIdToWorkerId[MAX_WORKER_ID];
    uint32_t maxWorkerId;
    char padding[4];   // 填充字节，确保结构体8 字节对齐
} DpWorkerIdTable; // data plane worker id table
static union KNET_CfgValue g_cfg = {.intValue = 1};

int32_t KnetDpdkSlaveLcoreMatchDpWorker(void);
int32_t KnetGenerateIpv4TcpPortFlow(struct KNET_FlowCfg *flowCfg, struct rte_flow **flow);
int32_t DpdkPortTxRxQueueNum(uint16_t portId, struct rte_eth_dev_info *devInfo, struct rte_eth_conf *localPortConf);
int32_t DpdkPortMtu(uint16_t portId, struct rte_eth_dev_info *devInfo);
int32_t SetupPort(uint16_t portId, struct rte_eth_dev_info *devInfo, struct rte_eth_conf *localPortConf);
int DpdkPortMacCheck(uint16_t portId);
int32_t KnetDpdkSlaveLcoreNumCheck(void);
int FreeDelayCpdRing(int procType, int processMode);
int DelayCpdRingInit(int procType, int processMode);
int GenerateDpdkCoreList(int dpdkCore, int procType, char* coreList, char* mainLcore);
int32_t KNET_ACC_WorkerGetSelfId(void);
void SetPdumpLock(KNET_SpinLock * lock);
uint16_t PdumpRx(uint16_t port, uint16_t queue, struct rte_mbuf **pkts, uint16_t nbPkts,
    uint16_t max_pkts __rte_unused, void *userParams);
uint16_t PdumpTx(uint16_t port, uint16_t queue, struct rte_mbuf **pkts, uint16_t nbPkts, void *userParams);
int GetHeaderLen(struct rte_mbuf *buf);
int ValidAndRegister(const struct PdumpRequest *pr, uint16_t port, struct rte_bpf *filter);
int32_t DpdkDfxInit(int procType, int processMode);
int32_t RteEalInit(int32_t argc, char **argv);
void DpdkMultiQueueSetup(const struct rte_eth_dev_info *devInfo, struct rte_eth_conf *portConf);
int32_t DpdkHwChecksumSetup(const struct rte_eth_dev_info *devInfo, struct rte_eth_conf *portConf);
int32_t BondInit(int procType);
int32_t BondSlavesInit(int procType);
int32_t DpdkPortInit(int procType);
int IsEthLinkUp(uint16_t portId);
}

static KnetPktPoolCtrl *mock_KnetPktGetPoolCtrl(uint32_t poolId)
{
    static KnetPktPoolCtrl pktPoolCtrl = {0};
    struct rte_mempool mempool = {0};
    pktPoolCtrl.mempool = &mempool;
    mempool.size = 1; // mock 1 size

    return &pktPoolCtrl;
}

static const struct rte_memzone* MOCK_KNET_MultiPdumpInit(void)
{
    const static struct rte_memzone ret = {0};
    return &ret;
}

static char* RealPathMock(const char *name, char *resolved)
{
    return NULL;
}

DTEST_CASE_F(CORE_DPDK, TEST_CORE_INIT_UNINIT_DPDK_NORMAL, NULL, NULL)
{
    int32_t ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_eal_init, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_MultiPdumpInit, MOCK_KNET_MultiPdumpInit);
    Mock->Create(KNET_InitDpdkTelemetry, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetDpdkSlaveLcoreMatchDpWorker, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_PktModInit, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_PktPoolCreate, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_eth_dev_get_port_by_name, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_eth_dev_info_get, mock_rte_eth_dev_info_get);
    Mock->Create(rte_eth_dev_configure, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_eth_dev_set_mtu, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_eth_dev_get_mtu, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_eth_tx_queue_setup, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_BondCreate, TEST_GetFuncRetPositive(0));
    /* KnetPktGetPoolCtrl返回的是指针，这里用1表示非空，需保证业务不会访问指针中的内容 */
    Mock->Create(KnetPktGetPoolCtrl, mock_KnetPktGetPoolCtrl);
    Mock->Create(rte_eth_rx_queue_setup, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_eth_dev_start, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_eth_dev_stop, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_PktPoolDestroy, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_HashTblDeinit, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_MultiPdumpUninit, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_eal_cleanup, TEST_GetFuncRetPositive(0));
    Mock->Create(DpdkPortMacCheck, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_socket_count, TEST_GetFuncRetPositive(1));
    Mock->Create(rte_socket_id_by_idx, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_malloc_get_socket_stats, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_mempool_avail_count, TEST_GetFuncRetPositive(0));
    Mock->Create(DelayCpdRingInit, TEST_GetFuncRetPositive(0));
    Mock->Create(FreeDelayCpdRing, TEST_GetFuncRetPositive(0));
    Mock->Create(realpath, RealPathMock);
    ret = KNET_InitDpdk(KNET_PROC_TYPE_PRIMARY, KNET_RUN_MODE_SINGLE);
    DT_ASSERT_EQUAL(ret, 0);
    KNET_INFO("knet dpdk init success");

    ret = KNET_InitDpdk(KNET_PROC_TYPE_PRIMARY, KNET_RUN_MODE_MULTIPLE);
    DT_ASSERT_EQUAL(ret, 0);
    KNET_INFO("knet dpdk init success");

    ret = KNET_InitDpdk(KNET_PROC_TYPE_SECONDARY, KNET_RUN_MODE_MULTIPLE);
    DT_ASSERT_EQUAL(ret, 0);
    KNET_INFO("knet dpdk init success");

    ret = KNET_UninitDpdk(KNET_PROC_TYPE_SECONDARY, KNET_RUN_MODE_MULTIPLE);
    DT_ASSERT_EQUAL(ret, 0);
    KNET_INFO("knet dpdk uninit success");
    Mock->Delete(realpath);
    Mock->Delete(rte_mempool_avail_count);
    Mock->Delete(rte_malloc_get_socket_stats);
    Mock->Delete(rte_socket_id_by_idx);
    Mock->Delete(rte_socket_count);
    Mock->Delete(DpdkPortMacCheck);
    Mock->Delete(rte_eal_init);
    Mock->Delete(KNET_MultiPdumpInit);
    Mock->Delete(KNET_InitDpdkTelemetry);
    Mock->Delete(KnetDpdkSlaveLcoreMatchDpWorker);
    Mock->Delete(KNET_PktModInit);
    Mock->Delete(KNET_PktPoolCreate);
    Mock->Delete(rte_eth_dev_get_port_by_name);
    Mock->Delete(rte_eth_dev_info_get);
    Mock->Delete(rte_eth_dev_configure);
    Mock->Delete(rte_eth_dev_get_mtu);
    Mock->Delete(rte_eth_dev_set_mtu);
    Mock->Delete(KNET_BondCreate);
    Mock->Delete(rte_eth_tx_queue_setup);
    Mock->Delete(KnetPktGetPoolCtrl);
    Mock->Delete(rte_eth_rx_queue_setup);
    Mock->Delete(rte_eth_dev_start);
    Mock->Delete(rte_eth_dev_stop);
    Mock->Delete(KNET_PktPoolDestroy);
    Mock->Delete(KNET_HashTblDeinit);
    Mock->Delete(KNET_MultiPdumpUninit);
    Mock->Delete(rte_eal_cleanup);
    Mock->Delete(DelayCpdRingInit);
    Mock->Delete(FreeDelayCpdRing);

    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_KNET_SET_LRO_NULL, NULL, NULL)
{
    uint16_t portId = 0;
    struct rte_eth_dev_info devInfo = {0};
    struct rte_eth_conf localPortConf = {0};
    devInfo.rx_offload_capa |= RTE_ETH_RX_OFFLOAD_TCP_LRO;

    int32_t ret = KNET_SetLRO(&devInfo, &localPortConf, 0);
    DT_ASSERT_EQUAL(ret, 0);

    devInfo.rx_offload_capa &= RTE_ETH_TX_OFFLOAD_TCP_TSO;
    ret = KNET_SetLRO(&devInfo, &localPortConf, 0);
    DT_ASSERT_EQUAL(ret, -1);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_KNET_SET_TSO_NORMAL, NULL, NULL)
{
    uint16_t portId = 0;
    struct rte_eth_dev_info devInfo = {0};
    struct rte_eth_conf localPortConf = {0};
    devInfo.tx_offload_capa |= RTE_ETH_TX_OFFLOAD_TCP_TSO;

    int32_t ret = KNET_SetTSO(&devInfo, &localPortConf);
    DT_ASSERT_EQUAL(ret, 0);

    devInfo.tx_offload_capa &= RTE_ETH_RX_OFFLOAD_TCP_LRO;
    ret = KNET_SetTSO(&devInfo, &localPortConf);
    DT_ASSERT_EQUAL(ret, -1);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_GENE_IPV4_TCP_PORT_FLOW_NORMAL, NULL, NULL)
{
    int32_t ret = 0;
    struct KNET_FlowCfg flowCfg;
    flowCfg.srcIp = RTE_IPV4(192U, 168U, 1, 1);
    flowCfg.dstIp = RTE_IPV4(192U, 168U, 1, 2);
    flowCfg.srcIpMask = RTE_IPV4(255U, 255U, 255U, 255U);
    flowCfg.dstIpMask = RTE_IPV4(255U, 255U, 255U, 255U);
    flowCfg.srcPort = 12345;
    flowCfg.dstPort = 80;
    flowCfg.srcPortMask = 0xFFFF;
    flowCfg.dstPortMask = 0xFFFF;
    flowCfg.rxQueueId[0] = 0;
    flowCfg.rxQueueIdSize = 1;
    flowCfg.proto = IPPROTO_TCP;
    struct rte_flow *flow = NULL;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    struct KNET_FlowTeleInfo teleInfo = {0};
    Mock->Create(rte_flow_validate, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_flow_create, TEST_GetFuncRetPositive(1));
    ret = KNET_GenerateIpv4Flow(KNET_GetNetDevCtx()->xmitPortId, &flowCfg, &flow, &teleInfo);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(rte_flow_validate);
    Mock->Delete(rte_flow_create);

    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_GENE_IPV4_TCP_PORT_FLOW_RSS_NORMAL, NULL, NULL)
{
    int32_t ret = 0;
    struct KNET_FlowCfg flowCfg;
    flowCfg.srcIp = RTE_IPV4(192U, 168U, 1, 1),
    flowCfg.dstIp = RTE_IPV4(192U, 168U, 1, 2),
    flowCfg.srcIpMask = RTE_IPV4(255U, 255U, 255U, 255U),
    flowCfg.dstIpMask = RTE_IPV4(255U, 255U, 255U, 255U),
    flowCfg.srcPort = 12345,
    flowCfg.dstPort = 80,
    flowCfg.srcPortMask = 0xFFFF,
    flowCfg.dstPortMask = 0xFFFF,
    flowCfg.rxQueueIdSize = 2,
    flowCfg.proto = IPPROTO_TCP,
    flowCfg.rxQueueId[0] = 0;
    flowCfg.rxQueueId[1] = 1;
    struct rte_flow *flow = NULL;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    struct KNET_FlowTeleInfo teleInfo = {0};
    Mock->Create(rte_flow_validate, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_flow_create, TEST_GetFuncRetPositive(1));
    Mock->Create(rte_eth_dev_rss_hash_conf_get, TEST_GetFuncRetPositive(0));
    ret = KNET_GenerateIpv4Flow(KNET_GetNetDevCtx()->xmitPortId, &flowCfg, &flow, &teleInfo);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(rte_flow_validate);
    Mock->Delete(rte_flow_create);
    Mock->Delete(rte_eth_dev_rss_hash_conf_get);

    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_GENE_IPV4_TCP_PORT_FLOW_VALIDATE_ABNORMAL, NULL, NULL)
{
    int32_t ret = 0;
    struct KNET_FlowCfg flowCfg;
    flowCfg.srcIp = RTE_IPV4(192U, 168U, 1, 1);
    flowCfg.dstIp = RTE_IPV4(192U, 168U, 1, 2);
    flowCfg.srcIpMask = RTE_IPV4(255U, 255U, 255U, 255U);
    flowCfg.dstIpMask = RTE_IPV4(255U, 255U, 255U, 255U);
    flowCfg.srcPort = 12345;
    flowCfg.dstPort = 80;
    flowCfg.srcPortMask = 0xFFFF;
    flowCfg.dstPortMask = 0xFFFF;
    flowCfg.rxQueueId[0] = 0;
    flowCfg.rxQueueIdSize = 1;
    struct rte_flow *flow = NULL;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    struct KNET_FlowTeleInfo teleInfo = {0};
    Mock->Create(rte_flow_validate, TEST_GetFuncRetPositive(1));
    ret = KNET_GenerateIpv4Flow(KNET_GetNetDevCtx()->xmitPortId, &flowCfg, &flow, &teleInfo);
    DT_ASSERT_NOT_EQUAL(ret, 0);
    Mock->Delete(rte_flow_validate);

    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_GENE_IPV4_TCP_PORT_FLOW_CREATE_ABNORMAL, NULL, NULL)
{
    int32_t ret = 0;
    struct KNET_FlowCfg flowCfg;
    flowCfg.srcIp = RTE_IPV4(192U, 168U, 1, 1);
    flowCfg.dstIp = RTE_IPV4(192U, 168U, 1, 2);
    flowCfg.srcIpMask = RTE_IPV4(255U, 255U, 255U, 255U);
    flowCfg.dstIpMask = RTE_IPV4(255U, 255U, 255U, 255U);
    flowCfg.srcPort = 12345;
    flowCfg.dstPort = 80;
    flowCfg.srcPortMask = 0xFFFF;
    flowCfg.dstPortMask = 0xFFFF;
    flowCfg.rxQueueId[0] = 0;
    flowCfg.rxQueueIdSize = 1;
    flowCfg.proto = IPPROTO_TCP;
    struct rte_flow *flow = NULL;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    struct KNET_FlowTeleInfo teleInfo = {0};
    Mock->Create(rte_flow_validate, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_flow_create, TEST_GetFuncRetPositive(0));
    ret = KNET_GenerateIpv4Flow(KNET_GetNetDevCtx()->xmitPortId, &flowCfg, &flow, &teleInfo);
    DT_ASSERT_NOT_EQUAL(ret, 0);
    Mock->Delete(rte_flow_validate);
    Mock->Delete(rte_flow_create);

    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_CONFIG_PORT_NORMAL, NULL, NULL)
{
    uint16_t portId = 0;
    struct rte_eth_dev_info devInfo = {0};
    struct rte_eth_conf localPortConf = {0};

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_eth_dev_configure, TEST_GetFuncRetPositive(0));

    int32_t ret = DpdkPortTxRxQueueNum(portId, &devInfo, &localPortConf);

    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(rte_eth_dev_configure);
    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_CONFIG_PORT_ABNORMAL, NULL, NULL)
{
    uint16_t portId = 0;
    struct rte_eth_dev_info devInfo = {0};
    struct rte_eth_conf localPortConf = {0};

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_eth_dev_configure, TEST_GetFuncRetNegative(1));

    int32_t ret = DpdkPortTxRxQueueNum(portId, &devInfo, &localPortConf);

    DT_ASSERT_NOT_EQUAL(ret, 0);

    Mock->Delete(rte_eth_dev_configure);
    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_SET_MTU_NORMAL, NULL, NULL)
{
    uint16_t portId = 0;
    struct rte_eth_dev_info devInfo;
    devInfo.min_mtu = 1500;
    devInfo.max_mtu = 1500;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_eth_dev_set_mtu, TEST_GetFuncRetPositive(0));

    int32_t ret = DpdkPortMtu(portId, &devInfo);

    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(rte_eth_dev_set_mtu);
    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_SET_MTU_GET_CFG_ABNORMAL, NULL, NULL)
{
    uint16_t portId = 0;
    struct rte_eth_dev_info devInfo;
    devInfo.min_mtu = 1600;
    devInfo.max_mtu = 1600;

    int32_t ret = DpdkPortMtu(portId, &devInfo);
    DT_ASSERT_NOT_EQUAL(ret, 0);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_SET_MTU_RET_ABNORMAL, NULL, NULL)
{
    uint16_t portId = 0;
    struct rte_eth_dev_info devInfo;
    devInfo.min_mtu = 1500;
    devInfo.max_mtu = 1500;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_eth_dev_set_mtu, TEST_GetFuncRetNegative(1));

    int32_t ret = DpdkPortMtu(portId, &devInfo);

    DT_ASSERT_NOT_EQUAL(ret, 0);

    Mock->Delete(rte_eth_dev_set_mtu);
    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_SETUP_PORT_ABNORMAL, NULL, NULL)
{
    uint16_t portId = 0;
    struct rte_eth_dev_info devInfo = {0};
    struct rte_eth_conf localPortConf = {0};

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(DpdkPortMtu, TEST_GetFuncRetPositive(0));

    int32_t ret = SetupPort(portId, &devInfo, &localPortConf);

    DT_ASSERT_NOT_EQUAL(ret, 0);

    Mock->Delete(DpdkPortMtu);
    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_INIT_DPDK_PORT_NORMAL, NULL, NULL)
{
    uint16_t portId = 0;
    int procType = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_eth_dev_info_get, TEST_GetFuncRetPositive(0));
    Mock->Create(SetupPort, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_eth_dev_start, TEST_GetFuncRetPositive(0));
    Mock->Create(DpdkPortMacCheck, TEST_GetFuncRetPositive(0));

    int32_t ret = KnetInitDpdkPort(portId, procType, DPDK_PORT_NORMAL);

    DT_ASSERT_EQUAL(ret, 0);

    procType = 1;
    ret = KnetInitDpdkPort(portId, procType, DPDK_PORT_NORMAL);

    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(DpdkPortMacCheck);
    Mock->Delete(rte_eth_dev_info_get);
    Mock->Delete(SetupPort);
    Mock->Delete(rte_eth_dev_start);
    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_INIT_DPDK_PORT_ABNORMAL, NULL, NULL)
{
    int32_t ret = 0;
    uint16_t portId = 0;
    int procTypePrimary = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_eth_dev_info_get, TEST_GetFuncRetPositive(0));
    Mock->Create(SetupPort, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_eth_dev_start, TEST_GetFuncRetPositive(0));
    Mock->Create(DpdkPortMacCheck, TEST_GetFuncRetPositive(0));

    ret = KnetInitDpdkPort(portId, procTypePrimary, DPDK_PORT_NORMAL);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(rte_eth_dev_start);
    Mock->Create(rte_eth_dev_start, TEST_GetFuncRetPositive(1));

    ret = KnetInitDpdkPort(portId, procTypePrimary, DPDK_PORT_NORMAL);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(SetupPort);
    Mock->Create(SetupPort, TEST_GetFuncRetPositive(1));

    ret = KnetInitDpdkPort(portId, procTypePrimary, DPDK_PORT_NORMAL);
    DT_ASSERT_NOT_EQUAL(ret, 0);

    Mock->Delete(rte_eth_dev_info_get);
    Mock->Create(rte_eth_dev_info_get, TEST_GetFuncRetPositive(1));

    ret = KnetInitDpdkPort(portId, procTypePrimary, DPDK_PORT_NORMAL);
    DT_ASSERT_NOT_EQUAL(ret, 0);

    Mock->Delete(DpdkPortMacCheck);
    Mock->Delete(rte_eth_dev_info_get);
    Mock->Delete(SetupPort);
    Mock->Delete(rte_eth_dev_start);
    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_ACC_WORKERGETSELFID, NULL, NULL)
{
    int32_t ret = 0;
    RTE_PER_LCORE(_lcore_id) = LCORE_ID_ANY;
    ret = KNET_ACC_WorkerGetSelfId();
    DT_ASSERT_EQUAL(ret, -1);

    RTE_PER_LCORE(_lcore_id) = MAX_WORKER_ID;
    ret = KNET_ACC_WorkerGetSelfId();
    DT_ASSERT_EQUAL(ret, -1);

    RTE_PER_LCORE(_lcore_id) = 0;
    ret = KNET_ACC_WorkerGetSelfId();
    DT_ASSERT_EQUAL(ret, 0);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_DELETEFLOWRULE_NORMAL, NULL, NULL)
{
    int32_t ret = 0;
    struct KNET_FlowCfg flowCfg;
    flowCfg.srcIp = RTE_IPV4(192U, 168U, 1, 1);
    flowCfg.dstIp = RTE_IPV4(192U, 168U, 1, 2);
    flowCfg.srcIpMask = RTE_IPV4(255U, 255U, 255U, 255U);
    flowCfg.dstIpMask = RTE_IPV4(255U, 255U, 255U, 255U);
    flowCfg.srcPort = 12345;
    flowCfg.dstPort = 80;
    flowCfg.srcPortMask = 0xFFFF;
    flowCfg.dstPortMask = 0xFFFF;
    flowCfg.rxQueueId[0] = 0;
    flowCfg.rxQueueIdSize = 1;
    flowCfg.proto = IPPROTO_TCP;
    struct rte_flow *flow = NULL;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    struct KNET_FlowTeleInfo teleInfo = {0};
    Mock->Create(rte_flow_validate, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_flow_create, TEST_GetFuncRetPositive(1));
    Mock->Create(rte_flow_destroy, TEST_GetFuncRetPositive(0));
    ret = KNET_GenerateIpv4Flow(KNET_GetNetDevCtx()->xmitPortId, &flowCfg, &flow, &teleInfo);
    DT_ASSERT_EQUAL(ret, 0);

    ret = KNET_DeleteFlowRule(KNET_GetNetDevCtx()->xmitPortId, flow);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(rte_flow_validate);
    Mock->Delete(rte_flow_create);
    Mock->Delete(rte_flow_destroy);
    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_DELETEFLOWRULE, NULL, NULL)
{
    int ret = 0;
    ret = KNET_DeleteFlowRule(KNET_GetNetDevCtx()->xmitPortId, NULL);
    DT_ASSERT_EQUAL(ret, -1);
}

int MockRteMemzoneFree(const struct rte_memzone *memzone)
{
    return 0;
}

void MockRteRingFree(struct rte_ring *ring)
{}

void MockRteMempoolFree(struct rte_mempool *mp)
{}

struct rte_mempool *MockRteMempoolLookup(const char *name)
{
    static struct rte_mempool mp = {0};
    return &mp;
}

struct rte_mempool *MockRteMempoolLookupNull(const char *name)
{
    return NULL;
}

struct rte_ring *MockRteRingLookup(const char *name)
{
    static struct rte_ring r = {0};
    return &r;
}

const struct rte_memzone *MockRteMemzoneLookupNull(const char *name)
{
    static struct rte_memzone mz = {0};
    return &mz;
}

const struct rte_memzone *MockRteMemzoneLookup(const char *name)
{
    static struct rte_memzone mz = {0};
    return &mz;
}

const struct rte_memzone *MockRteMemzoneReserve(const char *name, size_t len, int socketId, unsigned flags)
{
    static struct rte_memzone mz;
    static struct PdumpRequest pr;
    mz.addr = (void *)&pr;
    return &mz;
}

const struct rte_memzone *MockRteMemzoneReserveNull(const char *name, size_t len, int socketId, unsigned flags)
{
    return NULL;
}

static union KNET_CfgValue *MockKnetGetCfg(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    if (key == CONF_HW_BIFUR_ENABLE) {
        g_cfg.intValue = KERNEL_FORWARD_ENABLE;
    } else {
        g_cfg.intValue = 1;
    }
    return &g_cfg;
}

static union KNET_CfgValue *MockKnetGetCfgInt2(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    g_cfg.intValue = 2; // mock 2
    return &g_cfg;
}

int MockReturn1(const char *name, uint16_t *portId)
{
    return -1;
}

void* MockReturnNull()
{
    return NULL;
}

const struct rte_eth_rxtx_callback* Mock_rx_callback(uint16_t pid, uint16_t qid, rte_rx_callback_fn fn, void *param)
{
    static int ptr;
    return (struct rte_eth_rxtx_callback*)&ptr;
}

const struct rte_eth_rxtx_callback* Mock_tx_callback(uint16_t pid, uint16_t qid, rte_tx_callback_fn fn, void* param)
{
    static int ptr;
    return (struct rte_eth_rxtx_callback*)&ptr;
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_SetPdumpRxTxCbs, NULL, NULL)
{
    int32_t ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_memzone_lookup, MockRteMemzoneLookup);
    Mock->Create(rte_eth_dev_get_port_by_name, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_ring_lookup, MockRteRingLookup);
    Mock->Create(rte_mempool_lookup, MockRteMempoolLookup);
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    Mock->Create(rte_eth_add_first_rx_callback, Mock_rx_callback);
    Mock->Create(rte_eth_add_tx_callback, Mock_tx_callback);

    struct PdumpRequest pr;
    pr.op = 1;
    pr.ver = 1;
    pr.flags = 3;
    pr.queue = 1;
    pr.prm = NULL;

    struct rte_memzone mz;
    mz.addr = &pr;

    ret = KNET_SetPdumpRxTxCbs(&mz);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(rte_eth_dev_get_port_by_name);
    Mock->Delete(rte_memzone_lookup);
    Mock->Delete(KNET_GetCfg);
    Mock->Delete(rte_eth_add_first_rx_callback);
    Mock->Delete(rte_eth_add_tx_callback);

    Mock->Create(rte_memzone_free, MockRteMemzoneFree);
    Mock->Create(rte_ring_free, MockRteRingFree);
    Mock->Create(rte_mempool_free, MockRteMempoolFree);
    Mock->Create(memset_s, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_memzone_reserve, MockRteMemzoneReserve);

    const struct rte_memzone *pdumpRequestMz =  KNET_MultiPdumpInit();
    DT_ASSERT_NOT_EQUAL(pdumpRequestMz, NULL);

    ret = KNET_MultiPdumpUninit(pdumpRequestMz);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(rte_memzone_free);
    Mock->Delete(rte_ring_free);
    Mock->Delete(rte_mempool_free);
    Mock->Delete(rte_ring_lookup);
    Mock->Delete(rte_mempool_lookup);
    Mock->Delete(memset_s);
    Mock->Delete(rte_memzone_reserve);

    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_DpMaxWorkerIdGet, NULL, NULL)
{
    uint32_t ret = 0;
    ret = KNET_DpMaxWorkerIdGet();
    DT_ASSERT_EQUAL(ret, 0);
}

union KNET_CfgValue *GetCfgMock0(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    g_cfg.intValue = 1;
    return &g_cfg;
}
 
union KNET_CfgValue *GetCfgMock1(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    g_cfg.intValue = -1;
    return &g_cfg;
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_DpdkSlaveLcoreMatchDpWorker, NULL, NULL)
{
    int32_t ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, GetCfgMock0);
    Mock->Create(get_nprocs_conf, TEST_GetFuncRetPositive(1));
    Mock->Create(KNET_CpuDetected, TEST_GetFuncRetPositive(0));

    ret = KnetDpdkSlaveLcoreMatchDpWorker();
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KNET_GetCfg);
    Mock->Delete(get_nprocs_conf);
    Mock->Delete(KNET_CpuDetected);
    DeleteMock(Mock);
}

void Mock_rte_pktmbuf_free_bulk(struct rte_mbuf **mbufs, unsigned int count)
{
    return;
}

enum PdumpVersion {
    V1 = 1, /* no filtering or snap */
    V2 = 2,
};

struct PdumpRxTxCb {
    struct rte_ring *ring;
    struct rte_mempool *mp;
    const struct rte_eth_rxtx_callback *rxTxCb;
    const struct rte_bpf *filter;
    enum PdumpVersion ver;
    uint32_t snaplen;
};

DTEST_CASE_F(CORE_DPDK, TEST_PdumpTxRx, NULL, NULL)
{
    KNET_SpinLock *g_pdumpSpinlock;
    KNET_SpinLock spinLock = {0};
    g_pdumpSpinlock = &spinLock;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(rte_bpf_exec_burst, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_pktmbuf_copy, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_ring_enqueue_burst, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_pktmbuf_free_bulk, Mock_rte_pktmbuf_free_bulk);

    struct rte_ring r = {0};
    struct PdumpRxTxCb cb = {.ring = &r};
    struct rte_mbuf mbuf = {0};
    struct rte_mbuf *pkts[] = {&mbuf};

    KNET_SpinLock lock = {0};
    SetPdumpLock(&lock);
    int ret = PdumpRx(0, 0, pkts, 1, 1, &cb);
    DT_ASSERT_EQUAL(ret, 1);
    ret = PdumpTx(0, 0, pkts, 1, &cb);
    DT_ASSERT_EQUAL(ret, 1);

    Mock->Delete(rte_bpf_exec_burst);
    Mock->Delete(rte_pktmbuf_copy);
    Mock->Delete(rte_ring_enqueue_burst);
    Mock->Delete(rte_pktmbuf_free_bulk);

    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_GetHeaderLen, NULL, NULL)
{
    int32_t ret = 0;
    struct rte_mbuf buf = {0};
    buf.data_off = 0;
    buf.pkt_len = GENERAL_PKT_LEN;
    const uint8_t rawData[] = {
    0x00, 0x02, 0x03, 0x04, 0x05, 0x07, 0x52, 0x54, 0x00, 0x2b, 0x4b, 0x11,
    0x08, 0x00, 0x45, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x40, 0x00, 0x40, 0x06,
    0x9c, 0xfa, 0xc0, 0xa8, 0x0e, 0x6f, 0xc0, 0xa8, 0x0e, 0x02, 0x27, 0x10,
    0x82, 0x8c, 0xe8, 0x5f, 0x4e, 0xeb, 0x01, 0xb8, 0xbf, 0x0c, 0xa0, 0x12,
    0xfe, 0x88, 0x9d, 0xf0, 0x00, 0x00, 0x02, 0x04, 0x05, 0xb4, 0x04, 0x02,
    0x08, 0x0a, 0x5e, 0x87, 0x53, 0xe7, 0x8b, 0x32, 0x1f, 0x5c, 0x01, 0x03,
    0x03, 0x07
    };
    buf.buf_addr = (void*)rawData;
    ret = GetHeaderLen(&buf);
    DT_ASSERT_EQUAL(ret, GENERAL_PKT_LEN);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_SetPdumpRxTxCbs_Wrong, NULL, NULL)
{
    int32_t ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_memzone_lookup, MockRteMemzoneLookup);
    Mock->Create(rte_eth_dev_get_port_by_name, TEST_GetFuncRetPositive(0));

    struct PdumpRequest pr;
    pr.ver = 0;
    pr.prm = NULL;
    struct rte_memzone mz;
    mz.addr = &pr;
    ret = KNET_SetPdumpRxTxCbs(&mz);
    DT_ASSERT_NOT_EQUAL(ret, 0);

    pr.ver = 1;
    pr.prm = (const struct rte_bpf_prm *)malloc(sizeof(struct rte_bpf_prm));
    ret = KNET_SetPdumpRxTxCbs(&mz);
    DT_ASSERT_NOT_EQUAL(ret, 0);

    Mock->Delete(rte_memzone_lookup);
    Mock->Delete(rte_eth_dev_get_port_by_name);
    free((void *)pr.prm);
    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_ValidAndRegister, NULL, NULL)
{
    int32_t ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_memzone_lookup, MockRteMemzoneLookup);
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    Mock->Create(rte_ring_lookup, MockRteRingLookup);
    Mock->Create(rte_mempool_lookup, MockRteMempoolLookup);
    Mock->Create(rte_eth_add_first_rx_callback, Mock_rx_callback);
    Mock->Create(rte_eth_add_tx_callback, Mock_tx_callback);

    struct PdumpRequest pr;
    pr.ver = 1;
    pr.prm = NULL;
    pr.op = 2;
    strcpy(pr.device, "test");
    pr.queue = RTE_PDUMP_ALL_QUEUES;
    pr.flags = RTE_PDUMP_FLAG_RX | RTE_PDUMP_FLAG_TX;

    struct rte_memzone mz;
    mz.addr = &pr;

    ret = ValidAndRegister(&pr, 0, NULL);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(rte_memzone_lookup);
    Mock->Delete(KNET_GetCfg);
    Mock->Delete(rte_ring_lookup);
    Mock->Delete(rte_mempool_lookup);
    Mock->Delete(rte_eth_add_first_rx_callback);
    Mock->Delete(rte_eth_add_tx_callback);
    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_SetPdumpRxTxCbs_NULL, NULL, NULL)
{
    int32_t ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_ring_lookup, MockRteRingLookup);
    Mock->Create(rte_mempool_lookup, MockRteMempoolLookup);
    Mock->Create(rte_memzone_free, MockRteMemzoneFree);
    Mock->Create(rte_ring_free, MockRteRingFree);
    Mock->Create(rte_mempool_free, MockRteMempoolFree);
    Mock->Create(memset_s, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_memzone_reserve, MockRteMemzoneReserve);

    static struct rte_memzone *pdumpRequestMz = NULL;
    ret = KNET_MultiPdumpUninit(pdumpRequestMz);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(rte_memzone_free);
    Mock->Delete(rte_ring_free);
    Mock->Delete(rte_mempool_free);
    Mock->Delete(rte_ring_lookup);
    Mock->Delete(rte_mempool_lookup);
    Mock->Delete(memset_s);
    Mock->Delete(rte_memzone_reserve);

    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_MultiPdumpInit, NULL, NULL)
{
    int32_t ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_ring_lookup, MockRteRingLookup);
    Mock->Create(rte_mempool_lookup, MockRteMempoolLookup);
    Mock->Create(rte_memzone_free, MockRteMemzoneFree);
    Mock->Create(rte_ring_free, MockRteRingFree);
    Mock->Create(rte_mempool_free, MockRteMempoolFree);
    Mock->Create(memset_s, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_memzone_reserve, MockRteMemzoneReserve);

    const struct rte_memzone *pdumpRequestMz = KNET_MultiPdumpInit();
    DT_ASSERT_NOT_EQUAL(pdumpRequestMz, NULL);

    Mock->Delete(rte_memzone_free);
    Mock->Delete(rte_ring_free);
    Mock->Delete(rte_mempool_free);
    Mock->Delete(rte_ring_lookup);
    Mock->Delete(rte_mempool_lookup);
    Mock->Delete(memset_s);
    Mock->Delete(rte_memzone_reserve);

    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_DfxInit, NULL, NULL)
{
    int32_t ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_MultiPdumpInit, MOCK_KNET_MultiPdumpInit);
    Mock->Create(KNET_InitDpdkTelemetry, TEST_GetFuncRetPositive(0));

    ret = DpdkDfxInit(KNET_RUN_MODE_SINGLE, KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KNET_MultiPdumpInit);
    Mock->Delete(KNET_InitDpdkTelemetry);
    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_MultiPdumpInit_Wrong, NULL, NULL)
{
    int32_t ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_ring_lookup, MockRteRingLookup);
    Mock->Create(rte_mempool_lookup, MockRteMempoolLookup);
    Mock->Create(rte_memzone_free, MockRteMemzoneFree);
    Mock->Create(rte_ring_free, MockRteRingFree);
    Mock->Create(rte_mempool_free, MockRteMempoolFree);
    Mock->Create(memset_s, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_memzone_reserve, MockRteMemzoneReserveNull);

    const struct rte_memzone *pdumpRequestMz = KNET_MultiPdumpInit();
    DT_ASSERT_EQUAL(pdumpRequestMz, NULL);
    ret = DpdkDfxInit(KNET_RUN_MODE_SINGLE, KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(rte_memzone_free);
    Mock->Delete(rte_ring_free);
    Mock->Delete(rte_mempool_free);
    Mock->Delete(rte_ring_lookup);
    Mock->Delete(rte_mempool_lookup);
    Mock->Delete(memset_s);
    Mock->Delete(rte_memzone_reserve);

    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_SetPdumpRxTxCbs_Wrong1, NULL, NULL)
{
    int32_t ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_memzone_lookup, MockRteMemzoneLookup);
    Mock->Create(rte_eth_dev_get_port_by_name, MockReturn1);
    Mock->Create(rte_ring_lookup, MockRteRingLookup);
    Mock->Create(rte_mempool_lookup, MockRteMempoolLookup);

    struct PdumpRequest pr;
    pr.ver = 1;
    pr.prm = NULL;
    struct rte_memzone mz;
    mz.addr = &pr;

    ret = KNET_SetPdumpRxTxCbs(&mz);
    DT_ASSERT_EQUAL(ret, -EINVAL);
    Mock->Delete(rte_eth_dev_get_port_by_name);
    Mock->Delete(rte_memzone_lookup);
    Mock->Delete(rte_ring_lookup);
    Mock->Delete(rte_mempool_lookup);
    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_UninitDpdk_Wrong1, NULL, NULL)
{
    int32_t ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_eal_cleanup, MockReturn1);
    Mock->Create(FreeDelayCpdRing, TEST_GetFuncRetPositive(0));
    
    ret = KNET_UninitDpdk(KNET_PROC_TYPE_SECONDARY, KNET_RUN_MODE_MULTIPLE);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(rte_eal_cleanup);

    Mock->Create(rte_eal_cleanup, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_MultiPdumpUninit, MockReturn1);
    ret = KNET_UninitDpdk(KNET_PROC_TYPE_PRIMARY, KNET_RUN_MODE_SINGLE);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(rte_eal_cleanup);
    Mock->Delete(FreeDelayCpdRing);
    Mock->Delete(KNET_MultiPdumpUninit);
    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_InitDpdk_Wrong1, NULL, NULL)
{
    int32_t ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_eal_init, TEST_GetFuncRetNegative(1));
    Mock->Create(rte_eth_dev_get_port_by_name, TEST_GetFuncRetNegative(1));
    
    ret = KNET_InitDpdk(KNET_PROC_TYPE_PRIMARY, KNET_RUN_MODE_SINGLE);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(rte_eth_dev_get_port_by_name);
    Mock->Delete(rte_eal_init);
    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_InitDpdk_Wrong2, NULL, NULL)
{
    int32_t ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_eth_dev_get_port_by_name, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_eal_init, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_memzone_free, MockRteMemzoneFree);
    Mock->Create(rte_memzone_reserve, MockRteMemzoneReserve);
    Mock->Create(rte_mempool_lookup, MockRteMempoolLookup);
    Mock->Create(rte_ring_lookup, MockRteRingLookup);
    Mock->Create(rte_ring_create, MockRteRingLookup);
    
    ret = KNET_InitDpdk(KNET_PROC_TYPE_PRIMARY, KNET_RUN_MODE_SINGLE);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(rte_eth_dev_get_port_by_name);
    Mock->Delete(rte_eal_init);
    Mock->Delete(rte_memzone_free);
    Mock->Delete(rte_memzone_reserve);
    Mock->Delete(rte_mempool_lookup);
    Mock->Delete(rte_ring_lookup);
    Mock->Delete(rte_ring_create);
    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_InitDpdk_Wrong3, NULL, NULL)
{
    int32_t ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_eth_dev_get_port_by_name, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetInitDpdkPort, TEST_GetFuncRetPositive(0));
    Mock->Create(GenerateDpdkCoreList, MockReturn1);
    
    ret = KNET_InitDpdk(KNET_PROC_TYPE_PRIMARY, KNET_RUN_MODE_SINGLE);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(rte_eth_dev_get_port_by_name);
    Mock->Delete(KnetInitDpdkPort);
    Mock->Delete(GenerateDpdkCoreList);
    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_RteEalInit_Wrong1, NULL, NULL)
{
    int32_t ret = 0;
    int procType = 0;
    char **processMo = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetThreadAffinity, MockReturn1);
    Mock->Create(rte_eal_init, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_SetThreadAffinity, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_MultiPdumpInit, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_InitDpdkTelemetry, TEST_GetFuncRetPositive(0));

    ret = RteEalInit(procType, processMo);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KNET_GetThreadAffinity);
    Mock->Delete(KNET_SetThreadAffinity);
    Mock->Delete(KNET_MultiPdumpInit);
    Mock->Delete(KNET_InitDpdkTelemetry);

    Mock->Create(KNET_GetThreadAffinity, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_SetThreadAffinity, MockReturn1);
    Mock->Create(KNET_MultiPdumpInit, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_InitDpdkTelemetry, TEST_GetFuncRetPositive(0));

    ret = RteEalInit(procType, processMo);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(KNET_GetThreadAffinity);
    Mock->Delete(KNET_SetThreadAffinity);
    Mock->Delete(KNET_MultiPdumpInit);
    Mock->Delete(KNET_InitDpdkTelemetry);

    Mock->Create(KNET_GetThreadAffinity, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_SetThreadAffinity, MockReturn1);
    Mock->Create(KNET_MultiPdumpInit, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_InitDpdkTelemetry, TEST_GetFuncRetPositive(0));

    ret = RteEalInit(procType, processMo);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_GetThreadAffinity);
    Mock->Delete(KNET_SetThreadAffinity);
    Mock->Delete(KNET_MultiPdumpInit);
    Mock->Delete(KNET_InitDpdkTelemetry);
    Mock->Delete(rte_eal_init);

    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_GetNetDevCtx, NULL, NULL)
{
    int32_t ret = 0;
    KNET_GetNetDevCtx();
    DT_ASSERT_EQUAL(ret, 0);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_SetMac, NULL, NULL)
{
    int32_t ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(memcpy_s, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_eth_macaddr_get, TEST_GetFuncRetPositive(0));
    Mock->Create(memcmp, TEST_GetFuncRetPositive(0));
    uint16_t portId = 0;
    DpdkPortMacCheck(portId);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(memcpy_s);
    Mock->Delete(rte_eth_macaddr_get);
    Mock->Delete(memcmp);
    DeleteMock(Mock);
}

struct rte_flow *MockRteFlowCreate()
{
    struct rte_flow *flow = {0};
    return flow;
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_GenerateArpFlow, NULL, NULL)
{
    int32_t ret = 0;
    uint16_t portId = 0;
    uint32_t queueId = 0;
    struct rte_flow *flow = NULL;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_flow_validate, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_flow_create, MockRteFlowCreate);
    Mock->Create(memcmp, TEST_GetFuncRetPositive(0));
    KNET_GenerateArpFlow(portId, queueId, &flow);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(rte_flow_validate);
    Mock->Delete(rte_flow_create);
    Mock->Delete(memcmp);
    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_GenerateCtlArpFlow, NULL, NULL)
{
    int32_t ret = 0;
    uint16_t portId = 0;
    uint32_t queueId = 0;
    struct rte_flow *flow = NULL;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_flow_validate, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_flow_create, MockRteFlowCreate);
    Mock->Create(memcmp, TEST_GetFuncRetPositive(0));
    KNET_GenerateArpFlow(KNET_GetNetDevCtx()->xmitPortId, queueId, &flow);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(rte_flow_validate);
    Mock->Delete(rte_flow_create);
    Mock->Delete(memcmp);
    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_GenerateIpv4Flow, NULL, NULL)
{
    int32_t ret = 0;
    struct KNET_FlowCfg flowCfg;
    flowCfg.srcIp = RTE_IPV4(192U, 168U, 1, 1);
    flowCfg.dstIp = RTE_IPV4(192U, 168U, 1, 2);
    flowCfg.srcIpMask = RTE_IPV4(255U, 255U, 255U, 255U);
    flowCfg.dstIpMask = RTE_IPV4(255U, 255U, 255U, 255U);
    flowCfg.srcPort = 12345;
    flowCfg.dstPort = 80;
    flowCfg.srcPortMask = 0xFFFF;
    flowCfg.dstPortMask = 0xFFFF;
    flowCfg.rxQueueId[0] = 0;
    flowCfg.rxQueueIdSize = 1;
    flowCfg.proto = IPPROTO_TCP;
    struct rte_flow *flow = NULL;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    struct KNET_FlowTeleInfo teleInfo = {0};
    Mock->Create(rte_flow_validate, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_flow_create, TEST_GetFuncRetPositive(1));
    ret = KNET_GenerateIpv4Flow(g_dpdkDevInfo.netdevCtx.xmitPortId, &flowCfg, &flow, &teleInfo);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(rte_flow_validate);
    Mock->Delete(rte_flow_create);

    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_GenerateIpv4Flow_Wrong, NULL, NULL)
{
    int32_t ret = 0;
    struct KNET_FlowCfg flowCfg;
    flowCfg.srcIp = RTE_IPV4(192U, 168U, 1, 1);
    flowCfg.dstIp = RTE_IPV4(192U, 168U, 1, 2);
    flowCfg.srcIpMask = RTE_IPV4(255U, 255U, 255U, 255U);
    flowCfg.dstIpMask = RTE_IPV4(255U, 255U, 255U, 255U);
    flowCfg.srcPort = 12345;
    flowCfg.dstPort = 80;
    flowCfg.srcPortMask = 0xFFFF;
    flowCfg.dstPortMask = 0xFFFF;
    flowCfg.rxQueueId[0] = 0;
    flowCfg.rxQueueIdSize = 1;
    flowCfg.proto = IPPROTO_TCP;
    struct rte_flow *flow = NULL;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    struct KNET_FlowTeleInfo teleInfo = {0};
    Mock->Create(rte_flow_validate, MockReturn1);
    Mock->Create(rte_flow_create, TEST_GetFuncRetPositive(1));
    ret = KNET_GenerateIpv4Flow(g_dpdkDevInfo.netdevCtx.xmitPortId, &flowCfg, &flow, &teleInfo);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(rte_flow_validate);
    Mock->Delete(rte_flow_create);

    Mock->Create(rte_flow_validate, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_flow_create, MockReturnNull);
    flowCfg.proto = IPPROTO_UDP;
    ret = KNET_GenerateIpv4Flow(g_dpdkDevInfo.netdevCtx.xmitPortId, &flowCfg, &flow, &teleInfo);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(rte_flow_validate);
    Mock->Delete(rte_flow_create);

    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_HeaderLen1, NULL, NULL)
{
    int ret = 0;
    struct rte_mbuf buf;
    uint8_t data[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Ethernet Header
        0x08, 0x00, 0x45, 0x00, 0x00, 0x3c, // IP Header (IPv4)
        0x1c, 0x46, 0x40, 0x00, 0x40, 0x11, 0x72, 0x75 // Remaining Data
    };

    buf.buf_addr = data;   // 数据指针指向模拟数据
    buf.data_off = 0;      // 数据偏移
    buf.pkt_len = sizeof(data);  // 数据包总长度
    buf.data_len = sizeof(data); // 数据有效长度

    ret = GetHeaderLen(&buf);
    DT_ASSERT_EQUAL(ret, L2_ARP_HEADER_LEN);
}

DTEST_CASE_F(CORE_DPDK, TEST_HeaderLen2, NULL, NULL)
{
    int ret = 0;
    struct rte_mbuf buf;
    uint8_t packet[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x45, 0x00, 0x00, 0x1c,
    };
    buf.buf_addr = packet;   // 数据指针指向模拟数据
    buf.data_off = 0;      // 数据偏移
    buf.pkt_len = sizeof(packet);  // 数据包总长度
    buf.data_len = sizeof(packet); // 数据有效长度
    ret = GetHeaderLen(&buf);
    DT_ASSERT_EQUAL(ret, L2_ETH_HEADER_LEN);
}

DTEST_CASE_F(CORE_DPDK, TEST_GenerateCoreList, NULL, NULL)
{
    int ret = 0;
    int dpdkCore = 0;
    int procType = 0;
    char* coreList = NULL;
    char* mainLcore = NULL;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(sprintf_s, TEST_GetFuncRetPositive(0));

    Mock->Create(KNET_GetCfg, GetCfgMock0);
    ret = GenerateDpdkCoreList(dpdkCore, procType, coreList, mainLcore);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(KNET_GetCfg);

    Mock->Delete(sprintf_s);
    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_DpWorkerInfoGet, NULL, NULL)
{
    KNET_DpWorkerInfo *ret = NULL;
    ret = KNET_DpWorkerInfoGet(0);
    DT_ASSERT_NOT_EQUAL(ret, NULL);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_GetDelayRxRing, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
 
    Mock->Create(sprintf_s, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_GetCfg, GetCfgMock1);
    Mock->Create(rte_ring_lookup, MockRteRingLookup);
    void *ret = KNET_GetDelayRxRing(0);
    DT_ASSERT_NOT_EQUAL(ret, NULL);
    Mock->Delete(KNET_GetCfg);
    Mock->Delete(rte_ring_lookup);
    Mock->Delete(sprintf_s);
    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_IS_ETH_LINK_UP, NULL, NULL)
{
    uint16_t portId = 0;
    int ret = IsEthLinkUp(portId);
    DT_ASSERT_EQUAL(ret, 0);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_BondSlavesInit, NULL, NULL)
{
    int32_t ret = BondSlavesInit(KNET_PROC_TYPE_SECONDARY);
    DT_ASSERT_EQUAL(ret, 0);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KnetGetDpdkPortIdAndInit, TEST_GetFuncRetPositive(0));
    ret = BondSlavesInit(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(KnetGetDpdkPortIdAndInit);

    Mock->Create(KnetGetDpdkPortIdAndInit, TEST_GetFuncRetNegative(1));
    ret = BondSlavesInit(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KnetGetDpdkPortIdAndInit);

    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_BondInit, NULL, NULL)
{
    int32_t ret = BondInit(KNET_PROC_TYPE_SECONDARY);
    DT_ASSERT_EQUAL(ret, 0);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KNET_BondCreate, TEST_GetFuncRetNegative(1));
    ret = BondInit(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_BondCreate);

    Mock->Create(KNET_BondCreate, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_eth_dev_stop, TEST_GetFuncRetNegative(1));
    ret = BondInit(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(rte_eth_dev_stop);

    Mock->Create(rte_eth_dev_stop, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetInitDpdkPort, TEST_GetFuncRetNegative(1));
    ret = BondInit(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KnetInitDpdkPort);

    Mock->Create(KnetInitDpdkPort, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_BondWaitSlavesReady, TEST_GetFuncRetNegative(1));
    ret = BondInit(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_BondWaitSlavesReady);
    
    Mock->Create(KNET_BondWaitSlavesReady, TEST_GetFuncRetPositive(0));
    ret = BondInit(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(KNET_BondWaitSlavesReady);
    Mock->Delete(KnetInitDpdkPort);
    Mock->Delete(rte_eth_dev_stop);
    Mock->Delete(KNET_BondCreate);

    DeleteMock(Mock);
}

int DelayCpdRingInit(int procType, int processMode);
int DelayCpdRingCreate(int ringId);
struct rte_ring *
MockRteRingCreateNull(const char *name, unsigned int count, int socket_id,
    unsigned int flags)
{
    return NULL;
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_DelayCpdRingInit, NULL, NULL)
{
    int32_t ret = DelayCpdRingInit(KNET_PROC_TYPE_PRIMARY, KNET_RUN_MODE_SINGLE);
    DT_ASSERT_EQUAL(ret, 0);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    Mock->Create(rte_eth_dev_rx_queue_stop, TEST_GetFuncRetNegative(1));
    ret = DelayCpdRingInit(KNET_PROC_TYPE_PRIMARY, KNET_RUN_MODE_SINGLE);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(rte_eth_dev_rx_queue_stop);

    Mock->Create(rte_eth_dev_rx_queue_stop, TEST_GetFuncRetPositive(0));
    Mock->Create(snprintf_s, TEST_GetFuncRetNegative(1));
    ret = DelayCpdRingInit(KNET_PROC_TYPE_PRIMARY, KNET_RUN_MODE_SINGLE);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(snprintf_s);

    Mock->Create(snprintf_s, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_ring_create, MockRteRingCreateNull);
    ret = DelayCpdRingInit(KNET_PROC_TYPE_PRIMARY, KNET_RUN_MODE_SINGLE);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(rte_ring_create);

    Mock->Delete(snprintf_s);
    Mock->Delete(rte_eth_dev_rx_queue_stop);
    Mock->Delete(KNET_GetCfg);

    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_DpdkPortInit, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    Mock->Create(BondSlavesInit, TEST_GetFuncRetNegative(1));
    Mock->Create(realpath, RealPathMock);
    int32_t ret = DpdkPortInit(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(BondSlavesInit);

    Mock->Create(BondSlavesInit, TEST_GetFuncRetPositive(0));
    Mock->Create(BondInit, TEST_GetFuncRetNegative(1));
    ret = DpdkPortInit(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(BondInit);

    Mock->Create(BondInit, TEST_GetFuncRetPositive(0));
    ret = DpdkPortInit(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(realpath);
    Mock->Delete(BondInit);
    Mock->Delete(BondSlavesInit);
    Mock->Delete(KNET_GetCfg);

    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_FreeDelayCpdRing, NULL, NULL)
{
    int ret = FreeDelayCpdRing(KNET_PROC_TYPE_PRIMARY, KNET_RUN_MODE_SINGLE);
    DT_ASSERT_EQUAL(ret, 0);
    
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    g_dpdkInited = false;
    ret = FreeDelayCpdRing(KNET_PROC_TYPE_PRIMARY, KNET_RUN_MODE_SINGLE);
    DT_ASSERT_EQUAL(ret, -1);

    g_dpdkInited = true;
    ret = FreeDelayCpdRing(KNET_PROC_TYPE_PRIMARY, KNET_RUN_MODE_MULTIPLE);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Create(snprintf_s, TEST_GetFuncRetNegative(1));
    ret = FreeDelayCpdRing(KNET_PROC_TYPE_PRIMARY, KNET_RUN_MODE_SINGLE);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(snprintf_s);

    Mock->Create(snprintf_s, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_ring_lookup, MockRteRingLookup);
    ret = FreeDelayCpdRing(KNET_PROC_TYPE_PRIMARY, KNET_RUN_MODE_SINGLE);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(rte_ring_lookup);

    Mock->Delete(snprintf_s);
    Mock->Delete(KNET_GetCfg);

    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_DpdkMultiQueueSetup, NULL, NULL)
{
    struct rte_eth_conf portConf;
    portConf.rxmode.mq_mode = RTE_ETH_MQ_RX_NONE;
    portConf.rxmode.offloads = 0;
    portConf.txmode.mq_mode = RTE_ETH_MQ_TX_NONE;

    struct rte_eth_dev_info devInfo;
    devInfo.flow_type_rss_offloads = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    
    Mock->Create(KNET_GetCfg, MockKnetGetCfgInt2);
    DpdkMultiQueueSetup(&devInfo, &portConf);
    DT_ASSERT_EQUAL(portConf.dcb_capability_en, RTE_ETH_DCB_PG_SUPPORT);
    Mock->Delete(KNET_GetCfg);

    DeleteMock(Mock);
}

DTEST_CASE_F(CORE_DPDK, TEST_KNET_DpdkHwChecksumSetup, NULL, NULL)
{
    struct rte_eth_conf portConf;
    portConf.rxmode.mq_mode = RTE_ETH_MQ_RX_NONE;
    portConf.rxmode.offloads = 0;
    portConf.txmode.mq_mode = RTE_ETH_MQ_TX_NONE;

    struct rte_eth_dev_info devInfo;
    devInfo.rx_offload_capa = RTE_ETH_RX_OFFLOAD_IPV4_CKSUM | RTE_ETH_TX_OFFLOAD_TCP_CKSUM;
    devInfo.tx_offload_capa = RTE_ETH_TX_OFFLOAD_IPV4_CKSUM | RTE_ETH_TX_OFFLOAD_TCP_CKSUM;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    int32_t ret = DpdkHwChecksumSetup(&devInfo, &portConf);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(KNET_GetCfg);

    DeleteMock(Mock);
}