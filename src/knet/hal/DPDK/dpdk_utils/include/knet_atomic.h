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

/**
 * @brief 64位原子写，测试并设置value结构体的值.如果变量之前的值是0，则将其设置为1，并返回1，表示调用线程成功“获取”该变量；如果变量之前的值不是0，则不改变它的值，并返回0，表示调用线程“未能获取”该变量
 *
 * @param value [IN] 参数类型 KNET_ATOMIC64_T*。被测试数的内存地址
 * @return int 成功：1；失败：0
 */
int KNET_HalAtomicTestSet64(KNET_ATOMIC64_T *value);

/**
 * @brief 64位原子读，读取value结构体中count的值
 *
 * @param value [IN] 参数类型 KNET_ATOMIC64_T*。被读取数的内存地址
 * @return uint64_t value结构体中count的值
 */
uint64_t KNET_HalAtomicRead64(KNET_ATOMIC64_T *value);

/**
 * @brief 64位原子写，将newVal的值赋给value结构体的count
 *
 * @attention 用户保证value的有效性
 *
 * @param value [IN/OUT] 参数类型 #KNET_ATOMIC64_T*。被赋值数的内存地址
 * @param newVal [IN] 参数类型 #uint64_t。需要赋的值
 */
void KNET_HalAtomicSet64(KNET_ATOMIC64_T *value, uint64_t newVal);

/**
 * @brief 64位原子加，将incVal的值加到value结构体的count
 *
 * @attention 用户保证value的有效性
 *
 * @param value [IN/OUT] 参数类型 #KNET_ATOMIC64_T*。被加数的内存地址
 * @param newVal [IN] 参数类型 #uint64_t。需要加的值
 */
void KNET_HalAtomicAdd64(KNET_ATOMIC64_T *value, uint64_t incVal);

/**
 * @brief 64位原子减，将value结构体的count减去decVal
 *
 * @attention 用户保证value的有效性
 *
 * @param value [IN/OUT] 参数类型 #KNET_ATOMIC64_T*。被减数的内存地址
 * @param newVal [IN] 参数类型 #uint64_t。需要减的值
 */
void KNET_HalAtomicSub64(KNET_ATOMIC64_T *value, uint64_t decVal);

#ifdef __cplusplus
}
#endif /* __cpluscplus */

#endif /* __KNET_ATOMIC_H__ */
