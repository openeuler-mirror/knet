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

#ifndef NETDEV_H
#define NETDEV_H

#include <stdbool.h>

#include "dp_if_api.h"
#include "dp_netdev_api.h"

#include "utils_base.h"
#include "utils_ring.h"
#include "utils_atomic.h"
#include "utils_spinlock.h"
#include "ns.h"
#include "pbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEV_TBL_SIZE 16

#define QUE_SIZE_MAX 128

#define CACHE_DEEP_SIZE_MAX 4096

typedef struct Netdev Netdev_t;

typedef struct NetdevQue {
    uint16_t       wid;
    uint16_t       queid;
    Spinlock_t     lock;
    Ring_t         cached;
    Netdev_t*      dev;
    LIST_ENTRY(NetdevQue) node;
} NetdevQue_t;

typedef struct NETDEV_IfAddr {
    struct NETDEV_IfAddr* next;

    DP_InAddr_t local;
    DP_InAddr_t broadcast;
    DP_InAddr_t mask;

    Netdev_t* dev;
} NETDEV_IfAddr_t;

typedef struct NETDEV_Inet {
    // ip addr
    NETDEV_IfAddr_t* ifAddr;
    uint8_t          ndEntry;
} NETDEV_Inet_t;

#define NETDEV_VLANID_INVALID (0xFFFF)

// 设备属性和netif集合成一个结构
struct Netdev {
    char name[DP_IF_NAMESIZE];
    // 设备属性
    uint16_t maxMtu;
    uint16_t minMtu;
    uint16_t rxQueCnt;
    uint16_t txQueCnt;
    uint16_t devType;
    uint16_t linkType; // 对应ARP定义的硬件地址类型
    uint32_t linkSpeed; // 暂时无用
    void* ctx;
    union {
        DP_EthAddr_t mac;
    } hwAddr;
    uint16_t l2hdrlen; // 2层头长度
    uint16_t vlanid;
    uint16_t vrfId;
    uint8_t  dstEntry; // 出接口的协议类型
    uint8_t  freed; // 设备已经被free了，用户可以直接访问

    struct Netdev* master; // 主设备
    struct Netdev* subdevs; // 子设备链表，这里用单向链表，遍历查找
    struct Netdev* nxtSubNode; // 子设备链表节点

    int ifindex;

    NS_Net_t* net;

    // user config属性
    uint16_t ifflags;
    uint16_t mtu;
    uint16_t linkHdrLen;

    uint16_t tsoSize;
    uint32_t offloads;
    uint32_t enabledOffloads;

    NETDEV_Inet_t in; // ipv4地址管理

    atomic32_t ref;

    NetdevQue_t* rxQues;
    NetdevQue_t* txQues;
};

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
 * @brief 通过命名获取设备指针，计算场景不使用
 *
 */
DP_Netdev_t* DP_GetNetdevByName(const char* name);

/**
 * @brief 在cfg->rxBurst为空场景下，通过此接口将报文放到dev缓存队列中，计算场景不使用
 *
 */
int DP_PutPkts(DP_Netdev_t* dev, void** bufs, int cnt);

/**
 * @brief 计算场景不使用
 *
 */
void* DP_GetDevPrivate(const char* name);

