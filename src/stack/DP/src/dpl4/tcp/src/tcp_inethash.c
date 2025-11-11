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

#include "tcp_inethash.h"

#include <securec.h>

#include "tcp_inet.h"
#include "tcp_sock.h"

#include "shm.h"
#include "utils_debug.h"
#include "utils_cfg.h"
#include "utils_log.h"
#include "sock_addr_ext.h"

/**
 * @brief TCP HASH实现
 * 1. Listener Hash表为全局hash，暂时使用锁保护
 * 2. ESTABLISH Hash表为分实例(per worker)表，增加可能出现在实例和socket中，而删除仅在实例中操作
 * 需要补充：
 * 1. 整理hash表抽象，梳理规格项
 * 2. ESTABLISH hash命名不准确，需要调整
 */

#define JHASH_3(a, b, c) \
    do { \
        (a) -= (b); (a) -= (c); (a) ^= ((c) >> 13); \
        (b) -= (c); (b) -= (a); (b) ^= ((a) << 8); \
        (c) -= (a); (c) -= (b); (c) ^= ((b) >> 13); \
        (a) -= (b); (a) -= (c); (a) ^= ((c) >> 12); \
        (b) -= (c); (b) -= (a); (b) ^= ((a) << 16); \
        (c) -= (a); (c) -= (b); (c) ^= ((b) >> 5); \
        (a) -= (b); (a) -= (c); (a) ^= ((c) >> 3); \
        (b) -= (c); (b) -= (a); (b) ^= ((a) << 10); \
        (c) -= (a); (c) -= (b); (c) ^= ((b) >> 15); \
    } while (0)

#define TCP_PER_WORKER_HASH_BITS (13)
#define TCP_GLOBAL_HASH_BITS     (3)
#define TCP_LISTENER_HASH_BITS   (3)
#define TCP_CONNECTTBL_HASH_BITS   (3)

void* TcpInetAllocHash()
{
    size_t           allocSize;
    int              tblCnt = CFG_GET_VAL(DP_CFG_WORKER_MAX);
    TcpInetTbl_t*    tbl;
    HASH_NodeHead_t* nhs;

    // 每个worker一个tcp hash表，外加一张全局表用于管理监听socket,以及一个全局表管理主动建链socket
    allocSize = sizeof(TcpInetTbl_t);
    allocSize += sizeof(Hash_t) * tblCnt;
    allocSize += HASH_GET_SIZE(TCP_GLOBAL_HASH_BITS);
    allocSize += HASH_GET_SIZE(TCP_LISTENER_HASH_BITS);
    allocSize += HASH_GET_SIZE(TCP_CONNECTTBL_HASH_BITS);
    allocSize += HASH_GET_SIZE((uint32_t)TCP_PER_WORKER_HASH_BITS) * (uint32_t)tblCnt;

    tbl = SHM_MALLOC(allocSize, MOD_TCP, DP_MEM_FREE);
    if (tbl == NULL) {
        DP_LOG_ERR("Malloc memory failed for tcp hash.");
        return NULL;
    }

    (void)memset_s(tbl, allocSize, 0, allocSize);

    tbl->perWorkerHash = (Hash_t*)PTR_NEXT(tbl, sizeof(TcpInetTbl_t));
    tbl->ref           = 0;

    if (SPINLOCK_Init(&tbl->lock) != 0) {
        SHM_FREE(tbl, DP_MEM_FREE);
        return NULL;
    }

    nhs = (HASH_NodeHead_t*)PTR_NEXT(tbl->perWorkerHash, sizeof(Hash_t) * tblCnt);
    HASH_INIT(&tbl->global, nhs, TCP_GLOBAL_HASH_BITS);

    nhs = (HASH_NodeHead_t*)PTR_NEXT(nhs, HASH_GET_SIZE(TCP_GLOBAL_HASH_BITS));
    HASH_INIT(&tbl->listener, nhs, TCP_LISTENER_HASH_BITS);

    nhs = (HASH_NodeHead_t*)PTR_NEXT(nhs, HASH_GET_SIZE(TCP_LISTENER_HASH_BITS));
    HASH_INIT(&tbl->connectTbl, nhs, TCP_CONNECTTBL_HASH_BITS);

    nhs = (HASH_NodeHead_t*)PTR_NEXT(nhs, HASH_GET_SIZE(TCP_CONNECTTBL_HASH_BITS));
    for (int idx = 0; idx < tblCnt; idx++) {
        HASH_INIT(&tbl->perWorkerHash[idx], nhs, TCP_PER_WORKER_HASH_BITS);
        nhs = (HASH_NodeHead_t*)PTR_NEXT(nhs, HASH_GET_SIZE(TCP_PER_WORKER_HASH_BITS));
    }

    return tbl;
}

