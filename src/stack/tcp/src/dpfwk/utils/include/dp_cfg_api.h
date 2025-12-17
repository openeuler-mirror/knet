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
 * @file dp_cfg_api.h
 * @brief 全局规格配置对外接口
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

#define DP_LOWLIMIT_EPOLL_MAX     (1)
#define DP_HIGHLIMIT_EPOLL_MAX    (52000000)
#define DP_DEFAULT_EPOLL_MAX      (64)

#define DP_LOWLIMIT_ZIOV_LEN_MAX  (0)
#define DP_HIGHLIMIT_ZIOV_LEN_MAX (512 * 1024)
#define DP_DEFAULT_ZIOV_LEN_MAX   (65535)

#define DP_DEPLOYMENT_DEFAULT     (0)
#define DP_DEPLOYMENT_CO_THREAD   (1)

#define DP_LOWLIMIT_CPD_VCPU_NUM    (1)
#define DP_HIGHLIMIT_CPD_VCPU_NUM   (8)
#define DP_DEFAULT_CPD_VCPU_NUM     (1)

#define DP_LOWLIMIT_CPD_RING_PER_CPU_NUM    (1)
#define DP_HIGHLIMIT_CPD_RING_PER_CPU_NUM   (8)
#define DP_DEFAULT_CPD_RING_PER_CPU_NUM     (1)

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
    DP_CFG_EPOLLCB_MAX,
    DP_CFG_CPD_PKT_TRANS,
    DP_CFG_ZERO_COPY,
    DP_CFG_ZBUF_LEN_MAX,
    DP_CFG_DEPLOYMENT,
    DP_CFG_CPD_VCPU_NUM,
    DP_CFG_CPD_RING_PER_CPU_NUM,

    DP_CFG_MAX,
} DP_CfgKey_t;


#define DP_ENABLE  (1)
#define DP_DISABLE (0)
#define DP_TCP_LOWLIMIT_MSL_TIME     (1)
#define DP_TCP_HIGHLIMIT_MSL_TIME    (30)
#define DP_TCP_DEFAULT_MSL_TIME      DP_TCP_HIGHLIMIT_MSL_TIME

#define DP_TCP_LOWLIMIT_FIN_TIMEOUT  (0)
#define DP_TCP_HIGHLIMIT_FIN_TIMEOUT (INT_MAX / 1000)
#define DP_TCP_DEFAULT_FIN_TIMEOUT   (600)

#define DP_TCP_LOWLIMIT_PORT_MIN     (1)
#define DP_TCP_HIGHLIMIT_PORT_MIN    (65535)
#define DP_TCP_DEFAULT_PORT_MIN      (49152)

#define DP_TCP_LOWLIMIT_PORT_MAX     (1)
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

#define DP_TCP_LOWLIMIT_INIT_CWND    (10)
#define DP_TCP_HIGHLIMIT_INIT_CWND   (512)
#define DP_TCP_DEFAULT_INIT_CWND     DP_TCP_LOWLIMIT_INIT_CWND

#define DP_TCP_LOWLIMIT_SYNACK_RETRIES    (0)
#define DP_TCP_HIGHLIMIT_SYNACK_RETRIES   (255)
#define DP_TCP_DEFAULT_SYNACK_RETRIES     (5)

#define DP_TCP_LOWLIMIT_RND_PORT_STEP  (1)
#define DP_TCP_HIGHLIMIT_RND_PORT_STEP (4096)
#define DP_TCP_DEFAULT_RND_PORT_STEP   DP_TCP_LOWLIMIT_RND_PORT_STEP

#define DP_TCP_DEFAULT_USR_TIMEOUT   (0)
#define DP_TCP_LOWLIMIT_USR_TIMEOUT  (0)
#define DP_TCP_HIGHLIMIT_USR_TIMEOUT INT_MAX

#define DP_TCP_DEFAULT_DELAYACK_INTER (200)
#define DP_TCP_LOWLIMIT_DELAYACK_INTER (10)
#define DP_TCP_HIGHLIMIT_DELAYACK_INTER (200)

#define DP_TCP_DEFAULT_SNDBUF_PBUFCNT (5120)
#define DP_TCP_LOWLIMIT_SNDBUF_PBUFCNT (10)
#define DP_TCP_HIGHLIMIT_SNDBUF_PBUFCNT (DP_TCP_DEFAULT_SNDBUF_PBUFCNT)

#define DP_TCP_LOWLIMIT_KEEPALIVE_INTVL (0)
#define DP_TCP_HIGHLIMIT_KEEPALIVE_INTVL (INT_MAX / 1000)
#define DP_TCP_DEFAULT_KEEPALIVE_INTVL (75)

#define DP_TCP_LOWLIMIT_KEEPALIVE_PROBES (0)
#define DP_TCP_HIGHLIMIT_KEEPALIVE_PROBES (255)
#define DP_TCP_DEFAULT_KEEPALIVE_PROBES (9)

