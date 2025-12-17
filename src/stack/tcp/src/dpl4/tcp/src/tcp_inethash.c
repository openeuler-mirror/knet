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

#define TCP_PER_WORKER_HASH_BITS (13)
#define TCP_GLOBAL_HASH_BITS     (3)
#define TCP_LISTENER_HASH_BITS   (3)
#define TCP_CONNECTTBL_HASH_BITS   (3)

static int TcpPortTblAlloc(TcpHashTbl_t* tbl)
{
    int tblCnt = CFG_GET_VAL(DP_CFG_WORKER_MAX);
    uint16_t minPort = (uint16_t)CFG_GET_TCP_VAL(DP_CFG_TCP_RND_PORT_MIN);
    uint16_t maxPort = (uint16_t)CFG_GET_TCP_VAL(DP_CFG_TCP_RND_PORT_MAX);
    uint16_t portStep = (uint16_t)CFG_GET_TCP_VAL(DP_CFG_TCP_RND_PORT_STEP);

    // 步长是2的幂次
    uint16_t portMask = ~(portStep - 1);
    // 端口区间以步长的整数倍为界
    minPort = (uint16_t)(minPort + portStep - 1) & portMask;
    maxPort = maxPort & portMask;
    uint16_t portIntervalCnt = (maxPort - minPort) / portStep;
    if (portIntervalCnt == 0) {
        DP_LOG_ERR("no portInterval for max [%d], min [%d], step [%u].", CFG_GET_TCP_VAL(DP_CFG_TCP_RND_PORT_MAX),
            CFG_GET_TCP_VAL(DP_CFG_TCP_RND_PORT_MIN), CFG_GET_TCP_VAL(DP_CFG_TCP_RND_PORT_STEP));
        return -1;
    }

    // 每个worker使用一个区间表，维护worker已使用的端口区间。
    size_t allocSize = sizeof(TcpPortIntervalTbl_t) * tblCnt;
    // 分配每个端口区间的内存
    allocSize += sizeof(TcpPortInterval_t) * portIntervalCnt;

    TcpPortIntervalTbl_t* portTbl = SHM_MALLOC(allocSize, MOD_TCP, DP_MEM_FREE);
    if (portTbl == NULL) {
        DP_LOG_ERR("Malloc memory failed for tcp port tbl.");
        return -1;
    }

    (void)memset_s(portTbl, allocSize, 0, allocSize);

    tbl->perWorkerPortTbl = portTbl;
    for (int idx = 0; idx < tblCnt; idx++) {
        LIST_INIT_HEAD(&tbl->perWorkerPortTbl[idx].portList);
    }

    TcpPortInterval_t* portNode = (TcpPortInterval_t*)PTR_NEXT(portTbl, sizeof(TcpPortIntervalTbl_t) * tblCnt);
    for (uint16_t idx = 0; idx < portIntervalCnt; idx++) {
        portNode->port = minPort + idx * portStep;
        portNode->portMask = portMask;
        portNode->usedCnt = 0;
        portNode->maxCnt = portStep;
        LIST_INSERT_TAIL(&tbl->globalPortTbl.portList, (portNode), node);
        portNode = (TcpPortInterval_t*)PTR_NEXT(portNode, sizeof(TcpPortInterval_t));
    }
    return 0;
}