void TcpInetFreeHash(void* ptr)
{
    TcpInetTbl_t* tbl = (TcpInetTbl_t*)ptr;

    SPINLOCK_Deinit(&tbl->lock);

    SHM_FREE(tbl, DP_MEM_FREE);
}

static inline uint32_t TcpInetCalcHash(INET_Hashinfo_t* hashInfo)
{
    uint32_t a = hashInfo->laddr;
    uint32_t b = hashInfo->paddr;
    // 16代表左移16位，让两个u16的端口号组成一个u32的值
    uint32_t c = ((uint32_t)hashInfo->lport << 16) | (uint32_t)hashInfo->pport;

    JHASH_3(a, b, c);
    return c;
}

static inline uint32_t CalcHashByLport(INET_Hashinfo_t* hashInfo)
{
    return hashInfo->lport;
}

static int TcpInetCandBindInner(Hash_t* hash, INET_Hashinfo_t* hi, uint32_t hashVal, int reuse)
{
    InetSk_t*    inetSk;
    HASH_Node_t* node;

    HASH_FOREACH(hash, hashVal, node)
    {
        inetSk = (InetSk_t*)node;

        if (inetSk->hashinfo.lport != hi->lport) {
            continue;
        }

        // 二元组或三元组匹配
        if (inetSk->hashinfo.laddr == DP_INADDR_ANY || hi->laddr == DP_INADDR_ANY ||
            hi->laddr == inetSk->hashinfo.laddr) {
            if (reuse == 0 || !SOCK_CAN_REUSE(TcpInetSk2Sk(inetSk))) {
                return 0;
            }
        }
    }

    return 1;
}

static int TcpInetCanUseAddr(INET_Hashinfo_t* hi)
{
    struct DP_SockaddrIn addrIn = {
        .sin_family = DP_AF_INET,
        .sin_addr.s_addr = hi->laddr,
        .sin_port = hi->lport
    };

    DP_LOG_DBG("TCP addr notify create.");
    int ret = SOCK_AddrEventNotify(DP_ADDR_EVENT_CREATE, DP_IPPROTO_TCP, (struct DP_Sockaddr*)&addrIn, sizeof(addrIn));
    if (ret == 0) {
        return 1;
    }

    DP_LOG_WARN("TCP addr notify create failed.");
    return 0;
}

static void TcpInetReleaseAddr(INET_Hashinfo_t* hi)
{
    struct DP_SockaddrIn addrIn = {
        .sin_family = DP_AF_INET,
        .sin_addr.s_addr = hi->laddr,
        .sin_port = hi->lport
    };

    DP_LOG_DBG("TCP addr notify release.");
    SOCK_AddrEventNotify(DP_ADDR_EVENT_RELEASE, DP_IPPROTO_TCP, (struct DP_Sockaddr*)&addrIn, sizeof(addrIn));
}

int TcpInetCanConnect(TcpInetTbl_t* tbl, INET_Hashinfo_t* hi)
{
    uint32_t     hashVal;
    InetSk_t*    inetSk;
    HASH_Node_t* node;
    Hash_t*      hash = &tbl->connectTbl;

    hashVal = TcpInetCalcHash(hi);

    HASH_FOREACH(hash, hashVal, node)
    {
        inetSk = CONTAINER_OF(node, InetSk_t, connectTblNode);
        if (INET_HashinfoEqual(hi, &inetSk->hashinfo) != 0) {
            return 0;
        }
    }

    return TcpInetCanUseAddr(hi);
}

int TcpInetCanBind(TcpInetTbl_t* tbl, INET_Hashinfo_t* hi, int reuse)
{
    uint32_t hashVal;

    ASSERT(hi->lport != 0);

    hashVal = CalcHashByLport(hi);
    if (TcpInetCandBindInner(&tbl->global, hi, hashVal, reuse) != 0 &&
        TcpInetCandBindInner(&tbl->listener, hi, hashVal, reuse) != 0 &&
        TcpInetCanUseAddr(hi) != 0) {
        return 1;
    }

    return 0;
}

