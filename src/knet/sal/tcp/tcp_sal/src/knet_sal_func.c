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
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <semaphore.h>
#include "dp_clock_api.h"
#include "dp_mem_api.h"
#include "dp_rand_api.h"
#include "dp_show_api.h"
#include "dp_sem_api.h"
#include "dp_addr_ext_api.h"
#include "dp_worker_api.h"
#include "dp_hashtbl_api.h"
#include "dp_fib4tbl_api.h"
#include "dp_netdev_api.h"
#include "dp_debug_api.h"

#include "cJSON.h"
#include "rte_telemetry.h"
#include "rte_ring.h"

#include "securec.h"
#include "knet_init.h"
#include "knet_init_tcp.h"
#include "knet_rand.h"
#include "knet_tcp_symbols.h"
#include "knet_log.h"
#include "knet_mem.h"
#include "knet_hash_table.h"
#include "knet_dpdk_init.h"
#include "knet_transmission.h"
#include "knet_sal_tcp.h"
#include "knet_telemetry.h"
#include "knet_sal_inner.h"
#include "knet_sal_mp.h"
#include "knet_signal_tcp.h"
#include "tcp_socket.h"

#include "knet_sal_func.h"

#define MILLISECONDS_PER_SECOND 1000
#define NANOS_PER_MILLISECOND 1000000
#define ENQUE_ONE_PACKET 1
#define DST_IPMASK 0xFFFFFFFF

KNET_SAL_REG_FUNC_S g_knetSalRegFuncs[] = {
    {"MEM", KnetRegMem},
    {"MBUFMEMPOOL", KnetRegMbufMemPool},
    {"RAND", KnetRegRand},
    {"TIME", KnetRegTime},
    {"HASHTBL", KnetRegHashTable},
    {"DEBUG", KnetRegDebug},
    {"TRANS", KnetRegFdir},
    {"BIND", KnetRegBind},
    {"SEM", KnetRegSem},
    {"DelayOps", KnetRegDelayCpd},
};

#define MSEC_PER_SEC  (1000)
#define NSEC_PER_MSEC (1000 * 1000)
#define NSEC_PER_SEC  (1000LL * 1000LL * 1000LL)
#define KNET_WAITTIME (50)


int KNET_ACC_DelayInputEnque(void* pbuf, int cpdRingId)
{
    struct rte_ring* ring = (struct rte_ring*)KNET_GetDelayRxRing(cpdRingId);
    if (ring == NULL) {
        return 0;
    }
    return rte_ring_enqueue_burst(ring, (void* const*)&pbuf, ENQUE_ONE_PACKET, NULL);
}
 
int KNET_ACC_DelayInputDeque(void** pbuf, unsigned int burstSize, int cpdRingId)
{
    struct rte_ring* ring = (struct rte_ring*)KNET_GetDelayRxRing(cpdRingId);
    if (ring == NULL) {
        return 0;
    }
    return rte_ring_dequeue_burst(ring, pbuf, burstSize, NULL);
}
 
int DP_CpdQueHooksReg(void* queOps);

static void LeftTimeCalculate(int timeLeft, struct timespec *ts)
{
    int nsec;
    int sec;

    if (timeLeft >= 0) {
        /* timeLeft 毫秒数转换成秒和纳秒 */
        sec  = (timeLeft >= MSEC_PER_SEC) ? (timeLeft / MSEC_PER_SEC) : 0;
        nsec = (timeLeft - sec * MSEC_PER_SEC) * (int)NSEC_PER_MSEC;

        ts->tv_sec += sec;
        ts->tv_nsec += nsec;
        if (ts->tv_nsec > NSEC_PER_SEC) {
            ts->tv_sec += 1;
            ts->tv_nsec -= NSEC_PER_SEC;
        }
    }
}

