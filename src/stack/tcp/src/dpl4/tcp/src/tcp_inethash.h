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

#define JHASH_3(a, b, c)                            \
    do {                                            \
        (a) -= (b); (a) -= (c); (a) ^= ((c) >> 13); \
        (b) -= (c); (b) -= (a); (b) ^= ((a) << 8);  \
        (c) -= (a); (c) -= (b); (c) ^= ((b) >> 13); \
        (a) -= (b); (a) -= (c); (a) ^= ((c) >> 12); \
        (b) -= (c); (b) -= (a); (b) ^= ((a) << 16); \
        (c) -= (a); (c) -= (b); (c) ^= ((b) >> 5);  \
        (a) -= (b); (a) -= (c); (a) ^= ((c) >> 3);  \
        (b) -= (c); (b) -= (a); (b) ^= ((a) << 10); \
        (c) -= (a); (c) -= (b); (c) ^= ((b) >> 15); \
    } while (0)

typedef struct TcpPortInterval {
    uint16_t port;
    uint16_t portMask;
    uint16_t usedCnt;
    uint16_t maxCnt;
    LIST_ENTRY(TcpPortInterval) node;
} TcpPortInterval_t;

typedef struct TcpPortIntervalTbl {
    LIST_HEAD(, TcpPortInterval) portList;
} TcpPortIntervalTbl_t;

typedef struct {
    Spinlock_t     lock;
    Spinlock_t     listenLock;
    atomic32_t     ref;
    // 存放bind全局表
    Hash_t*        global;
    // 存放监听socket资源
    Hash_t*        listener;
    // 存放主动建链的五元组
    Hash_t*        connectTbl;
    // 存放已经完成建链的五元组
    Hash_t*        perWorkerHash;
    // 存放空闲的端口区间，全局共用
    TcpPortIntervalTbl_t globalPortTbl;
    // 存放每个worker使用的端口区间
    TcpPortIntervalTbl_t* perWorkerPortTbl;
} TcpHashTbl_t;

void* TcpHashAlloc(void);

void TcpHashFree(void* ptr);

static inline void TcpHashLockListenTbl(TcpHashTbl_t* tbl)
{
    SPINLOCK_Lock(&tbl->listenLock);
}

static inline void TcpHashUnlockListenTbl(TcpHashTbl_t* tbl)
{
    SPINLOCK_Unlock(&tbl->listenLock);
}

static inline void TcpHashLockTbl(TcpHashTbl_t* tbl)
{
    SPINLOCK_Lock(&tbl->lock);
}

static inline void TcpHashUnlockTbl(TcpHashTbl_t* tbl)
{
    SPINLOCK_Unlock(&tbl->lock);
}

static inline TcpHashTbl_t* TcpHashGetTbl(NS_Net_t* net)
{
    return NS_GET_TCP_TBL(net);
}

static inline TcpHashTbl_t* TcpHashRefTbl(NS_Net_t* net)
{
    TcpHashTbl_t* tbl = TcpHashGetTbl(net);

    ATOMIC32_Inc(&tbl->ref);

    return tbl;
}

static inline void TcpHashDerefTbl(TcpHashTbl_t* tbl)
{
    ATOMIC32_Dec(&tbl->ref);
}

static inline void TcpHashWaitTblIdle(TcpHashTbl_t* tbl)
{
    while (ATOMIC32_Load(&tbl->ref) != 0) {}
}

int TcpInetCanBind(uint32_t glbHashTblIdx, TcpHashTbl_t* tbl, INET_Hashinfo_t* hi, int reuse, void* userData);

int TcpInetCanConnect(uint32_t hashTblIdx, TcpHashTbl_t* tbl, INET_Hashinfo_t* hi, uint32_t isBinded, void* userData);

int TcpInetGenPort(uint32_t glbHashTblIdx, TcpHashTbl_t* tbl, uint8_t isBind,
    INET_Hashinfo_t* hi, int reuse, void* userData);

void TcpInetGlobalInsert(Sock_t* sk);

void TcpInetGlobalRemoveSafe(Sock_t* sk);

int TcpInetPerWorkerTblInsert(Sock_t* sk);

void TcpInetPerWorkerTblRemove(Sock_t* sk);

void TcpInetWaitIdle(Sock_t* sk);

TcpSk_t* TcpInetLookupByPkt(Pbuf_t* pbuf, TcpPktInfo_t* pi);

TcpSk_t* TcpInetLookupListener(uint8_t wid, NS_Net_t* net, INET_Hashinfo_t* hi);
TcpSk_t* TcpInetLookupPerWorker(int wid, NS_Net_t* net, INET_Hashinfo_t* hi);

void TcpInetListenerInsertSafe(Sock_t* sk);

void TcpInetListenerRemoveSafe(Sock_t* sk);

int TcpInetConnectTblInsert(Sock_t* sk);

int TcpInetConnectTblInsertSafe(Sock_t* sk);

void TcpInetConnectTblRemoveSafe(Sock_t *sk);

int TcpInetCanUseAddr(INET_Hashinfo_t* hi);

void TcpInetReleaseAddr(INET_Hashinfo_t* hi);

int TcpInetPreBind(INET_Hashinfo_t* hi, void* userData);

int TcpInetPortRef(NS_Net_t* net, uint16_t port, uint16_t portMask, int8_t wid);

#ifdef __cplusplus
}
#endif
#endif
