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

#ifndef K_NET_SAL_TCP_H
#define K_NET_SAL_TCP_H

#include "knet_init.h"

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief Dp资源初始化
 *
 * @return uint32_t 0: 成功, 0xFFFFFFFF: 失败
 */
uint32_t KNET_SAL_Init(void);

/**
 * @brief Dp Posix api初始化
 *
 * @param ops [IN] struct KNET_PosixApiOps *。posix api回调
 * @return int 0: 成功, -1: 失败
 */
int KNET_DpPosixOpsApiInit(struct KNET_PosixApiOps *ops);

#ifdef __cplusplus
}
#endif
#endif // K_NET_SAL_TCP_H