// 获取worker使用的空闲端口区间。只在共线程部署场景有效。
static TcpPortInterval_t* TcpPortIntervalGetNext(TcpHashTbl_t* tbl, int8_t wid, TcpPortInterval_t* cur)
{
    TcpPortIntervalTbl_t* workerPortTbl = &tbl->perWorkerPortTbl[wid];
    TcpPortInterval_t* portInterval;
    TcpPortInterval_t* head = (cur == NULL) ? workerPortTbl->portList.first : cur->node.next;
    for (portInterval = head; portInterval != NULL; portInterval = portInterval->node.next) {
        // 防止usedCnt翻转
        if (portInterval->usedCnt < UINT16_MAX) {
            portInterval->usedCnt++;
            return portInterval;
        }
    }

    // worker上无可用区间，需要从全局表中获取
    portInterval = LIST_FIRST(&tbl->globalPortTbl.portList);
    if (portInterval == NULL) {
        // 全局表中也没有可用区间，生成端口失败
        return NULL;
    }
    // 此处在tbl锁内，可以操作全局表
    LIST_REMOVE_HEAD(&tbl->globalPortTbl.portList, portInterval, node);
    LIST_INSERT_TAIL(&workerPortTbl->portList, portInterval, node);
    portInterval->usedCnt++;
    return portInterval;
}

// 端口区间释放，当区间内所有端口均被释放时调用。需要在锁内调用。只在共线程部署场景有效。
static void TcpPortIntervalPut(TcpHashTbl_t* tbl, TcpPortInterval_t* portInterval, int8_t wid)
{
    if (portInterval->usedCnt > 0) {
        portInterval->usedCnt--;
    } else {
        DP_LOG_DBG("port interval put: used cnt is zero! wid = %d", wid);
        DP_ADD_ABN_STAT(DP_PORT_INTERVAL_CNT_ERR);
    }
    if (portInterval->usedCnt > 0) {
        return;
    }
    TcpPortIntervalTbl_t* workerPortTbl = &tbl->perWorkerPortTbl[wid];
    LIST_REMOVE(&workerPortTbl->portList, portInterval, node);
    LIST_INSERT_TAIL(&tbl->globalPortTbl.portList, portInterval, node);
    return;
}

static void TcpPortPut(TcpHashTbl_t* tbl, uint16_t port, uint16_t portMask, int8_t wid)
{
    if (portMask == PORT_MASK_DEFAULT) {
        return;
    }
    TcpPortIntervalTbl_t* workerPortTbl = &tbl->perWorkerPortTbl[wid];
    TcpPortInterval_t* portInterval;
    LIST_FOREACH(&workerPortTbl->portList, portInterval, node)
    {
        if (portInterval->port == (UTILS_NTOHS(port) & portMask)) {
            break;
        }
    }
    if (portInterval == NULL) {
        DP_LOG_DBG("port put: can't find port interval! wid = %d, port = %d", wid, port);
        DP_ADD_ABN_STAT(DP_PORT_INTERVAL_PUT_ERR);
        return;
    }
    TcpPortIntervalPut(tbl, portInterval, wid);
}

// 端口区间引用增加，被动建链时调用。只在共线程部署场景有效。
int TcpInetPortRef(NS_Net_t* net, uint16_t port, uint16_t portMask, int8_t wid)
{
    if (portMask == PORT_MASK_DEFAULT) {
        return 0;
    }
    TcpHashTbl_t* tbl = TcpHashGetTbl(net);
    TcpPortIntervalTbl_t* workerPortTbl = &tbl->perWorkerPortTbl[wid];
    TcpPortInterval_t* portInterval;
    LIST_FOREACH(&workerPortTbl->portList, portInterval, node)
    {
        if (portInterval->port == (UTILS_NTOHS(port) & portMask)) {
            break;
        }
    }
    if (portInterval == NULL || portInterval->usedCnt == UINT16_MAX) {
        DP_LOG_DBG("portInterval is invalid");
        return -1;
    }
    portInterval->usedCnt++;
    return 0;
}

