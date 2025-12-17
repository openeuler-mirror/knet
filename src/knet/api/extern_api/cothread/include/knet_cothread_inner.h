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


#ifndef __KNET_COTHREAD_INNER_H__
#define __KNET_COTHREAD_INNER_H__

/**
 * @brief 判断是否走内核流量
 *
 * @return true: 如果开了共线程，且不在worker线程中，便走内核流量
 * @return false: 没开共线程，或者在worker线程中，便走用户态流量
 */
bool KNET_IsCothreadGoKernel(void);

#endif