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
#ifndef TCP_FRTO_H
#define TCP_FRTO_H

#include <stdbool.h>

#include "tcp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// 重传定时器到期时 FRTO 是否可用
bool TcpFrtoIsAvailable(TcpSk_t* tcp);

// FRTO 可用时进入 FRTO 重传处理
void TcpFrtoEnterLoss(TcpSk_t* tcp);

// 是否在 FRTO 探测阶段
static inline bool TcpFrtoIsDetecting(TcpSk_t* tcp)
{
    return tcp->frto == 1;
}

// 在 FRTO 开启时接收重复 ack
void TcpFrtoRcvDupAck(TcpSk_t* tcp, TcpPktInfo_t* pi);
    
// 在 FRTO 开启时接收确认了数据的 ack
void TcpFrtoRcvAck(TcpSk_t* tcp, TcpPktInfo_t* pi, uint32_t acked);

// FRTO 恢复
void TcpFrtoRecovery(TcpSk_t* tcp, bool spurios);


#ifdef __cplusplus
}
#endif
#endif