#define DP_TCP_LOWLIMIT_KEEPALIVE_TIME (0)
#define DP_TCP_HIGHLIMIT_KEEPALIVE_TIME (INT_MAX / 1000)
#define DP_TCP_DEFAULT_KEEPALIVE_TIME (120 * 60)

#define DP_TCP_LOWLIMIT_SYN_RETRIES (1)
#define DP_TCP_HIGHLIMIT_SYN_RETRIES (127)
#define DP_TCP_DEFAULT_SYN_RETRIES (6)

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
    DP_CFG_TCP_INIT_CWND,
    DP_CFG_TCP_SYNACK_RETRIES,
    DP_CFG_TCP_RND_PORT_STEP,
    DP_CFG_TCP_USR_TIMEOUT, // TCP用户超时时间，默认值为0，单位为ms
    DP_CFG_TCP_FRTO,
    DP_CFG_TCP_ADJUST_DELAY_ACK,  // 用户是否使能更低的延迟ACK间隔
    DP_CFG_TCP_DELAY_ACK_INTER,  // 用户设置延迟ACK间隔，时间单位为ms, 只有设置DP_CFG_TCP_ADJUST_DELAY_ACK后才能生效
    DP_CFG_TCP_MSS_USE_DEFAULT,  // 用户设置不协商mss是否使用默认mss，DP_ENABLE表示使用默认mss，DP_DISABLE表示使用本端mss值
    DP_CFG_TCP_SNDBUF_PBUFCNT_MAX, // 用户设置发送缓冲区可存储的pbuf数量， 只有共线程模式下生效。
    DP_CFG_TCP_SMALL_PACKET_ZCOPY,  // 用户根据网卡配置，选择是否支持小包走零拷贝
    DP_CFG_TCP_KEEPALIVE_INTVL,
    DP_CFG_TCP_KEEPALIVE_PROBES,
    DP_CFG_TCP_KEEPALIVE_TIME,
    DP_CFG_TCP_SYN_RETRIES,
    DP_CFG_TCP_TIMESTAMP,
    DP_CFG_TCP_WINDOW_SCALING,
    DP_CFG_TCP_RFC1337, // // 用户设置后，tcp socket 在 time-wait 状态下忽略RST报文
    DP_CFG_TCP_MAX,
} DP_CfgTcpKey_t;

#define DP_IP_LOWLIMIT_REASS_MAX    (1)
#define DP_IP_HIGHLIMIT_REASS_MAX   (4096)
#define DP_IP_DEFAULT_REASS_MAX     (1000)

#define DP_IP_LOWLIMIT_REASS_TIMEO  (0)
#define DP_IP_HIGHLIMIT_REASS_TIMEO (INT_MAX / 1000)
#define DP_IP_DEFAULT_REASS_TIMEO   (30)

#define DP_IP_LOWLIMIT_FRAG_DIST_MAX (0)
#define DP_IP_HIGHLIMIT_FRAG_DIST_MAX (INT_MAX)
#define DP_IP_DEFAULT_FRAG_DIST_MAX (64)

/**
 * @ingroup cfg
 * IP特性控制
 */
typedef enum {
    DP_CFG_IP_REASS_MAX,
    DP_CFG_IP_REASS_TIMEOUT,
    DP_CFG_IP6_FLABEL_REFLECT,
    DP_CFG_IP_FRAG_DIST_MAX,

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

/**
 * @ingroup cfg
 * @brief 获取协议栈支持的全量拥塞算法列表
 *
 * @param caAlg 支持的拥塞算法列表
 * @param size caAlg 数组的大小
 * @param used 已使用的数组大小
 * @return 0 成功，-1 异常

 */
int DP_GetAvalibleCaAlg(const char **caAlg, int size, int *used);

/**
 * @ingroup cfg
 * @brief 获取协议栈可配置的拥塞算法列表
 * @attention 默认 DP_GetAvalibleCaAlg 获取的拥塞算法均可配置
 *
 * @param caAlg 可配置的拥塞算法列表
 * @param size caAlg 数组的大小
 * @param used 已使用的数组大小
 * @return 0 成功，-1 异常

 */
int DP_GetAllowedCaAlg(const char **caAlg, int size, int *used);

/**
 * @ingroup cfg
 * @brief 设置协议栈可配置的拥塞算法列表
 * @attention 可配置的范围为 DP_GetAvalibleCaAlg 获取的拥塞算法
 *
 * @param caAlg 配置的拥塞算法列表
 * @param size caAlg 数组的大小
 * @return 0 成功，-1 异常

 */
int DP_SetAllowedCaAlg(const char **caAlg, int size);

/**
 * @ingroup cfg
 * @brief 设置协议栈使用的拥塞算法
 * @attention
 * @li 可设置的范围为 DP_GetAllowedCaAlg 获取的拥塞算法
 * @li 如果用户使用 socket 选项配置拥塞算法，则直接使用 socket 选项配置的内容，此时不受 DP_GetAvalibleCaAlg 限制
 *
 * @param caAlg 拥塞算法
 * @return 0 成功，-1 异常

 */
int DP_SetCaAlg(const char *caAlg);

#ifdef __cplusplus
}
#endif
#endif
