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

#ifndef KNET_SAL_FUNC_H
#define KNET_SAL_FUNC_H

#include "knet_types.h"

/**
 * @ingroup  knet_sal_func
 * @brief mem分区ID
 */
#define KNET_MEM_SYS_PT (0)

 /**
 * @ingroup knet_sal_func
 * @brief mbuf、内存池会出现重复，mbuf的poolId 0-31，所以内存池的poolId 的偏移量
 */
#define KNET_MEM_POOL_ID_OFFSET 32

/**
 * @ingroup knet_sal_func
 * @brief mbuf pool的buf个数
 */
#define KNET_MBUF_DEFAULT_BUFNUM 65536

/**
 * @ingroup knet_sal_func
 * @brief mbuf pool的cache数目
 */
#define KNET_MBUF_DEFAULT_CACHENUM 128

/**
 * @ingroup knet_sal_func
 * @brief mbuf pool的每个cache容纳的buf数目
 */
#define KNET_MBUF_DEFAULT_CACHESIZE 512

/**
 * @ingroup knet_sal_func
 * @brief mbuf pool的私有数据区大小
 */
#define KNET_MBUF_DEFAULT_PRIVATE_SIZE 256

/**
 * @ingroup knet_sal_func
 * @brief mbuf pool的报文数据区大小
 */
#define KNET_MBUF_DEFAULT_DATAROOM_SIZE 2048

/**
 * @ingroup knet_sal_func
 * @brief mbuf 内存池句柄类型映射最大数目
 */
#define KNET_MBUF_MEM_HNDLE_MAX_NUM 512

/**
 * @ingroup knet_sal_func
 * @brief mem cahcesize 默认值
 */
#define KNET_MEM_DEFAULT_CACHESIZE 0
/**
* @ingroup knet_sal_func
* @brief 任意socket
*/
#define KNET_SOCKET_ANY (~(uint32_t)0)

/**
 * @ingroup knet_sal_func
 * @brief 随机数创建最大尝试次数
 */
#define KNET_RAND_RETRY_NUM 10

/**
 * @ingroup knet_sal_func
 * @brief 协议栈要求随机数字节数
 */
#define KNET_RAND_LEN 4

/**
 * @ingroup knet_sal_func
 * @brief 随机数无效值
 */
#define KNET_INVALID_RAND 0xFFFFFFFF

/**
 * @ingroup knet_sal_func
 * @brief 模块名字的最大长度
 */
#define KNET_SAL_MODULENAME_MAX_LEN 17

/**
 * @ingroup knet_sal_func
 * @brief SAL平台相关注册接口统一定义
 */
typedef struct {
    uint8_t moduleName[KNET_SAL_MODULENAME_MAX_LEN]; /* 待注册模块名称 */
    uint32_t (*regFunc)(void); /* 模块注册接口 */
} KNET_SAL_REG_FUNC_S;

uint32_t KNET_HandleInit(void);
uint32_t KNET_RegFunc(void);
uint32_t KNET_RegMem(void);
uint32_t KNET_RegMbufMemPool(void);
uint32_t KNET_RegRand(void);
uint32_t KNET_RegTime(void);
uint32_t KNET_RegHashTable(void);
uint32_t KNET_RegDebug(void);
uint32_t KNET_RegFdir(void);
uint32_t KNET_RegSem(void);

#endif