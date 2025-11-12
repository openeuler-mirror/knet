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

#include <stdint.h>
#include <semaphore.h>

#include "dp_clock_api.h"
#include "dp_mem_api.h"
#include "dp_mp_api.h"
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

#include "securec.h"
#include "knet_rand.h"
#include "knet_symbols.h"
#include "knet_log.h"
#include "knet_fmm.h"
#include "knet_mem.h"
#include "knet_pkt.h"
#include "knet_pktpool.h"
#include "knet_statistics.h"
#include "knet_hash_table.h"
#include "knet_io_init.h"
#include "knet_transmission.h"
#include "knet_sal_dp.h"
#include "knet_dpdk_telemetry.h"
#include "knet_dp_hijack.h"

#include "knet_sal_func.h"

#define INVALID_MAP_ID UINT32_MAX
#define MILLISECONDS_PER_SECOND 1000
#define NANOS_PER_MILLISECOND 1000000

KNET_SAL_REG_FUNC_S g_knetSalRegFuncs[] = {
    {"MEM", KNET_RegMem},
    {"MBUFMEMPOOL", KNET_RegMbufMemPool},
    {"RAND", KNET_RegRand},
    {"TIME", KNET_RegTime},
    {"HASHTBL", KNET_RegHashTable},
    {"DEBUG", KNET_RegDebug},
    {"TRANS", KNET_RegFdir},
    {"SEM", KNET_RegSem}
};

typedef struct {
    DP_Mempool handler;   /** 句柄*/
    uint32_t type;          /** l类型*/
} KnetMbufMemHandleMap;

typedef struct {
    pthread_mutex_t mutex;
    uint32_t handleSize;
    uint32_t handleIdToMapId[KNET_MBUF_MEM_HNDLE_MAX_NUM];
    KnetMbufMemHandleMap handleMap[KNET_MBUF_MEM_HNDLE_MAX_NUM];
}KnetMbufMemHandleCtrl;

/* mbuf、内存池句柄类型映射表信息 */
static KnetMbufMemHandleCtrl g_knetMbufMemHandleCtrl = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
};

/* 内存分区管理handle */
uint32_t g_knetMemHandle;

#define MSEC_PER_SEC  (1000)
#define NSEC_PER_MSEC (1000 * 1000)
#define NSEC_PER_SEC  (1000LL * 1000LL * 1000LL)
#define KNET_WAITTIME (50)

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

/**
 * @brief 注册给协议栈的信号量阻塞接口, timeout单位为ms
 */
