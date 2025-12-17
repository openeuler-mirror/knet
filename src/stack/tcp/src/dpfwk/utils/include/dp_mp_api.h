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
/**
 * @file dp_mp_api.h
 * @brief 内存池操作钩子函数注册相关
 */

#ifndef DP_MEMPOOL_API_H
#define DP_MEMPOOL_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup dp_mp 内存池 */

/**
 * @ingroup dp_mp
 * @brief 内存池handle
 */
typedef void* DP_Mempool;     /**< 内存池handle */

/**
 * @ingroup dp_mp
 * 内存池属性handle
 */
typedef void* DP_MempoolAttr_S; /**< 内存池属性，预留参数 */

/**
 * @ingroup dp_mp
 * 内存池类型
 */
enum {
    DP_MEMPOOL_TYPE_MIN = 0,
    DP_MEMPOOL_TYPE_PBUF,         /**< 内存池MBUF类型 */
    DP_MEMPOOL_TYPE_FIXED_MEM,    /**< 内存池固定内存长度类型 */
    DP_MEMPOOL_TYPE_EBUF,         /**< 内存池EBUF PBUF类型，用于零拷贝 */
    DP_MEMPOOL_TYPE_REF_PBUF,     /**< 内存池REF PBUF类型，用于零拷贝 */
    DP_MEMPOOL_TYPE_MAX
};

/**
 * @ingroup dp_mp
 * 内存池配置
 */
typedef struct {
    char *name;        /**< 内存池名字 */
    uint32_t size;     /**< 内存单元长度 */
    uint32_t count;    /**< 内存单元数目 */
    uint32_t type;     /**< 创建选项, MBUF或定长内存池 */
} DP_MempoolCfg_S;

/**
 * @ingroup dp_mp
 * @brief 内存池申请接口
 *
 * @par 描述: 内存池申请接口
 * @attention
 * NA
 *
 * @param[IN] cfg: 内存池配置
 * @param[IN] attr: 内存池属性 预留参数, 默认传空指针
 * @param[OUT] handler: 内存池handler
 *
 * @retval 0 成功
 * @retval 其他值 失败

 * @see DP_MempoolDestroyHook | DP_MempoolAllocHook | DP_MempoolFreeHook
 */
typedef int32_t (*DP_MempoolCreateHook)(const DP_MempoolCfg_S* cfg,
                                          const DP_MempoolAttr_S* attr,
                                          DP_Mempool* handler);

/**
 * @ingroup dp_mp
 * @brief 内存池销毁接口
 *
 * @par 描述: 内存池销毁接口
 * @attention
 * NA
 *
 * @param mp [IN]  内存池handler
 *
 * @retval NA 无返回值

 * @see DP_MempoolCreateHook | DP_MempoolAllocHook | DP_MempoolFreeHook
 */
typedef void (*DP_MempoolDestroyHook)(DP_Mempool mp);

/**
 * @ingroup dp_mp
 * @brief 申请一个内存单元
 *
 * @par 描述: 申请一个内存单元
 * @attention
 * NA
 *
 * @param mp [IN]  内存池handler
 *
 * @retval 非NULL 申请成功
 * @retval NULL 申请失败

 * @see DP_MempoolCreateHook | DP_MempoolDestroyHook | DP_MempoolFreeHook
 */
typedef void* (*DP_MempoolAllocHook)(DP_Mempool mp);

/**
 * @ingroup dp_mp
 * @brief 释放一个内存单元
 *
 * @par 描述: 释放一个内存单元
 * @attention
 * NA
 *
 * @param mp [IN]  内存池handler
 * @param ptr [IN] 内存单元指针
 *
 * @retval NA 无返回值

 * @see DP_MempoolCreateHook | DP_MempoolDestroyHook | DP_MempoolAllocHook
 */
typedef void (*DP_MempoolFreeHook)(DP_Mempool mp, void* ptr);

/**
 * @ingroup dp_mp
 * @brief 根据指定的一段内存来构造一个内存单元
 *
 * @par 描述: 根据指定的一段内存来构造一个内存单元
 * @attention
 * NA
 *
 * @param mp [IN]  内存池handler
 * @param addr [IN] 内存起始虚拟地址
 * @param offset [IN] 偏移量
 * @param len [IN] 内存长度
 *
 * @retval 非NULL 构造成功
 * @retval NULL 构造失败
 */
typedef void* (*DP_MempoolConstructHook)(DP_Mempool mp, void* addr, uint64_t offset, uint16_t len);

/**
 * @ingroup dp_mp
 * 内存池操作集
 */
typedef struct {
    DP_MempoolCreateHook  mpCreate;       /**< 内存池申请接口, 必须接口 */
    DP_MempoolDestroyHook mpDestroy;      /**< 内存池销毁接口, 非必须接口 */
    DP_MempoolAllocHook   mpAlloc;        /**< 申请一个内存单元, 必须接口 */
    DP_MempoolFreeHook    mpFree;         /**< 释放一个内存单元, 必须接口 */
    DP_MempoolConstructHook  mpConstruct; /**< 根据外部缓冲区构造一个内存单元，零拷贝所需接口 */
} DP_MempoolHooks_S;

/**
 * @ingroup dp_mp
 * @brief 内存池操作接口注册函数
 *
 * @par 描述: 内存池操作接口注册函数
 * @attention
 * 必须在DP协议栈初始化前进行注册，不允许重复注册
 *
 * @param pstHooks [IN]  内存池操作集<非NULL>
 *
 * @retval 0 成功
 * @retval 其他值 失败

 * @see DP_MempoolHooks_S
 */
uint32_t DP_MempoolHookReg(DP_MempoolHooks_S* pHooks);

#ifdef __cplusplus
}
#endif

#endif
