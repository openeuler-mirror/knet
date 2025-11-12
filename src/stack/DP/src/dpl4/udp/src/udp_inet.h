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

#ifndef UDP_INET_H
#define UDP_INET_H

#include "inet_sk.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Sock_t sk;

    HASH_Node_t node; // 五元组节点

    InetSk_t inetSk;
} UdpInetSk_t;

#define UdpInetSk(sk) (&((UdpInetSk_t*)(sk))->inetSk)

#define UdpInetSk2Sk(inetSk) ((Sock_t*)CONTAINER_OF((inetSk), UdpInetSk_t, inetSk))

int UdpInetInit(int slave);
void UdpInetDeinit(int slave);

#ifdef __cplusplus
}
#endif
#endif