uint32_t SemWaitBlocking(DP_Sem_t sem, int timeout)
{
    struct timespec ts = {0};
    int timeLeft = timeout;
    bool timeoutFlag = false;
    int curSig = 0;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return errno;
    }

    while (1) {
        if (timeLeft < 0) { // 表示永久阻塞
            LeftTimeCalculate(KNET_WAITTIME, &ts);
        } else if (timeLeft - KNET_WAITTIME <= 0) {
            /* 剩余时间小于等于50ms,等待剩余的时间 */
            LeftTimeCalculate(timeLeft, &ts);
            timeoutFlag = true;
            timeLeft = 0;
        } else {
            LeftTimeCalculate(KNET_WAITTIME, &ts);
            timeLeft -= KNET_WAITTIME;
        }

        errno = 0;
        if (sem_timedwait((sem_t*)sem, &ts) != 0) {
            if (errno != ETIMEDOUT && errno != EINTR) {
                /* 意外的错误 */
                return errno;
            }
            if (timeoutFlag && errno == ETIMEDOUT) {
                /* 因超时退出 */
                return errno;
            }
        } else {
            return 0;
        }

        /* knet对每个信号都会捕获,在这里判断是否有信号中断需要退出 */
        /* 主线程被中断,其他线程也需要退出 */
        curSig = KNET_DpSignalGetSigDelayCurSig();
        if (KNET_UNLIKELY(curSig) || KNET_UNLIKELY(KNET_DpSignalGetWaitExit())) {
            KNET_DEBUG("Received signal %d, or other thread exit", curSig);
            errno = EINTR;
            return errno;
        }
    }

    return 0;
}

uint32_t SemWaitNonblock(DP_Sem_t sem)
{
    /*
    即使注册了sig handler，sem_trywait过程中收到信号也能返回EINTR
    所以此处不再判断sig handler是否触发
    */
    errno= 0;
    int ret = sem_trywait((sem_t*)sem);
    if (ret != 0 && errno == EAGAIN) {
        errno = ETIMEDOUT;
    }
    
    return errno;
}

/**
 * @brief 注册给协议栈的信号量阻塞接口, timeout单位为ms
 */
uint32_t KNET_SemWaitHook(DP_Sem_t sem, int timeout)
{
    if (sem == NULL) {
        errno = EINVAL;
        return EINVAL;
    }
    if (timeout == 0) {
        return SemWaitNonblock(sem);
    } else {
        return SemWaitBlocking(sem, timeout);
    }
}

uint32_t KNET_SemInitHook(DP_Sem_t sem, int32_t flag, uint32_t value)
{
    if (sem == NULL) {
        return 1;
    }
    (void)flag;
    (void)value;

    if (sem_init((sem_t*)sem, 0, 0) != 0) {
        return 1;
    }

    return 0;
}

void KNET_SemDeinitHook(DP_Sem_t sem)
{
    if (sem == NULL) {
        return;
    }

    sem_destroy(sem);
}

uint32_t KNET_SemPostHook(DP_Sem_t sem)
{
    if (sem == NULL) {
        return 1;
    }
    if (sem_post((sem_t*)sem) != 0) {
        return 1;
    }
    return 0;
}

uint32_t KnetRegDelayCpd(void)
{
    if (KNET_GetCfg(CONF_HW_BIFUR_ENABLE)->intValue != KERNEL_FORWARD_ENABLE) {
        return 0;
    }
    DP_CpdqueOps_t queOps = {0};
    queOps.cpdEnque = KNET_ACC_DelayInputEnque;
    queOps.cpdDeque = KNET_ACC_DelayInputDeque;
    return (uint32_t)(DP_CpdQueHooksReg(&queOps));
}

uint32_t KnetRegSem(void)
{
    DP_SemHooks_S stFunctions = {0};
    stFunctions.initHook = KNET_SemInitHook;
    stFunctions.deinitHook = KNET_SemDeinitHook;
    stFunctions.postHook = KNET_SemPostHook;
    stFunctions.timeWaitHook = KNET_SemWaitHook;
    stFunctions.waitHook = NULL;
    stFunctions.tryWaitHook = NULL;
    stFunctions.semSize = (uint16_t)sizeof(sem_t);

    return DP_SemHookReg(&stFunctions);
}

void *KNET_ACC_Malloc(size_t size)
{
    return KNET_MemAlloc(size);
}

void KNET_ACC_Free(void *ptr)
{
    KNET_MemFree(ptr);
}

uint32_t KnetRegMem(void)
{
    DP_MemHooks_S stFunctions = {0};

    stFunctions.mAlloc = KNET_ACC_Malloc;
    stFunctions.mFree  = KNET_ACC_Free;

    return DP_MemHookReg(&stFunctions);
}

