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
#ifndef TUN_H
#define TUN_H

#include <stdint.h>
#define IF_NAME_SIZE 16
#define INVALID_FD 0xFFFFFFFF
#define TUN_PRE_NAME "knet_tap"

int32_t KNET_TapFree(int32_t fd);
int KNET_TAPCreate(const uint32_t tapId, int32_t *fd, int *tapIfIndex);

#endif