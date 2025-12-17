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
#ifndef DP_TCP_CC_API_H
#define DP_TCP_CC_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DP_TCP_CA_EVENT_TX_START,     /* first transmit when no packets in flight */
    DP_TCP_CA_EVENT_CWND_RESTART, /* congestion window restart */
    DP_TCP_CA_EVENT_COMPLETE_CWR, /* end of congestion recovery */
    DP_TCP_CA_EVENT_LOSS,         /* loss timeout */
    DP_TCP_CA_EVENT_ECN_NO_CE,    /* ECT set, but not CE marked */
    DP_TCP_CA_EVENT_ECN_IS_CE,    /* received CE marked IP packet */
} DP_TcpCaEvent_t;

/**
 * @brief TCP 拥塞算法初始化，在tcp申请时调用
 */
typedef void (*DP_TcpCaInitFn_t)(void* tcp);

/**
 * @brief TCP拥塞算法去初始化
 */
typedef void (*DP_TcpCaDeinitFn_t)(void* tcp);

/**
 * @brief 两个调用点，TCP建链完成后以及长时间无数据传输后
 */
typedef void (*DP_TcpCaRestartFn_t)(void* tcp);

/**
 * @brief TCP正常ACK确认
 *        acked: 本次确认的数据长度
 *        rtt: 未经过平滑的rtt时间
 */
typedef void (*DP_TcpCaAckedFn_t)(void* tcp, uint32_t acked, uint32_t rtt);

/**
 * @brief TCP重复ACK确认
 */
typedef void (*DP_TcpCaDupAckFn_t)(void* tcp);

/**
 * @brief TCP超时处理
 */
typedef void (*DP_TcpCaTimeoutFn_t)(void* tcp);

/**
 * @brief TCP设置拥塞处理
 */
typedef void (*DP_TcpCaSetStateFn_t)(void* tcp, uint8_t newState);

/**
 * @brief TCP处理拥塞事件
 */
typedef void (*DP_TcpCaCwndEvetFn_t)(void* tcp, DP_TcpCaEvent_t event);

/**
 * @brief TCP拥塞控制
 */
typedef void (*DP_TcpCaCongControlFn_t)(void* tcp);


/**
 * @brief TCP拥塞控制取消，恢复窗口(预留，暂不实现)
 */
typedef uint32_t (*DP_TcpCaUndoCwndFn_t)(void* tcp);

/**
 * @brief TCP拥塞避免，在数据被ack时尝试恢复窗口(预留，暂不实现)
 */
typedef void (*DP_TcpCaCongAvoidFn_t)(void* tcp, uint32_t ack, uint32_t acked);

/**
 * @brief 调整tso的大小配置(预留，暂不实现)
 */
typedef uint32_t (*DP_TcpCaMinTsoSegFn_t)(void* tcp);

/**
 * @brief 发送缓冲区动态大小调整配置(预留，暂不实现)
 */
typedef uint32_t (*DP_TcpCaSndbufExpandFn_t)(void* tcp);

/**
 * @brief 获取拥塞算法调测信息(预留，暂不实现)
 */
typedef uint32_t (*DP_TcpCaGetInfoFn_t)(void* tcp, void* tcpCcInfo);

#define DP_TCP_CA_NAME_MAX_LEN 16

typedef struct TcpCaMeth {
    const int8_t               algId;           // 外部注册的拥塞算法id范围64-127
    int8_t                     reserve[3];
    const char                 algName[DP_TCP_CA_NAME_MAX_LEN];       // 设置/获取拥塞算法功能(DP_TCP_CONGESTION)依靠algName搜索
    DP_TcpCaInitFn_t           caInit;
    DP_TcpCaDeinitFn_t         caDeinit;
    DP_TcpCaRestartFn_t        caRestart;
    DP_TcpCaAckedFn_t          caAcked;
    DP_TcpCaDupAckFn_t         caDupAck;
    DP_TcpCaTimeoutFn_t        caTimeout;
    DP_TcpCaSetStateFn_t       caSetState;
    DP_TcpCaCwndEvetFn_t       caCwndEvent;
    DP_TcpCaCongControlFn_t    caCongCtrl;
    DP_TcpCaUndoCwndFn_t       caUndoCwnd;
    DP_TcpCaCongAvoidFn_t      caCongAvoid;
    DP_TcpCaMinTsoSegFn_t      caMinTsoSeg;
    DP_TcpCaSndbufExpandFn_t   caSndbufExpand;
    DP_TcpCaGetInfoFn_t        caGetInfo;
} DP_TcpCaMeth_t;

/**
 * @ingroup tcp
 * @brief 拥塞算法注册接口
 *
 * @attention
 * DP会保存meth指针，调用者需要管理次指针生命周期
 *
 * @param meth
 * @retval 0 成功
 * @retval -错误码 失败

 */
int32_t DP_TcpRegisterCcAlgo(DP_TcpCaMeth_t* meth);

/**
 * @ingroup tcp
 * @brief 拥塞算法去注册接口
 *
 * @attention
 *
 * @param algName
 * @retval 0 成功
 * @retval -错误码 失败

 */
int32_t DP_TcpUnregisterCcAlgo(const char *algName);

#ifdef __cplusplus
}
#endif
#endif
