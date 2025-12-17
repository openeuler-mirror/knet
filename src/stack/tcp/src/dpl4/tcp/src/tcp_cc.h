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
#ifndef TCP_CC_H
#define TCP_CC_H

#include "utils_cfg.h"
#include "dp_tcp_cc_api.h"

#include "tcp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TCP_CAMETH_NEWRENO  (CFG_TCP_CAMETH_NEWRENO)
#define TCP_CAMETH_BBR      (CFG_TCP_CAMETH_BBR)

#define TCP_CA_REG_ALGID_START  (64) /* 外部注册的拥塞算法自定义算法ID编号从64开始，范围为64-127 */

#define TCP_CA_MAX_NUM       (32)

typedef enum {
    TCP_CA_OPEN,
    TCP_CA_DISORDER,
    TCP_CA_CWR,
    TCP_CA_RECOVERY,
    TCP_CA_LOSS,
} TcpCaState_t;

#define TCP_IS_IN_OPEN(tcp)     ((tcp)->caState == TCP_CA_OPEN)
#define TCP_IS_IN_RECOVERY(tcp) ((tcp)->caState == TCP_CA_RECOVERY)
#define TCP_IS_IN_LOSS(tcp)     ((tcp)->caState == TCP_CA_LOSS)

static inline uint32_t TcpGetInitCwnd(TcpSk_t* tcp)
{
    return tcp->mss * tcp->initCwnd;
}

static inline void TcpCaInit(TcpSk_t* tcp)
{
    if (tcp->caMeth->caInit != NULL) {
        tcp->caMeth->caInit(tcp);
    }
    tcp->caIsInited = 1;
}

static inline void TcpCaDeinit(TcpSk_t* tcp)
{
    if (tcp->caMeth->caDeinit != NULL) {
        tcp->caMeth->caDeinit(tcp);
    }
    tcp->caIsInited = 0;
}

static inline void TcpCaRestart(TcpSk_t* tcp)
{
    if (tcp->caMeth->caRestart != NULL) {
        tcp->caMeth->caRestart(tcp);
    }
}

static inline void TcpCaAcked(TcpSk_t* tcp, uint32_t acked, uint32_t rtt)
{
    if (tcp->caMeth->caAcked != NULL) {
        tcp->caMeth->caAcked(tcp, acked, rtt);
    }
}

static inline void TcpCaDupAck(TcpSk_t* tcp)
{
    if (tcp->caMeth->caDupAck != NULL) {
        tcp->caMeth->caDupAck(tcp);
    }
}

static inline void TcpCaTimeout(TcpSk_t* tcp)
{
    if (tcp->caMeth->caTimeout != NULL) {
        tcp->caMeth->caTimeout(tcp);
    }
}

static inline void TcpCaSetState(TcpSk_t* tcp, uint8_t newState)
{
    if (tcp->caMeth->caSetState != NULL) {
        tcp->caMeth->caSetState(tcp, newState);
    }
    tcp->caState = newState;
}

static inline void TcpCaCwndEvent(TcpSk_t* tcp, DP_TcpCaEvent_t event)
{
    if (tcp->caMeth->caCwndEvent != NULL) {
        tcp->caMeth->caCwndEvent(tcp, event);
    }
}

static inline void TcpCaCongCtrl(TcpSk_t* tcp)
{
    if (tcp->caMeth->caCongCtrl != NULL) {
        tcp->caMeth->caCongCtrl(tcp);
    }
}

const DP_TcpCaMeth_t* TcpCaGet(int8_t algId);

const DP_TcpCaMeth_t* TcpCaGetByName(char* algName);

int TcpCaRegist(DP_TcpCaMeth_t* meth);

int TcpCaModuleInit(void);

void TcpCaModuleDeinit(void);

int TcpGetCaMethCnt(void);

static inline unsigned int TcpBytesInPipe(TcpSk_t* tcp)
{
    if (TCP_SACK_AVAILABLE(tcp) && tcp->caState == TCP_CA_RECOVERY) {
        return tcp->inflight;
    }
    return tcp->sndNxt - tcp->sndUna;
}

static inline unsigned int TcpPacketInPipe(TcpSk_t* tcp)
{
    return ((tcp->mss != 0) ? (TcpBytesInPipe(tcp) / tcp->mss) : 1);
}

#ifdef __cplusplus
}
#endif
#endif
