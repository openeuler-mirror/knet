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


#ifndef __KNET_CONFIG_HW_SCAN_H__
#define __KNET_CONFIG_HW_SCAN_H__

#include "knet_types.h"

#ifdef __cplusplus
extern "C" {
#endif

int KnetCheckCompatibleNic(void);
int KnetIsEnableNicFlowFun(void);

/**
 * @brief 判断是否组内核bond, 组内核bond且符合以下约束返回0，其他场景返回-1
 * 1、能查到内核bond名，或未检测到内核bond
 * 2、mode配置4
 * 3、xmit_hash_policy配置1
 * @return int 0 : 合法的内核bond配置
 *         int -1 : 配置非法
 */
int KnetKernelBondCfgScan(char *bondName, size_t bondLen);

#ifdef __cplusplus
}
#endif
#endif