static void TcpHashTblInit(TcpHashTbl_t* tbl, uint32_t globalTblCnt, uint32_t perWorkertblCnt)
{
    HASH_NodeHead_t* nhs;

    tbl->global = (Hash_t *)PTR_NEXT(tbl, sizeof(TcpHashTbl_t));
    tbl->listener = (Hash_t *)PTR_NEXT(tbl->global, sizeof(Hash_t) * globalTblCnt);
    tbl->connectTbl = (Hash_t *)PTR_NEXT(tbl->listener, sizeof(Hash_t) * globalTblCnt);
    tbl->perWorkerHash = (Hash_t *)PTR_NEXT(tbl->connectTbl, sizeof(Hash_t) * globalTblCnt);

    nhs = (HASH_NodeHead_t*)PTR_NEXT(tbl->perWorkerHash, sizeof(Hash_t) * perWorkertblCnt);
    for (uint32_t idx = 0; idx < globalTblCnt; idx++) {
        HASH_INIT(&tbl->global[idx], nhs, TCP_GLOBAL_HASH_BITS);
        nhs = (HASH_NodeHead_t*)PTR_NEXT(nhs, HASH_GET_SIZE(TCP_GLOBAL_HASH_BITS));
    }

    for (uint32_t idx = 0; idx < globalTblCnt; idx++) {
        HASH_INIT(&tbl->listener[idx], nhs, TCP_LISTENER_HASH_BITS);
        nhs = (HASH_NodeHead_t*)PTR_NEXT(nhs, HASH_GET_SIZE(TCP_LISTENER_HASH_BITS));
    }

    for (uint32_t idx = 0; idx < globalTblCnt; idx++) {
        HASH_INIT(&tbl->connectTbl[idx], nhs, TCP_CONNECTTBL_HASH_BITS);
        nhs = (HASH_NodeHead_t*)PTR_NEXT(nhs, HASH_GET_SIZE(TCP_CONNECTTBL_HASH_BITS));
    }

    for (uint32_t idx = 0; idx < perWorkertblCnt; idx++) {
        HASH_INIT(&tbl->perWorkerHash[idx], nhs, TCP_PER_WORKER_HASH_BITS);
        nhs = (HASH_NodeHead_t*)PTR_NEXT(nhs, HASH_GET_SIZE(TCP_PER_WORKER_HASH_BITS));
    }
}

void* TcpHashAlloc()
{
    size_t           allocSize;
    uint32_t         perWorkertblCnt = (uint32_t)CFG_GET_VAL(DP_CFG_WORKER_MAX);
    uint32_t         globalTblCnt = (CFG_GET_VAL(CFG_NOLOCK) == DP_ENABLE) ? perWorkertblCnt : 1;
    TcpHashTbl_t*    tbl;

    // 每个worker一个tcp hash表，外加一张全局表用于管理监听socket,以及一个全局表管理主动建链socket
    allocSize = sizeof(TcpHashTbl_t);
    allocSize += (sizeof(Hash_t) + HASH_GET_SIZE((uint32_t)TCP_GLOBAL_HASH_BITS)) * globalTblCnt;
    allocSize += (sizeof(Hash_t) + HASH_GET_SIZE((uint32_t)TCP_LISTENER_HASH_BITS)) * globalTblCnt;
    allocSize += (sizeof(Hash_t) + HASH_GET_SIZE((uint32_t)TCP_CONNECTTBL_HASH_BITS)) * globalTblCnt;
    allocSize += (sizeof(Hash_t) + HASH_GET_SIZE((uint32_t)TCP_PER_WORKER_HASH_BITS)) * perWorkertblCnt;

    tbl = SHM_MALLOC(allocSize, MOD_TCP, DP_MEM_FREE);
    if (tbl == NULL) {
        DP_LOG_ERR("Malloc memory failed for tcp hash.");
        return NULL;
    }

    (void)memset_s(tbl, allocSize, 0, allocSize);
    
    if (SPINLOCK_Init(&tbl->lock) != 0) {
        SHM_FREE(tbl, DP_MEM_FREE);
        return NULL;
    }

    if (SPINLOCK_Init(&tbl->listenLock) != 0) {
        SPINLOCK_Deinit(&tbl->lock);
        SHM_FREE(tbl, DP_MEM_FREE);
        return NULL;
    }

    tbl->ref = 0;

    TcpHashTblInit(tbl, globalTblCnt, perWorkertblCnt);

    // 共线程部署且设置了端口区间步长时，增加端口区间全局表和每个worker的区间表。此处通过卫语句结束函数
    if ((CFG_GET_VAL(DP_CFG_DEPLOYMENT) != DP_DEPLOYMENT_CO_THREAD) ||
        (CFG_GET_TCP_VAL(DP_CFG_TCP_RND_PORT_STEP) == 1)) {
        return tbl;
    }

    if (TcpPortTblAlloc(tbl) != 0) {
        TcpHashFree(tbl);
        return NULL;
    }

    return tbl;
}

