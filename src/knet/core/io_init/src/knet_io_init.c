/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 报文BUF相关操作
 */

#include "unistd.h"

#include "sys/sysinfo.h"

#include "rte_ethdev.h"
#include "rte_pdump.h"
#include "rte_malloc.h"

#include "dp_cfg_api.h"

#include "knet_types.h"
#include "knet_log.h"
#include "knet_pktpool.h"
#include "knet_thread.h"
#include "knet_dpdk_cfg.h"
#include "knet_config.h"
#include "knet_utils.h"
#include "knet_capability.h"
#include "knet_io_init.h"
#include "knet_pktpool.h"
#include "knet_dpdk_telemetry.h"
#include "knet_pdump.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CPU_NUM 128
#define MEMORY_THRESHOLD_RATIO 0.75 // 为系统内存的75%时，启动进程时进行告警
#define MEMPOOL_THRESHOLD_RATIO 0.7

static DpdkDevInfo g_dpdkDevInfo = {
    .netdevCtx = {
        .portId = 0,
    },
    .pktPoolId = KNET_PKTPOOL_INVALID_ID,
};

static struct rte_eth_conf g_PortConf = {
    .rxmode = {
        .mq_mode = RTE_ETH_MQ_RX_NONE,
        .offloads = 0,
    },
    .txmode = {
        .mq_mode = RTE_ETH_MQ_TX_NONE,
    },
};

static DpWorkerIdTable g_dpWorkerIdTable = {0};

const static struct rte_memzone *g_pdumpRequestMz = NULL;  // socket设计方案中pdump_request的共享内存

DpWorkerInfo *KNET_DpWorkerInfoGet(uint32_t dpWorkerId)
{
    return &g_dpWorkerIdTable.workerInfo[dpWorkerId];
}

uint32_t KNET_DpMaxWorkerIdGet(void)
{
    return g_dpWorkerIdTable.maxWorkerId;
}

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

int32_t KNET_ACC_WorkerGetSelfId(void)
{
    uint32_t lcoreId = rte_lcore_id();
    if (lcoreId == LCORE_ID_ANY) {
        /* 此时表示lcore在未注册的非EAL线程内，正常，不打印报错信息 */
        return -1;
    }
    if (lcoreId >= MAX_WORKER_ID) {
        KNET_ERR("Lcore id %u out of range", lcoreId);
        return -1;
    }
    if (g_dpWorkerIdTable.coreIdToWorkerId[lcoreId] == INVALID_WORKER_ID) {
        /* 目前返回0，待dp epoll_ctl修复后reuturn -1，打印添加回去。KNET_ERR("not find lcoreId %u", lcoreId); */
        return 0;
    }
    return g_dpWorkerIdTable.coreIdToWorkerId[lcoreId];
}

static void KnetPktPoolCfgInit(KnetPktPoolCfg *pktPoolCfg)
{
    (void)memcpy_s(pktPoolCfg->name, KNET_PKTPOOL_NAME_LEN, KNET_PKT_POOL_NAME, strlen(KNET_PKT_POOL_NAME));
    pktPoolCfg->bufNum = (uint32_t)KNET_GetCfg(CONF_DP_MAX_MBUF).intValue;
    pktPoolCfg->cacheNum = KNET_PKT_POOL_DEFAULT_CACHENUM;
    pktPoolCfg->cacheSize = KNET_PKT_POOL_DEFAULT_CACHESIZE;
    pktPoolCfg->privDataSize = KNET_PKT_POOL_DEFAULT_PRIVATE_SIZE;
    pktPoolCfg->headroomSize = KNET_PKT_POOL_DEFAULT_HEADROOM_SIZE;
    pktPoolCfg->dataroomSize = KNET_PKT_POOL_DEFAULT_DATAROOM_SIZE;
    pktPoolCfg->createAlg = KNET_PKT_POOL_DEFAULT_CREATE_ALG;
    pktPoolCfg->numaId = (int32_t)rte_socket_id();
    pktPoolCfg->init = NULL;
}

static int32_t KnetMbufMempoolCreate(void)
{
    KnetPktPoolCfg pktPoolCfg = { 0 };
    KnetPktPoolCfgInit(&pktPoolCfg);
    uint32_t ret = KNET_PktPoolCreate(&pktPoolCfg, &g_dpdkDevInfo.pktPoolId);
    if (ret != KNET_OK) {
        KNET_ERR("K-NET pkt pool create failed");
        return -1;
    }

    return 0;
}

static void KnetMbufMempoolDestroy(void)
{
    KNET_PktPoolDestroy(g_dpdkDevInfo.pktPoolId);
    g_dpdkDevInfo.pktPoolId = KNET_PKTPOOL_INVALID_ID;
}