uint32_t KnetRegMbufMemPool(void)
{
    DP_MempoolHooks_S stFunctions = {0};

    stFunctions.mpCreate = KNET_ACC_CreateMbufMemPool;
    stFunctions.mpDestroy = KNET_ACC_DestroyMbufMemPool;
    stFunctions.mpAlloc = KNET_ACC_MbufMemPoolAlloc;
    stFunctions.mpFree = KNET_ACC_MbufMemPoolFree;
    stFunctions.mpConstruct =  KNET_ACC_MbufConstruct;

    return DP_MempoolHookReg(&stFunctions);
}

uint32_t KNET_ACC_Rand(void)
{
    uint32_t data;
    int64_t cnt = 0;
    int64_t ret;
    uint32_t i = 0;

    do {
        ret = KNET_GetRandomNum((uint8_t *)&data + cnt, KNET_RAND_LEN - cnt);
        if (ret < 0) {
            break;
        }

        cnt += ret;
        if (cnt == KNET_RAND_LEN) {
            return data;
        }

        i++;
    } while (i < KNET_RAND_RETRY_NUM);

    return KNET_INVALID_RAND;
}

uint32_t KnetRegRand(void)
{
    DP_RandomHooks_S stFunctions = {0};
    stFunctions.randInt = KNET_ACC_Rand;
    return DP_RandIntHookReg(&stFunctions);
}

int32_t KnetRegWorkderId(void)
{
    DP_WorkerGetSelfIdHook pHook = KNET_ACC_WorkerGetSelfId;
    return DP_RegGetSelfWorkerIdHook(pHook);
}

uint32_t KNET_TimeHook(DP_ClockId_E clockId, int64_t *seconds, int64_t *nanoseconds)
{
    if (clockId != DP_CLOCK_MONOTONIC_COARSE) {
        return KNET_ERROR;
    }

    if (seconds == NULL || nanoseconds == NULL) {
        return KNET_ERROR;
    }

    // 获取当前时间（CLOCK_MONOTONIC）
    struct timespec ts = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        KNET_ERR("K-NET clock gettime error");
        return KNET_ERROR;
    }

    // 将时间转换为毫秒
    uint64_t milliseconds =
        (uint64_t)ts.tv_sec * MILLISECONDS_PER_SECOND + (uint64_t)ts.tv_nsec / NANOS_PER_MILLISECOND;
    // 转换为秒和纳秒
    *seconds = milliseconds / MILLISECONDS_PER_SECOND;
    *nanoseconds = (milliseconds % MILLISECONDS_PER_SECOND) * NANOS_PER_MILLISECOND;

    return KNET_OK;
}

uint32_t KnetRegTime(void)
{
    return DP_ClockReg(KNET_TimeHook);
}

KNET_STATIC int KNET_ACC_HashTblCreate(DP_HashTblCfg_t *pstHashTblCfg, DP_HashTbl_t *phTableId)
{
    if (pstHashTblCfg == NULL || phTableId == NULL) {
        return KNET_ERROR;
    }

    KNET_HashTblCfg hashCfg = {0};
    uint32_t table;
    int ret;
    hashCfg.hashFunc = pstHashTblCfg->hashFunc;
    hashCfg.keySize = pstHashTblCfg->keySize;
    hashCfg.entrySize = pstHashTblCfg->entrySize;
    hashCfg.entryNum = pstHashTblCfg->entryNum;
    hashCfg.flag = pstHashTblCfg->flag;
    hashCfg.updateFreq = pstHashTblCfg->updateFreq;
    hashCfg.delayTime = pstHashTblCfg->delayTime;
    ret = KNET_CreateHashTbl(&hashCfg, &table);
    *phTableId = (DP_HashTbl_t)(uintptr_t)table;
    return ret == 0 ? KNET_OK : KNET_ERROR;
}

KNET_STATIC int KNET_ACC_HashTblDestroy(DP_HashTbl_t hTableId)
{
    int ret;
    ret = KNET_DestroyHashTbl((uint32_t)(uintptr_t)hTableId);
    return ret == 0 ? KNET_OK : KNET_ERROR;
}

