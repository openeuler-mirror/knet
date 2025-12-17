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
#ifndef DP_NETDEV_H
#define DP_NETDEV_H

#include "dp_netdev_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 分配 dev 设备对象，如果 cfg->ifindex = -1 ，会自动分配 ifindex
 *
 */
DP_Netdev_t* DP_AllocNetdev(DP_NetdevCfg_t* cfg);

/**
 * @brief 释放 dev 设备对象内存，如果调用 DP_InitNetdev 成功
 *
 */
int DP_FreeNetdev(DP_Netdev_t* dev);

/**
 * @brief 初始化 netdev
 */
int DP_InitNetdev(DP_Netdev_t* dev, DP_NetdevCfg_t* cfg);

/**
 * @brief 通过接口index获取设备指针，计算场景不使用
 *
 */
DP_Netdev_t* DP_GetNetdev(int ifindex);

/**
 * @brief 通过devtbl的索引获取设备指针，CPD模块使用，计算场景不使用
 *
 */
DP_Netdev_t* DP_GetNetdevByIndex(int index);

/**
 * @brief 通过devtbl的索引无锁获取设备指针，CPD模块使用，计算场景不使用
 *
 */
DP_Netdev_t* DP_GetNetdevByIndexLockFree(int index);

/**
 * @brief 通过命名获取设备指针，计算场景不使用
 *
 */
DP_Netdev_t* DP_GetNetdevByName(const char* name);

/**
 * @brief 在cfg->rxBurst为空场景下，通过此接口将报文放到dev缓存队列中，计算场景不使用
 *
 */
int DP_PutPkts(DP_Netdev_t* dev, uint16_t queId, void** bufs, int cnt);

/**
 * @brief 计算场景不使用
 *
 */
void* DP_GetDevPrivate(const char* name);

struct DP_Ifconf {
    int ifc_len;
    union {
        char*            ifcu_buf;
        struct DP_Ifreq* ifcu_req;
    } ifc_ifcu;
};

#ifndef ifc_buf
#define ifc_buf ifc_ifcu.ifcu_buf /* Buffer address.  */
#define ifc_req ifc_ifcu.ifcu_req /* Array of structures.  */
#endif

/**
 * @brief 计算场景不使用
 *
 */
int DP_GetIfconf(struct DP_Ifconf* ifconf);

typedef struct {
    char ifname[DP_IF_NAMESIZE];
    struct {
        uint64_t bytes;
        uint64_t packets;
        uint64_t errs;
        uint64_t drop;
        uint64_t fifo;
        uint64_t frame;
        uint64_t compressed;
        uint64_t multicast;
    } rxStats;
    struct {
        uint64_t bytes;
        uint64_t packets;
        uint64_t errs;
        uint64_t drop;
        uint64_t fifo;
        uint64_t colls;
        uint64_t carrier;
        uint64_t compressed;
    } txStats;
} DP_DevStats_t;

/**
 * @brief     获取netdev的统计信息
 * @attention 适用于OS栈场景

 * @param  ifIndex [IN] 网络设备索引
 * @param  stats [IN] 获取返回的设备信息
 * @retval 返回0 成功
 * @retval 返回-1 失败
 */
int DP_NetdevGetStats(int ifIndex, DP_DevStats_t* stats);

/**
 * @brief     清零netdev的统计信息
 * @attention 适用于OS栈场景

 * @param  ifIndex [IN] 网络设备索引
 * @retval 返回0 成功
 * @retval 返回-1 失败
 */
int DP_NetdevCleanStats(int ifIndex);

/**
 * @brief 查询设备名，计算场景不使用
 *
 */
int DP_DumpDevStats(int ifindex, DP_DevStats_t* stats);

/**
 * @brief 查询所有已注册设备名，计算场景不使用
 *
 */
int DP_DumpAllDevStats(DP_DevStats_t* stats, int cnt);

typedef union {
    int VID;
} DP_VlanIoctlArgs_t;

#ifdef __cplusplus
}
#endif
#endif
