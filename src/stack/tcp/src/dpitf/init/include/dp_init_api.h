/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 协议栈初始化对外接口
 */

#ifndef DP_INIT_API_H
#define DP_INIT_API_H

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup init 初始化接口 */

/**
 * @ingroup init
 * @brief DP协议栈初始化接口
 *
 * @attention
 * 多线程运行协议栈只能调用一次
 *
 * @param slave
 * @retval 0 成功
 * @retval -1 失败

 */
int DP_Init(int slave);

#ifdef __cplusplus
}
#endif
#endif
