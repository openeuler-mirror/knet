/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 网络设备管理对外接口
 */

#ifndef DP_NETDEV_API_H
#define DP_NETDEV_API_H

#include "dp_socket_types_api.h"
#include "dp_ioctl_defs_api.h"
#include "dp_if_api.h"
#include "dp_pbuf_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup netdev 网络设备
 * @ingroup netdev
 */

#define DP_IF_NAME_SIZE 16

#define DP_NETDEV_OFFLOAD_RX_IPV4_CKSUM 0x1
#define DP_NETDEV_OFFLOAD_TX_IPV4_CKSUM 0x2

#define DP_NETDEV_OFFLOAD_RX_TCP_CKSUM 0x4
#define DP_NETDEV_OFFLOAD_TX_TCP_CKSUM 0x8

#define DP_NETDEV_OFFLOAD_RX_UDP_CKSUM 0x10
#define DP_NETDEV_OFFLOAD_TX_UDP_CKSUM 0x20

#define DP_NETDEV_OFFLOAD_TX_L4_CKSUM_PARTIAL 0x40

#define DP_NETDEV_OFFLOAD_TSO 0x80

#define DP_NETDEV_OFFLOAD_LRO 0x100

/**
 * @ingroup netdev
 * 网络设备各类型
 */
typedef enum {
    DP_NETDEV_TYPE_LO, // LO Back设备
    DP_NETDEV_TYPE_ETH, // 以太设备
    DP_NETDEV_TYPE_ETHVLAN, // VLAN设备
    DP_NETDEV_TYPE_BUTT,
} DP_NetdevType_t;

/**
 * @ingroup netdev
 * 网络设备操作接口
 */
typedef struct {
    int type; /**< 固定写arp中定义的设备类型，例如 ARPHRD_ETHER，参考 linux的if_arp.h定义值，需要填写 */
    int (*ctrl)(void* ctx, int opt, void* arg, uint32_t argLen);  /**< 预留，调整设备参数使用，当前不需注册 */
    int (*rxHash)(void* ctx, uint8_t* pkt, size_t len); /**< 预留，多队列使用，当前不需注册 */
    int (*txHash)(void* ctx, uint8_t* pkt, size_t len); /**< 预留，多队列使用，当前不需注册 */
    int (*rxBurst)(void* ctx, uint16_t queId, void** buf, int cnt); /**< 报文接收接口，需要注册 */
    int (*txBurst)(void* ctx, uint16_t queId, void** buf, int cnt); /**< 报文发送接口，需要注册 */
} DP_NetdevOps_t;

/**
 * @ingroup netdev
 * 网络设备配置项
 */
typedef struct {
    int  ifindex;
    char ifname[DP_IF_NAMESIZE];

    DP_NetdevType_t devType;

    uint16_t rxQueCnt;     /**< 网卡读队列数量，约束：需要和txQueCnt一致 */
    uint16_t txQueCnt;     /**< 网卡写队列数量，约束：需要和rxQueCnt一致 */

    uint16_t txCachedDeep; // tx缓存队列，UDP/RAW等报文会缓存在这里，因此必须>0
    uint16_t rxCachedDeep; // rx缓存，用户可以使用DP_PutPkts接口直接放到缓存队列

    uint32_t offloads;   // 设备支持的 offloads 能力
    uint16_t tsoSize;    // 设备 TSO 报文长度

    void* ctx;
    const DP_NetdevOps_t* ops;
} DP_NetdevCfg_t;

/**
 * @ingroup netdev
 * netdev类型
 */
typedef struct Netdev DP_Netdev_t;

/**
 * @ingroup netdev
 * @brief 分配并初始化 netdev
 *
 * @attention
 * 必须在调用DP_CpdInit之前调用
 *
 * @param cfg
 * @retval 返回设备指针 成功
 * @retval NULL 失败
 */
DP_Netdev_t* DP_CreateNetdev(DP_NetdevCfg_t* cfg);

/**
 * @ingroup netdev
 * @brief 处理通过ifreq类型的request
 *
 * @attention
 * SetIfFlags前需要先GetIfFlags
 *
 * @param ifreq
 * @param request
 * @retval 0 成功
 * @retval 错误码 失败
 */
int DP_ProcIfreq(DP_Netdev_t* dev, int request, struct DP_Ifreq* ifreq);

#ifdef __cplusplus
}
#endif
#endif
