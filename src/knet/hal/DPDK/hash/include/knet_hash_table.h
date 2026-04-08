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

#ifndef KNET_HASH_TABLE_H
#define KNET_HASH_TABLE_H

#include "knet_types.h"
#include "knet_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t (*KNET_HASH_FUNC)(uint8_t *key, uint32_t keyLen);

typedef struct tagHashTblCfg {
    KNET_HASH_FUNC hashFunc;        /**< hash表hash函数，如果不配置则使用系统默认 */
    uint32_t keySize;               /**< 散列键值长度 */
    uint32_t entrySize;             /**< 表项数据长度 */
    uint32_t entryNum;              /**< hash表项数量 */
    uint32_t flag;                  /**< 附加功能选项 */ /* 当前版本不使用默认0 */
    /**< updateFreq和delayTime延时删除方案专用，其他方案不响应 */
    uint64_t delayTime;             /**< 延迟删除等待时间，单位us, 默认10ms */ /* 当前版本不使用 */
    uint32_t updateFreq;             /**< 每秒更新的表项数 */ /* 当前版本不使用 */
    uint32_t bucketNum;             /**< HASH桶数量，该参数的调整将会影响性能，
                                         flag参数设置了UFP_TBM_HASH_BUCKET_NUM_SET该值生效。
                                         当flag参数设置了UFP_TBM_HASH_BUCKET_NUM_SET且该值为0时默认为uiEntryNum的2倍，
                                         出于性能考虑内部HASH桶真实数为2的幂 */
    uint32_t uiRsvd;
    char padding[4];   // 填充字节，确保结构体8 字节对齐
} KNET_HashTblCfg;

typedef struct tagHashTblInfo {
    KNET_HASH_FUNC pfHashFunc;   /**< hash表hash函数 */
    uint32_t tableId;           /**< 表单ID         */
    uint32_t maxEntryNum;         /**< 最大表项数     */
    uint32_t currEntryNum;        /**< 实际表项数     */
    uint32_t keySize;             /**< 散列键值长度   */
    uint32_t entrySize;           /**< 表项数据长度   */
    char padding[4];   // 填充字节，确保结构体8 字节对齐
} KNET_HashTblInfo;

/**
 * @brief 初始化进程的Hash表资源
 *
 * @param
 * @return int 初始化进程的Hash表资源的结果，成功返回0，失败返回-1。
 */
int KNET_HashTblInit(void);

/**
 * @brief 创建一个Hash表
 *
 * @param cfg [IN] 参数类型KNET_HashTblCfg*。创建Hash表的信息结构体。
 * @param tableId [OUT] 参数类型uint32_t*。创建的Hash表的ID号
 * @return int 创建一个Hash表的结果，成功返回0，失败返回-1。
 */
int KNET_CreateHashTbl(KNET_HashTblCfg *cfg, uint32_t *tableId);

/**
 * @brief 销毁一个Hash表
 *
 * @param tableId [IN] 参数类型uint32_t。Hash表的ID号。
 * @return int 销毁一个Hash表的结果，成功返回0，失败返回-1。
 */
int KNET_DestroyHashTbl(uint32_t tableId);

/**
 * @brief 增加Hash表的表项
 *
 * @param tableId [IN] 参数类型uint32_t。Hash表的ID号。
 * @param key [IN] 参数类型uint8_t*。需要增加表项的键。
 * @param data [IN] 参数类型uint8_t*。需要增加表项的值。
 * @return int 增加Hash表的表项的结果，成功返回0，失败返回-1。
 */
int KNET_HashTblAddEntry(uint32_t tableId, const uint8_t *key, const uint8_t *data);

/**
 * @brief 删除Hash表的表项
 *
 * @param tableId [IN] 参数类型uint32_t。Hash表的ID号。
 * @param key [IN] 参数类型uint8_t*。需要删除表项的键。
 * @return int 删除Hash表的表项的结果，成功返回0，失败返回-1。
 */
int KNET_HashTblDelEntry(uint32_t tableId, const uint8_t *key);

/**
 * @brief 修改Hash表的表项
 *
 * @param tableId [IN] 参数类型uint32_t。Hash表的ID号。
 * @param key [IN] 参数类型uint8_t*。需要修改表项的键。
 * @param data [IN] 参数类型uint8_t*。需要修改表项的值。
 * @return int 修改Hash表的表项的结果，成功返回0，失败返回-1。
 */
int KNET_HashTblModifyEntry(uint32_t tableId, const uint8_t *key, const uint8_t *data);

/**
 * @brief 查找Hash表的表项
 *
 * @param tableId [IN] 参数类型uint32_t。Hash表的ID号。
 * @param key [IN] 参数类型uint8_t*。需要查找表项的键。
 * @param data [OUT] 参数类型uint8_t*。需要查找表项的值。
 * @return int 查找Hash表的表项的结果，成功返回0，失败返回-1。
 */
int KNET_HashTblLookupEntry(uint32_t tableId, const uint8_t *key, uint8_t *data);

/**
 * @brief 获取Hash表的信息
 *
 * @param tableId [IN] 参数类型uint32_t。Hash表的ID号。
 * @param info [OUT] 参数类型KNET_HashTblInfo*。获取的Hash表信息。
 * @return int 获取Hash表信息的结果，成功返回0，失败返回-1。
 */
int KNET_GetHashTblInfo(uint32_t tableId, KNET_HashTblInfo *info);

/**
 * @brief 获取Hash表的第一个表项
 *
 * @param tableId [IN] 参数类型uint32_t。Hash表的ID号。
 * @param key [OUT] 参数类型uint8_t*。Hash表的第一个表项的键。
 * @param data [OUT] 参数类型uint8_t*。Hash表的第一个表项的值。
 * @return int 获取Hash表的第一个表项的结果，成功返回0，失败返回-1。
 */
int KNET_GetHashTblFirstEntry(uint32_t tableId, uint8_t *key, uint8_t *data);

/**
 * @brief 获取Hash表的下一个表项
 *
 * @param tableId [IN] 参数类型uint32_t。Hash表的ID号。
 * @param curKey [IN] 参数类型uint32_t。当前表项的键。
 * @param key [OUT] 参数类型uint8_t*。Hash表的下一个表项的键。
 * @param data [OUT] 参数类型uint8_t*。Hash表的下一个表项的值。
 * @return int 获取Hash表的下一个表项的结果，成功返回0，失败返回-1。
 */
int KNET_GetHashTblNextEntry(uint32_t tableId, const uint8_t *curKey, uint8_t *key, uint8_t *data);

/**
 * @brief 释放Hash表的资源（父进程退出之后才执行）
 *
 */
void KNET_HashTblDeinit(void);

/**
 * @brief Hash模块的RPC服务初始化
 *
 * @param procType [IN] 参数类型enum KNET_ProcType。进程类型。
 * @return int Hash模块的RPC服务初始化结果。成功返回0，失败返回-1。
 */
int KNET_InitHash(enum KNET_ProcType procType);

/**
 * @brief Hash模块的RPC服务注销
 *
 * @param procType [IN] 参数类型enum KNET_ProcType。进程类型。
 * @return int Hash模块的RPC服务注销结果。成功返回0，失败返回-1。
 */
int KNET_UninitHash(enum KNET_ProcType procType);

#ifdef __cplusplus
}
#endif
#endif // KNET_HASH_TABLE_H