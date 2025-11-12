/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 实例管理对外接口
 */

#ifndef DP_WORKER_API_H
#define DP_WORKER_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup worker 实例管理调度 */

/**
 * @ingroup worker
 * @brief 获取workerId钩子
 *
 * @par 描述: 获取workerId钩子
 * @attention
 * 1.在DP_RunWorkerOnce内部会调用此函数注册的接口，即有多个线程会并发调用;
 * 2.workerId与线程的关系应该是固定的，不应变化
 *
 * @retval -1 失败，返回-1表示当前代码没有运行
 * @retval 非负数 workerId

 * @see DP_RegGetSelfWorkerIdHook
 */
typedef int (*DP_WorkerGetSelfIdHook)(void);
 
/**
 * @ingroup worker
 * @brief 注册获取workerId钩子
 *
 * @par 描述: 注册获取workerId钩子
 * @attention
 * NA
 *
 * @param getSelf [IN]  钩子函数<非空指针>
 * @retval NA

 * @see DP_RunWorkerOnce
 */
int DP_RegGetSelfWorkerIdHook(DP_WorkerGetSelfIdHook getSelf);

/**
 * @ingroup worker
 * @brief worker运行一次，独占线程场景下，使用此接口，由适配者适配到不同的线程启动接口
 *
 * @par 描述: 注册获取workerId钩子
 * @attention
 * NA
 *
 * @param wid 实例ID
 * @retval NA

 * @see DP_RunWorkerOnce
 */
void DP_RunWorkerOnce(int wid);

#ifdef __cplusplus
}
#endif
#endif
