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
 * @file dp_debug_types_api.h
 * @brief 调试信息相关对外结构体
 */

#ifndef DP_DEBUG_TYPES_API_H
#define DP_DEBUG_TYPES_API_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @ingroup debug
 * @brief socket详细信息：IP信息
 */
typedef struct {
    uint8_t  ttl; // ip首部：ttl
    uint8_t  tos; // ip首部：tos
    uint16_t mtu; // 链路mtu

    union {
        struct {
            uint16_t incHdr : 1; // 是否配置了incHdr
            uint16_t tos : 1;    // 是否配置了tos
            uint16_t ttl : 1;    // 是否配置了ttl
            uint16_t mtu : 1;    // 是否配置了mtu
            uint16_t pktInfo : 1; // 是否配置了pktInfo
            uint16_t rcvTos : 1;  // 是否收到对端tos
            uint16_t rcvTtl : 1;  // 是否收到对端ttl
        };
        uint16_t options;
    } options;
    uint8_t resv[2];
} DP_InetDetails_t;

/**
 * @ingroup debug
 * @brief socket详细信息：IP6信息
 */
typedef struct {
    uint32_t origFLabel;    // 原始报文的flow label信息
    uint32_t fLabel; // ip6首部：flow label
    uint8_t  tClass; // ip6首部：traffic class
    uint8_t  hopLmt; // ip6首部：hop limit
    uint16_t mtu;    // 链路mtu

    union {
        struct {
            uint16_t tClass : 1; // 是否配置了traffic class
            uint16_t fLabel : 1; // 是否配置了flow label
            uint16_t hopLmt : 1; // 是否配置了hop limit
        };
        uint16_t options;
    } options;
    uint8_t resv[2];
} DP_Inet6Details_t;

/**
 * @ingroup debug
 * @brief socket详细信息：TCP基础信息
 */
typedef struct {
    uint8_t state;    // tcp状态，详见DP_SOCKET_STATE_CLOSED等
    uint8_t connType; // 0：主动建链 1：被动建链

    union {
        struct {
            uint8_t noVerifyCksum : 1;  // 不校验校验和标记位，暂不支持配置
            uint8_t ackNow : 1;         // 立即ack标记位，暂不支持配置
            uint8_t delayAckEnable : 1; // 延迟ACK功能标记位
            uint8_t nodelay : 1;        // nagle算法标记位
            uint8_t rttRecord : 1;      // rtt开始记录标记位
            uint8_t cork : 1;           // cork标记位
            uint8_t deferAccept : 1;    // 延迟建链标记位
        };
        uint8_t options;
    }; // tcp options
    uint8_t flags;   // tcp socket状态标志

    int16_t wid;     // tcp所属worker
    int16_t txQueid; // 队列id

    int childCnt;    // 子socket数量，只有监听socket有效
    int backlog;     // 子socket数量上限，只有监听socket有效

    uint8_t accDataCnt; // 累计收取的数据报文个数
    uint8_t accDataMax; // 不使能延迟ack时，每收到N个数据回复一个ACK

    int8_t caAlgId;      // 拥塞算法id
    uint8_t caState;     // 拥塞算法状态
    uint8_t dupAckCnt;   // 当前收到的重复ack数量
    uint32_t cwnd;       // 拥塞窗口大小
    uint32_t ssthresh;   // 拥塞避免门限
    uint32_t seqRecover; // 快速恢复recovery point
    uint32_t reorderCnt; // 乱序报文个数，重复ACK超过此数值后，触发快速重传

    uint32_t rttStartSeq; // 记录rtt评估开始时的序号
    uint32_t srtt;        // 平滑rtt
    uint32_t rttval;      // Round Trip Time，信道往返时间
    uint32_t maxRtt;      // 记录的最大rtt
    uint32_t tsVal;       // 发送的时间戳
    uint32_t tsEcho;      // 接收的时间戳

    uint32_t lastChallengeAckTime; // 上一个挑战ack的发送时间

    uint8_t fastMode;  // 加入快定时器的模式，重传or坚持定时器
    size_t sndQueSize; // tcp发送队列的数据大小
    size_t rcvQueSize; // tcp接收队列的数据大小
    size_t rexmitQueSize; // tcp重传队列的数据大小
    size_t reassQueSize; // tcp重组队列的数据大小
} DP_TcpBaseDetails_t;

/**
 * @ingroup debug
 * @brief socket详细信息：TCP传输信息
 */
