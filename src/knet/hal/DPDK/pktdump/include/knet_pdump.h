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
#ifndef __MULTI_PROC_PDUMP_H__
#define __MULTI_PROC_PDUMP_H__

#include <rte_memzone.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KNET_MULTI_PDUMP_MZ "knet_multi_pdump"

/**
 * @brief 开关抓包
 *
 * @param pdumpRequestMz KNET_MultiPdumpInit返回的初始化后的共享内存
 * @return 开关结果，成功返回0，失败返回错误码，错误码为负数
 */
int KNET_SetPdumpRxTxCbs(const struct rte_memzone *pdumpRequestMz);

/**
 * @brief 初始化抓包需要的资源
 *
 * @param void
 * @return 成功返回抓包资源的指针，失败返回NULL
 */
const struct rte_memzone* KNET_MultiPdumpInit(void);

/**
 * @brief 去初始化抓包申请的资源
 *
 * @param pdumpRequestMz KNET_MultiPdumpInit返回的初始化后的共享内存
 * @return 成功返回0，失败返回负数
 */
int32_t KNET_MultiPdumpUninit(const struct rte_memzone *pdumpRequestMz);

#ifdef __cplusplus
}
#endif
#endif
