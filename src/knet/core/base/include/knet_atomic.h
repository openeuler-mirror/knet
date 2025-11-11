/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 原子操作相关接口
 */

#ifndef __KNET_ATOMIC_H__
#define __KNET_ATOMIC_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @ingroup knet_atomic
 * @brief 64位原子结构体
 */
typedef struct {
    volatile uint64_t count; /**< 操作值 */
} KNET_ATOMIC64_T;

int KNET_HalAtomicTestSet64(KNET_ATOMIC64_T *value);

uint64_t KNET_HalAtomicRead64(KNET_ATOMIC64_T *value);

/**
 * @ingroup knet_atomic
 * @brief 64位原子写
 *
 * @par 描述: 64位原子写，将new_value的值赋给value结构体的count。
 *
 * @attention 用户保证value的有效性。
 *
 * @param    value   [IN] 参数类型 #KNET_ATOMIC64_T *。需要赋值的内存地址，该参数同时也是返回值。
 * @param    new_value     [IN] 参数类型 #uint64_t。需要赋的值。
 *
 * @retval 无
 *
 * @par 依赖
 * knet_hal：该接口所属的开发包。
 * knet_atomic.h：该接口声明所在的头文件。
 * @see 无
*/
void KNET_HalAtomicSet64(KNET_ATOMIC64_T *value, uint64_t newVal);

/**
 * @ingroup knet_atomic
 * @brief 64位原子加
 *
 * @par 描述: 64位原子加，将incVal原子加到value结构体的count中。
 *
 * @attention 用户保证value的有效性。
 *
 * @param    value   [IN] 参数类型 #KNET_ATOMIC64_T *。被加数。
 * @param    incVal   [IN] 参数类型 #uint64_t。加数。
 *
 * @retval 无
 *
 * @par 依赖
 * knet_hal：该接口所属的开发包。
 * knet_atomic.h：该接口声明所在的头文件。
 * @see 无
*/
void KNET_HalAtomicAdd64(KNET_ATOMIC64_T *value, uint64_t incVal);

/**
 * @ingroup knet_atomic
 * @brief 64位原子减
 *
 * @par 描述: 64位原子减，将value结构体的count减去decVal。
 *
 * @attention 用户保证value的有效性。
 *
 * @param    value   [IN] 参数类型 #KNET_ATOMIC64_T *。被减数，该参数同时也是返回值。
 * @param    decVal    [IN] 参数类型 #uint64_t。减数。
 *
 * @retval 无
 *
 * @par 依赖
 * knet_hal：该接口所属的开发包。
 * knet_atomic.h：该接口声明所在的头文件。
 * @see 无
*/
void KNET_HalAtomicSub64(KNET_ATOMIC64_T *value, uint64_t decVal);

#ifdef __cplusplus
}
#endif /* __cpluscplus */

#endif /* __KNET_ATOMIC_H__ */
