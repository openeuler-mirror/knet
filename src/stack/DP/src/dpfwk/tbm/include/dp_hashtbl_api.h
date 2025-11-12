/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
* Description: 定义需要适配哈希表项操作接口
 */

#ifndef DP_HASHTBL_API_H
#define DP_HASHTBL_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup hash 哈希表操作接口
 * @ingroup tbm
 */

/**
 * @ingroup hash
 * 哈希表项ID类型定义
 */
typedef void *DP_HashTbl_t;

/**
 * @ingroup hash
 * hash函数钩子
 */
typedef uint32_t (*DP_HashTblHashHook_t)(uint8_t *key, uint32_t keyLen);

/**
 * @ingroup hash
 * HASH表项创建初始化结构
 */
typedef struct {
    DP_HashTblHashHook_t hashFunc;  /**< hash表hash函数，如果不配置则使用系统默认 */
    uint32_t keySize;               /**< 散列键值长度 */
    uint32_t entrySize;             /**< 表项数据长度 */
    uint32_t entryNum;              /**< hash表项数量 */
    uint32_t flag;                  /**< 附加功能选项 */
    /**< u32UpdateFreq和u64DelayTime延时删除方案专用，其他方案不响应 */
    uint32_t updateFreq;            /**< 每秒更新的表项数 */
    uint64_t delayTime;             /**< 延迟删除等待时间，单位us, 默认10ms */
} DP_HashTblCfg_t;

/**
 * @ingroup hash
 * HASH表项信息结构
 */
typedef struct {
    DP_HashTblHashHook_t hashFunc; /**< hash表hash函数 */
    DP_HashTbl_t tableId;          /**< 表单ID         */
    uint32_t  maxEntryNum;         /**< 最大表项数     */
    uint32_t  currEntryNum;        /**< 实际表项数     */
    uint32_t  keySize;             /**< 散列键值长度   */
    uint32_t  entrySize;           /**< 表项数据长度   */
    uint32_t  rsvd;                /**< 保留字         */
} DP_HashTblSummaryInfo_t;

/**
 * @ingroup hash
 * @brief 创建Hash表项
 *
 * @par 描述: 创建Hash表项
 * @attention
 * NA
 *
 * @param hashTblCfg [IN]  Hash表项配置
 * @param tableId [OUT]  Hash表项ID
 *
 * @retval 0 成功
 * @retval 非0 失败 返回错误码

 * @see DP_HashTblCfg_t | DP_HashTbl_t
 */
typedef int (*DP_HashTblCreateHook_t)(DP_HashTblCfg_t *hashTblCfg, DP_HashTbl_t *tableId);

/**
 * @ingroup hash
 * @brief 销毁Hash表项
 *
 * @par 描述: 销毁Hash表项
 * @attention
 * NA
 *
 * @param tableId [IN]  Hash表项ID
 *
 * @retval 0 成功
 * @retval 非0 失败 返回错误码

 * @see DP_HashTbl_t
 */
typedef int (*DP_HashTblDestroyHook_t)(DP_HashTbl_t tableId);

/**
 * @ingroup hash
 * @brief 添加Hash表项
 *
 * @par 描述: 添加Hash表项
 * @attention
 * NA
 *
 * @param tableId [IN]  Hash表项ID
 * @param key [IN]  Hash表项Key
 * @param data [IN]  Hash表项内容
 *
 * @retval 0 成功
 * @retval 非0 失败 返回错误码

 * @see DP_HashTbl_t
 */
typedef int (*DP_HashTblInsertEntryHook_t)(DP_HashTbl_t tableId, const uint8_t *key, const void *data);

/**
 * @ingroup hash
 * @brief 修改Hash表项
 *
 * @par 描述: 修改Hash表项
 * @attention
 * NA
 *
 * @param tableId [IN]  Hash表项ID
 * @param key [IN]  Hash表项Key
 * @param data [IN]  Hash表项内容
 *
 * @retval 0 成功
 * @retval 非0 失败 返回错误码

 * @see DP_HashTbl_t
 */
typedef int (*DP_HashTblModifyEntryHook_t)(DP_HashTbl_t tableId, const uint8_t *key, const void *data);

