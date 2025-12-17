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
#ifndef TCP_BBR_H
#define TCP_BBR_H

#include "tcp_types.h"
#include "utils_minmax.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_AGGR_EPOCH_NUM 2

#define BBR_PROBE_RTT_MIN_CYCLE 10
#define BBR_PROBE_RTT_MAX_CYCLE 250

#define BBR_MIN_PROBERTT_TIMEOUT 10
#define BBR_MIN_CWND 4

typedef enum {
    BBR_STARTUP,
    BBR_DRAIN,
    BBR_PROBE_BW,
    BBR_PROBE_RTT,
} BbrMode_t;

/* TCP BBR 控制块 */
typedef struct BBR_Cb {
    uint32_t minRttMs;
    uint32_t minRttTime;
    uint32_t probeRttExitTime;  // 探测RTT模式结束时间
    uint8_t  probeRttCwnd;      // 应用配置的PROBECWND阶段使用的拥塞控制窗口
    uint8_t  probeRttTimeOut;   // 进入PROBERTT的周期,单位为s
    uint8_t  probeRttCycle;     // PROBERTT阶段的周期,单位为ms
    uint8_t  ignoreRecovery : 1;
    uint8_t  dismissLT : 1;
    uint8_t  incrFactor : 1;
    uint8_t  delProbeRtt : 1;
    uint8_t  reserve : 4;
    Minmax_t bw;                // 最近传送的最大带宽速率
    uint32_t rttCnt;            // 已过RTT的包计数
    uint32_t nextRttDelivered;  // 下一个RTT递送状态的包数量
    uint32_t cycleStartTime;    // 周期开始的时间戳
    uint32_t mode : 3;          // 状态机中bbr模式
    uint32_t prevCaState : 4;   // 上一个ack的CA状态
    uint32_t inRecovery : 1;
    uint32_t newRttStart : 1;
    uint32_t idleRestart : 1;
    uint32_t probeRttExit : 1;
    uint32_t slowBwEstimateStart : 1;
    uint32_t slowIntervalRttCnt : 7;
    uint32_t slowBwValid : 1;
    uint32_t unUsed : 12;

    uint64_t slowBw;
    uint32_t slowIntervalStartCumAckCnt;        // u32ItLastDelivered
    uint32_t slowIntervalStartTime;             // u32ItLastStamp
    uint32_t pacingGain : 10;   // 当前的pacing rate 的增益系数
    uint32_t cwndGain : 10;     // 当前设置拥塞窗口的增益系数
    uint32_t fullBwReached : 1;
    uint32_t fullBwCnt : 2;     // 不使用大带宽增益系数的循环次数
    uint32_t cycleIdx : 3;
    uint32_t unUsedB : 6;

    uint32_t priorCwnd;         // 进入丢包恢复前的拥塞窗口大小
    uint64_t fullBw;            // 根据最近的bw,推测带宽是否满载

    /* For tracking ACK aggregation: */
    uint32_t aggrEpochAcked : 20;
    uint32_t extraAckedRttCnt : 5;
    uint32_t extraAckedSlot : 1;
    uint32_t unUsedC : 6;

    uint32_t aggrEpochStartTime;
    uint32_t extraAcked[MAX_AGGR_EPOCH_NUM];
    uint32_t slowLastCumLostCnt;
} BBR_Cb_t;

/* TCP BBR 算法注册函数 */
void TcpBBRRegist(void);

void TcpRcvMinRttUpdate(TcpSk_t* tcp, int32_t caRttMs, uint32_t lastAckTime);

void TcpShowBBRInfo(TcpSk_t* tcp);

#ifdef __cplusplus
}
#endif
#endif
