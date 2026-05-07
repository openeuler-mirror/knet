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

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @ingroup  knet_sal_func
 * @brief mem分区ID
 */
#define KNET_MEM_SYS_PT (0)

/* 60 * 1024 */
#define PER_EBUF_MBUF_SIZE 61440
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

uint32_t KnetHandleInit(void);
uint32_t KnetRegFunc(void);
uint32_t KnetRegMem(void);
uint32_t KnetRegMbufMemPool(void);
uint32_t KnetRegRand(void);
uint32_t KnetRegTime(void);
uint32_t KnetRegHashTable(void);
uint32_t KnetRegDebug(void);
uint32_t KnetRegFdir(void);
uint32_t KnetRegBind(void);
uint32_t KnetRegSem(void);
uint32_t KnetRegDelayCpd(void);

#ifdef __cplusplus
}
#endif
#endif