KNET_STATIC int KNET_ACC_HashTblInsertEntry(DP_HashTbl_t hTableId, const uint8_t *pu8Key, const void *pData)
{
    int ret;
    ret = KNET_HashTblAddEntry((uint32_t)(uintptr_t)hTableId, pu8Key, pData);
    return ret == 0 ? KNET_OK : KNET_ERROR;
}

KNET_STATIC int KNET_ACC_HashTblModifyEntry(DP_HashTbl_t hTableId, const uint8_t *pu8Key, const void *pData)
{
    int ret;
    ret = KNET_HashTblModifyEntry((uint32_t)(uintptr_t)hTableId, pu8Key, pData);
    return ret == 0 ? KNET_OK : KNET_ERROR;
}

KNET_STATIC int KNET_ACC_HashTblDelEntry(DP_HashTbl_t hTableId, const uint8_t *pu8Key)
{
    int ret;
    ret = KNET_HashTblDelEntry((uint32_t)(uintptr_t)hTableId, pu8Key);
    return ret == 0 ? KNET_OK : KNET_ERROR;
}

KNET_STATIC int KNET_ACC_HashTblLookupEntry(DP_HashTbl_t hTableId, const uint8_t *pu8Key, void *pData)
{
    int ret;
    ret = KNET_HashTblLookupEntry((uint32_t)(uintptr_t)hTableId, pu8Key, pData);
    return ret == 0 ? KNET_OK : KNET_ERROR;
}

KNET_STATIC int KNET_ACC_HashTblGetInfo(DP_HashTbl_t hTableId, DP_HashTblSummaryInfo_t *pstHashSummaryInfo)
{
    KNET_HashTblInfo info = {0};
    int ret = 0;

    if (pstHashSummaryInfo == NULL) {
        KNET_ERR("Invalid summary info param");
        return KNET_ERROR;
    }
    ret = KNET_GetHashTblInfo((uint32_t) (uintptr_t) hTableId, &info);
    pstHashSummaryInfo->hashFunc = info.pfHashFunc;
    pstHashSummaryInfo->tableId = (DP_HashTbl_t)(uintptr_t)info.tableId;
    pstHashSummaryInfo->maxEntryNum = info.maxEntryNum;
    pstHashSummaryInfo->currEntryNum = info.currEntryNum;
    pstHashSummaryInfo->keySize = info.keySize;
    pstHashSummaryInfo->entrySize = info.entrySize;
    return ret == 0 ? KNET_OK : KNET_ERROR;
}

KNET_STATIC int KNET_ACC_HashTblEntryGetFirst(DP_HashTbl_t hTableId, uint8_t *pu8Key, void *pData)
{
    int ret;
    // hTableId小于UINT32_MAX
    ret = KNET_GetHashTblFirstEntry((uint32_t)(uintptr_t)hTableId, pu8Key, pData);
    return ret == 0 ? KNET_OK : KNET_ERROR;
}

KNET_STATIC int KNET_ACC_HashTblEntryGetNext(DP_HashTbl_t hTableId, uint8_t *pu8Key, void *pData, uint8_t *pu8NextKey)
{
    int ret;
    // hTableId小于UINT32_MAX
    ret = KNET_GetHashTblNextEntry((uint32_t)(uintptr_t)hTableId, pu8Key, pu8NextKey, pData);
    return ret == 0 ? KNET_OK : KNET_ERROR;
}

uint32_t KnetRegHashTable(void)
{
    DP_HashTblHooks_t hashHook = {0};

    hashHook.createTable = KNET_ACC_HashTblCreate;
    hashHook.destroyTable = KNET_ACC_HashTblDestroy;

    hashHook.insertEntry = KNET_ACC_HashTblInsertEntry;
    hashHook.modifyEntry = KNET_ACC_HashTblModifyEntry;
    hashHook.delEntry = KNET_ACC_HashTblDelEntry;

    hashHook.lookupEntry = KNET_ACC_HashTblLookupEntry;
    hashHook.getInfo = KNET_ACC_HashTblGetInfo;
    hashHook.hashtblEntryGetFirst = KNET_ACC_HashTblEntryGetFirst;
    hashHook.hashtblEntryGetNext = KNET_ACC_HashTblEntryGetNext;

    return DP_HashTblHooksReg(&hashHook);
}

