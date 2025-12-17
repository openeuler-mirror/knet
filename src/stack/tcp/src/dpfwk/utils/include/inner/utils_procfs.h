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
#ifndef UTILS_PROCFS_H
#define UTILS_PROCFS_H

#include "dp_in_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DP_DevProcfs {
    union {
        DP_In6Addr_t lAddr6; // 本端地址(IP6)
        DP_InAddr_t  lAddr4; // 本端地址(IP4)
    };
    DP_InPort_t lPort;       // 本端端口
    union {
        DP_In6Addr_t rAddr6; // 对端地址(IP6)
        DP_InAddr_t  rAddr4; // 对端地址(IP4)
    };
    DP_InPort_t rPort;       // 对端端口
    uint8_t     st;          // 连接状态
    size_t      tx_queue;    // 发送队列数据长度
    size_t      rx_queue;    // 接收队列数据长度
    uint8_t     tr;          // 定时器类型
    uint16_t    when;        // tm->when 定时器超时时间
    uint8_t     retrnsmt;    // 超时重传次数
    uint8_t     uid;         // 用户id, 协议栈不感知
    uint32_t    timeout;     // 持续/保活定时器周期发送但未被确认的TCP报文数量，收到ack后清零
    uint16_t    inode;       // socket对应的inode, 协议栈不感知

    uint32_t    ref;         // socket的引用数
    uint32_t    pointer;     // socket实例地址, 敏感信息, 不对外输出

    // tcp补充信息
    uint32_t    rto;         // RTO值
    uint16_t    delayAck;    // 计算延时确认的估值
    uint16_t    quickAck;    // (快速确认数量 | 是否启用的标志位)
    uint32_t    cwnd;        // 当前拥塞窗口大小
    int32_t     ssthresh;    // 慢启动阈值(>0x7fffffff，显示-1)

    // udp/raw补充信息
    uint32_t    drops;       // 丢包数
} DP_DevProcfs_t;

/**
 * @brief     获取tcp netstat统计信息

 * @param  info [IN] 网络连接信息列表
 * @param  count [IN] 统计socket的数量，传入info数组大小，传出当前type的有效socket数量
 * @retval 返回0 成功
 * @retval 返回-1 失败
 */
int DP_GetTcpProcfsInfo(DP_DevProcfs_t* info, int* count);

/**
 * @brief     获取udp netstat统计信息

 * @param  info [IN] 网络连接信息列表
 * @param  count [IN] 统计socket的数量，传入info数组大小，传出当前type的有效socket数量
 * @retval 返回0 成功
 * @retval 返回-1 失败
 */
int DP_GetUdpProcfsInfo(DP_DevProcfs_t* info, int* count);

/**
 * @brief     获取raw netstat统计信息

 * @param  info [IN] 网络连接信息列表
 * @param  count [IN] 统计socket的数量，传入info数组大小，传出当前type的有效socket数量
 * @retval 返回0 成功
 * @retval 返回-1 失败
 */
int DP_GetRawProcfsInfo(DP_DevProcfs_t* info, int* count);

#ifdef __cplusplus
}
#endif
#endif