static void KnetDpdkMultiQueueSetup(const struct rte_eth_dev_info *devInfo, struct rte_eth_conf *portConf)
{
    if (KNET_GetCfg(CONF_DP_MAX_WORKER_NUM).intValue <= 1) {
        return;
    }

    struct rte_eth_dcb_rx_conf *rxConf = &portConf->rx_adv_conf.dcb_rx_conf;
    struct rte_eth_dcb_tx_conf *txConf = &portConf->tx_adv_conf.dcb_tx_conf;
    enum rte_eth_nb_tcs numTcs = RTE_ETH_4_TCS; // 暂定 4 traffic classes，后续如果多队列支持用户配置再支持其他个数
    struct rte_eth_rss_conf rssConf = { 0 };
    rssConf.rss_hf = devInfo->flow_type_rss_offloads;

    rxConf->nb_tcs = numTcs;
    txConf->nb_tcs = numTcs;

    for (uint32_t i = 0; i < RTE_ETH_DCB_NUM_USER_PRIORITIES; i++) {
        rxConf->dcb_tc[i] = i % numTcs;
        txConf->dcb_tc[i] = i % numTcs;
    }

    portConf->rxmode.mq_mode = RTE_ETH_MQ_RX_DCB_RSS;
    portConf->rx_adv_conf.rss_conf = rssConf;
    portConf->txmode.mq_mode = RTE_ETH_MQ_TX_DCB;

    portConf->dcb_capability_en = RTE_ETH_DCB_PG_SUPPORT;

    KNET_INFO("Rss key len %u, rss hf %x", rssConf.rss_key_len, rssConf.rss_hf);
}

static int32_t KnetDpdkHwChecksumSetup(const struct rte_eth_dev_info *devInfo, struct rte_eth_conf *portConf)
{
    if (KNET_GetCfg(CONF_HW_TCP_CHECKSUM).intValue <= 0) {
        return 0;
    }

    if ((devInfo->tx_offload_capa
        & (RTE_ETH_TX_OFFLOAD_IPV4_CKSUM | RTE_ETH_TX_OFFLOAD_TCP_CKSUM))
        != (RTE_ETH_TX_OFFLOAD_IPV4_CKSUM | RTE_ETH_TX_OFFLOAD_TCP_CKSUM)) {
        KNET_ERR("Hardware tx checksum enabled but not supported, tx_offload_capa %x", devInfo->tx_offload_capa);
        return -1;
    }
    portConf->txmode.offloads |=
        (RTE_ETH_TX_OFFLOAD_IPV4_CKSUM | RTE_ETH_TX_OFFLOAD_TCP_CKSUM);

    if ((devInfo->rx_offload_capa
        & (RTE_ETH_RX_OFFLOAD_IPV4_CKSUM | RTE_ETH_RX_OFFLOAD_TCP_CKSUM))
        != (RTE_ETH_RX_OFFLOAD_IPV4_CKSUM | RTE_ETH_RX_OFFLOAD_TCP_CKSUM)) {
        KNET_ERR("Hardware rx checksum enabled but not supported, rx_offload_capa %x", devInfo->rx_offload_capa);
        return -1;
    }
    portConf->rxmode.offloads |=
        (RTE_ETH_RX_OFFLOAD_IPV4_CKSUM | RTE_ETH_RX_OFFLOAD_TCP_CKSUM);

    KNET_INFO("Txmode offloads 0x%x, rxmode offloads 0x%x", portConf->txmode.offloads, portConf->rxmode.offloads);

    return 0;
}

/**
 * @brief 配置dpdk支持mbuf分片组链
 */
static int32_t KnetDpdkRxOffloadScatterSetup(const struct rte_eth_dev_info *devInfo, struct rte_eth_conf *portConf)
{
    if ((devInfo->rx_offload_capa & RTE_ETH_RX_OFFLOAD_SCATTER) != RTE_ETH_RX_OFFLOAD_SCATTER) {
        KNET_ERR("Rx offload scatter not supported, rx_offload_capa %x", devInfo->rx_offload_capa);
        return -1;
    }
    portConf->rxmode.offloads |= RTE_ETH_RX_OFFLOAD_SCATTER;

    if ((devInfo->tx_offload_capa & RTE_ETH_TX_OFFLOAD_MULTI_SEGS) != RTE_ETH_TX_OFFLOAD_MULTI_SEGS) {
        KNET_ERR("Tx offload multi segs not supported, tx_offload_capa %x", devInfo->tx_offload_capa);
        return -1;
    }
    portConf->txmode.offloads |= RTE_ETH_TX_OFFLOAD_MULTI_SEGS;

    KNET_INFO("Txmode offloads 0x%x, rxmode offloads 0x%x", portConf->txmode.offloads, portConf->rxmode.offloads);

    return 0;
}

int32_t KnetSetTSO(const struct rte_eth_dev_info *devInfo, struct rte_eth_conf *portConf)
{
    if (devInfo->tx_offload_capa & RTE_ETH_TX_OFFLOAD_TCP_TSO) {
        portConf->txmode.offloads |= RTE_ETH_TX_OFFLOAD_TCP_TSO;
        KNET_INFO("TSO enable!");
    } else {
        KNET_ERR("Dev not support TSO!");
        return -1;
    }
    return 0;
}