int TcpInetGenPort(TcpInetTbl_t* tbl, uint8_t isBind, INET_Hashinfo_t* hi, int reuse)
{
    uint16_t port;
    uint16_t minPort = (uint16_t)CFG_GET_TCP_VAL(DP_CFG_TCP_RND_PORT_MIN);
    uint16_t maxPort = (uint16_t)CFG_GET_TCP_VAL(DP_CFG_TCP_RND_PORT_MAX);
    // DP_CFG_TCP_RND_PORT_MAX 和 DP_CFG_TCP_RND_PORT_MIN不会超过65535，且MAX一定大于MIN
    uint16_t range = maxPort - minPort;
    int      ret;

    port    = TcpGetRsvPort(minPort, range);

    for (uint16_t i = 0; i < range; i++, port++) {
        if (port >= maxPort) {
            port = minPort;
        }

        hi->lport = UTILS_HTONS(port);
        if (isBind == 1) {
            ret = TcpInetCanBind(tbl, hi, reuse);
        } else {
            ret = TcpInetCanConnect(tbl, hi);
        }

        if (ret != 0) {
            return 0;
        }
    }

    return -1;
}

void TcpInetGlobalInsert(Sock_t* sk)
{
    InetSk_t*       inetSk = TcpInetSk(sk);
    TcpInetTbl_t*   tbl = TcpInetGetTbl(sk->net);
    uint32_t hashVal = CalcHashByLport(&inetSk->hashinfo);

    INET_InsertHashItem(&tbl->global, &inetSk->node, hashVal);
}

void TcpInetGlobalRemove(Sock_t* sk)
{
    TcpInetTbl_t* tbl    = TcpInetGetTbl(sk->net);
    InetSk_t*     inetSk = TcpInetSk(sk);
    uint32_t hashVal = CalcHashByLport(&inetSk->hashinfo);

    INET_RemoveHashItem(&tbl->global, &inetSk->node, hashVal);
}

void TcpInetGlobalRemoveSafe(Sock_t* sk)
{
    TcpInetTbl_t* tbl    = TcpInetGetTbl(sk->net);
    InetSk_t*     inetSk = TcpInetSk(sk);
    uint32_t hashVal = CalcHashByLport(&inetSk->hashinfo);

    TcpInetLockTbl(tbl);

    INET_RemoveHashItem(&tbl->global, &inetSk->node, hashVal);

    TcpInetUnlockTbl(tbl);
}

void TcpInetListenerInsertSafe(Sock_t* sk)
{
    TcpInetTbl_t* tbl = TcpInetGetTbl(sk->net);
    InetSk_t* inetSk = TcpInetSk(sk);
    uint32_t hashVal = CalcHashByLport(&inetSk->hashinfo);

    TcpInetLockTbl(tbl);

    INET_RemoveHashItem(&tbl->global, &inetSk->node, hashVal);

    INET_InsertHashItem(&tbl->listener, &inetSk->node, hashVal);

    TcpInetUnlockTbl(tbl);
}

void TcpInetListenerRemoveSafe(Sock_t *sk)
{
    TcpInetTbl_t* tbl = TcpInetGetTbl(sk->net);
    InetSk_t* inetSk = TcpInetSk(sk);
    uint32_t hashVal = CalcHashByLport(&inetSk->hashinfo);

    TcpInetLockTbl(tbl);

    INET_RemoveHashItem(&tbl->listener, &inetSk->node, hashVal);

    TcpInetUnlockTbl(tbl);
}

int TcpInetConnectTblInsert(Sock_t* sk)
{
    TcpInetTbl_t* tbl = TcpInetGetTbl(sk->net);
    InetSk_t* inetSk = TcpInetSk(sk);
    uint32_t hashVal = TcpInetCalcHash(&inetSk->hashinfo);

    INET_InsertHashItem(&tbl->connectTbl, &inetSk->connectTblNode, hashVal);
    return 0;
}

int TcpInetConnectTblInsertSafe(Sock_t* sk)
{
    TcpInetTbl_t* tbl = TcpInetGetTbl(sk->net);
    InetSk_t* inetSk = TcpInetSk(sk);
    uint32_t hashVal = TcpInetCalcHash(&inetSk->hashinfo);

    TcpInetLockTbl(tbl);
    INET_InsertHashItem(&tbl->connectTbl, &inetSk->connectTblNode, hashVal);
    TcpInetUnlockTbl(tbl);
    return 0;
}