uint32_t KNET_ACC_Debug(uint32_t flag, char *output, uint32_t len)
{
    if (output == NULL) {
        KNET_ERR("K-NET acc debug output is null");
        return KNET_ERROR;
    }

    KNET_StatOutputType outType = (KNET_StatOutputType)flag;

    if (outType == KNET_STAT_OUTPUT_TO_LOG) {
        KNET_DEBUG("%s", output);
    } else if (outType == KNET_STAT_OUTPUT_TO_TELEMETRY) {
        if (KNET_DebugOutputToTelemetry(output, len) != KNET_OK) {
            KNET_ERR("K-NET stat output to telemetry failed");
            return KNET_ERROR;
        }
    } else if (outType == KNET_STAT_OUTPUT_TO_SCREEN) {
        printf("%s\n", output);
    } else if (outType == KNET_STAT_OUTPUT_TO_FILE) {
        if (KNET_DebugOutputToFile(output, len) != KNET_OK) {
            KNET_ERR("K-NET stat output to file failed");
            return KNET_ERROR;
        }
    }

    return KNET_OK;
}

uint32_t KnetRegDebug(void)
{
    KNET_DpTelemetryHooks dpTelemetryhooks = {
        DP_ShowStatistics, DP_SocketCountGet, DP_GetSocketState, DP_GetSocketDetails, DP_GetEpollDetails};
    if (KNET_DpTelemetryHookReg(dpTelemetryhooks) != KNET_OK) {
        KNET_ERR("K-NET register telemetry run dp show statistics failed");
        return KNET_ERROR;
    }
    /* 单独注册持久化hook，避免影响 */
    if (KNET_DpShowStatisticsHookRegPersist(DP_ShowStatistics) != KNET_OK) {
        KNET_ERR("K-NET register telemetry persistence run dp show statistics failed");
        return KNET_ERROR;
    }

    DP_DebugShowHook hook = KNET_ACC_Debug;
    return DP_DebugShowHookReg(hook);
}

/**
 * @brief Set the Fdir Dp Que Info object
 *
 * @param type
 * @param queueIdSize
 * @param queueId
 * @return int 0:正常获取qid数组与size，-1:异常场景失败
 */
int SetFdirDpQueInfo(uint32_t type, const unsigned int *queMap, uint16_t *queueIdSize, uint16_t *queueId)
{
    if (KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue == 1 && type == DP_ADDR_EVENT_CREATE) {
        unsigned __int128 queMapNum = 0;
        queMapNum |= (unsigned __int128)(uint32_t)queMap[3] << 96;
        queMapNum |= (unsigned __int128)(uint32_t)queMap[2] << 64;
        queMapNum |= (unsigned __int128)(uint32_t)queMap[1] << 32;
        queMapNum |= (unsigned __int128)(uint32_t)queMap[0];
        *queueIdSize = KnetGetFdirQid(queMapNum, queueId);
        if (*queueIdSize == 0) {
            KNET_ERR("K-NET get fdir qid num is 0");
            return KNET_ERROR;
        }
    }
    return 0;
}

int CheckFlowCfgValid(uint32_t dstIp, uint16_t dstPort, int32_t  proto)
{
    if (dstIp == INVALID_IP) {
        KNET_ERR("Invalid ip %u", dstIp);
        return KNET_ERROR;
    }

    // 判断port在[0,65535] portuint32_t类型不判断是否 < 0
    if (dstPort > PORT_MAX) {
        KNET_ERR("Invalid port %u", dstPort);
        return KNET_ERROR;
    }

    if (proto != IPPROTO_TCP && proto != IPPROTO_UDP) {
        KNET_ERR("Invalid proto %d", proto);
        return KNET_ERROR;
    }
    return KNET_OK;
}

/**
 * @brief 协议栈下流表回调函数
 * 下流表规则：单进程：开流分叉或者共线程会下流表；
 *                   只要是开了流分叉：下所有流表；
 *                   单开共线程：不具有下rss流表的能力，仅下单queue流表；
 *                              如果直接使用网卡的rss功能，流表便会优化；
 *            多进程：目前仅每个进程下单queue流表。
 * @param type 下流表或者删流表
 * @param addrEvent 网络信息
 * @return int 0: 成功, -1: 失败
 */