int32_t KnetSetLRO(const struct rte_eth_dev_info *devInfo, struct rte_eth_conf *portConf)
{
    if (devInfo->rx_offload_capa & RTE_ETH_RX_OFFLOAD_TCP_LRO) {
        portConf->rxmode.offloads |= RTE_ETH_RX_OFFLOAD_TCP_LRO;
        portConf->rxmode.max_lro_pkt_size = MAX_LRO_SEG;
        KNET_INFO("LRO enable!");
    } else {
        KNET_ERR("Dev not support LRO!");
        return -1;
    }
    return 0;
}

static int32_t KnetDpdkPortConfSetup(const struct rte_eth_dev_info *devInfo, struct rte_eth_conf *portConf)
{
    if (devInfo->tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE) {
        portConf->txmode.offloads |= RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;
    }
    
    KnetDpdkMultiQueueSetup(devInfo, portConf);

    int32_t ret = KnetDpdkHwChecksumSetup(devInfo, portConf);
    if (ret != 0) {
        KNET_ERR("K-NET dpdk hw checksum setup failed, ret %d", ret);
        return -1;
    }

    if (KNET_GetCfg(CONF_HW_TSO).intValue > 0) {
        ret = KnetSetTSO(devInfo, portConf);
        if (ret != 0) {
            KNET_ERR("K-NET dpdk tso setup failed, ret %d", ret);
            return -1;
        }
    }

    if (KNET_GetCfg(CONF_HW_LRO).intValue > 0) {
        ret = KnetSetLRO(devInfo, portConf);
        if (ret != 0) {
            KNET_ERR("K-NET dpdk lro setup failed, ret %d", ret);
            return -1;
        }
    }

    ret = KnetDpdkRxOffloadScatterSetup(devInfo, portConf);
    if (ret != 0) {
        KNET_ERR("K-NET dpdk rx scatter offload setup failed, ret %d", ret);
        return -1;
    }

    return 0;
}

static int32_t KnetDpdkTxRxQueueSetup(uint16_t portId, const struct rte_eth_dev_info *devInfo,
    struct rte_eth_conf *portConf)
{
    int32_t rxQueue = KNET_GetCfg(CONF_INNER_RX_QUEUE_NUM).intValue;
    int32_t txQueue = KNET_GetCfg(CONF_INNER_TX_QUEUE_NUM).intValue;
    int32_t txCacheSize = KNET_GetCfg(CONF_DPDK_TX_CACHE_SIZE).intValue;
    int32_t rxCacheSize = KNET_GetCfg(CONF_DPDK_RX_CACHE_SIZE).intValue;
    if (txCacheSize < devInfo->tx_desc_lim.nb_min || txCacheSize > devInfo->tx_desc_lim.nb_max ||
        rxCacheSize < devInfo->rx_desc_lim.nb_min || rxCacheSize > devInfo->rx_desc_lim.nb_max) {
        KNET_ERR("Tx cache size %d must be in range [%u, %u], rx_cache_size %d must be in range [%u, %u]",
            txCacheSize, devInfo->tx_desc_lim.nb_min, devInfo->tx_desc_lim.nb_max,
            rxCacheSize, devInfo->rx_desc_lim.nb_min, devInfo->rx_desc_lim.nb_max);
        return -1;
    }

    int32_t ret;
    struct rte_eth_txconf txqConf = devInfo->default_txconf;
    txqConf.offloads = portConf->txmode.offloads;
    for (int32_t i = 0; i < txQueue; ++i) {
        ret = rte_eth_tx_queue_setup(portId, i, txCacheSize, rte_eth_dev_socket_id(portId), &txqConf);
        if (ret < 0) {
            KNET_ERR("Set tx queue info failed, portId %u, ret %d", portId, ret);
            return -1;
        }
    }

    KnetPktPoolCtrl *poolCtl = KnetPktGetPoolCtrl(g_dpdkDevInfo.pktPoolId);
    if (poolCtl == NULL) {
        return -1;
    }
    struct rte_eth_rxconf rxqConf = devInfo->default_rxconf;
    rxqConf.offloads = portConf->rxmode.offloads;
    for (int32_t i = 0; i < rxQueue; i++) {
        ret = rte_eth_rx_queue_setup(portId, i, rxCacheSize, rte_eth_dev_socket_id(portId), &rxqConf, poolCtl->mempool);
        if (ret < 0) {
            KNET_ERR("Set rx queue info failed, portId %u, ret %d", portId, ret);
            return -1;
        }
    }

    return 0;
}

/**
 * @brief 创建一个 IPv4 和 TCP 的流量规则，并将其应用到指定的 DPDK 网络端口上
 * @note 参考 dpdk examples 代码 examples\flow_filtering\flow_blocks.c:generate_ipv4_flow()
 * flowAction[0] 队列动作 (QUEUE)
 * flowAction[1] 结束动作 (END)
 * flowPattern[0] 以太网头 (ETH)
 * flowPattern[1] IPv4 数据包
 * flowPattern[2] TCP 数据包
 * flowPattern[3] 结束项 (END)
 */
