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

#ifndef __KNET_TYPES_H__
#define __KNET_TYPES_H__

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>
#include <inttypes.h>
#include <securec.h>

#ifndef __USE_GNU
typedef void (*sighandler_t)(int);
#endif

enum MAC_PARM {
    MAC_FIRST = 0,
    MAC_SECOND,
    MAC_THIRD,
    MAC_FOURTH,
    MAC_FIFTH,
    MAC_SIXTH,
};

#define KNET_API __attribute__((visibility("default")))

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define MAX_QUEUE_NUM 128

#define M_SEC_2_N_SEC 1000000
#define SEC_2_M_SEC 1000

#define MAC_SCAN_FMT      "%" SCNx8 ":%" SCNx8 ":%" SCNx8 ":%" SCNx8 ":%" SCNx8 ":%" SCNx8
#define MAC_SCAN_ARGS(EA) &(EA)[MAC_FIRST], &(EA)[MAC_SECOND], \
    &(EA)[MAC_THIRD], &(EA)[MAC_FOURTH], &(EA)[MAC_FIFTH], &(EA)[MAC_SIXTH]

/**
 * @brief 解析MAC地址，转换为可用于网络通信的格式
 *
 * @param mac [IN] 参数类型 const char*。指向被解析的MAC地址的指针
 * @param outputMac [IN/OUT] 参数类型 uint8_t*。若解析成功，指向存储解析后MAC地址的指针
 * @return int 0：成功；-1：失败
 */
int KNET_ParseMac(const char *mac, uint8_t *outputMac);

/**
* @ingroup knet_types
* @brief cacheline长度(字节)
*/
#ifndef KNET_MBUF_CACHE_LINE_SIZE
#ifdef __aarch64__
#define KNET_MBUF_CACHE_LINE_SIZE (128)
#else
#define KNET_MBUF_CACHE_LINE_SIZE (64)
#endif
#endif

/**
* @ingroup knet_types
* @brief 成功
*/
#define KNET_OK (0)
/**
* @ingroup knet_types
* @brief 失败
*/
#define KNET_ERROR (0xFFFFFFFF)

/**
* @ingroup knet_types
* @brief unlikely修饰符定义
*/
#define KNET_UNLIKELY(x) __builtin_expect((x), 0)

/**
* @ingroup knet_types
* @brief always_inline修饰符定义
*/
#define KNET_ALWAYS_INLINE inline __attribute__((always_inline))

/**
* @ingroup knet_types
* @brief 获取最小值
*/
#define KNET_MIN(a, b) (((a) < (b)) ? (a) : (b))

/**
* @ingroup knet_types
* @brief 获取最大值
*/
#define KNET_MAX(a, b) (((a) > (b)) ? (a) : (b))

/**
* @brief     指针ptr加x个字节偏移
* @param     ptr [IN] 参数类型 void*。指向被偏移的指针
* @param     x [IN] 参数类型 uintptr_t。需要偏移的字节数
*/
static KNET_ALWAYS_INLINE void *KnetPtrAdd(void *ptr, uintptr_t x)
{
    return ((void*)((uintptr_t)(ptr) + (uintptr_t)(x)));
}

/**
* @brief     指针ptr减x个字节偏移
* @param     ptr [IN] 参数类型 void*。指向被偏移的指针
* @param     x [IN] 参数类型 uintptr_t。需要偏移的字节数
*/
static KNET_ALWAYS_INLINE void *KnetPtrSub(void *ptr, uintptr_t x)
{
    return ((void*)((uintptr_t)(ptr) - (uintptr_t)(x)));
}

#endif // __KNET_TYPES_H__