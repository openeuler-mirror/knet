/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
* Description: 定义需要适配FIB4表项操作接口
 */

#ifndef DP_FIB4TBL_API_H
#define DP_FIB4TBL_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup fib4tbl FIB4表项操作接口
 * @ingroup tbm
 */

/**
 * @ingroup fib4tbl
 * FIB4表项ID类型定义
 */
typedef void *DP_Fib4Tbl_t;

/**
 * @ingroup fib4tbl
 * FIB4表项KEY值结构体
 */
typedef struct {
    uint32_t vpn;           /**< 表项VPN */
    uint32_t dip;           /**< 表项IP,主机序 */
    uint32_t pfxlen;        /**< 表项掩码 */
    uint32_t rsvd;          /**< 保留字 */
} DP_Fib4Key_t;

/**
 * @ingroup fib4tbl
 * FIB4表项创建初始化结构
 */
typedef struct {
    uint32_t entrySize;   /**< 表项数据长度 */
    uint32_t entryNum;    /**< 表项数量 */
    uint32_t vpnNum;      /**< fib表VPN数量 */
    uint32_t createType;  /**< 创建类型，16+8+8比8+8+8+8查询速度更快，但会耗费更多的内存 \n
                                u32CreateType为0:vpn(0): 16+8+8; vpn(i): 8+8+8+8  \n
                                u32CreateType为1:vpn(0): 16+8+8; vpn(i): 16+8+8   \n
                                u32CreateType为2:vpn(0): 8+8+8+8; vpn(i): 16+8+8  \n
                                u32CreateType为3:vpn(0): 8+8+8+8; vpn(i): 8+8+8+8 \n
                                不为以上四种情况时,默认:vpn(0): 16+8+8; vpn(i): 8+8+8+8 */
    uint32_t flag;        /**< 附加功能选项 */
    /**< updateFreq和delayTime延时删除方案专用，其他方案不响应 */
    uint32_t updateFreq;  /**< 每秒更新的表项数 */
    uint64_t delayTime;   /**< 延迟删除等待时间，单位us, 默认10ms */
} DP_Fib4TblCfg_t;

/**
 * @ingroup fib4tbl
 * FIB4表项信息结构
 */
typedef struct {
    DP_Fib4Tbl_t tableId;      /**< 表单handle    */
    uint32_t vpnNum;           /**< fib表VPN数量  */
    uint32_t maxEntryNum;      /**< 最大表项数    */
    uint32_t currEntryNum;     /**< 实际表项数    */
    uint32_t keySize;          /**< fib表键值长度 */
    uint32_t entrySize;        /**< 表项数据长度  */
} DP_Fib4SummaryInfo_t;

/**
 * @ingroup fib4tbl
 * FIB4表项查找方式
 */
typedef struct {
    uint32_t getType;       /**< 表项遍历方式:
                                uiGetType为0时表示，遍历所有vpn，从vpn=0开始
                                uiGetType为1时表示，遍历用户指定vpn，vpn值由本结构体uiVpn决定。 */
    uint32_t vpn;           /**< 表项VPN */
} DP_Fib4EntryGet_t;

/**
 * @ingroup fib4tbl
 * @brief 创建Fib4表项
 *
 * @par 描述: 创建Fib4表项
 * @attention
 * NA
 *
 * @param cfg [IN]  fib4表项配置
 * @param tableId [OUT]  fib4表项ID
 *
 * @retval 0 成功
 * @retval 非0 失败 返回错误码

 * @see DP_Fib4TblCfg_t | DP_Fib4Tbl_t
 */
typedef int (*DP_Fib4TblCreateHook_t)(DP_Fib4TblCfg_t *cfg, DP_Fib4Tbl_t *tableId);

/**
 * @ingroup fib4tbl
 * @brief 销毁fib4表
 *
 * @par 描述: 销毁fib4表
 * @attention
 * NA
 *
 * @param tableId [IN]  fib4表ID
 *
 * @retval 0 成功
 * @retval 非0 失败 返回错误码

 * @see DP_Fib4Tbl_t
 */
typedef int (*DP_Fib4TblDestroyHook_t)(DP_Fib4Tbl_t tableId);

/**
 * @ingroup fib4tbl
 * @brief 添加fib4表项
 *
 * @par 描述: 添加fib4表项
 * @attention
 * NA
 *
 * @param tableId [IN]  fib4表项ID
 * @param key [IN]  fib4 key
 * @param index [IN]  表项存储的位置
 *
 * @retval 0 成功
 * @retval 非0 失败 返回错误码

 * @see DP_Fib4Tbl_t | DP_Fib4Key_t
 */
typedef int (*DP_Fib4TblInsertEntryHook_t)(DP_Fib4Tbl_t tableId, DP_Fib4Key_t *key, uint32_t index);

/**
 * @ingroup fib4tbl
 * @brief 修改fib4表项
 *
 * @par 描述: 修改fib4表项
 * @attention
 * NA
 *
 * @param tableId [IN]  fib4表项ID
 * @param key [IN]  fib4 key
 * @param index [IN]  表项存储的位置
 *
 * @retval 0 成功
 * @retval 非0 失败 返回错误码

 * @see DP_Fib4Tbl_t | DP_Fib4Key_t
 */
typedef int (*DP_Fib4TblModifyEntryHook_t)(DP_Fib4Tbl_t tableId, DP_Fib4Key_t *key, uint32_t index);

/**
 * @ingroup fib4tbl
 * @brief 删除指定fib4表项
 *
 * @par 描述: 删除指定fib4表项
 * @attention
 * NA
 *
 * @param tableId [IN]  fib4表项ID
 * @param key [IN]  fib4 key
 * @param index [OUT]  表项存储的位置
 *
 * @retval 0 成功
 * @retval 非0 失败 返回错误码

 * @see DP_Fib4Tbl_t | DP_Fib4Key_t
 */