struct DP_Ifconf {
    int ifc_len;
    union {
        char*              ifcu_buf;
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
 * @brief 查询设备名，计算场景不使用
 *
 */
int DP_DumpDevStats(int ifindex, DP_DevStats_t* stats);

/**
 * @brief 查询所有已注册设备名，计算场景不使用
 *
 */
int DP_DumpAllDevStats(DP_DevStats_t* stats, int cnt);

/**
 * @brief 维测接口，不对外暴露
 *
 */
void CleanDevTasksQue(void);

// 初始化
int NETDEV_Init(int slave);
void NETDEV_Deinit(int slave);

Netdev_t* NETDEV_GetDev(NS_Net_t* net, int ifindex);
Netdev_t* NETDEV_GetDevInArray(NS_Net_t* net, int index);

static inline uint16_t NETDEV_GetTxQueWid(Netdev_t* dev, uint8_t queId)
{
    return dev->txQues[queId].wid;
}

static inline uint16_t NETDEV_GetRxQueWid(Netdev_t* dev, uint8_t queId)
{
    return dev->rxQues[queId].wid;
}

Netdev_t* NETDEV_RefDevByIfindex(NS_Net_t* net, int ifindex);
Netdev_t* NETDEV_RefDevByName(NS_Net_t* net, const char* name);
void      NETDEV_DerefDev(Netdev_t* dev);

static inline bool NETDEV_IsDevUsed(Netdev_t* dev)
{
    return dev->freed == 0u;
}

Netdev_t* NETDEV_GetDevByName(NS_Net_t* net, const char* name);

static inline int NETDEV_GetIfindexByName(NS_Net_t* net, const char* name)
{
    Netdev_t* dev = NETDEV_GetDevByName(net, name);
    if (dev == NULL) {
        return -1;
    }

    return dev->ifindex;
}

NETDEV_IfAddr_t* NETDEV_AllocIfAddr(void);

NETDEV_IfAddr_t* NETDEV_CopyIfAddr(NETDEV_IfAddr_t* addr);

void NETDEV_FreeIfAddr(NETDEV_IfAddr_t* ifAddr);

typedef union {
    int VID;
} DP_VlanIoctlArgs_t;

Netdev_t* NETDEV_FindVlanDev(Netdev_t* dev, uint16_t vlanid);

// 发送报文
void NETDEV_XmitPbuf(Pbuf_t* pbuf);

static inline NETDEV_IfAddr_t* NETDEV_GetLocalIfaddr(const Netdev_t* dev, DP_InAddr_t addr)
{
    NETDEV_IfAddr_t* ifaddr = dev->in.ifAddr;

    while (ifaddr != NULL) {
        if (ifaddr->local == addr) {
            return ifaddr;
        }
        ifaddr = ifaddr->next;
    }

    return NULL;
}

static inline NETDEV_IfAddr_t* NETDEV_GetBroadcastIfaddr(const Netdev_t* dev, DP_InAddr_t addr)
{
    NETDEV_IfAddr_t* ifaddr = dev->in.ifAddr;

    while (ifaddr != NULL) {
        if (ifaddr->broadcast == addr) {
            return ifaddr;
        }
        ifaddr = ifaddr->next;
    }

    return NULL;
}

static inline DP_InAddr_t DP_GetFirstIpv4Addr(Netdev_t* dev)
{
    return dev->in.ifAddr == NULL ? DP_INADDR_ANY : dev->in.ifAddr->local;
}

static inline const Netdev_t* NETDEV_GetRealDev(const Netdev_t* dev)
{
    const Netdev_t* real = dev;

    while (real != NULL && real->rxQueCnt == 0) {
        real = real->master;
    }

    return real;
}

static inline int16_t NETDEV_GetRxWid(const Netdev_t* dev, int queid)
{
    dev = NETDEV_GetRealDev(dev);
    ASSERT(dev != NULL);

    return (int16_t)(dev->rxQues[queid].wid); // wid上限不会超过65535，强转无风险
}

static inline int16_t NETDEV_GetTxWid(const Netdev_t* dev, int queid)
{
    dev = NETDEV_GetRealDev(dev);
    ASSERT(dev != NULL);

    return (int16_t)(dev->txQues[queid].wid); // wid上限不会超过65535，强转无风险
}

static inline uint8_t NETDEV_GetTxQueid(const Netdev_t* dev, uint8_t rxqueid)
{
    // 当前实现中rxqueid与txqueid一一对应，此处为之后不对应情况做预留
    (void)dev;
    return rxqueid;
}

#define NETDEV_RX_IPV4_CKSUM_ENABLED(dev) (((dev)->enabledOffloads & DP_NETDEV_OFFLOAD_RX_IPV4_CKSUM) != 0)

#define NETDEV_TX_IPV4_CKSUM_ENABLED(dev) (((dev)->enabledOffloads & DP_NETDEV_OFFLOAD_TX_IPV4_CKSUM) != 0)

#define NETDEV_RX_TCP_CKSUM_ENABLED(dev) (((dev)->enabledOffloads & DP_NETDEV_OFFLOAD_RX_TCP_CKSUM) != 0)

#define NETDEV_TX_TCP_CKSUM_ENABLED(dev) (((dev)->enabledOffloads & DP_NETDEV_OFFLOAD_TX_TCP_CKSUM) != 0)

#define NETDEV_RX_UDP_CKSUM_ENABLED(dev) (((dev)->enabledOffloads & DP_NETDEV_OFFLOAD_RX_UDP_CKSUM) != 0)

#define NETDEV_TX_UDP_CKSUM_ENABLED(dev) (((dev)->enabledOffloads & DP_NETDEV_OFFLOAD_TX_UDP_CKSUM) != 0)

#define NETDEV_TX_L4_CKSUM_PARTIAL(dev) (((dev)->enabledOffloads & DP_NETDEV_OFFLOAD_TX_L4_CKSUM_PARTIAL) != 0)

#define NETDEV_TSO_ENABLED(dev) (((dev)->enabledOffloads & DP_NETDEV_OFFLOAD_TSO) != 0)

typedef struct {
    int parent; // 父设备ifindex
} DP_VlanDevCfg_t;

// 维测接口，不对外暴露
void DevTask(int wid);


#ifdef __cplusplus
}
#endif
#endif
