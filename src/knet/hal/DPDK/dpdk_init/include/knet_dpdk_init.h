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
#ifndef __KNET_DPDK_INIT_H__
#define __KNET_DPDK_INIT_H__

#include "rte_flow.h"
#include "knet_config.h"

typedef struct {
    uint16_t portId;
    uint16_t bondPortId;
    uint16_t slavePortIds[2]; // 目前支持2个网卡作为slave进行bond
    uint16_t xmitPortId; // 实际用于tx/rx burst收发的port
    char padding[6];   // 填充字节，确保结构体8 字节对齐
} KNET_DpdkNetdevCtx;

/**
 * @brief 获取DPDk设备配置信息
 *
 * @return KNET_DpdkNetdevCtx* 包含DPDK设备配置信息的结构体指针
 */
KNET_DpdkNetdevCtx *KNET_GetNetDevCtx(void);

/**
 * @brief DPDk初始化
 *
 * @param procType 进程模式
 * @param processMode 运行模式
 * @return int32_t -1: 失败, 0: 成功
 */
int32_t KNET_InitDpdk(int procType, int processMode);

/**
 * @brief DPDK反初始化，释放资源
 *
 * @param procType 进程模式
 * @param processMode 运行模式
 * @return int32_t -1: 失败, 0: 成功
 */
int32_t KNET_UninitDpdk(int procType, int processMode);

/**
 * @brief 获取DPDK延后处理队列信息
 *
  * @param cpdRingId 队列id
 * @return void* 队列指针
 */
void* KNET_GetDelayRxRing(int cpdRingId);

#endif // __KNET_dpdk_init_H__