typedef int (*DP_Fib4TblDelEntryHook_t)(DP_Fib4Tbl_t tableId, DP_Fib4Key_t *key, uint32_t *index);

/**
 * @ingroup fib4tbl
 * @brief 根据fib4 Key查询表项，精确查找
 *
 * @par 描述: 根据fib4 Key查询表项，精确查找
 * @attention
 * NA
 *
 * @param tableId [IN]  fib4表项ID
 * @param key [IN]  fib4 key
 * @param index [OUT]  表项存储的位置
 *
 * @retval 0 成功
 * @retval 非0 失败 返回错误码

 * @see DP_Fib4Tbl_t | DP_Fib4Key_t
 */
typedef int (*DP_Fib4TblExactMatchHook_t)(DP_Fib4Tbl_t tableId, DP_Fib4Key_t *key, uint32_t *index);

/**
 * @ingroup fib4tbl
 * @brief 根据fib4 Key查询表项
 *
 * @par 描述: 根据fib4 Key查询表项，非精确查找(最长掩码匹配:longest prefix match)
 * @attention
 * NA
 *
 * @param tableId [IN]  fib4表项ID
 * @param key [IN]  fib4 key
 * @param index [OUT]  表项存储的位置
 *
 * @retval 0 成功
 * @retval 非0 失败 返回错误码

 * @see DP_Fib4Tbl_t | DP_Fib4Key_t
 */
typedef int (*DP_Fib4TblLpmEntryHook_t)(DP_Fib4Tbl_t tableId, DP_Fib4Key_t *key, uint32_t *index);

/**
 * @ingroup fib4tbl
 * @brief 获取fib4表项信息
 *
 * @par 描述: 获取fib4表项信息
 * @attention
 * NA
 *
 * @param tableId [IN]  fib4表项ID
 * @param summaryInfo [OUT]  fib4表项信息
 *
 * @retval 0 成功
 * @retval 非0 失败 返回错误码

 * @see DP_Fib4Tbl_t | DP_Fib4SummaryInfo_t
 */
typedef int (*DP_Fib4TblGetInfoHook_t)(DP_Fib4Tbl_t tableId, DP_Fib4SummaryInfo_t *summaryInfo);

/**
 * @ingroup fib4tbl
 * @brief 获取第一个fib4表项信息
 *
 * @par 描述: 获取第一个fib4表项信息
 * @attention
 * NA
 *
 * @param tableId [IN]  fib4表项ID
 * @param fib4EntryGet [IN]  fib4表项获取方式
 * @param key [OUT]  获取fib4表项Key
 * @param index [OUT]  fib4表项存储位置
 *
 * @retval 0 成功
 * @retval 非0 失败 返回错误码

 * @see DP_Fib4Tbl_t | DP_Fib4EntryGet_t | DP_Fib4Key_t
 */
typedef int (*DP_Fib4TblGetFirstEntryHook_t)(DP_Fib4Tbl_t tableId, DP_Fib4EntryGet_t *fib4EntryGet,
    DP_Fib4Key_t *key, uint32_t *index);

/**
 * @ingroup fib4tbl
 * @brief 获取下一个fib4表项信息
 *
 * @par 描述: 获取下一个fib4表项信息
 * @attention
 * NA
 *
 * @param tableId [IN]  fib4表项ID
 * @param key [IN]  fib4表项获取方式
 * @param fib4EntryGet [OUT]  获取fib4表项Key
 * @param index [OUT]  fib4表项存储位置
 * @param nextKey [OUT]  获取下一个fib4表项Key
 *
 * @retval 0 成功
 * @retval 非0 失败 返回错误码

 * @see DP_Fib4Tbl_t | DP_Fib4EntryGet_t | DP_Fib4Key_t
 */
typedef int (*DP_Fib4TblGetNextEntryHook_t)(DP_Fib4Tbl_t tableId, DP_Fib4Key_t *key,
    DP_Fib4EntryGet_t *fib4EntryGet, uint32_t *index, DP_Fib4Key_t *nextKey);

/**
 * @ingroup fib4tbl
 * FIB4操作集
 */
typedef struct {
    DP_Fib4TblCreateHook_t                createTable;
    DP_Fib4TblDestroyHook_t               destroyTable;

    DP_Fib4TblInsertEntryHook_t           insertEntry;
    DP_Fib4TblModifyEntryHook_t           modifyEntry;
    DP_Fib4TblDelEntryHook_t              delEntry;

    DP_Fib4TblExactMatchHook_t            exactMatchEntry;
    DP_Fib4TblLpmEntryHook_t              lmpEntry;
    DP_Fib4TblGetInfoHook_t               getInfo;
    DP_Fib4TblGetFirstEntryHook_t         fib4EntryGetFirst;
    DP_Fib4TblGetNextEntryHook_t          fib4EntryGetNext;
} DP_Fib4TblHooks_t;

/**
 * @ingroup fib4tbl
 * @brief fib4表项操作接口注册函数
 *
 * @par 描述: fib4表项操作接口注册函数
 * @attention
 * 在DP_Init前调用
 *
 * @param pstHooks [IN]  fib4表项操作集<非NULL>，对应成员下的指针也不能为空
 *
 * @retval 0 成功
 * @retval #错误码 失败

 * @see DP_Fib4TblHooks_t
 */
int DP_Fib4TblHooksReg(DP_Fib4TblHooks_t *fib4TblHooks);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif // DP_FIB4TBL_API_H
