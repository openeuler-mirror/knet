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
#include "rte_eth_bond.h"
#include "rte_version.h"
#include "knet_log.h"
#include "knet_config.h"
#include "knet_dpdk_init.h"
#include "knet_bond.h"

static int g_slavePortIds[KNET_BOND_SLAVE_NUM] = { 0 };

KNET_STATIC int AdapterBondAdd(uint16_t bondedPortId, uint16_t slavePortId)
{
    int ret = 0;
    #if RTE_VERSION >= RTE_VERSION_NUM(23,11,0,0)
        ret = rte_eth_bond_member_add(bondedPortId, slavePortId);
    #else
        ret = rte_eth_bond_slave_add(bondedPortId, slavePortId);
    #endif
    return ret;
}

KNET_STATIC int AdapterBondSlavesGet(uint16_t bondPortId, uint16_t slaves[], uint16_t len)
{
    int ret = 0;
    #if RTE_VERSION >= RTE_VERSION_NUM(23,11,0,0)
        ret = rte_eth_bond_active_members_get(bondPortId, slaves, len);
    #else
        ret = rte_eth_bond_active_slaves_get(bondPortId, slaves, len);
    #endif
    return ret;
}

KNET_STATIC int BondXmitPolicySet(int bondPortID, int bondMode)
{
    if (bondMode == BONDING_MODE_BALANCE || bondMode == BONDING_MODE_8023AD) {
        int ret =
            rte_eth_bond_xmit_policy_set(bondPortID, BALANCE_XMIT_POLICY_LAYER34);  // 目前默认用户只使用ip+port散列
        if (ret != 0) {
            KNET_ERR("BondPort %d xmit policy set failed, ret %d, bondMode %d, maybe invalid bond port id", bondPortID,
                     ret, bondMode);
            return -1;
        }
    }

    return 0;
}

KNET_STATIC int AddSlavesToBond(int bondPortID, uint16_t slavePortIds[], uint16_t slavePortNum)
{
    int ret = 0;
    for (uint16_t i = 0; i < slavePortNum && i < KNET_BOND_SLAVE_NUM; i++) {
        ret = AdapterBondAdd(bondPortID, slavePortIds[i]);
        if (ret != 0) {
            KNET_ERR("Bond slave %d add failed, ret %d", i, ret);
            return -1;
        }
        g_slavePortIds[i] = slavePortIds[i];
    }

    return 0;
}

int KNET_BondCreate(uint16_t* slavePortIds, uint16_t slavePortNum)
{
    int bondMode = KNET_GetCfg(CONF_INTERFACE_BOND_MODE)->intValue;
    /* 第三个参数为numa节点id，0表示any */
    int bondPortID = rte_eth_bond_create("net_bonding0", bondMode, 0);
    if (bondPortID < 0) {
        KNET_ERR("Create dpdk bond port failed, ret %d", bondPortID);
        return -1;
    }

    int ret = AddSlavesToBond(bondPortID, slavePortIds, slavePortNum);
    if (ret != 0) {
        return -1; // 详细日志已在AddSlavesToBond中打印
    }

    ret = rte_eth_promiscuous_enable(bondPortID);
    if (ret != 0) {
        KNET_ERR("Bond port promiscuous enable failed, ret %d", ret);
        return -1;
    }

    ret = BondXmitPolicySet(bondPortID, bondMode);
    if (ret != 0) {
        KNET_ERR("Bond port xmit policy set failed, ret %d", ret);
        return -1;
    }

    uint8_t* macAddr = (uint8_t*)KNET_GetCfg(CONF_INTERFACE_MAC)->strValue;
    struct rte_ether_addr bondMac = {0};
    (void)memcpy_s(bondMac.addr_bytes, RTE_ETHER_ADDR_LEN, macAddr, RTE_ETHER_ADDR_LEN);
    ret = rte_eth_bond_mac_address_set(bondPortID, &bondMac);
    if (ret != 0) {
        KNET_ERR("Bond port mac set failed, ret %d", ret);
        return -1;
    }

    return bondPortID;
}

KNET_STATIC int GetBondXmitPolicy(int bondPortID)
{
    int xmitPolicy = rte_eth_bond_xmit_policy_get(bondPortID);
    if (xmitPolicy < 0) {
        KNET_ERR("Get balance xmit policy for bondPortId %d failed, ret %d", bondPortID, xmitPolicy);
        return -1;
    }
    switch (xmitPolicy) {
        case BALANCE_XMIT_POLICY_LAYER2:
            KNET_INFO("Xmit Policy: BALANCE_XMIT_POLICY_LAYER2");
            break;
        case BALANCE_XMIT_POLICY_LAYER23:
            KNET_INFO("Xmit Policy: BALANCE_XMIT_POLICY_LAYER23");
            break;
        case BALANCE_XMIT_POLICY_LAYER34:
            KNET_INFO("Xmit Policy: BALANCE_XMIT_POLICY_LAYER34");
            break;
        default: {
            KNET_INFO("Other Xmit Policy %d", xmitPolicy);
            break;
        }
    }
    return 0;
}

