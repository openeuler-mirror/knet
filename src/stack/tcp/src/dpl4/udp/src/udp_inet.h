/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 
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
#include "utils_mem_pool.h"

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

/*
UDP端口分配规则：
二元组、三元组使用inetsk中的hash节点，五元组使用udpcb中的节点
*/
typedef struct {
    Spinlock_t   lock;
    atomic32_t   ref;
    Hash_t       hash;
} UdpHashTbl_t;

static inline void UdpHashRefTbl(UdpHashTbl_t* tbl)
{
    ATOMIC32_Inc(&tbl->ref);
}

static inline void UdpHashDerefTbl(UdpHashTbl_t* tbl)
{
    ATOMIC32_Dec(&tbl->ref);
}

static inline void UdpHashWaitTblIdle(UdpHashTbl_t* tbl)
{
    while (ATOMIC32_Load(&tbl->ref) != 0) { }
}

static inline UdpHashTbl_t* UdpHashGetTbl(NS_Net_t* net)
{
    return NS_GET_TBL(net, NS_NET_UDP);
}

extern char* g_udpMpName;
extern DP_Mempool g_udpMemPool;

int UdpSetSockopt(Sock_t* sk, int level, int optName, const void* optVal, DP_Socklen_t optLen);
int UdpGetSockopt(Sock_t* sk, int level, int optName, void* optVal, DP_Socklen_t* optLen);

int UdpInetErrInput(Pbuf_t* pbuf);

void UdpShowInfo(Sock_t* sk);

int UdpInetInit(int slave);

void UdpInetDeinit(int slave);

#ifdef __cplusplus
}
#endif
#endif
