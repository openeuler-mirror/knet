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

#ifndef K_NET_KNET_THREAD_H
#define K_NET_KNET_THREAD_H

#include "knet_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 线程创建
 *
 * @param thread [OUT] 参数类型 uint64_t*。新创建线程的 ID
 * @param func [IN] 参数类型 void*。线程启动函数的指针
 * @param arg [IN] 参数类型 void*。传递给线程启动函数 func 的参数
 * @return int32_t 成功时返回0；失败时返回负数
 */
int32_t KNET_CreateThread(uint64_t *thread, void *(* func) (void *), void *arg);

/**
 * @brief 通过设置线程的cpu亲和性将线程限制在指定cpu上运行
 *
 * @param threadId [IN] 参数类型 uint64_t。线程id
 * @param cpus [IN] 参数类型 const uint16_t*。数组，指定线程运行的cpu核心id
 * @param len [IN] 参数类型 uint32_t。cpu数组的长度
 * @return int32_t 成功时返回0；失败时返回负数
 */
int32_t KNET_SetThreadAffinity(uint64_t threadId, const uint16_t *cpus, uint32_t len);

/**
 * @brief 获取线程的cpu亲和性
 *
 * @param threadId [IN] 参数类型 uint64_t。线程id
 * @param cpus [IN/OUT] 参数类型 const uint16_t*。数组，获取到的cpu核心id
 * @param len [IN/OUT] 参数类型 uint32_t。cpu数组的长度
 * @return int32_t 成功时返回0；失败时返回负数
 */
int32_t KNET_GetThreadAffinity(uint64_t threadId, uint16_t *cpus, uint32_t *len);

/**
 * @brief 设置线程名称
 *
 * @param threadId [IN] 参数类型 uint64_t。线程id
 * @param name [IN] 参数类型 const char*。线程名称的字符串
 * @return int32_t 成功时返回0；失败时返回错误码
 */
int32_t KNET_ThreadNameSet(uint64_t threadId, const char *name);

/**
 * @brief 线程终止
 *
 * @param threadId [IN] 参数类型 uint64_t。线程id
 * @param ret [IN/OUT] 参数类型 void**。接收线程的返回值
 * @return int32_t 成功时返回0；失败时返回错误码
 */
int32_t KNET_JoinThread(uint64_t threadId, void **ret);

/**
 * @brief 获取当前线程id
 *
 * @return uint64_t 当前线程的id
 */
uint64_t KNET_ThreadId(void);

#ifdef __cplusplus
}
#endif
#endif // K_NET_KNET_THREAD_H