KNET_STATIC int BondInfoPrint(int bondPortID)
{
    int bondMode = rte_eth_bond_mode_get(bondPortID);
    if (bondMode < 0) {
        KNET_ERR("Failed to get bondPort %d mode, ret %d", bondPortID, bondMode);
        return -1;
    }
    KNET_INFO("Bond mode %d", bondMode);

    if (bondMode == BONDING_MODE_BALANCE || bondMode == BONDING_MODE_8023AD) {
        if (GetBondXmitPolicy(bondPortID) != 0) {
            return -1; // 日志已在GetXmitPolicy中打印
        }
    }

    return 0;
}

int KNET_BondWaitSlavesReady(int bondPortID)
{
    uint32_t waitCounter = 20;  // 等待从端口活跃最长20秒，超时视为失败
    while (waitCounter > 0) {
        uint16_t activeSlaves[KNET_BOND_SLAVE_NUM] = {0};
        if (AdapterBondSlavesGet(bondPortID, activeSlaves, KNET_BOND_SLAVE_NUM) == KNET_BOND_SLAVE_NUM) {
            break;
        }
        sleep(1);  // 睡眠等待 1s

        KNET_INFO("Bond port wait slaves ready, seconds %u left ...", waitCounter);
        if (--waitCounter == 0) {
            KNET_ERR("BondPort %d wait slaves activate failed", bondPortID);
            return -1;
        }
    }

    int ret = BondInfoPrint(bondPortID);
    if (ret != 0) {
        KNET_ERR("Print bond info failed");
        return -1;
    }
    return 0;
}

void KNET_BondSendLacpPkt(void)
{
    bool bondEnable = (KNET_GetCfg(CONF_INTERFACE_BOND_ENABLE)->intValue == 1);
    if (!bondEnable) {
        return;
    }
    rte_eth_tx_burst(KNET_GetNetDevCtx()->bondPortId, 0, NULL, 0);
}

int KNET_BondPortMacCheck(void)
{
    struct rte_ether_addr addrBondPort = {0};
    int ret = rte_eth_macaddr_get(KNET_GetNetDevCtx()->bondPortId, &addrBondPort);
    if (ret != 0) {
        KNET_ERR("Get bond addr failed, ret %d", ret);
        return -1;
    }

    struct rte_ether_addr addrSlavePort1 = {0};
    ret = rte_eth_macaddr_get(g_slavePortIds[0], &addrSlavePort1);
    if (ret != 0) {
        KNET_ERR("Get slavePort1 addr failed, ret %d", ret);
        return -1;
    }

    struct rte_ether_addr addrSlavePort2 = {0};
    ret = rte_eth_macaddr_get(g_slavePortIds[1], &addrSlavePort2);
    if (ret != 0) {
        KNET_ERR("Get slavePort2 addr failed, ret %d", ret);
        return -1;
    }

    if (memcmp(&addrBondPort, &addrSlavePort1, sizeof(struct rte_ether_addr)) != 0 &&
        memcmp(&addrBondPort, &addrSlavePort2, sizeof(struct rte_ether_addr)) != 0) {
        KNET_ERR("K-NET check bond mac error, mac should be set to the mac of one of the slave ports");
        return -1;
    }
    return 0;
}

int KNET_BondUninit(int procType)
{
    if (procType == KNET_PROC_TYPE_SECONDARY) {
        return 0;
    }

    int32_t ret = 0;
    int32_t flag = 0;

    for (uint16_t i = 0; i < KNET_BOND_SLAVE_NUM; i++) {
        ret = rte_eth_dev_stop(g_slavePortIds[i]);
        if (ret != 0) {
            KNET_ERR("K-NET uninit dpdk port %d failed, ret %d", g_slavePortIds[i], ret);
            flag = 1;
        }
    }

    ret = rte_eth_dev_stop(KNET_GetNetDevCtx()->bondPortId);
    if (ret != 0) {
        KNET_ERR("K-NET stop bondPort %d failed, ret %d", KNET_GetNetDevCtx()->bondPortId, ret);
        flag = 1;
    }

    return flag;
}