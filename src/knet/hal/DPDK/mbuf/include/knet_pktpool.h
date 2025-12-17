/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 报文相关操作
 */

#ifndef __KNET_PKT_POOL_H__
#define __KNET_PKT_POOL_H__

#include <stdint.h>
#include <pthread.h>
#include "rte_mempool.h"
#include "knet_pkt.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @ingroup knet_pkt
 * @brief 报文池可创建的最大数目
 */
#define KNET_PKTPOOL_POOL_MAX_NUM 32

/**
 * @ingroup knet_pkt
 * @brief 报文cache规格
 */
#define KNET_PKTPOOL_CACHE_MAX_NUM  256    /**< Cache最大数量为256 */
#define KNET_PKTPOOL_CACHE_MAX_SIZE 512    /**< Cache size最大512 */

/**
 * @ingroup knet_pkt
 * @brief 报文池数据区规格
 */
#define KNET_PKTPOOL_DATAROOM_MIN_SIZE 256
#define KNET_PKT_POOL_DEFAULT_DATAROOM_SIZE 2048

#define CACHE_FLUSHTHRESH_SIZE_MULTIPLIER 1.5

#define KNET_PKT_POOL_NAME "MBUF"

#define MBUF_ATTACHED (3ULL << 61)

/**< 报文池控制块 */
typedef struct {
    struct rte_mempool *mempool;          /**< 报文池地址 */
    char poolName[KNET_PKTPOOL_NAME_LEN];  /**< 报文池名字 */
} KnetPktPoolCtrl;

/**
 * @brief 获取报文池
 *
 * @param poolId 参数类型 uint32_t，报文池ID
 * @return struct rte_mempool* 报文池指针
 */
struct rte_mempool *KnetPktGetMemPool(uint32_t poolId);

/**
 * @brief 获取报文池控制块
 *
 * @param poolId 参数类型 uint32_t，报文池ID
 * @return KnetPktPoolCtrl* 报文池控制块指针
 */
KnetPktPoolCtrl *KnetPktGetPoolCtrl(uint32_t poolId);

#ifdef __cplusplus
}
#endif /* __cpluscplus */

#endif /* __KNET_PKT_POOL_H__ */