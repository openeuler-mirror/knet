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

#ifndef DP_SELECT_H
#define DP_SELECT_H

#include <stdint.h>

#include "dp_socket_types_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#undef DP_FD_SETSIZE
#define DP_FD_SETSIZE 1024

#ifndef CHAR_BIT
#define CHAR_BIT 8
#endif

typedef struct DP_FdSet {
    uint32_t fdmask[(DP_FD_SETSIZE + (CHAR_BIT * sizeof(uint32_t) - 1)) / (CHAR_BIT * sizeof(uint32_t))]; // 32
} DP_FdSet_t;

void DP_FdClr(int fd, DP_FdSet_t* set);

int DP_FdIsSet(int fd, DP_FdSet_t* set);

void DP_FdSet(int fd, DP_FdSet_t* set);

void DP_FdZero(DP_FdSet_t* set);

int DP_Select(int nfds, DP_FdSet_t* readfds, DP_FdSet_t* writefds, DP_FdSet_t* exceptfds,
              struct DP_Timeval* timeout);

#ifdef __cplusplus
}
#endif

#endif