/**
 * @ingroup hash
 * @brief 基于key值删除表项信息
 *
 * @par 描述: 基于key值删除表项信息
 * @attention
 * NA
 *
 * @param tableId [IN]  Hash表项ID
 * @param key [IN]  Hash表项Key
 *
 * @retval 0 成功
 * @retval 非0 失败 返回错误码

 * @see DP_HashTbl_t
 */
typedef int (*DP_HashTblDeleteEntryHook_t)(DP_HashTbl_t tableId, const uint8_t *key);

/**
 * @ingroup hash
 * @brief 基于key值查找表项信息
 *
 * @par 描述: 基于key值查找表项信息
 * @attention
 * NA
 *
 * @param tableId [IN]  Hash表项ID
 * @param key [IN]  Hash表项Key
 * @param data [OUT]  Hash表项内容
 *
 * @retval 0 成功
 * @retval 非0 失败 返回错误码

 * @see DP_HashTbl_t
 */
typedef int (*DP_HashTblLookupEntryHook_t)(DP_HashTbl_t tableId, const uint8_t *key, void *data);

/**
 * @ingroup hash
 * @brief 获取Hash表项信息
 *
 * @par 描述: 获取Hash表项信息
 * @attention
 * NA
 *
 * @param tableId [IN]  Hash表项ID
 * @param pstHashSummaryInfo [OUT]  Hash表项信息
 *
 * @retval 0 成功
 * @retval 非0 失败 返回错误码

 * @see DP_HashTbl_t | DP_HashTblSummaryInfo
 */
typedef int (*DP_HashTblGetInfoHook_t)(DP_HashTbl_t tableId, DP_HashTblSummaryInfo_t *hashTblSummaryInfo);

/**
 * @ingroup hash
 * @brief 获取第一个Hash表项
 *
 * @par 描述: 获取第一个Hash表项
 * @attention
 * NA
 *
 * @param tableId [IN]  Hash表项ID
 * @param key [OUT]  Hash表项Key
 * @param data [OUT]  Hash表项内容
 *
 * @retval 0 成功
 * @retval 非0 失败 返回错误码

 * @see DP_HashTbl_t
 */
typedef int (*DP_HashTblEntryGetFirst_t)(DP_HashTbl_t tableId, uint8_t *key, void *data);

/**
 * @ingroup hash
 * @brief 获取下一个Hash表项
 *
 * @par 描述: 获取下一个Hash表项
 * @attention
 * NA
 *
 * @param tableId [IN]  Hash表项ID
 * @param key [IN]  Hash表项Key
 * @param data [OUT]  Hash表项内容
 * @param nextKey [OUT]  下一个Hash表项Key
 *
 * @retval 0 成功
 * @retval 非0 失败 返回错误码

 * @see DP_HashTbl_t
 */
typedef int (*DP_HashTblEntryGetNextHook_t)(DP_HashTbl_t tableId, uint8_t *key, void *data, uint8_t *nextKey);

/**
 * @ingroup hash
 * HASHTBL操作集
 */
typedef struct {
    DP_HashTblCreateHook_t createTable;
    DP_HashTblDestroyHook_t destroyTable;

    DP_HashTblInsertEntryHook_t insertEntry;
    DP_HashTblModifyEntryHook_t  modifyEntry;
    DP_HashTblDeleteEntryHook_t  delEntry;

    DP_HashTblLookupEntryHook_t lookupEntry;
    DP_HashTblGetInfoHook_t getInfo;
    DP_HashTblEntryGetFirst_t hashtblEntryGetFirst;
    DP_HashTblEntryGetNextHook_t hashtblEntryGetNext;
} DP_HashTblHooks_t;

/**
 * @ingroup hash
 * @brief Hash表项操作接口注册函数
 *
 * @par 描述: Hash表项操作接口注册函数
 * @attention
 * 必须在初始化前进行注册
 *
 * @param pstHooks [IN]  Hash表项操作集<非NULL>
 *
 * @retval 0 成功
 * @retval #错误码 失败

 * @see DP_HashTblHooks_t
 */
int DP_HashTblHooksReg(DP_HashTblHooks_t *hashTblHooks);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif // DP_HASHTBL_API_H
