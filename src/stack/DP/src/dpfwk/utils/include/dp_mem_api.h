/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 内存处理钩子函数注册相关
 */

#ifndef DP_MEM_API_H
#define DP_MEM_API_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup dp_mem 变长内存 */

/**
 * @ingroup dp_mem
 * @brief 内存申请接口
 *
 * @par 描述: 不定长内存申请接口
 * @attention
 * NA
 *
 * @param uiSize [IN]  分配大小
 *
 * @retval 成功 内存地址
 * @retval 失败 NULL

 * @see DP_MemFreeHook
 */
typedef void* (*DP_MemAllocHook)(size_t size);

/**
 * @ingroup dp_mem
 * @brief 内存释放接口
 *
 * @par 描述: 不定长内存释放接口
 * @attention
 * NA
 *
 * @param ptr [IN]  内存地址
 *
 * @retval NA 无返回值

 * @see DP_MemAllocHook
 */
typedef void (*DP_MemFreeHook)(void *ptr);

/**
 * @ingroup dp_mem
 * 变长内存操作集
 */
typedef struct DP_MemHooks {
    DP_MemAllocHook mAlloc;
    DP_MemFreeHook  mFree;
} DP_MemHooks_S;

/**
 * @ingroup dp_mem
 * @brief 变长内存操作接口注册函数
 *
 * @par 描述: 变长内存操作接口注册函数
 * @attention
 * 必须在初始化前进行注册
 *
 * @param pstHooks [IN]  内存操作集<非NULL>
 *
 * @retval 0 成功
 * @retval #错误码 失败

 * @see DP_MemHooks_S
 */
uint32_t DP_MemHookReg(DP_MemHooks_S *memHooks);

#ifdef __cplusplus
}
#endif
#endif
