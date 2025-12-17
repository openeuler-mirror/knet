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
/**
 * @file dp_if_api.h
 * @brief 网络接口信息相关头文件
 */

#ifndef DP_IF_API_H
#define DP_IF_API_H

#include "dp_socket_types_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup if Interface API
 * @ingroup netdev
 */

#define DP_IF_NAMESIZE 16
#define DP_IP_DFT_MTU  536

/**
 * @ingroup if
 * if map信息
 */
struct DP_Ifmap {
    unsigned long  mem_start;
    unsigned long  mem_end;
    unsigned short base_addr;
    unsigned char  irq;
    unsigned char  dma;
    unsigned char  port;
};

/**
 * @ingroup if
 * if request信息
 */
struct DP_Ifreq {
    union {
        char ifrn_name[DP_IF_NAMESIZE]; /* Interface name */
    } ifr_ifrn;

    union {
        struct DP_Sockaddr ifru_addr;
        struct DP_Sockaddr ifru_dstaddr;
        struct DP_Sockaddr ifru_broadaddr;
        struct DP_Sockaddr ifru_netmask;
        struct DP_Sockaddr ifru_hwaddr;
        short              ifru_flags;
        int                ifru_ivalue;
        int                ifru_mtu;
        struct DP_Ifmap    ifru_map;
        char               ifru_slave[DP_IF_NAMESIZE];
        char               ifru_newname[DP_IF_NAMESIZE];
        char*              ifru_data;
    } ifr_ifru;
};

#ifndef ifr_name // 兼容net/if.h
#define ifr_name      ifr_ifrn.ifrn_name      /**< interface name       */
#define ifr_hwaddr    ifr_ifru.ifru_hwaddr    /**< MAC address          */
#define ifr_addr      ifr_ifru.ifru_addr      /**< address              */
#define ifr_dstaddr   ifr_ifru.ifru_dstaddr   /**< other end of p-p lnk */
#define ifr_broadaddr ifr_ifru.ifru_broadaddr /**< broadcast address    */
#define ifr_netmask   ifr_ifru.ifru_netmask   /**< interface net mask   */
#define ifr_flags     ifr_ifru.ifru_flags     /**< flags                */
#define ifr_metric    ifr_ifru.ifru_ivalue    /**< metric               */
#define ifr_mtu       ifr_ifru.ifru_ivalue    /**< mtu                  */
#define ifr_map       ifr_ifru.ifru_map       /**< device map           */
#define ifr_slave     ifr_ifru.ifru_slave     /**< slave device         */
#define ifr_data      ifr_ifru.ifru_data      /**< for use by interface */
#define ifr_ifindex   ifr_ifru.ifru_ivalue    /**< interface index      */
#define ifr_bandwidth ifr_ifru.ifru_ivalue    /**< link bandwidth       */
#define ifr_qlen      ifr_ifru.ifru_ivalue    /**< queue length         */
#define ifr_newname   ifr_ifru.ifru_newname   /**< New name             */
#endif

/**
 * @ingroup if
 * if flag信息
 */
enum {
    DP_IFF_UP          = 0x1,   /**< Interface is up.  */
    DP_IFF_BROADCAST   = 0x2,   /**< Broadcast address valid.  */
    DP_IFF_DEBUG       = 0x4,   /**< Turn on debugging.  */
    DP_IFF_LOOPBACK    = 0x8,   /**< Is a loopback net.  */
    DP_IFF_POINTOPOINT = 0x10,  /**< Interface is point-to-point link.  */
    DP_IFF_NOTRAILERS  = 0x20,  /**< Avoid use of trailers.  */
    DP_IFF_RUNNING     = 0x40,  /**< Resources allocated.  */
    DP_IFF_NOARP       = 0x80,  /**< No address resolution protocol.  */
    DP_IFF_PROMISC     = 0x100, /**< Receive all packets.  */

    /**< Not supported */
    DP_IFF_ALLMULTI  = 0x200,  /**< Receive all multicast packets.  */
    DP_IFF_MASTER    = 0x400,  /**< Master of a load balancer.  */
    DP_IFF_SLAVE     = 0x800,  /**< Slave of a load balancer.  */
    DP_IFF_MULTICAST = 0x1000, /**< Supports multicast.  */
    DP_IFF_PORTSEL   = 0x2000, /**< Can set media type.  */
    DP_IFF_AUTOMEDIA = 0x4000, /**< Auto media select active.  */
    DP_IFF_DYNAMIC   = 0x8000  /**< Dialup device with changing addresses.  */
};

#ifdef __cplusplus
}
#endif
#endif