uint32_t KNET_SemWaitHook(DP_Sem_t sem, int timeout)
{
    if (sem == NULL) {
        return 1;
    }
    struct timespec ts = {0};
    int timeLeft = timeout;
    bool timeoutFlag = false;
    int curSig = 0;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return errno;
    }

    /* 该标志位仅为了在每次这里阻塞时,判断是否需要因为信号中断而退出
       为了防止之前的流程触发了某个信号污染了标志位，在这里清0 */
    KNET_CleanSignalTriggered();
    while (1) {
        if (timeLeft < 0) { // 表示永久阻塞
            LeftTimeCalculate(KNET_WAITTIME, &ts);
        } else if (timeLeft - KNET_WAITTIME <= 0) {
            /* 剩余时间小于等于50ms,等待剩余的时间 */
            LeftTimeCalculate(timeLeft, &ts);
            timeoutFlag = true;
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
        curSig = KNET_IsSignalTriggered();
        if (KNET_UNLIKELY(curSig)) {
            KNET_DEBUG("Received signal %d, stop waitting", curSig);
            errno = EINTR;
            return errno;
        }

        /* 主线程被中断,其他线程也需要退出 */
        if (KNET_UNLIKELY(KNET_IsDpWaitingExit())) {
            KNET_DEBUG("other thread exit, cur thread return");
            errno = EINTR;
            return errno;
        }
    }

    return 0;
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

uint32_t KNET_RegSem(void)
{
    DP_SemHooks_S stFunctions = {0};
    stFunctions.init = KNET_SemInitHook;
    stFunctions.deinit = KNET_SemDeinitHook;
    stFunctions.post = KNET_SemPostHook;
    stFunctions.timeWait = KNET_SemWaitHook;
    stFunctions.wait = NULL;
    stFunctions.tryWait = NULL;
    stFunctions.size = (uint16_t)sizeof(sem_t);

    return DP_SemHookReg(&stFunctions);
}

static int32_t MbufMemPoolAddMaps(const DP_Mempool handler, const uint32_t type)
{
    if ((uint64_t)(uintptr_t)handler >= KNET_MBUF_MEM_HNDLE_MAX_NUM) {
        KNET_ERR("Handler %llu out of range", (uint64_t)(uintptr_t)handler);
        return KNET_ERROR;
    }
    pthread_mutex_lock(&(g_knetMbufMemHandleCtrl.mutex));

    KnetMbufMemHandleCtrl *handleCtrl = &g_knetMbufMemHandleCtrl;
    uint32_t size = handleCtrl->handleSize;

    if (size >= KNET_MBUF_MEM_HNDLE_MAX_NUM) {
        pthread_mutex_unlock(&(g_knetMbufMemHandleCtrl.mutex));
        KNET_ERR("Mbuf mem pool mappings reach max num, size %u", size);
        return -1;
    }

    if (handleCtrl->handleMap[size].handler == NULL) {
        handleCtrl->handleMap[size].handler = handler;
        handleCtrl->handleMap[size].type = type;
        handleCtrl->handleIdToMapId[(uint64_t)(uintptr_t)handler] = size;
        handleCtrl->handleSize++;
        pthread_mutex_unlock(&(g_knetMbufMemHandleCtrl.mutex));
        return KNET_OK;
    }

    pthread_mutex_unlock(&(g_knetMbufMemHandleCtrl.mutex));

    return -1;
}

static uint32_t MbufMempoolGetType(const DP_Mempool handler, uint32_t *type)
{
    KnetMbufMemHandleCtrl *handleCtrl = &g_knetMbufMemHandleCtrl;
    if ((uint64_t)(uintptr_t)handler >= KNET_MBUF_MEM_HNDLE_MAX_NUM) {
        KNET_ERR("Handler %llu out of range", (uint64_t)(uintptr_t)handler);
        return KNET_ERROR;
    }
    uint32_t mapId = handleCtrl->handleIdToMapId[(uint64_t)(uintptr_t)handler];

    if (mapId >= KNET_MBUF_MEM_HNDLE_MAX_NUM) {
        return KNET_ERROR;
    }
    *type = handleCtrl->handleMap[mapId].type;
    return KNET_OK;
}

static void MbufMempoolFreeMaps(const DP_Mempool handler)
{
    if ((uint64_t)(uintptr_t)handler >= KNET_MBUF_MEM_HNDLE_MAX_NUM) {
        KNET_ERR("Handler %llu out of range", (uint64_t)(uintptr_t)handler);
        return;
    }
    pthread_mutex_lock(&(g_knetMbufMemHandleCtrl.mutex));

    KnetMbufMemHandleCtrl *handleCtrl = &g_knetMbufMemHandleCtrl;
    uint32_t size = handleCtrl->handleSize;
    uint32_t mapId = handleCtrl->handleIdToMapId[(uint64_t)(uintptr_t)handler];

    if (mapId >= KNET_MBUF_MEM_HNDLE_MAX_NUM) {
        pthread_mutex_unlock(&(g_knetMbufMemHandleCtrl.mutex));
        return;
    }

    handleCtrl->handleMap[mapId].handler = handleCtrl->handleMap[size - 1].handler;
    handleCtrl->handleMap[mapId].type = handleCtrl->handleMap[size - 1].type;
    handleCtrl->handleMap[size - 1].handler = NULL;
    handleCtrl->handleMap[size - 1].type = 0;
    handleCtrl->handleIdToMapId[(uint64_t)(uintptr_t)handler] = INVALID_MAP_ID;
    handleCtrl->handleSize--;
    pthread_mutex_unlock(&(g_knetMbufMemHandleCtrl.mutex));
    return;
}

void *KNET_ACC_Malloc(size_t size)
{
    return KNET_MemAlloc(size);
}

void KNET_ACC_Free(void *ptr)
{
    KNET_MemFree(ptr);
}

uint32_t KNET_RegMem(void)
{
    DP_MemHooks_S stFunctions = {0};

    stFunctions.mAlloc = KNET_ACC_Malloc;
    stFunctions.mFree  = KNET_ACC_Free;

    return DP_MemHookReg(&stFunctions);
}

static int32_t MbufPoolCfg(KnetPktPoolCfg *stPktpoolCfg, const char *name)
{
    int32_t ret;

    ret = strncpy_s(stPktpoolCfg->name, KNET_PKTPOOL_NAME_LEN, name, strlen(name));
    if (ret != EOK) {
        return -1;
    }

    size_t nameLen = strlen(stPktpoolCfg->name);
    stPktpoolCfg->name[nameLen] = '\0';
    stPktpoolCfg->bufNum       = KNET_MBUF_DEFAULT_BUFNUM;
    stPktpoolCfg->cacheNum     = KNET_MBUF_DEFAULT_CACHENUM;
    stPktpoolCfg->cacheSize    = KNET_MBUF_DEFAULT_CACHESIZE;
    stPktpoolCfg->privDataSize = KNET_MBUF_DEFAULT_PRIVATE_SIZE;
    stPktpoolCfg->headroomSize = KNET_MBUF_DEFAULT_PRIVATE_SIZE;
    stPktpoolCfg->dataroomSize = KNET_MBUF_DEFAULT_DATAROOM_SIZE;
    stPktpoolCfg->createAlg    = KNET_PKTPOOL_ALG_RING_MP_MC;
    stPktpoolCfg->numaId       = (int32_t)rte_socket_id();
    stPktpoolCfg->init         = NULL;

    return ret;
}

static int32_t MemPoolCfg(KNET_FmmPoolCfg *stFmmpoolCfg, const char *name, const uint32_t size, const uint32_t count)
{
    int32_t ret;

    ret = strncpy_s(stFmmpoolCfg->name, KNET_PKTPOOL_NAME_LEN, name, strlen(name));
    if (ret != EOK) {
        return -1;
    }

    size_t nameLen = strlen(stFmmpoolCfg->name);
    stFmmpoolCfg->name[nameLen] = '\0';
    stFmmpoolCfg->eltSize   = size;
    stFmmpoolCfg->eltNum    = count;
    stFmmpoolCfg->cacheSize = KNET_MEM_DEFAULT_CACHESIZE;
    stFmmpoolCfg->socketId  = KNET_SOCKET_ANY;
    stFmmpoolCfg->objInit   = NULL;

    return ret;
}

int32_t KNET_ACC_CreateMbufMemPool(const DP_MempoolCfg_S *cfg, const DP_MempoolAttr_S *attr, DP_Mempool *handler)
{
    if (cfg == NULL || cfg->name == NULL || handler == NULL) {
        KNET_ERR("Mbuf pool cfg invalid");
        return -1;
    }

    int32_t ret = -1;
    uint32_t poolId;
    uint32_t type = cfg->type;
    if (type == DP_MEMPOOL_TYPE_PBUF) {
        KnetPktPoolCfg stPktpoolCfg;
        ret = MbufPoolCfg(&stPktpoolCfg, cfg->name);
        if (ret != KNET_OK) {
            KNET_ERR("Mbuf pool cfg set failed, name %s", cfg->name);
            return ret;
        }

        ret = (int32_t)KNET_PktPoolCreate(&stPktpoolCfg, &poolId);
        if (ret != KNET_OK) {
            KNET_ERR("Mbuf pool create failed, ret %d", ret);
            return -1;
        }

        *handler = (DP_Mempool)(uintptr_t)poolId;
        ret = MbufMemPoolAddMaps(*handler, DP_MEMPOOL_TYPE_PBUF);
        if (ret != KNET_OK) {
            KNET_ERR("Mbuf pool add handle mappings failed, poolId %u, type %u", poolId, DP_MEMPOOL_TYPE_PBUF);
            KNET_PktPoolDestroy(poolId);
            return -1;
        }
    } else if (type == DP_MEMPOOL_TYPE_FIXED_MEM) {
        KNET_FmmPoolCfg stFmmpoolCfg;
        ret = MemPoolCfg(&stFmmpoolCfg, cfg->name, cfg->size, cfg->count);
        if (ret != KNET_OK) {
            KNET_ERR("Mem pool cfg set failed, name %s", cfg->name);
            return ret;
        }

        ret = (int32_t)KNET_FmmCreatePool(&stFmmpoolCfg, &poolId);
        if (ret != KNET_OK) {
            KNET_ERR("Mem pool create failed, ret %d", ret);
            return -1;
        }

        *handler = (DP_Mempool)(uintptr_t)(poolId + KNET_MEM_POOL_ID_OFFSET);
        ret = MbufMemPoolAddMaps(*handler, DP_MEMPOOL_TYPE_FIXED_MEM);
        if (ret != KNET_OK) {
            KNET_ERR("Mem pool add handle mappings failed, poolId %u, type %u", poolId, DP_MEMPOOL_TYPE_FIXED_MEM);
            KNET_FmmDestroyPool(poolId);
            return -1;
        }
    }

    return ret;
}

void KNET_ACC_DestroyMbufMemPool(DP_Mempool mp)
{
    uint32_t type;
    uint32_t ret;
    uint32_t poolId = (uint32_t)(uintptr_t)mp;

    ret = MbufMempoolGetType(mp, &type);
    if (ret != KNET_OK) {
        KNET_ERR("Mbuf memm pool destroy failed, get type error, ret %u", ret);
        return;
    }

    if (type == DP_MEMPOOL_TYPE_PBUF) {
        KNET_PktPoolDestroy(poolId);
        MbufMempoolFreeMaps(mp);
    } else if (type == DP_MEMPOOL_TYPE_FIXED_MEM) {
        ret = KNET_FmmDestroyPool(poolId - KNET_MEM_POOL_ID_OFFSET);
        if (ret != KNET_OK) {
            KNET_ERR("Mem pool destroy failed, pooId %u", poolId);
            return;
        }

        MbufMempoolFreeMaps(mp);
    } else {
        KNET_ERR("Mem pool destroy failed, type invalid");
    }
}

void *KNET_ACC_MbufMemPoolAlloc(DP_Mempool mp)
{
    void *ptr = NULL;
    uint32_t poolId = (uint32_t)(uintptr_t)mp;

    if ((uint64_t)mp < KNET_MEM_POOL_ID_OFFSET) {
        struct rte_mbuf *mbuf = KNET_PktAlloc(poolId);
        if (unlikely(mbuf == NULL)) {
            KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "knet acc mbuf mem pool alloc null");
            return NULL;
        }
        ptr = KnetMbuf2Pkt(mbuf);
        DP_PbufRawReset(ptr, mbuf->buf_addr, mbuf->buf_len);
    }

    return ptr;
}

