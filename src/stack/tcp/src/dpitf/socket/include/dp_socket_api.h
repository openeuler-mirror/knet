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
 * @file dp_socket_api.h
 * @brief 非posix标准socket相关接口
 */

#ifndef DP_SOCKET_API_H
#define DP_SOCKET_API_H

#include <stddef.h>
#include <stdint.h>

#include "dp_socket_types_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @ingroup sock_types
 * 产品获取协议栈socket信息
 */
typedef struct DP_SocketInfo {
    int resetFlag;          // 是否重新遍历(非0 重新开始遍历; 0 继续上次遍历; 建议使用0、1)

    unsigned int socketNum; // 产品指定获取的有效socket个数，返回实际有效socket个数
    int *socketList;        // 产品管理对应内存(建议使用int类型数组)，内存长度大于等于sizeof(int) * socketNum
} DP_SocketInfo_t;

/**
 * @ingroup socket
 * @brief 遍历获取有效socket fd
 *
 * @par 描述: 遍历获取有效socket fd
 * @attention
 * NA
 *
 * @param para [IN/OUT] 指定遍历获取的fd个数，结构体具体字段请参考结构体定义
 *
 * @retval 0 成功
 * @retval -1 失败 \n
 * \n
 * 约束说明：
 * 1 产品指定本地调用是否重新遍历，重新遍历是指从0开始遍历；不设置重新遍历则值从上次遍历值开始；
 * 2 socketNum为产品指定本次获取的最大有效fd个数；
 * 3 socketList对应的内存由产品管理，该内存长度需要大于等于sizeof(int) * socketNum；
 * 4 实际返回的有效fd个数会设置到socketNum变量中；
 * 5 API返回成功（0）结构体中成员字段有效，否则成员字段无效；
 * 6 不支持在高性能模型下（免锁）多线程部署环境中使用，可能会导致内存异常。
 *   高性能模型下仅支持与业务同线程中调用；多线程场景下返回的fd可能是无效的；
 * 7 不区分socket类型；
 * @par 依赖:
 *     <ul><li>无。</li></ul>

 */
int DP_GetSocketStatus(DP_SocketInfo_t *para);

typedef struct {
    uint8_t  tcpState;                  /**< TCP当前状态 */
    uint8_t  tcpCaState;                /**< TCP拥塞控制状态 */
    uint8_t  tcpSndWScale : 4;          /**< 对端的窗口扩大因子 */
    uint8_t  tcpRcvWScale : 4;          /**< 本端的窗口扩大因子 */

    uint32_t tcpSndMSS;                /**< mss值 对照 tcpi_snd_mss 支持时间戳情况会将协商的mss-12*/
    uint32_t tcpRcvMSS;                /**< 对端通告的mss */

    uint32_t tcpRtt;                   /**< rtt值 对照 tcpi_rtt */
    uint32_t tcpRttVar;                /**< 简单平滑偏差估计，单位为微秒 */
    uint32_t tcpSndCwnd;               /**< 拥塞控制窗口大小 对照tcpi_snd_cwnd */

    uint32_t tcpTotalRetrans;          /**< 本连接的总重传数据段数 */
    // 产品环境仅包含linux中以上属性

    uint32_t tcpSndWnd;
    uint32_t tcpRcvWnd;
    // 以上为linux中tcp_info定义

    struct {
        uint32_t tcpMaxRtt;                 // 链路的最大时延
        uint32_t tcpRcvDrops;               // 接收丢包计数
        uint32_t tcpConnLatency;            // 建链的时延
        uint16_t tcpMss;                    // mss值，根据mtu值、IP首部长度、TCP基础首部长度计算
    };
} DP_TcpInfo_t; // 分为linux中定义和补充定义两部分，不支持情况值为0