int KNET_ACC_EventNotify(DP_AddrEventType_t type, const DP_AddrEvent_t *addrEvent)
{
    int ret = KNET_IsNeedFlowRule();
    if (ret == 0) {
        KNET_DEBUG("K-NET disabled rss flow rule in DP event notify");
        return ret;
    }

    if (addrEvent == NULL || addrEvent->localAddr == NULL) {
        KNET_ERR("Invalid addrEvent");
        return KNET_ERROR;
    }

    if (((struct sockaddr *)addrEvent->localAddr)->sa_family != AF_INET) {
        KNET_ERR("Invalid address family, expected AF_INET");
        return KNET_ERROR;
    }

    const struct sockaddr_in *addrIn = (const struct sockaddr_in *)addrEvent->localAddr;

    struct KNET_FDirRequest fdirDp = {0};
    fdirDp.type = type;
    uint32_t ip = addrIn->sin_addr.s_addr;
    if (ip == 0) {
        fdirDp.dstIp = ntohl((uint32_t)KNET_GetCfg(CONF_INTERFACE_IP)->intValue);
    } else {
        fdirDp.dstIp = ntohl(ip);
    }

    fdirDp.dstIpMask = DST_IPMASK;
    fdirDp.dstPort = ntohs(addrIn->sin_port);
    fdirDp.dstPortMask = addrEvent->portMask;
    fdirDp.dstPort &= fdirDp.dstPortMask; // 如果开了共线程，为区间左值的port下流表；未开就为原port
    fdirDp.proto = addrEvent->protocol;

    ret = SetFdirDpQueInfo(type, addrEvent->queMap, &fdirDp.queueIdSize, fdirDp.queueId);
    if (ret != 0) {
        return ret; // 在函数内部打印日志
    }

    ret = CheckFlowCfgValid(fdirDp.dstIp, fdirDp.dstPort, fdirDp.proto);
    if (ret != 0) {
        return ret; // 在函数内部打印日志
    }

    // 根据事件类型进行处理
    switch (type) {
        case DP_ADDR_EVENT_CREATE:
            KNET_DEBUG("Socket bound: IP=%u, Port=%u, Proto=%d", fdirDp.dstIp, fdirDp.dstPort, fdirDp.proto);
            break;
        case DP_ADDR_EVENT_RELEASE:
            KNET_DEBUG("Socket destroyed: IP=%u, Port=%u, Proto=%d", fdirDp.dstIp, fdirDp.dstPort, fdirDp.proto);
            break;
        default:
            KNET_ERR("%d not support", type);
            return KNET_ERROR;
    }

    return KNET_EventNotify(&fdirDp);
}

int KNET_ACC_PreBind(void* userData, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen)
{
    return KNET_PreBind(userData, addr, addrlen);
}

uint32_t KnetRegBind(void)
{
    DP_AddrBindHooks_t addrBindHook = {0};
    addrBindHook.preBind = KNET_ACC_PreBind;
    int ret = DP_AddrBindHooksReg(&addrBindHook);
    return ret == 0 ? KNET_OK : KNET_ERROR;
}

uint32_t KnetRegFdir(void)
{
    DP_AddrHooks_t addrHook = {0};
    addrHook.eventNotify = KNET_ACC_EventNotify;
    int ret = DP_AddrHooksReg(&addrHook);
    return ret == 0 ? KNET_OK : KNET_ERROR;
}

uint32_t KnetRegFunc(void)
{
    uint32_t index;
    uint32_t ret;
    size_t itemCount = (size_t)(sizeof(g_knetSalRegFuncs)) / sizeof(KNET_SAL_REG_FUNC_S);

    for (index = 0; index < itemCount; index++) {
        if (g_knetSalRegFuncs[index].regFunc != NULL) {
            ret = g_knetSalRegFuncs[index].regFunc();
            if (ret != KNET_OK) {
                KNET_ERR("Reg %s failed, ret %u", g_knetSalRegFuncs[index].moduleName, ret);
                return ret;
            }
        }
    }

    return KNET_OK;
}