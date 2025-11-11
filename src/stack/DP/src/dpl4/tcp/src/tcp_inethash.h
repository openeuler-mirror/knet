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

#ifndef TCP_INET_HASH_H
#define TCP_INET_HASH_H

#include "sock.h"
#include "ns.h"
#include "inet_sk.h"
#include "utils_spinlock.h"

#include "tcp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Hash_t hash;
} TcpInetPerCpuTbl_t;

typedef struct {
    Spinlock_t     lock;
    atomic32_t     ref;
    // 存放bind全局表
    Hash_t         global;
    // 存放监听socket资源
    Hash_t         listener;
    // 存放主动建链的五元组
    Hash_t         connectTbl;
    // 存放已经完成建链的五元组
    Hash_t*        perWorkerHash;
} TcpInetTbl_t;

void* TcpInetAllocHash(void);

void TcpInetFreeHash(void* ptr);

static inline void TcpInetLockTbl(TcpInetTbl_t* tbl)
{
    SPINLOCK_Lock(&tbl->lock);
}

static inline void TcpInetUnlockTbl(TcpInetTbl_t* tbl)
{
    SPINLOCK_Unlock(&tbl->lock);
}

static inline TcpInetTbl_t* TcpInetGetTbl(NS_Net_t* net)
{
    return NS_GET_TCP_TBL(net);
}

static inline TcpInetTbl_t* TcpInetRefTbl(NS_Net_t* net)
{
    TcpInetTbl_t* tbl = TcpInetGetTbl(net);

    ATOMIC32_Inc(&tbl->ref);

    return tbl;
}

static inline void TcpInetDerefTbl(TcpInetTbl_t* tbl)
{
    ATOMIC32_Dec(&tbl->ref);
}

static inline void TcpInetWaitTblIdle(TcpInetTbl_t* tbl)
{
    while (ATOMIC32_Load(&tbl->ref) != 0) {}
}

int TcpInetCanBind(TcpInetTbl_t* tbl, INET_Hashinfo_t* hi, int reuse);

int TcpInetCanConnect(TcpInetTbl_t* tbl, INET_Hashinfo_t* hi);

int TcpInetGenPort(TcpInetTbl_t* tbl, uint8_t isBind, INET_Hashinfo_t* hi, int reuse);

void TcpInetGlobalInsert(Sock_t* sk);

void TcpInetGlobalRemove(Sock_t* sk);

void TcpInetGlobalRemoveSafe(Sock_t* sk);

int TcpInetPerWorkerTblInsert(Sock_t* sk);

void TcpInetPerWorkerTblRemove(Sock_t* sk);

void TcpInetWaitIdle(Sock_t* sk);

TcpSk_t* TcpInetLookup(NS_Net_t* net, int wid, INET_Hashinfo_t* hi);

TcpSk_t* TcpInetLookupPerWorker(NS_Net_t* net, int wid, INET_Hashinfo_t* hi);

void TcpInetListenerInsertSafe(Sock_t* sk);

void TcpInetListenerRemoveSafe(Sock_t* sk);

int TcpInetConnectTblInsert(Sock_t* sk);

int TcpInetConnectTblInsertSafe(Sock_t* sk);

void TcpInetConnectTblRemoveSafe(Sock_t *sk);

void TcpInetConnectTblTryRemove(Sock_t *sk);
#ifdef __cplusplus
}
#endif
#endif
