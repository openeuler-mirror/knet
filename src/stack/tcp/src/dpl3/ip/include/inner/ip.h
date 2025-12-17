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
#ifndef IP_H
#define IP_H

#ifdef __cplusplus
extern "C" {
#endif

#define IP_POLICY_DROP (-1)
#define IP_POLICY_HOST (0)
#define IP_POLICY_FWD  (1)

#define IP_RSV_HEADROOM (64)

int IP_Init(int slave);

#ifdef __cplusplus
}
#endif
#endif
