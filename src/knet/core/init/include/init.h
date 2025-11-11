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

#ifndef __INIT_H__
#define __INIT_H__

#include "knet_types.h"
#include "knet_lock.h"

/**
 * @brief 最大rt长度
 */
#define MAX_RT_ATTR_LEN 32

/**
 * @ingroup init
 * @brief 控制面参数结构体
 */
typedef struct {
    uint32_t ctrlVcpuID; // 控制面绑核核号
    char padding[4];   // 填充字节，确保结构体8 字节对齐
} CtrlThreadArgs;

typedef struct {
    KNET_SpinLock lock;
    uint64_t threadID;
    bool isCreated;
} KnetThreadInfo;

void KNET_ConfigInit(void);
int KNET_TrafficResourcesInit(void);
void KNET_Uninit(void);
void KNET_SetDpdkAndStackThreadStop(void);
int KNET_JoinDpdkAndStackThread(void);

/**
 * @brief 对控制面和数据面线程加锁
 * @note 用于解决fork锁继承问题
 */
void KNET_AllThreadLock(void);
void KNET_AllThreadUnlock(void);

#endif