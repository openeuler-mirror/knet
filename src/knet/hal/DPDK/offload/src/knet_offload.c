/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 硬件卸载相关操作
 */

#include "knet_types.h"
#include "knet_config.h"
#include "knet_log.h"
#include "knet_offload.h"

typedef enum {
    ETH_PATTERN_INDEX = 0,
    IPV4_PATTERN_INDEX,
    TCP_PATTERN_INDEX,
    END_PATTERN_INDEX,
} PatternIndex;

#define BOND_LRO_SEG 1518
#define RSS_KEY_LEN 64
#define MAX_TRANS_PATTERN_NUM 4
#define MAX_ARP_PATTERN_NUM 2
#define MAX_ACTION_NUM 2

int32_t KNET_SetLRO(const struct rte_eth_dev_info *devInfo, struct rte_eth_conf *portConf, int dpdkPortType)
{
    if (devInfo->rx_offload_capa & RTE_ETH_RX_OFFLOAD_TCP_LRO) {
        portConf->rxmode.offloads |= RTE_ETH_RX_OFFLOAD_TCP_LRO;
        if (dpdkPortType == DPDK_PORT_BOND) {
            portConf->rxmode.max_lro_pkt_size = BOND_LRO_SEG; /* 1518为dpdk bond设备的lro size限制最大值 */
        } else {
            portConf->rxmode.max_lro_pkt_size = devInfo->max_lro_pkt_size;
        }
        KNET_INFO("LRO enable, lro pkt size %u", devInfo->max_lro_pkt_size);
    } else {
        KNET_ERR("Dev not support LRO");
        return -1;
    }
    return 0;
}

int32_t KNET_SetTSO(const struct rte_eth_dev_info *devInfo, struct rte_eth_conf *portConf)
{
    if (devInfo->tx_offload_capa & RTE_ETH_TX_OFFLOAD_TCP_TSO) {
        portConf->txmode.offloads |= RTE_ETH_TX_OFFLOAD_TCP_TSO;
        KNET_INFO("TSO enable");
    } else {
        KNET_ERR("Dev not support TSO");
        return -1;
    }
    return 0;
}

static int32_t GetActionRss(struct rte_flow_action_rss *flowActionRss, struct KNET_FlowCfg *flowCfg,
    uint16_t portId, uint8_t *rssKey)
{
    if (KNET_GetCfg(CONF_COMMON_MODE)->intValue == KNET_RUN_MODE_MULTIPLE) {
        KNET_ERR("Mutiple mode not support rss");
        return -1;
    }

    struct rte_eth_rss_conf rssConf = {
        .rss_key = rssKey,
        .rss_key_len = RSS_KEY_LEN,
    };
    int32_t ret = rte_eth_dev_rss_hash_conf_get(portId, &rssConf);
    if (ret != 0) {
        KNET_ERR("rte_eth_dev_rss_hash_conf_get ret %d", ret);
        return -1;
    }

    flowActionRss->types = rssConf.rss_hf;
    flowActionRss->key_len = rssConf.rss_key_len;
    flowActionRss->queue_num = flowCfg->rxQueueIdSize;
    flowActionRss->key = rssKey;
    flowActionRss->queue = flowCfg->rxQueueId;

    return 0;
}

KNET_STATIC void GetPatternIpv4(struct rte_flow_item *flowPattern, size_t flowPatternLen, struct KNET_FlowCfg *flowCfg,
    struct rte_flow_item_ipv4 *ipSpec, struct rte_flow_item_ipv4 *ipMask)
{
    ipSpec->hdr.dst_addr = htonl(flowCfg->dstIp);
    ipSpec->hdr.src_addr = htonl(flowCfg->srcIp);
    ipMask->hdr.dst_addr = htonl(flowCfg->dstIpMask);
    ipMask->hdr.src_addr = htonl(flowCfg->srcIpMask);
    flowPattern[IPV4_PATTERN_INDEX].type = RTE_FLOW_ITEM_TYPE_IPV4;
    flowPattern[IPV4_PATTERN_INDEX].spec = ipSpec;
    flowPattern[IPV4_PATTERN_INDEX].mask = ipMask;
}