int32_t KnetGenerateIpv4Flow(uint16_t portId, int proto, struct KnetFlowCfg *flowCfg, struct rte_flow **flow)
{
    struct rte_flow_attr flowAttr = {0};
    struct rte_flow_item flowPattern[MAX_TRANS_PATTERN_NUM] = {0};
    struct rte_flow_action flowAction[MAX_ACTION_NUM] = {0};
    struct rte_flow_action_queue flowActionQueue = {.index = flowCfg->rxQueueId};
    struct rte_flow_item_tcp tcpSpec = {0};
    struct rte_flow_item_tcp tcpMask = {0};
    struct rte_flow_item_ipv4 ipSpec = {0};
    struct rte_flow_item_ipv4 ipMask = {0};
    struct rte_flow_error flowError = {0};
    int32_t ret;

    flowAttr.ingress = 1;
    flowAction[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
    flowAction[0].conf = &flowActionQueue;
    flowAction[1].type = RTE_FLOW_ACTION_TYPE_END;

    flowPattern[ETH_PATTERN_INDEX].type = RTE_FLOW_ITEM_TYPE_ETH;

    ipSpec.hdr.dst_addr = htonl(flowCfg->dstIp);
    ipSpec.hdr.src_addr = htonl(flowCfg->srcIp);
    ipMask.hdr.dst_addr = htonl(flowCfg->dstIpMask);
    ipMask.hdr.src_addr = htonl(flowCfg->srcIpMask);
    flowPattern[IPV4_PATTERN_INDEX].type = RTE_FLOW_ITEM_TYPE_IPV4;
    flowPattern[IPV4_PATTERN_INDEX].spec = &ipSpec;
    flowPattern[IPV4_PATTERN_INDEX].mask = &ipMask;

    tcpSpec.hdr.src_port = htons(flowCfg->srcPort);
    tcpSpec.hdr.dst_port = htons(flowCfg->dstPort);
    tcpMask.hdr.src_port = htons(flowCfg->srcPortMask);
    tcpMask.hdr.dst_port = htons(flowCfg->dstPortMask);

    if (proto == IPPROTO_TCP) {
        flowPattern[TCP_PATTERN_INDEX].type = RTE_FLOW_ITEM_TYPE_TCP;
    } else if (proto == IPPROTO_UDP) {
        flowPattern[TCP_PATTERN_INDEX].type = RTE_FLOW_ITEM_TYPE_UDP;
    } else {
        KNET_ERR("Proto type %d is not support", proto);
        return -1;
    }
    
    flowPattern[TCP_PATTERN_INDEX].spec = &tcpSpec;
    flowPattern[TCP_PATTERN_INDEX].mask = &tcpMask;
    flowPattern[END_PATTERN_INDEX].type = RTE_FLOW_ITEM_TYPE_END;

    ret = rte_flow_validate(portId, &flowAttr, flowPattern, flowAction, &flowError);
    if (ret != 0) {
        KNET_ERR("Flow validate failed: %s", flowError.message);
        return -1;
    }

    *flow = rte_flow_create(portId, &flowAttr, flowPattern, flowAction, &flowError);
    if (*flow == NULL) {
        KNET_ERR("Failed to create flow rule: %s", flowError.message);
        return -1;
    }
    return 0;
}

int32_t KnetGenerateArpFlow(uint16_t portId, uint32_t queueId, struct rte_flow **flow)
{
    struct rte_flow_attr flowAttr = {0};
    struct rte_flow_item flowPattern[MAX_ARP_PATTERN_NUM] = {0};
    struct rte_flow_action flowAction[MAX_ACTION_NUM] = {0};
    struct rte_flow_action_queue flowActionQueue = {.index = queueId};
    struct rte_flow_item_eth item_eth_mask = {.type = 0xFFFF}; // full mask
    struct rte_flow_item_eth item_eth_spec = {
        .dst.addr_bytes = "\xff\xff\xff\xff\xff\xff",
        .type = RTE_BE16(RTE_ETHER_TYPE_ARP)
    };
    struct rte_flow_error flowError = {0};
    int32_t ret;

    flowAttr.ingress = 1;
    flowAction[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
    flowAction[0].conf = &flowActionQueue;
    flowAction[1].type = RTE_FLOW_ACTION_TYPE_END;

    flowPattern[ETH_PATTERN_INDEX].type = RTE_FLOW_ITEM_TYPE_ETH;
    flowPattern[ETH_PATTERN_INDEX].mask = &item_eth_mask;
    flowPattern[ETH_PATTERN_INDEX].spec = &item_eth_spec;

    flowPattern[1].type = RTE_FLOW_ITEM_TYPE_END;

    ret = rte_flow_validate(portId, &flowAttr, flowPattern, flowAction, &flowError);
    if (ret != 0) {
        KNET_ERR("Flow validate failed: %s", flowError.message);
        return -1;
    }

    *flow = rte_flow_create(portId, &flowAttr, flowPattern, flowAction, &flowError);
    if (*flow == NULL) {
        KNET_ERR("Failed to create flow rule: %s", flowError.message);
        return -1;
    }
    return 0;
}

int32_t KnetGenerateIpv4TcpPortFlow(struct KnetFlowCfg *flowCfg, struct rte_flow **flow)
{
    return KnetGenerateIpv4Flow(g_dpdkDevInfo.netdevCtx.portId, IPPROTO_TCP, flowCfg, flow);
}

int32_t KnetGenerateIpv4UdpPortFlow(struct KnetFlowCfg *flowCfg, struct rte_flow **flow)
{
    return KnetGenerateIpv4Flow(g_dpdkDevInfo.netdevCtx.portId, IPPROTO_UDP, flowCfg, flow);
}

int32_t KnetGenerateCtlArpFlow(uint32_t queueId, struct rte_flow **flow)
{
    return KnetGenerateArpFlow(g_dpdkDevInfo.netdevCtx.portId, queueId, flow);
}

/**
 * @brief 配置指定端口的队列数量
 */
int32_t KnetConfigurePort(uint16_t portId, struct rte_eth_dev_info *devInfo, struct rte_eth_conf *localPortConf)
{
    int32_t rxQueue = KNET_GetCfg(CONF_INNER_RX_QUEUE_NUM).intValue;
    int32_t txQueue = KNET_GetCfg(CONF_INNER_TX_QUEUE_NUM).intValue;
    int32_t ret = rte_eth_dev_configure(portId, rxQueue, txQueue, localPortConf);
    if (ret < 0) {
        KNET_ERR("rte eth dev configure failed, portId %u, ret %d", portId, ret);
        return -1;
    }

    return 0;
}

/**
 * @brief 设置指定端口的最大传输单元（MTU）
 */
int32_t KnetSetMtu(uint16_t portId, struct rte_eth_dev_info *devInfo)
{
    int32_t mtu = KNET_GetCfg(CONF_INTERFACE_MTU).intValue;
    if (mtu < devInfo->min_mtu || mtu > devInfo->max_mtu) {
        KNET_ERR("Mtu %d must be in range [%u, %u]", mtu, devInfo->min_mtu, devInfo->max_mtu);
        return -1;
    }

    int32_t ret = rte_eth_dev_set_mtu(portId, mtu);
    if (ret < 0) {
        KNET_ERR("rte eth dev set mtu %d failed, ret %d, portId %u", mtu, ret, portId);
        return -1;
    }

    return 0;
}

/**
 * @brief 设置端口配置
 */
int32_t KnetSetupPort(uint16_t portId, struct rte_eth_dev_info *devInfo, struct rte_eth_conf *localPortConf)
{
    if (KnetDpdkPortConfSetup(devInfo, localPortConf) != 0) {
        KNET_ERR("K-NET dpdk port conf setup failed");
        return -1;
    }

    if (KnetConfigurePort(portId, devInfo, localPortConf) != 0) {
        KNET_ERR("K-NET configure port failed");
        return -1;
    }

    if (KnetSetMtu(portId, devInfo) != 0) {
        KNET_ERR("K-NET set mtu failed");
        return -1;
    }

    if (KnetDpdkTxRxQueueSetup(portId, devInfo, localPortConf) != 0) {
        KNET_ERR("K-NET dpdk tx rx queue setup failed");
        return -1;
    }

    return 0;
}

int KnetSetMac(uint16_t portId)
{
    int ret;
    uint8_t *macAddr =  (uint8_t *)KNET_GetCfg(CONF_INTERFACE_MAC).strValue;
    if (macAddr == NULL) {
        KNET_ERR("K-NET cfg mac is null");
        return -1;
    }
    struct rte_ether_addr addrCfg = {0};
    ret = memcpy_s(addrCfg.addr_bytes, RTE_ETHER_ADDR_LEN, macAddr, RTE_ETHER_ADDR_LEN);
    if (ret != 0) {
        KNET_ERR("Memcpy ip failed, ret %d", ret);
        return -1;
    }

    struct rte_ether_addr addrAim = {0};
    ret = rte_eth_macaddr_get(portId, &addrAim);
    if (ret != 0) {
        KNET_ERR("Get mac actualladdr failed, ret %d", ret);
        return -1;
    }

    if (memcmp(&addrCfg, &addrAim, sizeof(struct rte_ether_addr)) != 0) {
        KNET_ERR("K-NET cfg set mac error, do not match actully mac");
        return -1;
    }
    return 0;
}

/**
 * @brief 初始化DPDK端口
 * @note
 * devInfo 用于存储设备信息
 * localPortConf 本地端口配置
 * flowCfgs 用于存储流配置
 */
int32_t KnetInitDpdkPort(uint16_t portId, int procType)
{
    struct rte_eth_dev_info devInfo = {0};
    struct rte_eth_conf localPortConf = g_PortConf;

    if (procType == KNET_PROC_TYPE_SECONDARY) {
        return 0;
    }

    /* 获取指定端口的设备信息 */
    int32_t ret = rte_eth_dev_info_get(portId, &devInfo);
    if (ret != 0) {
        KNET_ERR("rte eth dev info get failed, portId %u, ret %d", portId, ret);
        return -1;
    }

    /* 设置端口配置 */
    if (KnetSetupPort(portId, &devInfo, &localPortConf) != 0) {
        KNET_ERR("Port setup failed, portId %u", portId);
        return -1;
    }

    /* 启动DPDK设备 */
    if (rte_eth_dev_start(portId) < 0) {
        KNET_ERR("rte eth dev start failed, portId %u", portId);
        return -1;
    }

    if (KnetSetMac(portId) != 0) {
        KNET_ERR("K-NET setup mac, portId %u", portId);
        return -1;
    }
    
    return 0;
}

static int32_t KnetUninitDpdkPort(uint16_t portId)
{
    int32_t ret = rte_eth_dev_stop(portId);
    if (ret < 0) {
        KNET_ERR("rte eth dev stop failed, portId %u, ret %d", portId, ret);
        return -1;
    }

    return 0;
}

static int32_t KnetModInit(void)
{
    uint32_t ret = KNET_PktModInit();
    if (ret != 0) {
        KNET_ERR("K-NET pkt mod init failed");
        return -1;
    }

    return 0;
}

DpdkNetdevCtx *KNET_GetNetDevCtx(void)
{
    return &g_dpdkDevInfo.netdevCtx;
}

static int KNET_FindDpdkCore(void)
{
    int dpdkCore = -1;

    int32_t ret = 0;
    uint64_t serviceTid = KNET_ThreadId();
    uint16_t cpus[MAX_CPU_NUM] = {0};
    uint32_t cpuNums = MAX_CPU_NUM;

    ret = KNET_GetThreadAffinity(serviceTid, cpus, &cpuNums);
    if (ret != 0) {
        KNET_ERR("Service cpu get failed, ret %d", ret);
        return -1;
    }
    
    uint16_t core;
    for (uint32_t i = 0; i < cpuNums; ++i) {
        core = cpus[i];
        if (KNET_FindCoreInList(core) == -1) {
            dpdkCore = core;
            break;
        }
    }
    return dpdkCore;
}

int KnetGenerateCoreList(int dpdkCore, int procType, char* coreList, char* mainLcore)
{
    int32_t ret = 0;
    char* coreListStr = KNET_GetCfg(CONF_INNER_CORE_LIST).strValue;
    if (coreListStr == NULL) {
        KNET_ERR("Get core list failed");
        return -1;
    }

    int runMode = KNET_GetCfg(CONF_COMMON_MODE).intValue;
    if (runMode == KNET_RUN_MODE_MULTIPLE && procType == KNET_PROC_TYPE_PRIMARY) {
        ret = sprintf_s(coreList, MAX_STRVALUE_NUM, "%d", dpdkCore);
        if (ret < 0) {
            KNET_ERR("Sprintf failed, ret:%d", ret);
            return -1;
        }
    } else {
        ret = sprintf_s(coreList, MAX_STRVALUE_NUM, "%s,%d", coreListStr, dpdkCore);
        if (ret < 0) {
            KNET_ERR("Sprintf failed, ret:%d", ret);
            return -1;
        }
    }

    if ((runMode == KNET_RUN_MODE_MULTIPLE && procType == KNET_PROC_TYPE_SECONDARY) ||
            runMode == KNET_RUN_MODE_SINGLE) {
        ret = sprintf_s(mainLcore, MAX_STRVALUE_NUM, "--main-lcore=%d", dpdkCore);
        if (ret < 0) {
            KNET_ERR("Sprintf failed, ret:%d", ret);
            return -1;
        }
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
static int32_t KnetDpdkArgvAssign(char **argv, int procType)
{
    int dpdkCore = KNET_FindDpdkCore();
    if (dpdkCore == -1) {
        KNET_ERR("Dpdk core not found");
        return -1;
    }

    char coreList[MAX_STRVALUE_NUM] = {0};
    char mainLcore[MAX_STRVALUE_NUM] = {0};

    int32_t ret = KnetGenerateCoreList(dpdkCore, procType, coreList, mainLcore);
    if (ret != 0) {
        KNET_ERR("Generate core index failed");
        return -1;
    }

    char* dpdkArgs[KNET_DPDK_PRIM_ARGC] = {
        KNET_LOG_MODULE_NAME,
        "-l",
        coreList,
        "-n2",
        "--file-prefix=knet",
        KNET_GetCfg(CONF_DPDK_TELEMETRY).intValue == 1 ? "--telemetry" : "--no-telemetry",
        (procType == KNET_PROC_TYPE_PRIMARY) ? "--proc-type=primary" : "--proc-type=secondary",
        mainLcore,
        KNET_GetCfg(CONF_DPDK_SOCKET_MEM).strValue,
        KNET_GetCfg(CONF_DPDK_SOCKET_LIM).strValue,
        "-a",
        KNET_GetCfg(CONF_INTERFACE_BDF_NUM).strValue,
        KNET_GetCfg(CONF_DPDK_EXTERNAL_DRIVER).strValue,
        KNET_GetCfg(CONF_DPDK_HUGE_DIR).strValue
    };

    for (uint32_t i = 0; i < KNET_DPDK_PRIM_ARGC; ++i) {
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

static int32_t KnetDpdkSlaveLcoreNumCheck(void)
{
    int32_t ctrlVcpuId = KNET_GetCfg(CONF_COMMON_CTRL_VCPU_ID).intValue;
    int maxVcpuNum = get_nprocs_conf();
    if (ctrlVcpuId >= maxVcpuNum) {
        KNET_ERR("Ctrl vcpu Id %d equal or exceeds max vcpu num %d", ctrlVcpuId, maxVcpuNum);
        return -1;
    }
    int ret = KNET_CpuDetected(ctrlVcpuId);
    if (ret < 0) {
        KNET_ERR("Ctrl vcpu Id %d is not available in the available CPUs ", ctrlVcpuId);
        return -1;
    }

    int32_t slaveLcoreNum = 0;
    uint32_t lcoreId = 0;
    /* 遍历slave lcore并计算总个数 */
    RTE_LCORE_FOREACH_WORKER(lcoreId) {
        /* 考虑性能，协议栈数据面和控制面线程不允许在同一个核上 */
        if (lcoreId == (uint32_t)ctrlVcpuId) {
            KNET_ERR("Dpdk lcore %u and ctrl_vcpu_id must be on different cores", lcoreId);
            return -1;
        }
        ++slaveLcoreNum;
    }
 
    int32_t workerNum = KNET_GetCfg(CONF_DP_MAX_WORKER_NUM).intValue;
    if (slaveLcoreNum != workerNum) {
        KNET_ERR("SlaveL core num %d not equal to max worker id %d", slaveLcoreNum, workerNum);
        return -1;
    }

    return 0;
}

int KNET_DeleteFlowRule(struct rte_flow *flow)
{
    int32_t ret;
    struct rte_flow_error error;

    if (flow == NULL) {
        KNET_ERR("Invalid flow pointer");
        return -1;
    }

    ret = rte_flow_destroy(g_dpdkDevInfo.netdevCtx.portId, flow, &error);
    if (ret != 0) {
        KNET_ERR("Failed to destroy flow rule, error:%d", error.type);
        return -1;
    }

    return 0;
}

/**
 * @brief 将协议栈dpWorkerId与dpdk lcoreId一对一映射
 * @attention 必须在DP_Init()之前完成，DP_Init中定时器会用到workerId，需要将workerId转换成lcoreId使能dpdk的定时器
 */
KNET_STATIC int32_t KnetDpdkSlaveLcoreMatchDpWorker(void)
{
    int32_t ret = KnetDpdkSlaveLcoreNumCheck();
    if (ret != 0) {
        KNET_ERR("Dpdk slave lcore num check failed, ret %d", ret);
        return -1;
    }

    for (uint32_t i = 0; i < MAX_WORKER_ID; ++i) {
        g_dpWorkerIdTable.coreIdToWorkerId[i] = INVALID_WORKER_ID;
    }

    g_dpWorkerIdTable.maxWorkerId = 0;
    /* 将协议栈dpWorkerId映射到dpdk lcoreId */
    uint32_t lcoreId = 0;
    RTE_LCORE_FOREACH_WORKER(lcoreId) {
        if (g_dpWorkerIdTable.maxWorkerId >= MAX_WORKER_ID) {
            KNET_ERR("WorkerId %u equal to or exceeds MAX_WORKER_ID %u", g_dpWorkerIdTable.maxWorkerId, MAX_WORKER_ID);
            return -1;
        }

        g_dpWorkerIdTable.workerInfo[g_dpWorkerIdTable.maxWorkerId].workerId = g_dpWorkerIdTable.maxWorkerId;
        g_dpWorkerIdTable.workerInfo[g_dpWorkerIdTable.maxWorkerId].lcoreId = lcoreId;
        g_dpWorkerIdTable.coreIdToWorkerId[lcoreId] = g_dpWorkerIdTable.maxWorkerId;
        KNET_INFO("DpWorkerId %u match lcoreId %u", g_dpWorkerIdTable.maxWorkerId, lcoreId);

        ++g_dpWorkerIdTable.maxWorkerId;
    }

    return 0;
}

int32_t RteInit(int32_t argc, char **argv, int procType, int processMode)
{
    int32_t ret = 0;
    uint64_t serviceTid = 0;
    uint32_t cpuNums = MAX_CPU_NUM;
    uint16_t cpus[MAX_CPU_NUM] = {0};

    /* 获取 业务线程绑核 cpu */
    serviceTid = KNET_ThreadId();
    ret = KNET_GetThreadAffinity(serviceTid, cpus, &cpuNums);
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

    if (processMode == KNET_RUN_MODE_SINGLE) {
        ret = KNET_MultiPdumpInit(&g_pdumpRequestMz);
        if (ret < 0) {
            KNET_ERR("Rte dump init failed, ret %d", ret);
            return -1;
        }
    }

    if (procType == KNET_PROC_TYPE_PRIMARY && KNET_GetCfg(CONF_DPDK_TELEMETRY).intValue == 1) {
        ret = KNET_InitDpdkTelemetry();
        if (ret < 0) {
            KNET_ERR("K-NET init dpdk telemetry failed, ret %d", ret);
            return -1;
        }
    }

    return 0;
}

static int32_t KnetDpdkComponentsInit(int procType, int processMode)
{
    int32_t ret;
    int32_t argc = KNET_DPDK_PRIM_ARGC;
    char *argv[KNET_DPDK_PRIM_ARGC] = {0};
    char argvArr[KNET_DPDK_PRIM_ARGC][KNET_DPDK_ARG_MAX_LEN + 1] = {0};  // +1因为要预留截断符

    for (uint32_t i = 0; i < KNET_DPDK_PRIM_ARGC; ++i) {
        argv[i] = argvArr[i];
    }

    ret = KnetDpdkArgvAssign(argv, procType);
    if (ret != 0) {
        KNET_ERR("K-NET dpdk argv assign failed, ret %d", ret);
        return -1;
    }

    ret = RteInit(argc, argv, procType, processMode);
    if (ret < 0) {
        KNET_ERR("rte eal init failed, ret %d", ret);
        return -1;
    }

    if (procType == KNET_PROC_TYPE_SECONDARY || processMode == KNET_RUN_MODE_SINGLE) {
        ret = KnetDpdkSlaveLcoreMatchDpWorker();
        if (ret != 0) {
            KNET_ERR("Dpdk slave lcore match dp worker failed, ret %d", ret);
            return -1;
        }
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
            KNET_WARN("Socket %d: Memory usage is too high: %lf", sock, memUsage);
        }
    }

    DpdkMbufPoolUsageCheck();
}

int32_t KNET_InitDpdk(int procType, int processMode)
{
    int32_t ret;

    /* DPDK组件初始化 */
    ret = KnetDpdkComponentsInit(procType, processMode);
    if (ret < 0) {
        KNET_ERR("K-NET dpdk components init failed");
        return -1;
    }

    /* KNET模块初始化（mbuf和内存管理依赖此初始化） */
    ret = KnetModInit();
    if (ret < 0) {
        KNET_ERR("K-NET mod init failed");
        return -1;
    }

    ret = KnetMbufMempoolCreate();
    if (ret < 0) {
        KNET_ERR("K-NET mbuf mempool create failed");
        return -1;
    }

    /* 目前只适配一个设备名称 */
    ret = rte_eth_dev_get_port_by_name(KNET_GetCfg(CONF_INTERFACE_BDF_NUM).strValue, &g_dpdkDevInfo.netdevCtx.portId);
    if (ret < 0) {
        KNET_ERR("rte get port by name %s failed, ret %d", KNET_GetCfg(CONF_INTERFACE_BDF_NUM).strValue, ret);
        return -1;
    }
    KNET_INFO("DevName %s, portId %u", KNET_GetCfg(CONF_INTERFACE_BDF_NUM).strValue, g_dpdkDevInfo.netdevCtx.portId);

    ret = KnetInitDpdkPort(g_dpdkDevInfo.netdevCtx.portId, procType);
    if (ret < 0) {
        KNET_ERR("K-NET init dpdk portId %u failed, ret %d", g_dpdkDevInfo.netdevCtx.portId, ret);
        return -1;
    }

    DpdkResourceCheck();

    return 0;
}

int32_t KNET_UninitDpdk(int procType, int processMode)
{
    int32_t ret;
    int32_t flag = 0;
    if (procType == KNET_PROC_TYPE_PRIMARY) {
        ret = KnetUninitDpdkPort(g_dpdkDevInfo.netdevCtx.portId);
        if (ret != 0) {
            KNET_ERR("K-NET uninit dpdk port failed, ret %d", ret);
            flag = 1;
        }
        g_dpdkDevInfo.netdevCtx.portId = 0;
    }

    KnetMbufMempoolDestroy();

    if (processMode == KNET_RUN_MODE_SINGLE) {
        ret = KNET_MultiPdumpUninit(g_pdumpRequestMz);
        if (ret < 0) {
            KNET_ERR("rte dump cleaunup failed, ret %d", ret);
            flag = 1;
        }
        g_pdumpRequestMz = NULL;
    }

    ret = rte_eal_cleanup();
    if (ret != 0) {
        KNET_ERR("rte eal cleanup failed, ret %d", ret);
        flag = 1;
    }

    return flag == 0 ? 0 : -1;
}
#ifdef __cplusplus
}
#endif /* __cpluscplus */