void TcpHashFree(void* ptr)
{
    TcpHashTbl_t* tbl = (TcpHashTbl_t*)ptr;

    if (tbl->perWorkerPortTbl != NULL) {
        SHM_FREE(tbl->perWorkerPortTbl, DP_MEM_FREE);
    }

    SPINLOCK_Deinit(&tbl->lock);
    SPINLOCK_Deinit(&tbl->listenLock);

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
        if ((TcpInetSk2Sk(CONTAINER_OF(node, InetSk_t, globalNode)))->family != DP_AF_INET) {
            continue;
        }

        inetSk = CONTAINER_OF(node, InetSk_t, globalNode);
        if ((inetSk->hashinfo.lport != hi->lport) || (inetSk->hashinfo.vpnid != hi->vpnid)) {
            // lport或者vpnid不同，继续检查下一个
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

static uint8_t GetFirstFromMap(const uint32_t* queMap)
{
    uint8_t idx = 0;
    // 内部函数，queMap必定有非0值，不会越界
    while (queMap[idx] == 0) {
        idx++;
    }
    uint8_t off = 0;
    uint32_t tmp = queMap[idx];
    while ((tmp & 0x01) == 0) {
        off++;
        tmp = tmp >> 1;
    }
    return idx * (int8_t)32 + off; // 32：uint32_t有32个bit，可以指代32个que
}

static inline void TcpInetAddrEventSet(INET_Hashinfo_t* hi, DP_AddrEvent_t* event)
{
    struct DP_SockaddrIn* addrIn = (struct DP_SockaddrIn *)event->localAddr;
    addrIn->sin_family = DP_AF_INET;
    addrIn->sin_addr.s_addr = hi->laddr;
    addrIn->sin_port = hi->lport;

    event->localAddrLen = sizeof(struct DP_SockaddrIn);
    event->protocol = DP_IPPROTO_TCP;
    event->portMask = hi->lportMask;
    event->ifIndex = hi->ifIndex;
}

int TcpInetCanUseAddr(INET_Hashinfo_t* hi)
{
    struct DP_SockaddrIn addrIn = { 0 };
    DP_AddrEvent_t event = {
        .localAddr = (struct DP_Sockaddr*)&addrIn,
        .localAddrLen = sizeof(addrIn),
    };
    TcpInetAddrEventSet(hi, &event);
    if ((CFG_GET_VAL(DP_CFG_DEPLOYMENT) == DP_DEPLOYMENT_CO_THREAD) && (hi->ifIndex != -1)) {
        uint8_t queCnt = NETDEV_TaskQueMapGet(hi->wid, hi->ifIndex, event.queMap, DP_ADDR_QUE_MAP_MAX);
        if (queCnt == 0) {
            DP_LOG_ERR("netdev with ifIndex [%d] miss rxque for worker [%d].", hi->ifIndex, hi->wid);
            return 0;
        }
        hi->queCnt = queCnt;
        hi->que = GetFirstFromMap(event.queMap);
    }

    DP_LOG_DBG("TCP addr notify create.");
    int ret = SOCK_AddrEventNotify(DP_ADDR_EVENT_CREATE, &event);
    if (ret == 0) {
        return 1;
    }

    DP_LOG_DBG("TCP addr notify create failed.");
    return 0;
}

void TcpInetReleaseAddr(INET_Hashinfo_t* hi)
{
    struct DP_SockaddrIn addrIn = { 0 };
    DP_AddrEvent_t event = {
        .localAddr = (struct DP_Sockaddr*)&addrIn,
        .localAddrLen = sizeof(addrIn),
    };
    TcpInetAddrEventSet(hi, &event);
    if (CFG_GET_VAL(DP_CFG_DEPLOYMENT) == DP_DEPLOYMENT_CO_THREAD) {
        NETDEV_TaskQueMapGet(hi->wid, hi->ifIndex, event.queMap, DP_ADDR_QUE_MAP_MAX);
    }

    DP_LOG_DBG("TCP addr notify release.");
    int ret = SOCK_AddrEventNotify(DP_ADDR_EVENT_RELEASE, &event);
    if (ret != 0) {
        DP_LOG_DBG("TCP addr notify release failed.");
    }
}

int TcpInetPreBind(INET_Hashinfo_t* hi, void* userData)
{
    if (TcpInetCanUseAddr(hi) != 1) {
        return -1;
    }
    struct DP_SockaddrIn addrIn = {
        .sin_family = DP_AF_INET,
        .sin_addr.s_addr = hi->laddr,
        .sin_port = hi->lport
    };
    if (SOCK_AddrPreBind(userData, (struct DP_Sockaddr*)&addrIn, sizeof(addrIn)) == 0) {
        return 0;
    }
    TcpInetReleaseAddr(hi);
    return -1;
}

int TcpInetCanConnect(uint32_t hashTblIdx, TcpHashTbl_t *tbl, INET_Hashinfo_t *hi, uint32_t isBinded, void* userData)
{
    uint32_t     hashVal;
    InetSk_t*    inetSk;
    HASH_Node_t* node;
    Hash_t*      hash = &tbl->connectTbl[hashTblIdx];

    hashVal = TcpInetCalcHash(hi);

    HASH_FOREACH(hash, hashVal, node)
    {
        if ((TcpInetSk2Sk(CONTAINER_OF(node, InetSk_t, connectTblNode)))->family != DP_AF_INET) {
            continue;
        }

        inetSk = CONTAINER_OF(node, InetSk_t, connectTblNode);
        if (INET_HashinfoEqual(hi, &inetSk->hashinfo) != 0) {
            return 0;
        }
    }

    if (isBinded == 1) {
        return 1;
    }
    return (TcpInetPreBind(hi, userData) == 0);
}

static int TcpInetCandBindListenerSafe(uint32_t glbHashTblIdx, TcpHashTbl_t* tbl,
    INET_Hashinfo_t* hi, uint32_t hashVal, int reuse)
{
    int ret = 0;

    TcpHashLockListenTbl(tbl);

    ret = TcpInetCandBindInner(&tbl->listener[glbHashTblIdx], hi, hashVal, reuse);

    TcpHashUnlockListenTbl(tbl);

    return ret;
}

int TcpInetCanBind(uint32_t glbHashTblIdx, TcpHashTbl_t* tbl, INET_Hashinfo_t* hi, int reuse, void* userData)
{
    uint32_t hashVal;

    ASSERT(hi->lport != 0);

    hashVal = CalcHashByLport(hi);
    if (TcpInetCandBindInner(&tbl->global[glbHashTblIdx], hi, hashVal, reuse) != 0 &&
        TcpInetCandBindListenerSafe(glbHashTblIdx, tbl, hi, hashVal, reuse) != 0 &&
        TcpInetPreBind(hi, userData) == 0) {
        return 1;
    }

    return 0;
}

struct GenPortParam {
    INET_Hashinfo_t* hi;
    void* userData;
    uint16_t minPort;
    uint16_t maxPort;
    uint8_t isBind;
    uint8_t reuse;
};

static int TcpInetGenPortInner(uint32_t glbHashTblIdx, TcpHashTbl_t* tbl, struct GenPortParam *param)
{
    uint16_t port;
    // DP_CFG_TCP_RND_PORT_MAX 和 DP_CFG_TCP_RND_PORT_MIN不会超过65535，且MAX一定大于MIN
    uint16_t range = param->maxPort - param->minPort;
    int      ret;

    port    = TcpGetRsvPort(param->minPort, range);

    for (uint16_t i = 0; i < range; i++, port++) {
        if (port >= param->maxPort) {
            port = param->minPort;
        }

        param->hi->lport = UTILS_HTONS(port);
        if (param->isBind == 1) {
            ret = TcpInetCanBind(glbHashTblIdx, tbl, param->hi, param->reuse, param->userData);
        } else {
            ret = TcpInetCanConnect(glbHashTblIdx, tbl, param->hi, 0, param->userData);
        }

        if (ret != 0) {
            return 0;
        }
    }

    return -1;
}

int TcpInetGenPort(uint32_t glbHashTblIdx, TcpHashTbl_t* tbl, uint8_t isBind,
    INET_Hashinfo_t* hi, int reuse, void* userData)
{
    struct GenPortParam param = {
        .hi = hi,
        .userData = userData,
        .isBind = isBind,
        .reuse = reuse,
    };
    // 非共线程或未设置端口区间步长，遍历整个随机端口范围
    if ((CFG_GET_VAL(DP_CFG_DEPLOYMENT) != DP_DEPLOYMENT_CO_THREAD) ||
        (CFG_GET_TCP_VAL(DP_CFG_TCP_RND_PORT_STEP) == 1)) {
        param.minPort = (uint16_t)CFG_GET_TCP_VAL(DP_CFG_TCP_RND_PORT_MIN);
        param.maxPort = (uint16_t)CFG_GET_TCP_VAL(DP_CFG_TCP_RND_PORT_MAX);
        hi->lportMask = PORT_MASK_DEFAULT;
        return TcpInetGenPortInner(glbHashTblIdx, tbl, &param);
    }

    // 从worker上获取端口区间并遍历查找，若没有可用的端口则查找下一个区间
    TcpPortInterval_t* portInterval = TcpPortIntervalGetNext(tbl, param.hi->wid, NULL); // connect太多，可能导致碰撞概率变高
    for (; portInterval != NULL;) {
        param.minPort = portInterval->port;
        param.maxPort = portInterval->port + portInterval->maxCnt;
        param.hi->lportMask = portInterval->portMask;
        if (TcpInetGenPortInner(glbHashTblIdx, tbl, &param) == 0) {
            return 0;
        }
        TcpPortInterval_t* next = TcpPortIntervalGetNext(tbl, param.hi->wid, portInterval);
        TcpPortIntervalPut(tbl, portInterval, param.hi->wid);
        portInterval = next;
    }
    return -1;
}

// bind成功时调用
void TcpInetGlobalInsert(Sock_t* sk)
{
    TcpHashTbl_t* tbl = TcpHashGetTbl(sk->net);
    InetSk_t* inetSk = TcpInetSk(sk);
    uint32_t hashVal = CalcHashByLport(&inetSk->hashinfo);

    HASH_INSERT(&tbl->global[sk->glbHashTblIdx], hashVal, &inetSk->globalNode);
}

// bind成功，未进行listen或connect，close时调用
void TcpInetGlobalRemoveSafe(Sock_t* sk)
{
    TcpHashTbl_t* tbl    = TcpHashGetTbl(sk->net);
    InetSk_t*     inetSk = TcpInetSk(sk);
    uint32_t hashVal = CalcHashByLport(&inetSk->hashinfo);

    TcpInetReleaseAddr(&inetSk->hashinfo);

    TcpHashLockTbl(tbl);

    HASH_REMOVE(&tbl->global[sk->glbHashTblIdx], hashVal, &inetSk->globalNode);
    TcpPortPut(tbl, inetSk->hashinfo.lport, inetSk->hashinfo.lportMask, inetSk->hashinfo.wid);

    TcpHashUnlockTbl(tbl);
}

// bind成功，进行listen时调用
void TcpInetListenerInsertSafe(Sock_t* sk)
{
    TcpHashTbl_t* tbl = TcpHashGetTbl(sk->net);
    InetSk_t* inetSk = TcpInetSk(sk);
    uint32_t hashVal = CalcHashByLport(&inetSk->hashinfo);

    TcpHashLockTbl(tbl);
    HASH_REMOVE(&tbl->global[sk->glbHashTblIdx], hashVal, &inetSk->globalNode);
    TcpHashUnlockTbl(tbl);

    TcpHashLockListenTbl(tbl);
    HASH_INSERT(&tbl->listener[sk->glbHashTblIdx], hashVal, &inetSk->globalNode);
    TcpHashUnlockListenTbl(tbl);
}

// listen成功，close时调用
void TcpInetListenerRemoveSafe(Sock_t *sk)
{
    TcpHashTbl_t* tbl = TcpHashGetTbl(sk->net);
    InetSk_t* inetSk = TcpInetSk(sk);
    uint32_t hashVal = CalcHashByLport(&inetSk->hashinfo);

    TcpInetReleaseAddr(&inetSk->hashinfo);

    TcpHashLockListenTbl(tbl);

    HASH_REMOVE(&tbl->listener[sk->glbHashTblIdx], hashVal, &inetSk->globalNode);
    TcpPortPut(tbl, inetSk->hashinfo.lport, inetSk->hashinfo.lportMask, inetSk->hashinfo.wid);

    TcpHashUnlockListenTbl(tbl);
}

// connect成功时调用
int TcpInetConnectTblInsert(Sock_t* sk)
{
    TcpHashTbl_t* tbl = TcpHashGetTbl(sk->net);
    InetSk_t* inetSk = TcpInetSk(sk);
    uint32_t hashVal = TcpInetCalcHash(&inetSk->hashinfo);

    HASH_INSERT(&tbl->connectTbl[sk->glbHashTblIdx], hashVal, &inetSk->connectTblNode);
    return 0;
}

// 被动建链成功时调用
int TcpInetConnectTblInsertSafe(Sock_t* sk)
{
    TcpHashTbl_t* tbl = TcpHashGetTbl(sk->net);
    InetSk_t* inetSk = TcpInetSk(sk);
    uint32_t hashVal = TcpInetCalcHash(&inetSk->hashinfo);
    uint32_t hashValPort = CalcHashByLport(&inetSk->hashinfo);

    TcpHashLockTbl(tbl);
    HASH_INSERT(&tbl->connectTbl[sk->glbHashTblIdx], hashVal, &inetSk->connectTblNode);
    HASH_INSERT(&tbl->global[sk->glbHashTblIdx], hashValPort, &inetSk->globalNode);
    TcpHashUnlockTbl(tbl);
    return 0;
}

// 开始建链后释放socket时调用
void TcpInetConnectTblRemoveSafe(Sock_t *sk)
{
    TcpHashTbl_t* tbl = TcpHashGetTbl(sk->net);
    InetSk_t* inetSk = TcpInetSk(sk);
    uint32_t hashVal = TcpInetCalcHash(&inetSk->hashinfo);
    uint32_t hashValPort = CalcHashByLport(&inetSk->hashinfo);

    TcpInetReleaseAddr(&TcpInetSk(sk)->hashinfo);

    TcpHashLockTbl(tbl);

    HASH_REMOVE(&tbl->connectTbl[sk->glbHashTblIdx], hashVal, &inetSk->connectTblNode);
    HASH_REMOVE(&tbl->global[sk->glbHashTblIdx], hashValPort, &inetSk->globalNode);
    TcpPortPut(tbl, inetSk->hashinfo.lport, inetSk->hashinfo.lportMask, inetSk->hashinfo.wid);

    TcpHashUnlockTbl(tbl);
}

// 发送建链报文，或被动建链成功时调用
int TcpInetPerWorkerTblInsert(Sock_t* sk)
{
    TcpHashTbl_t* tbl    = TcpHashGetTbl(sk->net);
    InetSk_t*     inetSk = TcpInetSk(sk);
    uint32_t      hashVal;
    int16_t      tblId = TcpSK(sk)->wid;

    ASSERT(tbl != NULL);

    TcpSetPport(TcpSK(sk), inetSk->hashinfo.pport);
    TcpSetLport(TcpSK(sk), inetSk->hashinfo.lport);
    hashVal = TcpInetCalcHash(&inetSk->hashinfo);

    HASH_INSERT(&tbl->perWorkerHash[tblId], hashVal, &inetSk->node);
    return 0;
}

// 开始建链后释放socket时调用
void TcpInetPerWorkerTblRemove(Sock_t* sk)
{
    TcpHashTbl_t* tbl     = TcpHashGetTbl(sk->net);
    uint32_t      hashVal = TcpInetCalcHash(&TcpInetSk(sk)->hashinfo);

    ASSERT(tbl != NULL);

    HASH_REMOVE(&tbl->perWorkerHash[TcpSK(sk)->wid], hashVal, &TcpInetSk(sk)->node);
}

TcpSk_t* TcpInetLookupListener(uint8_t wid, NS_Net_t* net, INET_Hashinfo_t* hi)
{
    TcpHashTbl_t* tbl = TcpHashRefTbl(net);
    uint32_t      hashVal;
    InetSk_t*     tempSk  = NULL;
    InetSk_t*     inetSk  = NULL;
    InetSk_t*     inetSk2 = NULL;
    TcpSk_t*      tcp     = NULL;
    HASH_Node_t*  node;

    hashVal = CalcHashByLport(hi);

    TcpHashLockListenTbl(tbl);

    Hash_t *listener = (CFG_GET_VAL(CFG_NOLOCK) == DP_ENABLE) ? &tbl->listener[wid] : tbl->listener;

    HASH_FOREACH(listener, hashVal, node)
    {
        if ((TcpInetSk2Sk(CONTAINER_OF(node, InetSk_t, globalNode)))->family != DP_AF_INET) {
            continue;
        }

        tempSk = CONTAINER_OF(node, InetSk_t, globalNode);
        if ((hi->lport != tempSk->hashinfo.lport) || (hi->vpnid != tempSk->hashinfo.vpnid)) {
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

    TcpHashUnlockListenTbl(tbl);

    TcpHashDerefTbl(tbl);

    return tcp;
}

TcpSk_t* TcpInetLookupPerWorker(int wid, NS_Net_t* net, INET_Hashinfo_t* hi)
{
    TcpHashTbl_t* tbl     = TcpHashGetTbl(net);
    uint32_t      hashVal = TcpInetCalcHash(hi);
    InetSk_t*     inetSk;
    HASH_Node_t*  node;
    Hash_t*       hash = &tbl->perWorkerHash[wid];

    ASSERT(wid >= 0);

    HASH_FOREACH(hash, hashVal, node)
    {
        if ((TcpInetSk2Sk((InetSk_t*)node))->family != DP_AF_INET) {
            continue;
        }

        inetSk = (InetSk_t*)node;

        if (INET_HashinfoEqual(hi, &inetSk->hashinfo) != 0) {
            return TcpInetTcpSK(inetSk);
        }
    }

    return NULL;
}

void TcpInetWaitIdle(Sock_t* sk)
{
    TcpHashTbl_t* tbl    = TcpHashGetTbl(sk->net);
    TcpHashWaitTblIdle(tbl);
    return;
}