/**
 * @ingroup socket
 * @brief 设置socket属性。
 *
 * @par 描述: 设置socket属性。
 * @attention
 * @li 此接口调用要在协议栈启动初始化完成之后才能正常使用。
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param level [IN] 级别<DP_SOL_SOCKET:socket   \n
 *                   DP_IPPROTO_IP:IPV4，level为DP_IPPROTO_IP的只能对IPv4的socket进行设置  \n
 *                   DP_IPPROTO_IPV6:IPV6，level为DP_IPPROTO_IPV6的只能对IPv6的socket进行设置  \n
 *                   DP_IPPROTO_TCP:TCP  \n
 *                   DP_IPPROTO_UDP:UDP >
 * @param optname [IN]
 *                  DP_SO_SNDTIMEO: 阻塞模式发送发送超时时间 \n
 *                  DP_SO_RCVTIMEO: 阻塞模式接收发送超时时间 \n
 *                  DP_SO_REUSEADDR: 地址重用开关 \n
 *                  DP_SO_REUSEPORT: 端口重用开关 \n
 *                  DP_SO_KEEPALIVE: 保活选项开关 \n
 *                  DP_SO_LINGER: linger选项开关 \n
 *                  DP_SO_SNDBUF: 发送缓存即高水位 \n
 *                  DP_SO_RCVBUF 接收缓存即高水位 \n
 *                  DP_SO_ERROR：获取socket错误状态 \n
 *                  DP_SO_RCVLOWAT：接收缓存低水位，大于低水位时通知可读事件 \n
 *                  DP_SO_PRIORITY：优先级，可以设置[0, 6] \n
 *                  DP_SO_PROTOCOL：协议类型，取值为DP_IPPROTO_UDP或者DP_IPPROTO_TCP \n
 *                  DP_SO_USERDATA：userdata \n
 *                  DP_TCP_KEEPIDLE: 在指定的空闲时间后启动保活探测，单位秒，规格为[1, 7200] \n
 *                  DP_TCP_KEEPIDLE_LIMIT：保活探测的次数, 规格[0, INT_MAX]，默认0不生效，
 *                                         配置大于0时，第N次触发保活探测时会直接RST断链 \n
 *                  DP_TCP_KEEPINTVL: 设置保活探测的时间间隔，单位秒，规格为[1, 7200] \n
 *                  DP_TCP_KEEPCNT: 设置保活探测的次数, 规格为[1, 127] \n
 *                  DP_TCP_NODELAY: 设置是否禁止TCP的Nagle算法，默认开启Nagle算法 \n
 *                  DP_TCP_CORK: 设置cork选项。,1表示开启, 0表示关闭,默认关闭 \n
 *                  DP_TCP_MAXSEG: 设置TCP最大报文段 \n
 *                  DP_TCP_DEFER_ACCEPT: 子socket收到数据再上报监听socket建链完成,单位秒, 规格为[0, 7200] \n
 *                  DP_TCP_CONGESTION：TCP拥塞控制算法 \n
 *                  DP_TCP_USER_TIMEOUT：TCP超时重传时间，单位ms \n
 *                  DP_TCP_SYNCNT：TCP SYN重传次数，可以设置[0, 6]，默认0不生效，
 *                                 配置大于0时，SYN包不会被DP_TCP_USER_TIMEOUT和DP_CFG_TCP_SYNACK_RETRIES控制 \n
 *                  DP_IP_TTL: ime To Live \n
 *                  DP_IP_TOS: 设置TOS \n
 *                  DP_IP_PKTINFO: 设置收UDP报文时获取目的IP及入端口信息，保存到CMSG \n
 * @param optval [IN] 选项值，对于optval为数值的选项设置和获取，当前只支持uint32_t 型输入和输出<用户根据实际情况填充><非空指针>
 * @param optlen [IN] 选项值长度<用户根据实际情况填充>
 *
 * @retval 0 成功
 * @retval -1 具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 * EBADF: 入参sockfd无效 \n
 * EDOM: 入参optname为超时时间情况，入参val中usec不在0~（1000 * 1000 - 1）中 \n
 * EINVAL: 1.入参optlen大于INT_MAX \n
 * 	       2.入参optlen小于对应长度 \n
 * 	       3.入参*optval值超过限制 \n
 * 	       4.fd存在，但是fd对应的socketcb有异常 \n
 * EISCONN: mss、deferaccept在连接状态下不会设置成功 \n
 * ENOPROTOOPT: 入参optname不支持 \n
 * ENOTSOCK: 入参sockfd不是套接字类型fd \n
 * EFAULT: 入参optval是空指针 \n
 * @par 依赖:
 *     <ul><li>dp_posix_socket_api.h：该接口声明所在的头文件。</li></ul>

 */
int DP_Getsockopt(int sockfd, int level, int optname, void* optval, DP_Socklen_t* optlen);

