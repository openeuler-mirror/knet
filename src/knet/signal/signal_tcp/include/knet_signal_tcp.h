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

#ifndef __KNET_SIGNAL_TCP_H__
#define __KNET_SIGNAL_TCP_H__

#include <signal.h>
#include <pthread.h>
#include "knet_osapi.h"
#include "knet_types.h"
#include "knet_lock.h"

/* Linux下未定义的信号 */
#define SIGUNKNOWN1 32
#define SIGUNKNOWN2 33

/* 信号退出时等待时间 10ms */
#define KNET_SIGQUIT_WAIT (10 * 1000)

struct KnetDpSignalFlags {
    bool sigDelay;          // 判断是否在dp锁流程内
    bool sigExitTriggered;  // 判断是否触发了退出信号
    bool inExitUserHandler; // 收到退出信号处理时判断是否在用户回调函数内
    bool inSigHandler;      // 判断是否在信号处理函数流程内
    int curExitSig;         // 当前退出信号

    /* 因为信号延迟处理流程sem_wait时的信号清0，会与accept中while DP_PosixAccept流程耦合，所以信号延迟处理单独使用一个标志 */
    int sigDelayCurSig;     // 信号延迟处理过程中，当前收到的信号，针对除退出信号外的处理
    int curSig;             // 非信号延迟处理过程中，当前收到的信号，针对除退出信号外的处理
};

struct SignalTriggerTimes {
    uint32_t knetSignalEnterCnt;
    uint32_t knetSignalExitCnt;
    KNET_SpinLock lock;
};

/**
 * @brief 获取信号触发次数
 * @return
 */
struct SignalTriggerTimes* KNET_DpSignalTriggerTimesGet(void);

/**
 * @brief tcp signal函数入口
 * @param [IN] int signum 信号编号
 * @param [IN] sighandler_t handler 信号处理函数
 * @return sighandler_t 原信号处理函数
 */
sighandler_t KNET_DpSignalDoSignal(int signum, sighandler_t handler);

/**
 * @brief tcp sigaction函数入口
 * @param [IN] int signum 信号编号
 * @param [IN] sigaction act 信号处理结构体
 * @param [OUT] sigaction oldact 原信号处理结构体
 * @return int 0成功，-1失败
 */
int KNET_DpSignalDoSigaction(int signum, const struct sigaction *act, struct sigaction *oldact);

/**
 * @brief 是否在信号流程内
 * @return bool 在信号流程内返回true，否则返回false*
 */
bool KNET_DpSignalIsInSigHandler(void);

/**
 * @brief 主进程是否在等待其他线程退出DP流程
 * @return bool 在等待其他线程退出时返回true，否则返回false
 */
bool KNET_DpSignalGetWaitExit(void);

/**
 * @brief 信号延迟处理流程中，是否收到中断信号需要退出
 * @return bool 收到中断信号返回true，否则返回false
 */
bool KNET_DpSignalGetSigDelayCurSig(void);

/**
 * @brief 是否收到中断信号需要退出
 * @return bool 收到中断信号返回true，否则返回false
 */
bool KNET_DpSignalGetCurSig(void);

/**
 * @brief 初始化时注册所有信号处理函数
 */
void KNET_DpSignalRegAll(void);

/**
 * @brief 信号延迟处理流程中，清除信号标志位
 */
void KNET_DpSignalClearSigDelayCurSig(void);

/**
 * @brief 清除信号标志位
 */
void KNET_DpSignalClearCurSig(void);

/**
 * @brief 设置主线程等待其他线程退出标志
 */
void KNET_DpSignalSetWaitExit(void);

/**
 * @brief 收到信号时执行用户注册的信号回调函数(仅限退出信号)
 */
void KNET_DpSigProcUserSigHandler(void);

/* 信号处理用到的全局变量 */
extern __thread struct KnetDpSignalFlags g_knetDpSignalFlags;

/* 调用dp接口前处理 */
#define BEFORE_DPFUNC()                                                \
    do {                                                               \
        /* 该标志位仅为了在dp协议栈sem_wait阻塞时，判断是否需要因为信号中断而退出，为了防止之前的流程触发了某个信号污染了标志位，在这里清0 */ \
        g_knetDpSignalFlags.sigDelayCurSig = 0;                        \
        g_knetDpSignalFlags.sigDelay = true;                           \
    } while (0)

/* 调用dp接口后处理 */
#define AFTER_DPFUNC()                                                 \
    do {                                                               \
        g_knetDpSignalFlags.sigDelay = false;                          \
        if (KNET_UNLIKELY(g_knetDpSignalFlags.sigExitTriggered)) {     \
            g_knetDpSignalFlags.sigExitTriggered = false;              \
            KNET_DpSigProcUserSigHandler();                            \
        }                                                              \
    } while (0)

#endif // KNET_SIGNAL_TCP_H