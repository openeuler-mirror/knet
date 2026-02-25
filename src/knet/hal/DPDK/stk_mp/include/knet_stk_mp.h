/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 无锁指针栈内存池，worker 间相互隔离
 */

#ifndef __KNET_STK_MP_H__
#define __KNET_STK_MP_H__

#include "knet_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KNET_STK_MP_POOLID 0
#define KNET_STK_MP_MAX_NUM 1

/**
 * @brief 初始化内存池，每个进程仅在第一次调用时完成初始化
 *
 * @param size 内存单元的大小
 * @param count 内存单元的数量
 * @return uint32_t 0 成功，非 0 失败
 */
uint32_t KNET_StkMpInit(const uint32_t size, const uint32_t count);

/**
 * @brief 释放内存池资源
 *
 * @return uint32_t 0 成功，非 0 失败
 */
uint32_t KNET_StkMpDeInit(void);
 
/**
  * @brief 申请内存单元，该函数对内存池的操作是无锁的，需确保不存在同个 worker 下的并发
  *
  * @return void* 非 NULL 成功，NULL 失败
  */
void *KNET_StkMpAlloc(void);

/**
 * @brief 释放内存单元
 *
 * @param buf 内存单元起始地址，该函数对内存池的操作是无锁的，需确保不存在同个 worker 下的并发
 */
void KNET_StkMpFree(void *buf);

#ifdef __cplusplus
}
#endif
#endif