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
 
#include "rte_config.h"
#include "rte_cycles.h"

#include "knet_pktpool.h"
#include "knet_config.h"
#include "knet_dpdk_init_dev.h"

// 单位秒，实测物理机SP卡2-3秒状态置为UP, TM卡1-2秒职位UP 网卡link down -> link up最久30s,K-NET此处最多等待5s
#define WAIT_ETH_LINK_UP_TIME 5
// 驱动默认值tx_free_thresh为32，与驱动设置为一致
#define KNET_TX_FREE_THRESH 32

static struct rte_eth_conf g_PortConf = {
    .rxmode = {
        .mq_mode = RTE_ETH_MQ_RX_NONE,
        .offloads = 0,
    },
    .txmode = {
        .mq_mode = RTE_ETH_MQ_TX_NONE,
    },
};

/**
 * @attention 该函数仅在rte_eth_dev_configure失败时调用，取消dcb配置然后重新rte_eth_dev_configure
 */
static void DpdkMultiQueueResetupWithoutDcb(struct rte_eth_conf *portConf)
{
    #ifndef KNET_TEST
    portConf->rxmode.mq_mode = RTE_ETH_MQ_RX_RSS;
    portConf->txmode.mq_mode = RTE_ETH_MQ_TX_NONE;
    portConf->dcb_capability_en = 0;
    #else
    (void)portConf;
    #endif
}

