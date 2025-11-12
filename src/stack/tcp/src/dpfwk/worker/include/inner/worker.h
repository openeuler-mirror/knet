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

#ifndef WORKER_H
#define WORKER_H

#include "dp_worker_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORKER_HZ          (100) // 每个tick 10ms
#define WORKER_MS_PER_TICK (1000 / WORKER_HZ) // 每个tick的毫秒数

typedef struct {
    uint32_t bits; // 最大支持32个worker管理
} WORKER_BitMap_t; // worker id bit map

#define WORKER_CLEAN_BIT_MAP(map)     ((map)->bits) = 0
#define WORKER_SET_BIT_MAP_ALL(map)    ((map)->bits) = 0xFFFFFFFF
#define WORKER_SET_BIT_MAP(map, wid)  ((map)->bits) |= (1U << (wid))
#define WORKER_IS_IN_BIT_MAP(map, wid) (((map)->bits) & (1U << (wid)))
#define WORKER_BITMAP_ALL \
    {                     \
        0xFFFFFFFF        \
    }

#define WORKER_FLAGS_EVENT 0x1

enum {
    WORKER_WORK_TYPE_FIX = 0, // 固定任务，每次worker调度都会执行
    WORKER_WORK_TYPE_TIMER, // 定时任务，指定TICK
};

//! 固定任务和定时器任务，实例不处理错误码
typedef void (*WORKER_FixWorkCb_t)(int wid);
typedef void (*WORKER_TimerCb_t)(int wid, uint32_t tickNow);
typedef void (*WORKER_TimerInitCb_t)(int wid, uint32_t tickNow);

typedef struct WORKER_Work {
    int type;

    union {
        struct {
            uint32_t         internalTick; // tick间隔
            WORKER_TimerCb_t timerCb;
            WORKER_TimerInitCb_t initCb;
        } timerWork;
        WORKER_FixWorkCb_t fixWork;
    } task;

    WORKER_BitMap_t     map; // wid绑定
    struct WORKER_Work* next;
} WORKER_Work_t;

/**
 * @brief 有协议实现者在初始化时实现work注册
 *
 * @param work
 */
void WORKER_AddWork(WORKER_Work_t* work);

void WORKER_ClearWork(void);

/**
 * @brief 完成实例内存申请，在调用之前保证定时器任务、常驻任务均以通过WORKER_AddWork注册进来
 *
 * @return
 */
int WORKER_Init(void);

void WORKER_Deinit(void);

uint32_t WORKER_GetTime(void);

uint32_t WORKER_GetTick(int wid);

/**
 * @brief ms转换为worker 计时的tick，不足一个tick按照1个tick计算
 *
 * @param time
 * @return
 */
#define WORKER_TIME2_TICK(time) (((time) + WORKER_MS_PER_TICK - 1) / WORKER_MS_PER_TICK)

/**
 * @brief worker运行，独占线程场景下，使用此接口，由适配者适配到不同的线程启动接口，调用一次即可一直运行
 *
 * @param wid 实例ID
 */
void DP_RunWorker(int wid);

/**
 * @brief 唤醒worker信号量
 *
 * @param wid 实例ID
 */
void DP_WakeupWorker(int wid);

uint32_t WORKER_GetSelfId(void);

void WORKER_DeregGetWorkerId(void);

void DP_StopWorker(int wid);

#ifdef __cplusplus
}
#endif
#endif