void TcpInetConnectTblRemoveSafe(Sock_t *sk)
{
    TcpInetTbl_t* tbl = TcpInetGetTbl(sk->net);
    InetSk_t* inetSk = TcpInetSk(sk);
    uint32_t hashVal = TcpInetCalcHash(&inetSk->hashinfo);

    TcpInetLockTbl(tbl);

    INET_RemoveHashItem(&tbl->connectTbl, &inetSk->connectTblNode, hashVal);

    TcpInetUnlockTbl(tbl);
}

int TcpInetPerWorkerTblInsert(Sock_t* sk)
{
    TcpInetTbl_t* tbl    = TcpInetGetTbl(sk->net);
    InetSk_t*     inetSk = TcpInetSk(sk);
    uint32_t      hashVal;
    int16_t      tblId = TcpSK(sk)->wid;

    ASSERT(tbl != NULL);

    TcpSetPport(TcpSK(sk), inetSk->hashinfo.pport);
    TcpSetLport(TcpSK(sk), inetSk->hashinfo.lport);
    hashVal = TcpInetCalcHash(&inetSk->hashinfo);

    INET_InsertHashItem(&tbl->perWorkerHash[tblId], &inetSk->node, hashVal);
    return 0;
}

void TcpInetPerWorkerTblRemove(Sock_t* sk)
{
    TcpInetTbl_t* tbl     = TcpInetGetTbl(sk->net);
    uint32_t      hashVal = TcpInetCalcHash(&TcpInetSk(sk)->hashinfo);

    ASSERT(tbl != NULL);

    INET_RemoveHashItem(&tbl->perWorkerHash[TcpSK(sk)->wid], &TcpInetSk(sk)->node, hashVal);
    TcpInetReleaseAddr(&TcpInetSk(sk)->hashinfo);
}

static TcpSk_t* TcpInetLookupListener(NS_Net_t* net, INET_Hashinfo_t* hi)
{
    TcpInetTbl_t* tbl = TcpInetRefTbl(net);
    uint32_t      hashVal;
    InetSk_t*     tempSk  = NULL;
    InetSk_t*     inetSk  = NULL;
    InetSk_t*     inetSk2 = NULL;
    TcpSk_t*      tcp     = NULL;
    HASH_Node_t*  node;

    hashVal = CalcHashByLport(hi);

    HASH_FOREACH(&tbl->listener, hashVal, node)
    {
        tempSk = (InetSk_t*)node;

        if (hi->lport != tempSk->hashinfo.lport) {
            continue;
        }

        if (tempSk->hashinfo.laddr != DP_INADDR_ANY) {
            if (hi->laddr != tempSk->hashinfo.laddr) {
                continue;
            } else {
                inetSk = tempSk;
                break;
            }
        } else { // 二元组匹配
            inetSk2 = tempSk;
        }
    }

    inetSk = inetSk == NULL ? inetSk2 : inetSk;

    if (inetSk != NULL) { // 必须在这里引用socket
        tcp = TcpInetTcpSK(inetSk);
        SOCK_Ref(TcpSk2Sk(tcp));
    }

    TcpInetDerefTbl(tbl);

    return tcp;
}

TcpSk_t* TcpInetLookupPerWorker(NS_Net_t* net, int wid, INET_Hashinfo_t* hi)
{
    TcpInetTbl_t* tbl     = TcpInetGetTbl(net);
    uint32_t      hashVal = TcpInetCalcHash(hi);
    InetSk_t*     inetSk;
    HASH_Node_t*  node;
    Hash_t*       hash = &tbl->perWorkerHash[wid];

    ASSERT(wid >= 0);

    HASH_FOREACH(hash, hashVal, node)
    {
        inetSk = (InetSk_t*)node;

        if (INET_HashinfoEqual(hi, &inetSk->hashinfo) != 0) {
            return TcpInetTcpSK(inetSk);
        }
    }

    return NULL;
}

TcpSk_t* TcpInetLookup(NS_Net_t* net, int wid, INET_Hashinfo_t* hi)
{
    TcpSk_t* ret = TcpInetLookupPerWorker(net, wid, hi);
    if (ret != NULL) {
        return ret;
    }

    return TcpInetLookupListener(net, hi);
}

void TcpInetWaitIdle(Sock_t* sk)
{
    TcpInetTbl_t* tbl    = TcpInetGetTbl(sk->net);
    TcpInetWaitTblIdle(tbl);
    return;
}