KNET_STATIC void DpdkMultiQueueSetup(const struct rte_eth_dev_info *devInfo, struct rte_eth_conf *portConf)
{
    // 目前不支持多进程单核多队列，调用CONF_DPDK_QUEUE_NUM处统一修改为内置值
    if (KNET_GetCfg(CONF_INNER_QUEUE_NUM)->intValue <= 1) {
        return;
    }

    struct rte_eth_rss_conf rssConf = { 0 };
    rssConf.rss_hf = devInfo->flow_type_rss_offloads;

    struct rte_eth_dcb_rx_conf *rxConf = &portConf->rx_adv_conf.dcb_rx_conf;
    struct rte_eth_dcb_tx_conf *txConf = &portConf->tx_adv_conf.dcb_tx_conf;
    enum rte_eth_nb_tcs numTcs = RTE_ETH_4_TCS; // 暂定 4 traffic classes，后续如果多队列支持用户配置再支持其他个数
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

KNET_STATIC int32_t DpdkHwChecksumSetup(const struct rte_eth_dev_info *devInfo, struct rte_eth_conf *portConf)
{
    if (KNET_GetCfg(CONF_HW_TCP_CHECKSUM)->intValue <= 0) {
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
static int32_t DpdkRxTxOffloadScatterSetup(const struct rte_eth_dev_info *devInfo, struct rte_eth_conf *portConf)
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

static int32_t DpdkPortConfSetup(const struct rte_eth_dev_info *devInfo, struct rte_eth_conf *portConf,
                                 int dpdkPortType)
{
    DpdkMultiQueueSetup(devInfo, portConf);

    int32_t ret = DpdkHwChecksumSetup(devInfo, portConf);
    if (ret != 0) {
        KNET_ERR("K-NET dpdk hw checksum setup failed, ret %d", ret);
        return -1;
    }

    if (KNET_GetCfg(CONF_HW_TSO)->intValue > 0) {
        ret = KNET_SetTSO(devInfo, portConf);
        if (ret != 0) {
            KNET_ERR("K-NET dpdk tso setup failed, ret %d", ret);
            return -1;
        }
    }

    if (KNET_GetCfg(CONF_HW_LRO)->intValue > 0) {
        ret = KNET_SetLRO(devInfo, portConf, dpdkPortType);
        if (ret != 0) {
            KNET_ERR("K-NET dpdk lro setup failed, dpdkPortType %d, ret %d", dpdkPortType, ret);
            return -1;
        }
    }

    ret = DpdkRxTxOffloadScatterSetup(devInfo, portConf);
    if (ret != 0) {
        KNET_ERR("K-NET dpdk rx scatter offload setup failed, ret %d", ret);
        return -1;
    }

    return 0;
}

/**
 * @brief 配置指定端口的队列数量
 */
int32_t DpdkPortTxRxQueueNum(uint16_t portId, struct rte_eth_dev_info *devInfo, struct rte_eth_conf *localPortConf,
    int dpdkPortType)
{
    // 目前不支持多进程单核多队列，调用CONF_DPDK_QUEUE_NUM处统一修改为内置值
    int32_t rxQueue = KNET_GetCfg(CONF_INNER_QUEUE_NUM)->intValue;
    int32_t txQueue = KNET_GetCfg(CONF_INNER_QUEUE_NUM)->intValue;

    /* TM280网卡，直接舍弃dcb功能, 原因是偶数bdf保留dcb会影响rss.非TM280网卡正常下带dcb的rss */
    if (KNET_GetCfg(CONF_INNER_HW_TYPE)->intValue ==
        KNET_HW_TYPE_TM280) {
        DpdkMultiQueueResetupWithoutDcb(localPortConf);
    }
    
    int32_t ret = rte_eth_dev_configure(portId, rxQueue, txQueue, localPortConf);
    if (ret < 0) {
        KNET_ERR("Rte eth dev configure without dcb failed, portId %u, ret %d", portId, ret);
        return -1;
    }

    return 0;
}

/**
 * @brief 设置指定端口的最大传输单元（MTU）
 */
int32_t DpdkPortMtu(uint16_t portId, struct rte_eth_dev_info *devInfo)
{
    int32_t mtu = KNET_GetCfg(CONF_INTERFACE_MTU)->intValue;
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

int DpdkPortMacCheck(uint16_t portId)
{
    uint8_t *macAddr =  (uint8_t *)KNET_GetCfg(CONF_INTERFACE_MAC)->strValue;
    if (macAddr == NULL) {
        KNET_ERR("K-NET cfg mac is null");
        return -1;
    }
    struct rte_ether_addr addrCfg = {0};
    int ret = memcpy_s(addrCfg.addr_bytes, RTE_ETHER_ADDR_LEN, macAddr, RTE_ETHER_ADDR_LEN);
    if (ret != 0) {
        KNET_ERR("Memcpy ip failed, ret %d", ret);
        return -1;
    }

    struct rte_ether_addr addrAim = {0};
    ret = rte_eth_macaddr_get(portId, &addrAim);
    if (ret != 0) {
        KNET_ERR("Get mac actual addr failed, ret %d", ret);
        return -1;
    }

    if (memcmp(&addrCfg, &addrAim, sizeof(struct rte_ether_addr)) != 0) {
        KNET_ERR("K-NET cfg check mac error, do not match actual mac");
        return -1;
    }
    return 0;
}

int32_t DpdkMacJudge(uint16_t portId, int dpdkPortType)
{
    // 非bond场景下对单网卡端口进行mac地址检查, bond场景下对bond端口进行mac地址检查
    bool bondEnable = (KNET_GetCfg(CONF_INTERFACE_BOND_ENABLE)->intValue == 1);
    if ((!bondEnable && DpdkPortMacCheck(portId) != 0) ||
        (bondEnable && (dpdkPortType == DPDK_PORT_BOND) && KNET_BondPortMacCheck() != 0)) {
        KNET_ERR("K-NET check mac failed, portId %u", portId);
        return -1;
    }
    return 0;
}

static int32_t DpdkTxRxQueueSetup(uint16_t portId, const struct rte_eth_dev_info *devInfo,
    struct rte_eth_conf *portConf)
{
    // 目前不支持多进程单核多队列，调用CONF_DPDK_QUEUE_NUM处统一修改为内置值
    int32_t txCacheSize = KNET_GetCfg(CONF_DPDK_TX_CACHE_SIZE)->intValue;
    int32_t rxCacheSize = KNET_GetCfg(CONF_DPDK_RX_CACHE_SIZE)->intValue;
    if (txCacheSize < devInfo->tx_desc_lim.nb_min || txCacheSize > devInfo->tx_desc_lim.nb_max ||
        rxCacheSize < devInfo->rx_desc_lim.nb_min || rxCacheSize > devInfo->rx_desc_lim.nb_max) {
        KNET_ERR("Tx cache size %d must be in range [%u, %u], rx_cache_size %d must be in range [%u, %u]",
            txCacheSize, devInfo->tx_desc_lim.nb_min, devInfo->tx_desc_lim.nb_max,
            rxCacheSize, devInfo->rx_desc_lim.nb_min, devInfo->rx_desc_lim.nb_max);
        return -1;
    }

    int32_t ret = 0;
    int32_t txQueue = KNET_GetCfg(CONF_INNER_QUEUE_NUM)->intValue;
    struct rte_eth_txconf txqConf = devInfo->default_txconf;
    txqConf.offloads = portConf->txmode.offloads;
    txqConf.tx_free_thresh = KNET_TX_FREE_THRESH;
    for (int32_t i = 0; i < txQueue; ++i) {
        ret = rte_eth_tx_queue_setup(portId, i, txCacheSize, rte_eth_dev_socket_id(portId), &txqConf);
        if (ret < 0) {
            KNET_ERR("Set tx queue info failed, portId %u, ret %d", portId, ret);
            return -1;
        }
    }

    KnetPktPoolCtrl *poolCtl = KnetPktGetPoolCtrl(KnetGetPktPoolId());
    if (poolCtl == NULL) {
        return -1;
    }

    int32_t rxQueue = KNET_GetCfg(CONF_INNER_QUEUE_NUM)->intValue;
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
 * @brief 设置端口配置
 */
int32_t SetupPort(uint16_t portId, struct rte_eth_dev_info *devInfo, struct rte_eth_conf *localPortConf,
                  int dpdkPortType)
{
    if (DpdkPortConfSetup(devInfo, localPortConf, dpdkPortType) != 0) {
        KNET_ERR("K-NET dpdk port conf setup failed");
        return -1;
    }

    if (DpdkPortTxRxQueueNum(portId, devInfo, localPortConf, dpdkPortType) != 0) {
        KNET_ERR("K-NET configure port failed, portId %u, dpdkPortType %d", portId, dpdkPortType);
        return -1;
    }

    if (DpdkPortMtu(portId, devInfo) != 0) {
        KNET_ERR("K-NET set mtu failed, portId %u", portId);
        return -1;
    }

    if (DpdkMacJudge(portId, dpdkPortType) != 0) {
        KNET_ERR("K-NET check mac failed, portId %u", portId);
        return -1;
    }

    if (DpdkTxRxQueueSetup(portId, devInfo, localPortConf) != 0) {
        KNET_ERR("K-NET dpdk tx rx queue setup failed, portId %u", portId);
        return -1;
    }

    return 0;
}

/**
 * @brief 确保网卡状态UP
 * 未开流分叉时，通过轮询查看网卡状态，直到网卡UP或者到达WAIT_ETH_LINK_UP_TIME秒, 如果超时未UP，继续执行后面的逻辑
 * 不关注流分叉场景：流分叉时port状态为down
 * @param portId
 * @return int : 0表示成功UP或者超时， -1表示获取状态失败
 */
KNET_STATIC int IsEthLinkUp(uint16_t portId)
{
    if (KNET_GetCfg(CONF_HW_BIFUR_ENABLE)->intValue == BIFUR_ENABLE) {
        return 0;
    }

    struct rte_eth_link link = {0};
    uint64_t beforeLinkGetTime = rte_get_timer_cycles();
    uint64_t hz = rte_get_timer_hz();
    uint64_t waitTime = WAIT_ETH_LINK_UP_TIME * hz;  // WAIT_ETH_LINK_UP_TIME 秒对应的 cycle 数, 无溢出风险
    int ret = 0;
    while (1) {
        uint64_t nowTime = rte_get_timer_cycles();
        if ((nowTime - beforeLinkGetTime) > waitTime) {
            KNET_WARN("The eth link status is still down until %d seconds , portId %u", WAIT_ETH_LINK_UP_TIME, portId);
            return 0; // 此时不一定有问题，继续执行后面的逻辑
        }
        ret = rte_eth_link_get_nowait(portId, &link);
        if (ret != 0) {
            KNET_ERR("The rte eth link get failed, portId %u, ret %d", portId, ret);
            return -1;
        }
        if (link.link_status == RTE_ETH_LINK_UP) {
            KNET_INFO("The rte eth link up uses %d seconds",  ((rte_get_timer_cycles() - beforeLinkGetTime) / hz));
            break;
        }
 
        usleep(10000); // 10000表示10ms
    }
    KNET_INFO("The rte eth port %u link up!", portId);
    return 0;
}

/**
 * @brief 初始化DPDK端口
 * @note
 * devInfo 用于存储设备信息
 * localPortConf 本地端口配置
 * flowCfgs 用于存储流配置
 */
int32_t KnetInitDpdkPort(uint16_t portId, int procType, int dpdkPortType)
{
    if (procType == KNET_PROC_TYPE_SECONDARY) {
        return 0;
    }

    struct rte_eth_dev_info devInfo = {0};
    struct rte_eth_conf localPortConf = g_PortConf;

    /* 获取指定端口的设备信息 */
    int32_t ret = rte_eth_dev_info_get(portId, &devInfo);
    if (ret != 0) {
        KNET_ERR("The rte eth dev info get failed, portId %u, dpdkPortType %d, ret %d", portId, dpdkPortType, ret);
        return -1;
    }

    /* 设置端口配置 */
    if (SetupPort(portId, &devInfo, &localPortConf, dpdkPortType) != 0) {
        KNET_ERR("Port setup failed, portId %u, dpdkPortType %d", portId, dpdkPortType);
        return -1;
    }

    /* 启动DPDK设备 */
    if (rte_eth_dev_start(portId) < 0) {
        KNET_ERR("The rte eth dev start failed, portId %u", portId);
        return -1;
    }

    /* 确保网卡状态UP */
    if (IsEthLinkUp(portId) < 0) {
        KNET_ERR("The eth link up failed, portId %u, ret %d", portId, ret);
        return -1;
    }

    return 0;
}

int32_t KnetGetDpdkPortIdAndInit(const char *devName, uint16_t *portId, int procType)
{
    int32_t ret = rte_eth_dev_get_port_by_name(devName, portId);
    if (ret < 0) {
        KNET_ERR("Rte get port by name %s failed, portId %u, ret %d", devName, *portId, ret);
        return -1;
    }
    KNET_INFO("DevName %s, portId %u", devName, *portId);

    ret = KnetInitDpdkPort(*portId, procType, DPDK_PORT_NORMAL);
    if (ret < 0) {
        KNET_ERR("K-NET init dpdk portId %u failed, ret %d", *portId, ret);
        return -1;
    }

    return 0;
}

int32_t KnetUninitUnbondDpdkPort(uint16_t portId, int procType)
{
    if (procType == KNET_PROC_TYPE_SECONDARY) {
        return 0;
    }

    int32_t ret = rte_eth_dev_stop(portId);
    if (ret != 0) {
        KNET_ERR("K-NET uninit dpdk port failed, portId %u, ret %d", portId, ret);
        return -1;
    }
    return 0;
}