typedef struct {
    uint16_t lport; // 本端端口
    uint16_t pport; // 对端端口

    uint8_t synOpt; // 可协商的选项
    uint8_t negOpt; // 已协商的选项

    uint8_t rcvWs; // 对端窗口缩放因子
    uint8_t sndWs; // 本端窗口缩放因子

    uint16_t rcvMss; // 对端mss
    uint16_t mss;    // 本端mss

    uint32_t iss; // 初始发送序列号 initial send sequence number
    uint32_t irs; // 初始接收序列号 initial receive sequence number

    uint32_t sndUna; // send unacknowledged
    uint32_t sndNxt; // send next
    uint32_t sndMax; // send max
    uint32_t sndWnd; // send window, peer window
    uint32_t sndUp;  // send urgent pointer
    uint32_t sndWl1; // segment sequence number used for last window update
    uint32_t sndSml; // send small pkt end seq

    uint32_t rcvNxt; // receive next
    uint32_t rcvWnd; // receive window, local window
    union {
        uint32_t rcvUp;  // receive urgent pointer
        uint32_t rcvMax; // 收到FIN后，记录此时的最大序号
    };
    uint32_t rcvWup; // 最后一次发送窗口更新时的 rcvNxt

    uint32_t idleStart;         // 链路空间起始时间
    uint16_t keepIdle;          // 保活时间间隔
    uint16_t keepIntvl;         // 保活探测时间
    uint8_t  keepProbes;        // 保活探测最大次数
    uint8_t  keepProbeCnt;      // 保活探测次数
    uint32_t keepIdleLimit;     // 保活超时上限
    uint32_t keepIdleCnt;       // 保活超时次数
    uint8_t  backoff;           // 当前重传次数
    uint8_t  maxRexmit;         // 用户设置DefferAccept场景下的最大重传次数
    uint32_t rexmitCnt;         // 累计重传的数据报文个数
    uint32_t userTimeout;       // 用户配置的userTimeOut时间
    uint32_t userTimeStartFast; // 快超时定时器起始时间
    uint32_t userTimeStartSlow; // 慢超时定时器起始时间

    uint16_t fastTimeoutTick;    // 快超时定时器超时时间
    uint16_t slowTimeoutTick;    // 慢超时定时器超时时间
    uint16_t delayAckTimoutTick; // 延迟ack定时器超时时间

    uint32_t synRetries; // SYN或SYN/ACK重传的次数
} DP_TcpTransDetails_t;

/**
 * @ingroup debug
 * @brief socket详细信息：TCP信息
 */
typedef struct {
    DP_TcpBaseDetails_t baseDetails;
    DP_TcpTransDetails_t transDetails;
} DP_TcpDetails_t;

/**
 * @ingroup debug
 * @brief socket详细信息
 */
typedef struct {
    /* socket基础信息 */
    int32_t protocol;
    union {
        struct {
            uint16_t lingerOnoff : 1; // linger标记位
            uint16_t nonblock : 1;    // nonblock标记位
            uint16_t reuseAddr : 1;   // reuseAddr标记位
            uint16_t reusePort : 1;   // reusePort标记位
            uint16_t broadcast : 1;   // broadcast标记位
            uint16_t keepalive : 1;   // keepalive标记位
            uint16_t bindDev : 1;     // 绑定vif标记位
            uint16_t dontRoute : 1;   // dontRoute标记位
        };
        uint16_t options;
    };
    uint16_t error;     // socket错误缓存
    uint16_t family;    // 协议族，PF_INET/PF_INET6
    int linger;         // linger时间
    uint16_t flags;     // socket连接状态
    uint8_t  state;     // socket事件状态
    uint16_t rdSemCnt;  // 读信号量等待计数
    uint16_t wrSemCnt;  // 写信号量等待计数
    int rcvTimeout;     // 接收超时
    int sndTimeout;     // 发送超时
    size_t sndDataLen;  // 待发送数据长度
    size_t rcvDataLen;  // 待接收数据长度
    uint32_t sndLowat;  // 发送缓冲区低水位
    uint32_t sndHiwat;  // 发送缓冲区高水位
    uint32_t rcvLowat;  // 接收缓冲区低水位
    uint32_t rcvHiwat;  // 接收缓冲区高水位
    uint32_t bandWidth; // 限速带宽
    int priority;       // 优先级
    int associateFd;    // 事件通知关联FD
    int notifyType;     // 事件通知类型
    int32_t wid;        // worker id

    union {
        DP_InetDetails_t inetDetails;   // IP信息，family == PF_INET 时使用
        DP_Inet6Details_t inet6Details; // IP6信息，family == PF_INET6 时使用
    };
    union {
        DP_TcpDetails_t tcpDetails;
    };
} DP_SockDetails_t;

/**
 * @ingroup debug
 * @brief socket连接状态信息
 */
typedef struct {
    uint32_t pf;            // 协议族，DP_PF_INET/DP_PF_INET6
    uint32_t proto;         // 协议，DP_IPPROTO_TCP/DP_IPPROTO_UDP
    union {
        uint32_t lAddr6[4]; // 本端地址(IP6)
        uint32_t lAddr4;    // 本端地址(IP4)
    };
    uint32_t lPort;         // 本端端口
    union {
        uint32_t rAddr6[4]; // 对端地址(IP6)
        uint32_t rAddr4;    // 对端地址(IP4)
    };
    uint32_t rPort;         // 对端端口
    uint32_t state;         // 连接状态
    uint32_t workerId;      // worker Id
} DP_SocketState_t;

/**
 * @ingroup debug
 * @brief epoll的详细信息
 */
typedef struct {
    uint32_t fd;             // epoll监听的socket fd
    uint32_t expectEvents;   // epoll监听的事件，例如EPOLLIN/EPOLLET等
    uint32_t readyEvents;    // socket已就绪的事件，例如EPOLLIN/EPOLLERR等，边缘触发模式上报事件后将同步至notifiedEvents，
    // readyEvents的值保留至下次事件更新
    uint32_t notifiedEvents; // 边缘触发模式下有效，socket已经上报过的事件，例如EPOLLIN等
    uint32_t shoted : 1;     // one shot 模式下有效，socket上报事件后置1
    uint32_t res : 31;
    union {
        void* ptr;
        int fd;
        uint32_t u32;
        uint64_t u64;
    } eventData;            // 用户数据
} DP_EpollDetails_t;

#ifdef __cplusplus
}
#endif
#endif
