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
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <netinet/in.h>

#include "rte_ethdev.h"
#include "rte_eth_bond.h"

#include "knet_config.h"
#include "knet_bond.h"
#include "securec.h"
#include "common.h"
#include "mock.h"

#define SLAVE_NUM 2

DTEST_CASE_F(BOND, TEST_KNET_BOND_CREATE_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_eth_bond_create, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_eth_bond_slave_add, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_eth_promiscuous_enable, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_eth_bond_xmit_policy_set, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_eth_bond_mac_address_set, TEST_GetFuncRetPositive(0));

    uint16_t slavePortIds[2] = {0, 1};
    int ret = KNET_BondCreate(slavePortIds, SLAVE_NUM);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Create(rte_eth_bond_slave_add, TEST_GetFuncRetPositive(1));
    ret = KNET_BondCreate(slavePortIds, SLAVE_NUM);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Create(rte_eth_bond_xmit_policy_set, TEST_GetFuncRetPositive(1));
    Mock->Create(rte_eth_bond_slave_add, TEST_GetFuncRetPositive(0));
    ret = KNET_BondCreate(slavePortIds, SLAVE_NUM);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(rte_eth_bond_create);
    Mock->Delete(rte_eth_bond_slave_add);
    Mock->Delete(rte_eth_promiscuous_enable);
    Mock->Delete(rte_eth_bond_xmit_policy_set);
    Mock->Delete(rte_eth_bond_mac_address_set);
    DeleteMock(Mock);
}

static int FuncRetXmitPolicyL34(uint16_t bondPortId)
{
    return BALANCE_XMIT_POLICY_LAYER34;
}

static int FuncRetXmitPolicyL2(uint16_t bondPortId)
{
    return BALANCE_XMIT_POLICY_LAYER2;
}

static int FuncRetXmitPolicyL23(uint16_t bondPortId)
{
    return BALANCE_XMIT_POLICY_LAYER23;
}

static int FuncRetMode4(uint16_t bondPortId)
{
    return BONDING_MODE_8023AD;
}

static int FuncRet2(uint16_t bondPortId)
{
    return SLAVE_NUM;
}

DTEST_CASE_F(BOND, TEST_KNET_BOND_WAIT_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_eth_bond_active_slaves_get, FuncRet2);
    Mock->Create(rte_eth_bond_mode_get, FuncRetMode4);
    Mock->Create(rte_eth_bond_xmit_policy_get, FuncRetXmitPolicyL34);

    int ret = KNET_BondWaitSlavesReady(0);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Create(rte_eth_bond_xmit_policy_get, FuncRetXmitPolicyL23);
    ret = KNET_BondWaitSlavesReady(0);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Create(rte_eth_bond_xmit_policy_get, FuncRetXmitPolicyL2);
    ret = KNET_BondWaitSlavesReady(0);
    DT_ASSERT_EQUAL(ret, 0);
    
    Mock->Create(rte_eth_bond_xmit_policy_get, TEST_GetFuncRetNegative(1));
    ret = KNET_BondWaitSlavesReady(0);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Delete(rte_eth_bond_active_slaves_get);
    Mock->Delete(rte_eth_bond_mode_get);
    Mock->Delete(rte_eth_bond_xmit_policy_get);
    DeleteMock(Mock);
}

DTEST_CASE_F(BOND, TEST_KNET_BOND_CHECK_MAC_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_eth_macaddr_get, TEST_GetFuncRetPositive(0));
    Mock->Create(memcmp, TEST_GetFuncRetPositive(0));

    int ret = KNET_BondPortMacCheck();
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(rte_eth_macaddr_get);
    Mock->Delete(memcmp);
    DeleteMock(Mock);
}

DTEST_CASE_F(BOND, TEST_KNET_BOND_UNINIT_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_eth_dev_stop, TEST_GetFuncRetPositive(0));

    int ret = KNET_BondUninit(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(rte_eth_dev_stop);
    DeleteMock(Mock);
}

DTEST_CASE_F(BOND, TEST_KNET_BOND_SEND_LACP_NORMAL, NULL, NULL)
{
    (void)KNET_BondSendLacpPkt();
}