KNET_STATIC int32_t GetPattern(struct rte_flow_item *flowPattern, size_t flowPatternLen, struct KNET_FlowCfg *flowCfg,
    struct rte_flow_item_tcp *tcpSpec, struct rte_flow_item_tcp *tcpMask)
{
    tcpSpec->hdr.src_port = htons(flowCfg->srcPort);
    tcpSpec->hdr.dst_port = htons(flowCfg->dstPort);
    tcpMask->hdr.src_port = htons(flowCfg->srcPortMask);
    tcpMask->hdr.dst_port = htons(flowCfg->dstPortMask);

    if (flowCfg->proto == IPPROTO_TCP) {
        flowPattern[TCP_PATTERN_INDEX].type = RTE_FLOW_ITEM_TYPE_TCP;
    } else if (flowCfg->proto == IPPROTO_UDP) {
        flowPattern[TCP_PATTERN_INDEX].type = RTE_FLOW_ITEM_TYPE_UDP;
    } else {
        KNET_ERR("Proto type %d is not support", flowCfg->proto);
        return -1;
    }

    flowPattern[TCP_PATTERN_INDEX].spec = tcpSpec;
    flowPattern[TCP_PATTERN_INDEX].mask = tcpMask;

    return 0;
}

KNET_STATIC void TcpFlowCreationSuccessLog(uint16_t portId, struct KNET_FlowCfg *flowCfg)
{
    KNET_DEBUG("Create tcp flow rule success, port %hu, dstIp %u, dstPort %hu, dstPortMask %hu",
        portId, flowCfg->dstIp, flowCfg->dstPort, flowCfg->dstPortMask);

    if (flowCfg->rxQueueIdSize == 1) {
        KNET_DEBUG("QueueSize set to 1, queueId is %hu", flowCfg->rxQueueId[0]);
        return;
    }
    KNET_DEBUG("Rss queueNum %hu", flowCfg->rxQueueIdSize);
}

/* *
 * @brief 创建一个 IPv4 和 TCP 的流量规则，并将其应用到指定的 DPDK 网络端口上
 * @note 参考 dpdk examples 代码 examples\flow_filtering\flow_blocks.c:generate_ipv4_flow()
 * flowAction[0] 队列动作 (QUEUE)
 * flowAction[1] 结束动作 (END)
 * flowPattern[0] 以太网头 (ETH)
 * flowPattern[1] IPv4 数据包
 * flowPattern[2] TCP 数据包
 * flowPattern[3] 结束项 (END)
 */