/**
 * @ingroup socket
 * @brief 设置socket属性。
 *
 * @par 描述: 设置socket属性。
 * @attention
 * @li 此接口调用要在协议栈启动初始化完成之后才能正常使用。
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param level [IN] 级别<DP_SOL_SOCKET:socket   \n
 *                   DP_IPPROTO_IP:IPV4，level为DP_IPPROTO_IP的只能对IPv4的socket进行设置  \n
 *                   DP_IPPROTO_IPV6:IPV6，level为DP_IPPROTO_IPV6的只能对IPv6的socket进行设置  \n
 *                   DP_IPPROTO_TCP:TCP  \n
 *                   DP_IPPROTO_UDP:UDP >
 * @param optname [IN]
 *                  DP_SO_SNDTIMEO: 阻塞模式发送发送超时时间 \n
 *                  DP_SO_RCVTIMEO: 阻塞模式接收发送超时时间 \n
 *                  DP_SO_REUSEADDR: 地址重用开关 \n
 *                  DP_SO_REUSEPORT: 端口重用开关 \n
 *                  DP_SO_KEEPALIVE: 保活选项开关 \n
 *                  DP_SO_LINGER: linger选项开关 \n
 *                  DP_SO_SNDBUF: 发送缓存即高水位 \n
 *                  DP_SO_RCVBUF 接收缓存即高水位 \n
 *                  DP_SO_RCVLOWAT：接收缓存低水位，大于低水位时通知可读事件 \n
 *                  DP_SO_PRIORITY：优先级，可以设置[0, 6] \n
 *                  DP_SO_USERDATA：userdata \n
 *                  DP_TCP_KEEPIDLE: 在指定的空闲时间后启动保活探测，单位秒，规格为[1, 7200] \n
 *                  DP_TCP_KEEPIDLE_LIMIT：保活探测的次数, 规格[0, INT_MAX]，默认0不生效，
 *                                         配置大于0时，第N次触发保活探测时会直接RST断链 \n
 *                  DP_TCP_KEEPINTVL: 设置保活探测的时间间隔，单位秒，规格为[1, 7200] \n
 *                  DP_TCP_KEEPCNT: 设置保活探测的次数, 规格为[1, 127] \n
 *                  DP_TCP_NODELAY: 设置是否禁止TCP的Nagle算法，默认开启Nagle算法 \n
 *                  DP_TCP_CORK: 设置cork选项。,1表示开启, 0表示关闭,默认关闭 \n
 *                  DP_TCP_MAXSEG: 设置TCP最大报文段 \n
 *                  DP_TCP_DEFER_ACCEPT: 子socket收到数据再上报监听socket建链完成,单位秒, 规格为[0, 7200] \n
 *                  DP_TCP_CONGESTION：TCP拥塞控制算法 \n
 *                  DP_TCP_USER_TIMEOUT：TCP超时重传时间 \n
 *                  DP_TCP_SYNCNT：TCP SYN重传次数，规格[0, 6]，默认0不生效，
 *                                 配置大于0时，SYN包不会被DP_TCP_USER_TIMEOUT和DP_CFG_TCP_SYNACK_RETRIES控制， \n
 *                  DP_IP_TTL: ime To Live \n
 *                  DP_IP_TOS: 设置TOS \n
 *                  DP_IP_PKTINFO: 设置收UDP报文时获取目的IP及入端口信息，保存到CMSG \n
 *                  DP_TCP_QUICKACKNUM: 设置逐包回复ACK的个数, 规格为[0, 65535] \n
 *                  DP_TCP_BBR_CONGESTION_PARAM: 设置BBR拥塞控制算法参数，具体参数请参考DP_TcpBbrParam结构体说明 \n
 * @param optval [IN] 选项值，对于optval为数值的选项设置和获取，当前只支持uint32_t 型输入和输出<用户根据实际情况填充><非空指针>
 * @param optlen [IN] 选项值长度<用户根据实际情况填充>
 *
 * @retval 0 成功
 * @retval -1 具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 * EBADF: 入参sockfd无效 \n
 * EDOM: 入参optname为超时时间情况，入参val中usec不在0~（1000 * 1000 - 1）中 \n
 * EINVAL: 1.入参optlen大于INT_MAX \n
 * 	       2.入参optlen小于对应长度 \n
 * 	       3.入参*optval值超过限制 \n
 * 	       4.fd存在，但是fd对应的socketcb有异常 \n
 * EISCONN: mss、deferaccept在连接状态下不会设置成功 \n
 * ENOPROTOOPT: 入参optname不支持 \n
 * ENOTSOCK: 入参sockfd不是套接字类型fd \n
 * EFAULT: 入参optval是空指针 \n
 * @par 依赖:
 *     <ul><li>dp_posix_socket_api.h：该接口声明所在的头文件。</li></ul>

 */
int DP_Setsockopt(int sockfd, int level, int optname, const void* optval, DP_Socklen_t optlen);

#ifdef __cplusplus
}
#endif
#endif
