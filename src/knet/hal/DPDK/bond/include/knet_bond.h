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

#ifndef __KNET_BOND_H__
#define __KNET_BOND_H__

#define KNET_BOND_SLAVE_NUM 2

#define KNET_SECONDARY_BOND_PORT_ID 2 // todo: 从进程bond port id打桩为2，后续需要通过进程间通信从主进程获取

/**
 * @brief 创建dpdk bond端口
 * @param slavePortIds 存放slave端口id的数组
 * @param slavePortNum slave端口数
 * @return int 小于0表示失败，否则返回bond端口id
 */
int KNET_BondCreate(uint16_t *slavePortIds, uint16_t slavePortNum);

/**
 * @brief 等待slave端口准备就绪并将bond端口信息写入日志
 * @param bondPortID bond端口id
 * @return int 0为成功,-1为失败
 */
int KNET_BondWaitSlavesReady(int bondPortID);

/**
 * @brief 给控制面线程调用发送lacp报文，确保满足dpdk mode4要求
 */
void KNET_BondSendLacpPkt(void);

/**
 * @brief 检查bond端口配置的mac地址是否为其中一个从端口原先的mac
 * @return int 0为成功，-1为失败
 */
int KNET_BondPortMacCheck(void);

/**
 * @brief 销毁dpdk bond端口
 * @param procType 进程类型
 * @return int 0为成功，1为失败
 */
int KNET_BondUninit(int procType);

#endif  // __KNET_BOND_H__