int32_t KNET_GenerateIpv4Flow(uint16_t portId, struct KNET_FlowCfg *flowCfg, struct rte_flow **flow)
{
    struct rte_flow_action flowAction[MAX_ACTION_NUM] = {0};
    struct rte_flow_action_queue flowActionQueue = {0};
    struct rte_flow_action_rss flowActionRss  = {0};
    uint8_t rssKey[RSS_KEY_LEN] = {0};

    int32_t ret;
    if (flowCfg->rxQueueIdSize == 1) {
        flowActionQueue.index = flowCfg->rxQueueId[0];
        flowAction[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
        flowAction[0].conf = &flowActionQueue;
    } else {
        ret = GetActionRss(&flowActionRss, flowCfg, portId, rssKey);
        if (ret != 0) {
            KNET_ERR("rte_flow_action_rss set failed, ret %d", ret);
            return ret;
        }

        flowAction[0].type = RTE_FLOW_ACTION_TYPE_RSS;
        flowAction[0].conf = &flowActionRss;
    }
    struct rte_flow_attr flowAttr = {0};
    flowAttr.ingress = 1;
    flowAction[1].type = RTE_FLOW_ACTION_TYPE_END;

    struct rte_flow_item flowPattern[MAX_TRANS_PATTERN_NUM] = {0};
    flowPattern[ETH_PATTERN_INDEX].type = RTE_FLOW_ITEM_TYPE_ETH;

    struct rte_flow_item_ipv4 ipSpec = {0};
    struct rte_flow_item_ipv4 ipMask = {0};
    GetPatternIpv4(flowPattern, MAX_TRANS_PATTERN_NUM, flowCfg, &ipSpec, &ipMask);

    struct rte_flow_item_tcp tcpSpec = {0};
    struct rte_flow_item_tcp tcpMask = {0};
    ret = GetPattern(flowPattern, MAX_TRANS_PATTERN_NUM, flowCfg, &tcpSpec, &tcpMask);
    if (ret != 0) {
        KNET_ERR("Get tcp pattern failed, ret %d", ret);
        return ret;
    }
    flowPattern[END_PATTERN_INDEX].type = RTE_FLOW_ITEM_TYPE_END;

    struct rte_flow_error flowError = {0};
    ret = rte_flow_validate(portId, &flowAttr, flowPattern, flowAction, &flowError);
    if (ret != 0) {
        KNET_ERR("Flow validate failed, ret %d, error %s", ret, flowError.message);
        return ret;
    }

    *flow = rte_flow_create(portId, &flowAttr, flowPattern, flowAction, &flowError);
    if (*flow == NULL) {
        KNET_ERR("Failed to create flow rule, error %s", flowError.message);
        return -1;
    }

    TcpFlowCreationSuccessLog(portId, flowCfg);
    return 0;
}

int32_t KNET_GenerateArpFlow(uint16_t portId, uint32_t queueId, struct rte_flow **flow)
{
    struct rte_flow_attr flowAttr = {0};
    flowAttr.ingress = 1;

    struct rte_flow_action flowAction[MAX_ACTION_NUM] = {0};
    flowAction[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;

    struct rte_flow_action_queue flowActionQueue = {.index = queueId};
    flowAction[0].conf = &flowActionQueue;
    flowAction[1].type = RTE_FLOW_ACTION_TYPE_END;

    struct rte_flow_item flowPattern[MAX_ARP_PATTERN_NUM] = {0};
    flowPattern[ETH_PATTERN_INDEX].type = RTE_FLOW_ITEM_TYPE_ETH;

    struct rte_flow_item_eth item_eth_mask = {.type = 0xFFFF}; // full mask
    flowPattern[ETH_PATTERN_INDEX].mask = &item_eth_mask;

    struct rte_flow_item_eth item_eth_spec = {
        .dst.addr_bytes = "\xff\xff\xff\xff\xff\xff",
        .type = RTE_BE16(RTE_ETHER_TYPE_ARP)
    };
    flowPattern[ETH_PATTERN_INDEX].spec = &item_eth_spec;

    flowPattern[1].type = RTE_FLOW_ITEM_TYPE_END;

    struct rte_flow_error flowError = {0};
    int32_t ret = rte_flow_validate(portId, &flowAttr, flowPattern, flowAction, &flowError);
    if (ret != 0) {
        KNET_ERR("Flow validate failed, ret %d, error %s", ret, flowError.message);
        return ret;
    }

    *flow = rte_flow_create(portId, &flowAttr, flowPattern, flowAction, &flowError);
    if (*flow == NULL) {
        KNET_ERR("Failed to create flow rule, %s", flowError.message);
        return -1;
    }

    KNET_DEBUG("Create arp flow rule success, port %hu, queue %u", portId, queueId);
    return 0;
}

int32_t KNET_DeleteFlowRule(uint16_t portId, struct rte_flow *flow)
{
    if (flow == NULL) {
        KNET_ERR("Invalid flow pointer");
        return -1;
    }
    struct rte_flow_error flowError = {0};
    int32_t ret = rte_flow_destroy(portId, flow, &flowError);
    if (ret != 0) {
        KNET_ERR("Failed to destroy flow rule, %s", flowError.message);
        return -1;
    }
    KNET_DEBUG("Destroy flow rule success, port %hu", portId);
    return 0;
}