void KNET_ACC_MbufMemPoolFree(DP_Mempool mp, void *ptr)
{
    if (ptr == NULL) {
        KNET_ERR("Null pointer failed to free");
        return;
    }
    if ((uint64_t)mp < KNET_MEM_POOL_ID_OFFSET) {
        DP_Pbuf_t *pbuf = ptr;
        int refCnt = DP_PBUF_GET_REF(pbuf) - 1;
        DP_PBUF_SET_REF(pbuf, refCnt);
        /**
         * 如果 pbuf 的引用计数减 1 后为 0，说明协议栈不再持有 pbuf ，此时 mbuf 的引用计数有两种情况:
         * 1: 驱动释放了一次 mbuf ，mbuf 引用计数减为了 1，
         * 2: 驱动还没有释放 mbuf ，mbuf 引用计数仍为 2，
         * 上面两种情况都可以直接调用 mbuf 带引用计数的释放函数来释放 mbuf
         */
        if (refCnt == 0) {
            KNET_PktFree(ptr);
        }
    }
}

uint32_t KNET_RegMbufMemPool(void)
{
    DP_MempoolHooks_S stFunctions = {0};

    stFunctions.mpCreate = KNET_ACC_CreateMbufMemPool;
    stFunctions.mpDestroy = KNET_ACC_DestroyMbufMemPool;
    stFunctions.mpAlloc = KNET_ACC_MbufMemPoolAlloc;
    stFunctions.mpFree = KNET_ACC_MbufMemPoolFree;

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

uint32_t KNET_RegRand(void)
{
    DP_RandomHooks_S stFunctions = {0};
    stFunctions.randInt = KNET_ACC_Rand;
    return DP_RandIntHookReg(&stFunctions);
}

int32_t KNET_RegWorkderId(void)
{
    DP_WorkerGetSelfIdHook pHook;
    pHook = KNET_ACC_WorkerGetSelfId;
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

uint32_t KNET_RegTime(void)
{
    return DP_ClockReg(KNET_TimeHook);
}

static int KNET_ACC_HashTblCreate(DP_HashTblCfg_t *pstHashTblCfg, DP_HashTbl_t *phTableId)
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

static int KNET_ACC_HashTblDestroy(DP_HashTbl_t hTableId)
{
    int ret;
    ret = KNET_DestroyHashTbl((uint32_t)(uintptr_t)hTableId);
    return ret == 0 ? KNET_OK : KNET_ERROR;
}

static int KNET_ACC_HashTblInsertEntry(DP_HashTbl_t hTableId, const uint8_t *pu8Key, const void *pData)
{
    int ret;
    ret = KNET_HashTblAddEntry((uint32_t)(uintptr_t)hTableId, pu8Key, pData);
    return ret == 0 ? KNET_OK : KNET_ERROR;
}

static int KNET_ACC_HashTblModifyEntry(DP_HashTbl_t hTableId, const uint8_t *pu8Key, const void *pData)
{
    int ret;
    ret = KNET_HashTblModifyEntry((uint32_t)(uintptr_t)hTableId, pu8Key, pData);
    return ret == 0 ? KNET_OK : KNET_ERROR;
}

static int KNET_ACC_HashTblDelEntry(DP_HashTbl_t hTableId, const uint8_t *pu8Key)
{
    int ret;
    ret = KNET_HashTblDelEntry((uint32_t)(uintptr_t)hTableId, pu8Key);
    return ret == 0 ? KNET_OK : KNET_ERROR;
}

static int KNET_ACC_HashTblLookupEntry(DP_HashTbl_t hTableId, const uint8_t *pu8Key, void *pData)
{
    int ret;
    ret = KNET_HashTblLookupEntry((uint32_t)(uintptr_t)hTableId, pu8Key, pData);
    return ret == 0 ? KNET_OK : KNET_ERROR;
}

static int KNET_ACC_HashTblGetInfo(DP_HashTbl_t hTableId, DP_HashTblSummaryInfo_t *pstHashSummaryInfo)
{
    KNET_HashTblInfo info = {0};
    int ret = 0;

    if (pstHashSummaryInfo == NULL) {
        KNET_ERR("Invali summary info param");
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

static int KNET_ACC_HashTblEntryGetFirst(DP_HashTbl_t hTableId, uint8_t *pu8Key, void *pData)
{
    int ret;
    // hTableId小于UINT32_MAX
    ret = KNET_GetHashTblFirstEntry((uint32_t)(uintptr_t)hTableId, pu8Key, pData);
    return ret == 0 ? KNET_OK : KNET_ERROR;
}

static int KNET_ACC_HashTblEntryGetNext(DP_HashTbl_t hTableId, uint8_t *pu8Key, void *pData, uint8_t *pu8NextKey)
{
    int ret;
    // hTableId小于UINT32_MAX
    ret = KNET_GetHashTblNextEntry((uint32_t)(uintptr_t)hTableId, pu8Key, pu8NextKey, pData);
    return ret == 0 ? KNET_OK : KNET_ERROR;
}

uint32_t KNET_RegHashTable(void)
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

    KnetStatOutputType outType = (KnetStatOutputType)flag;

    if (outType == KNET_STAT_OUTPUT_TO_LOG) {
        KNET_DEBUG("%s", output);
    } else if (outType == KNET_STAT_OUTPUT_TO_TELEMETRY) {
        if (KNET_DebugOutputToTelemetry(output, len) != KNET_OK) {
            KNET_ERR("K-NET stat output to telemetry failed");
            return KNET_ERROR;
        }
    } else if (outType == KNET_STAT_OUTPUT_TO_SCREEN) {
        printf("%s\n", output);
    }

    return KNET_OK;
}

uint32_t KNET_RegDebug(void)
{
    if (KNET_TelemetryDpShowStatisticsHookReg(DP_ShowStatistics) != KNET_OK) {
        KNET_ERR("K-NET register telemetry run dp show statistics failed");
        return KNET_ERROR;
    }

    DP_DebugShowHook hook = KNET_ACC_Debug;
    return DP_DebugShowHookReg(hook);
}

uint32_t KNET_HandleInit(void)
{
    g_knetMemHandle = 0;

    for (uint32_t i = 0; i < KNET_MBUF_MEM_HNDLE_MAX_NUM; i++) {
        g_knetMbufMemHandleCtrl.handleIdToMapId[i] = INVALID_MAP_ID;
    }

    return KNET_OK;
}

int KNET_ACC_EventNotify(DP_AddrEventType_t type, const DP_AddrEvent_t *addrEvent)
{
    if (KNET_GetCfg(CONF_COMMON_MODE).intValue != KNET_RUN_MODE_MULTIPLE) {
        return 0;
    }

    if (addrEvent == NULL) {
        KNET_ERR("Invalid addrEvent");
        return KNET_ERROR;
    }

    if (((struct sockaddr *)&addrEvent->localAddr)->sa_family != AF_INET) {
        KNET_ERR("Invalid address family, expected AF_INET");
        return KNET_ERROR;
    }

    const struct sockaddr_in *addrIn = (const struct sockaddr_in *)&addrEvent->localAddr;

    uint32_t ip = addrIn->sin_addr.s_addr;
    uint32_t port = ntohs(addrIn->sin_port);
    int32_t proto = addrEvent->protocol;

    if (ip == INVALID_IP) {
        KNET_ERR("Invalid ip %u", ip);
        return KNET_ERROR;
    }

    if (port == INVALID_PORT) {
        KNET_ERR("Invalid port %u", port);
        return KNET_ERROR;
    }

    if (proto != IPPROTO_TCP && proto != IPPROTO_UDP) {
        KNET_ERR("Invalid proto %d", proto);
        return KNET_ERROR;
    }

    // 根据事件类型进行处理
    switch (type) {
        case DP_ADDR_EVENT_CREATE:
            KNET_DEBUG("Socket bound: IP=%u, Port=%u, Proto=%d", ip, port, proto);
            break;
        case DP_ADDR_EVENT_RELEASE:
            KNET_DEBUG("Socket destroyed: IP=%u, Port=%u, Proto=%d", ip, port, proto);
            break;
        default:
            KNET_ERR("%d not support", type);
            return KNET_ERROR;
    }

    return KNET_EventNotify(ip, port, proto, type);
}

uint32_t KNET_RegFdir(void)
{
    DP_AddrHooks_t addrHook = {0};
    addrHook.eventNotify = KNET_ACC_EventNotify;
    int ret = DP_AddrHooksReg(&addrHook);
    return ret == 0 ? KNET_OK : KNET_ERROR;
}

uint32_t KNET_RegFunc(void)
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