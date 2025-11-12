/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 全局规格配置对外接口
 */

#ifndef DP_CFG_API_H
#define DP_CFG_API_H

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup cfg config配置 */

#ifndef INT_MAX
#define INT_MAX (0x7FFFFFFF)
#endif

#define DP_LOWLIMIT_MBUF_MAX      (1024)
#define DP_HIGHLIMIT_MBUF_MAX     INT_MAX
#define DP_DEFAULT_MBUF_MAX       (8192)

#define DP_LOWLIMIT_WORKER_MAX    (1)
#define DP_HIGHLIMIT_WORKER_MAX   (64)
#define DP_DEFAULT_WORKER_MAX     (32)

#define DP_LOWLIMIT_RT_MAX        (1)
#define DP_HIGHLIMIT_RT_MAX       (100000)
#define DP_DEFAULT_RT_MAX         (1024)

#define DP_LOWLIMIT_ARP_MAX       (1)
#define DP_HIGHLIMIT_ARP_MAX      (8192)
#define DP_DEFAULT_ARP_MAX        (1024)

#define DP_LOWLIMIT_TCPCB_MAX     (32)
#define DP_HIGHLIMIT_TCPCB_MAX    (26000000)
#define DP_DEFAULT_TCPCB_MAX      DP_LOWLIMIT_TCPCB_MAX

#define DP_LOWLIMIT_UDPCB_MAX     (32)
#define DP_HIGHLIMIT_UDPCB_MAX    (26000000)
#define DP_DEFAULT_UDPCB_MAX      DP_LOWLIMIT_UDPCB_MAX

/**
 * @ingroup cfg
 * 全局规格配置
 */
typedef enum {
    DP_CFG_MBUF_MAX,
    DP_CFG_WORKER_MAX,
    DP_CFG_RT_MAX,
    DP_CFG_ARP_MAX,
    DP_CFG_TCPCB_MAX,
    DP_CFG_UDPCB_MAX,
    DP_CFG_CPD_PKT_TRANS,

    DP_CFG_MAX,
} DP_CfgKey_t;


#define DP_ENABLE  (1)
#define DP_DISABLE (0)
#define DP_TCP_LOWLIMIT_MSL_TIME     (1)
#define DP_TCP_HIGHLIMIT_MSL_TIME    (30)
#define DP_TCP_DEFAULT_MSL_TIME      DP_TCP_HIGHLIMIT_MSL_TIME

#define DP_TCP_LOWLIMIT_FIN_TIMEOUT  (1)
#define DP_TCP_HIGHLIMIT_FIN_TIMEOUT (600)
#define DP_TCP_DEFAULT_FIN_TIMEOUT   DP_TCP_HIGHLIMIT_FIN_TIMEOUT

#define DP_TCP_LOWLIMIT_PORT_MIN     (1)
#define DP_TCP_HIGHLIMIT_PORT_MIN    (49152)
#define DP_TCP_DEFAULT_PORT_MIN      DP_TCP_HIGHLIMIT_PORT_MIN

#define DP_TCP_LOWLIMIT_PORT_MAX     (50000)
#define DP_TCP_HIGHLIMIT_PORT_MAX    (65535)
#define DP_TCP_DEFAULT_PORT_MAX      DP_TCP_HIGHLIMIT_PORT_MAX

#define DP_TCP_LOWLIMIT_RMEM_MAX     (8192)
#define DP_TCP_HIGHLIMIT_RMEM_MAX    INT_MAX
#define DP_TCP_DEFAULT_RMEM          DP_TCP_LOWLIMIT_RMEM_MAX
#define DP_TCP_DEFAULT_RMEM_MAX      (1024 * 10240)

#define DP_TCP_LOWLIMIT_WMEM_MAX     (8192)
#define DP_TCP_HIGHLIMIT_WMEM_MAX    INT_MAX
#define DP_TCP_DEFAULT_WMEM          DP_TCP_LOWLIMIT_WMEM_MAX
#define DP_TCP_DEFAULT_WMEM_MAX      (1024 * 10240)

#define DP_CFG_TCP_MIN_PORT DP_CFG_TCP_RND_PORT_MIN
#define DP_CFG_TCP_MAX_PORT DP_CFG_TCP_RND_PORT_MAX

/**
 * @ingroup cfg
 * TCP特性控制
 */
typedef enum {
    DP_CFG_TCP_SELECT_ACK,
    DP_CFG_TCP_DELAY_ACK,
    DP_CFG_TCP_MSL_TIME,
    DP_CFG_TCP_FIN_TIMEOUT,
    DP_CFG_TCP_COOKIE,
    DP_CFG_TCP_RND_PORT_MIN,
    DP_CFG_TCP_RND_PORT_MAX,
    DP_CFG_TCP_WMEM_MAX,
    DP_CFG_TCP_WMEM_DEFAULT,
    DP_CFG_TCP_RMEM_MAX,
    DP_CFG_TCP_RMEM_DEFAULT,

    DP_CFG_TCP_MAX,
} DP_CfgTcpKey_t;

#define DP_IP_LOWLIMIT_REASS_MAX    (1)
#define DP_IP_HIGHLIMIT_REASS_MAX   (4096)
#define DP_IP_DEFAULT_REASS_MAX     (1000)

#define DP_IP_LOWLIMIT_REASS_TIMEO  (1)
#define DP_IP_HIGHLIMIT_REASS_TIMEO (30)
#define DP_IP_DEFAULT_REASS_TIMEO   DP_IP_HIGHLIMIT_REASS_TIMEO

/**
 * @ingroup cfg
 * IP特性控制
 */
typedef enum {
    DP_CFG_IP_REASS_MAX,
    DP_CFG_IP_REASS_TIMEOUT,

    DP_CFG_IP_MAX,
} DP_CfgIpKey_t;

/**
 * @ingroup cfg
 * ETH特性控制
 */
typedef enum {
    DP_CFG_ETH_MAX,
} DP_CfgEthKey_t;

/**
 * @ingroup cfg
 * 分层配置类型
 */
typedef enum {
    DP_CFG_TYPE_SYS,
    DP_CFG_TYPE_TCP,
    DP_CFG_TYPE_IP,
    DP_CFG_TYPE_ETH,
    DP_CFG_TYPE_MAX,
} DP_CfgType_t;

/**
 * @ingroup cfg
 * 配置信息
 */
typedef struct {
    DP_CfgType_t   type;
    int            key;
    int            val;
} DP_CfgKv_t;

/**
 * @ingroup cfg
 * @brief 规格参数配置
 * @attention 非线程安全，建议在DP_Init之前调用
 *
 * @param DP_CfgKv_t 下发的配置
 * @param cnt 配置的个数
 * @return 0 成功， -1 异常

 */
int DP_Cfg(DP_CfgKv_t* kv, int cnt);

/**
 * @ingroup cfg
 * @brief 规格参数获取
 * @attention 非线程安全
 *
 * @param DP_CfgKv_t 需要获取的配置
 * @param cnt 获取的配置个数
 * @return 0 成功， -1 异常

 */
int DP_CfgGet(DP_CfgKv_t* kv, int cnt);

#ifdef __cplusplus
}
#endif
#endif
