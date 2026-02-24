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

#ifndef __INIT_STACK_H__
#define __INIT_STACK_H__

#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include "knet_lock.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CACHE_LINE 64

typedef struct {
    KNET_SpinLock lock;
    uint32_t workerId;  // 协议栈workerId
    uint32_t lcoreId;   // dpdk lcoreId
} __attribute__((__aligned__((CACHE_LINE)))) KNET_DpWorkerInfo;

/**
 * @brief Dp资源初始化
 *
 * @return int 0: 成功, -1: 失败
 */
int KNET_InitDp(void);

/**
 * @brief Dp资源析构
 *
 */
void KNET_UninitDp(void);

/**
 * @brief 获取工作线程Info
 *
 * @param dpWorkerId [IN] uint32_t。工作线程id
 * @param poolId [OUT] 参数类型uint32_t*。内存池的ID号
 * @return const KNET_DpWorkerInfo * 工作线程的Info。成功返回非空，失败返回NULL
 */
KNET_DpWorkerInfo *KNET_DpWorkerInfoGet(uint32_t dpWorkerId);

/**
 * @brief 获取最大工作线程id
 *
 * @return uint32_t 工作线程的最大id
 */
uint32_t KNET_DpMaxWorkerIdGet(void);

/**
 * @brief 设置工作线程的dpdk lcoreId与dp WorkerId的映射关系
 *
 * @param lcoreId [IN] dpdk lcoreId
 * @return int 0: 成功, -1: 失败
 */
int KNET_DpdkLcoreMatchDpWorker(uint32_t lcoreId);

/**
 * @brief free调全局tap口
 * @return int32_t 0: 成功, -1: 失败
 */
int32_t KNET_FreeTapGlobal(void);

/**
 * @brief 获取创建之后的fd
 *
 * @return int fd > 0：成功；-1：无效
 */
int KNET_GetIfIndex(void);

#ifdef __cplusplus
}
#endif
#endif