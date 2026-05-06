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

#include "dp_pbuf_api.h"

#include "knet_log.h"
#include "knet_pkt.h"
#include "knet_pktpool.h"
#include "knet_fmm.h"
#include "knet_stk_mp.h"
#include "knet_sal_mp.h"
#include "knet_sal_func.h"

#define KNET_MEM_POOL_ID_OFFSET 32

/* mbuf pkt pool 创建所需参数 */
#define KNET_MBUF_DEFAULT_BUFNUM 65536
#define KNET_MBUF_DEFAULT_CACHENUM 128
#define KNET_MBUF_DEFAULT_CACHESIZE 512
#define KNET_MBUF_DEFAULT_PRIVATE_SIZE 256
#define KNET_MBUF_DEFAULT_DATAROOM_SIZE 2048
#define KNET_MEM_DEFAULT_CACHESIZE 256

#define KNET_SOCKET_ANY (~(uint32_t)0)

/**
 * @brief pool id 根据不同 K-NET 内存池类型划分为各个不同的区间
 *
 *  [KNET_MBUF_POOL_ID_OFFSET, KNET_FMM_POOL_ID_OFFSET - 1] : mbuf pool id 的范围
 *  [KNET_FMM_POOL_ID_OFFSET, KNET_STK_POOL_ID_OFFSET - 1] ： fixed mempool id 的范围
 *  [KNET_STK_POOL_ID_OFFSET, KNET_POOL_ID_MAX] ： stack mempool id 的范围
 */
enum {
    KNET_MBUF_POOL_ID_OFFSET = 0,
    KNET_FMM_POOL_ID_OFFSET = KNET_MBUF_POOL_ID_OFFSET + KNET_PKTPOOL_POOL_MAX_NUM,
    KNET_EBUF_POOL_ID_OFFSET = KNET_FMM_POOL_ID_OFFSET + KNET_PKTPOOL_POOL_MAX_NUM,
    KNET_STK_POOL_ID_OFFSET = KNET_EBUF_POOL_ID_OFFSET + KNET_FMM_POOL_MAX_NUM,
    KNET_POOL_ID_MAX = KNET_STK_POOL_ID_OFFSET + KNET_STK_MP_MAX_NUM,
};


#define KNET_MP_HNDLE_MAX_NUM KNET_POOL_ID_MAX
#define KNET_MP_HNDLE_TYPE_NUM 4
#define KNET_MP_HNDLE_TYPE_INVALID 0xFFFFFFFF

typedef struct {
    uint32_t type;
    DP_Mempool handler;
} KnetMempoolHandleCtrl;

typedef struct {
    pthread_mutex_t mutex;
    KnetMempoolHandleCtrl mpHandleCtrl[KNET_MP_HNDLE_MAX_NUM];
}KnetSalMpCtrl;

/* mbuf、内存池句柄类型映射表信息 */
static KnetSalMpCtrl g_knetSalMpCtrl = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
};

uint32_t KnetHandleInit(void)
{
    for (uint32_t i = 0; i < KNET_MP_HNDLE_MAX_NUM; i++) {
        g_knetSalMpCtrl.mpHandleCtrl[i].type = KNET_MP_HNDLE_TYPE_INVALID;
    }

    return KNET_OK;
}

KNET_STATIC uint32_t MpHandleCtrlCreate(const DP_Mempool handler, const uint32_t type)
{
    uint64_t handleId = (uint64_t)(uintptr_t)handler;
    if (unlikely(handleId >= KNET_MP_HNDLE_MAX_NUM)) {
        KNET_ERR("Mempool handle ctrl create failed, handleId %lu is invalid, max %lu",
            handleId, KNET_MP_HNDLE_MAX_NUM - 1);
        return KNET_ERROR;
    }

    pthread_mutex_lock(&(g_knetSalMpCtrl.mutex));

    KnetMempoolHandleCtrl *handleCtrl = &g_knetSalMpCtrl.mpHandleCtrl[handleId];
    handleCtrl->handler = handler;
    handleCtrl->type = type;

    pthread_mutex_unlock(&(g_knetSalMpCtrl.mutex));

    return KNET_OK;
}

KNET_STATIC void MpHandleCtrlDestroy(const DP_Mempool handler)
{
    uint64_t handleId = (uint64_t)(uintptr_t)handler;
    if (unlikely(handleId >= KNET_MP_HNDLE_MAX_NUM)) {
        KNET_ERR("Mempool handle ctrl destroy failed, handleId %lu is invalid, max %lu",
            handleId, KNET_MP_HNDLE_MAX_NUM - 1);
        return;
    }

    pthread_mutex_lock(&(g_knetSalMpCtrl.mutex));

    KnetMempoolHandleCtrl *handleCtrl = &g_knetSalMpCtrl.mpHandleCtrl[handleId];
    handleCtrl->handler = NULL;
    handleCtrl->type = KNET_MP_HNDLE_TYPE_INVALID;

    pthread_mutex_unlock(&(g_knetSalMpCtrl.mutex));
    return;
}

KNET_STATIC uint32_t MbufMempoolGetType(const DP_Mempool handler)
{
    uint64_t handleId = (uint64_t)(uintptr_t)handler;
    if (unlikely(handleId >= KNET_MP_HNDLE_MAX_NUM)) {
        return KNET_MP_HNDLE_TYPE_INVALID;
    }

    KnetMempoolHandleCtrl *handleCtrl = &g_knetSalMpCtrl.mpHandleCtrl[handleId];
    return handleCtrl->type;
}

typedef uint32_t (*KnetMpCreate)(const DP_MempoolCfg_S *cfg, uint32_t *poolId);
typedef uint32_t (*KnetMpDestroy)(uint32_t poolId);
typedef void* (*KnetMpAlloc)(uint32_t poolId);
typedef void (*KnetMpFree)(uint32_t poolId, void *ptr);
typedef void* (*KnetMpConstruct)(uint32_t poolId, void *addr, uint64_t offset, uint16_t len);

/* mp 操作 table 表项 */
typedef struct {
    uint32_t mpOffset;        // pool id 的 offset
    KnetMpCreate mpCreat;
    KnetMpDestroy mpDestroy;
    KnetMpAlloc mpAlloc;
    KnetMpFree mpFree;
    KnetMpConstruct mpConstruct;
} KnetSalMpOpEntry;

/* pbuf pool 类型的操作集合 */
static int32_t MbufPoolCfg(KNET_PktPoolCfg *stPktpoolCfg, const char *name)
{
    int32_t ret;

    ret = strncpy_s(stPktpoolCfg->name, KNET_PKTPOOL_NAME_LEN - 1, name, strlen(name));
    if (unlikely(ret != EOK)) {
        KNET_ERR("Mbuf pool cfg set failed, strncpy_s failed, ret %d", ret);
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

static uint32_t g_pbufPoolId;

static uint32_t PbufMpCreate(const DP_MempoolCfg_S *cfg, uint32_t *poolId)
{
    int32_t ret;
    KNET_PktPoolCfg stPktpoolCfg = {0};
    ret = MbufPoolCfg(&stPktpoolCfg, cfg->name);
    if (ret != KNET_OK) {
        KNET_ERR("Pbuf mempool create failed, cfg set failed, ret %d", ret);
        return KNET_ERROR;
    }

    ret = (int32_t)KNET_PktPoolCreate(&stPktpoolCfg, poolId);
    if (ret != KNET_OK) {
        KNET_ERR("Pbuf mempool create failed, pkt pool create failed, ret %d", ret);
        return KNET_ERROR;
    }

    g_pbufPoolId = *poolId;

    return KNET_OK;
}

static uint32_t PbufMpDestroy(uint32_t poolId)
{
    KNET_PktPoolDestroy(poolId);
    return KNET_OK;
}

static void *PbufMpAlloc(uint32_t poolId)
{
    struct rte_mbuf *mbuf = KNET_PktAlloc(poolId);
    if (unlikely(mbuf == NULL)) {
        return NULL;
    }

    void *ptr = KNET_Mbuf2Pkt(mbuf);
    DP_PbufRawReset(ptr, mbuf->buf_addr, mbuf->buf_len);
    return ptr;
}

static void PbufMpFree(uint32_t poolId, void *ptr)
{
    /**
    * 如果 pbuf 的引用计数减 1 后为 0，说明协议栈不再持有 pbuf ，此时 mbuf 的引用计数有两种情况:
    * 1: 驱动释放了一次 mbuf ，mbuf 引用计数减为了 1，
    * 2: 驱动还没有释放 mbuf ，mbuf 引用计数仍为 2，
    * 上面两种情况都可以直接调用 mbuf 带引用计数的释放函数来释放 mbuf
    */
    if (KNET_UNLIKELY(ptr == NULL)) {
        KNET_ERR("Pbuf free failed, input ptr is null");
        return;
    }

    struct rte_mbuf *mbuf = KNET_Pkt2Mbuf(ptr);
    KNET_PktFree(mbuf);
}

static void KnetMbufDefaultFreeCb(void *addr, void *opaque)
{
    (void)addr;

    if (opaque == NULL) {
        KNET_ERR("K-NET mbuf default free callback, opaque is null");
        return;
    }

    struct KNET_ExtBuf *ebuf = (struct KNET_ExtBuf *)opaque;
    if (ebuf->freeCb != NULL) {
        ebuf->freeCb(ebuf->addr, ebuf->opaque);
    }
}

static void *PbufMpConstruct(uint32_t poolId, void *addr, uint64_t offset, uint16_t len)
{
    if (addr == NULL) {
        KNET_ERR("Pbuf mempool construct failed, input addr ptr is null");
        return NULL;
    }

    // 分配单片
    struct rte_mbuf *mbuf = KNET_PktAlloc(poolId);
    if (unlikely(mbuf == NULL)) {
        KNET_ERR("Pbuf mempool construct failed, mbuf alloc failed");
        return NULL;
    }

    struct KNET_ExtBuf *ebuf = KnetPtrSub(addr, sizeof(struct KNET_ExtBuf));
    struct rte_mbuf_ext_shared_info *shinfo = (struct rte_mbuf_ext_shared_info *)ebuf;
    shinfo->free_cb = KnetMbufDefaultFreeCb;
    shinfo->fcb_opaque = ebuf;
    uint64_t iova = rte_mempool_virt2iova(ebuf) + (uint64_t)sizeof(struct KNET_ExtBuf) + offset;

    // 将 mbuf attach 到外部缓冲区
    KNET_MbufAttachExtBuf(mbuf, addr + offset, iova, len, shinfo);
    DP_Pbuf_t *pkt = KNET_Mbuf2Pkt(mbuf);
    DP_PbufRawReset(pkt, mbuf->buf_addr, mbuf->buf_len);

    return pkt;
}

/* fixed mempool 类型的操作集合 */
static int32_t FmmPoolCfg(KNET_FmmPoolCfg *stFmmpoolCfg, const char *name, const uint32_t size, const uint32_t count)
{
    int32_t ret;

    ret = strncpy_s(stFmmpoolCfg->name, KNET_PKTPOOL_NAME_LEN - 1, name, strlen(name));
    if (ret != EOK) {
        KNET_ERR("Fixed mempool cfg set failed, strncpy_s failed, ret %d", ret);
        return -1;
    }

    int workerNum = KNET_GetCfg(CONF_TCP_MAX_WORKER_NUM)->intValue;
    
    size_t nameLen = strlen(stFmmpoolCfg->name);
    stFmmpoolCfg->name[nameLen] = '\0';
    stFmmpoolCfg->eltSize   = size;
    stFmmpoolCfg->eltNum    = count + KNET_MEM_DEFAULT_CACHESIZE * (uint32_t)workerNum;
    stFmmpoolCfg->cacheSize = KNET_FmmNormalizeCacheSize(stFmmpoolCfg->eltNum, KNET_MEM_DEFAULT_CACHESIZE);
    stFmmpoolCfg->socketId  = KNET_SOCKET_ANY;
    stFmmpoolCfg->objInit   = NULL;

    return ret;
}

static uint32_t FixedMpCreate(const DP_MempoolCfg_S *cfg, uint32_t *poolId)
{
    int32_t ret;
    KNET_FmmPoolCfg stFmmpoolCfg = {0};
    ret = FmmPoolCfg(&stFmmpoolCfg, cfg->name, cfg->size, cfg->count);
    if (ret != KNET_OK) {
        KNET_ERR("Fixed mempool create failed, cfg set failed, ret %d, size %u, count %u", ret, cfg->size, cfg->count);
        return KNET_ERROR;
    }

    ret = (int32_t)KNET_FmmCreatePool(&stFmmpoolCfg, poolId);
    if (ret != KNET_OK) {
        KNET_ERR("Fixed mempool create failed, fmm create pool failed, ret %d", ret);
        return KNET_ERROR;
    }

    return KNET_OK;
}

static uint32_t FixedMpDestroy(uint32_t poolId)
{
    return KNET_FmmDestroyPool(poolId);
}

static void *FixedMpAlloc(uint32_t poolId)
{
    void *ptr = NULL;
    uint32_t ret = KNET_FmmAlloc(poolId, &ptr);
    if (unlikely(ret != KNET_OK)) {
        KNET_ERR("Fixed mempool alloc failed, ret %u, poolId %u", ret, poolId);
        return NULL;
    }

    return ptr;
}

static void FixedMpFree(uint32_t poolId, void *ptr)
{
    (void)KNET_FmmFree(poolId, ptr);
}

/* extern buffer mempool 操作集合 */
static int32_t EbufPoolCfg(KNET_FmmPoolCfg *stFmmpoolCfg, const char *name, const uint32_t size, const uint32_t count)
{
    int32_t ret = strncpy_s(stFmmpoolCfg->name, KNET_FMM_POOL_NAME_LEN - 1, name, strlen(name));
    if (unlikely(ret != EOK)) {
        KNET_ERR("Ebuf mempool cfg set failed, strncpy_s failed, ret %d", ret);
        return -1;
    }

    size_t nameLen = strlen(stFmmpoolCfg->name);
    stFmmpoolCfg->name[nameLen] = '\0';
    stFmmpoolCfg->eltSize = (uint32_t)KNET_GetCfg(CONF_TCP_SGE_LEN)->intValue + (uint32_t)sizeof(struct KNET_ExtBuf);
    stFmmpoolCfg->eltNum = (uint32_t)KNET_GetCfg(CONF_TCP_SGE_NUM)->intValue;
    stFmmpoolCfg->cacheSize = KNET_MEM_DEFAULT_CACHESIZE;
    stFmmpoolCfg->socketId = KNET_SOCKET_ANY;
    stFmmpoolCfg->objInit = NULL;
    return ret;
}

static uint32_t EbufMpCreate(const DP_MempoolCfg_S *cfg, uint32_t *poolId)
{
    int32_t ret;
    KNET_FmmPoolCfg stFmmpoolCfg = {0};
    ret = EbufPoolCfg(&stFmmpoolCfg, cfg->name, cfg->size, cfg->count);
    if (ret != KNET_OK) {
        KNET_ERR("Ebuf mempool create failed, cfg set failed, ret %d", ret);
        return KNET_ERROR;
    }

    ret = (int32_t)KNET_FmmCreatePool(&stFmmpoolCfg, poolId);
    if (ret != KNET_OK) {
        KNET_ERR("Ebuf mempool create failed, fmm create pool failed, ret %d", ret);
        return KNET_ERROR;
    }

    return KNET_OK;
}

static uint32_t EbufMpDestroy(uint32_t poolId)
{
    return KNET_FmmDestroyPool(poolId);
}

static void *EbufMpAlloc(uint32_t poolId)
{
    struct KNET_ExtBuf* ebuf = NULL;
    uint32_t ret = KNET_FmmAlloc(poolId, (void**)&ebuf);
    if (unlikely(ebuf == NULL)) {
        KNET_ERR("Extern buffer mempool alloc failed, ret %u, poolId %u", ret, poolId);
        return NULL;
    }

/* 编译期检测 KNET_MbufExtSharedInfo 与 rte_mbuf_ext_shared_info 布局一致 */
    RTE_BUILD_BUG_ON(offsetof(struct KNET_ExtBuf, freeCb) !=
                    offsetof(struct rte_mbuf_ext_shared_info, free_cb));
    RTE_BUILD_BUG_ON(offsetof(struct KNET_ExtBuf, opaque) !=
                    offsetof(struct rte_mbuf_ext_shared_info, fcb_opaque));
    RTE_BUILD_BUG_ON(offsetof(struct KNET_ExtBuf, refcnt) !=
                    offsetof(struct rte_mbuf_ext_shared_info, refcnt));

    void *ptr = KnetPtrAdd(ebuf, sizeof(struct KNET_ExtBuf));
    uint32_t offset = 0;
    uint32_t idx = 0;
    for (; idx < ((uint32_t)KNET_GetCfg(CONF_TCP_SGE_LEN)->intValue + PER_EBUF_MBUF_SIZE - 1) / PER_EBUF_MBUF_SIZE; ++idx) {
        struct rte_mbuf* mbuf = KNET_PktAlloc(poolId);
        if (unlikely(mbuf == NULL)) {
            KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "knet acc mbuf mem pool alloc null, poolId %u", g_pbufPoolId);
            return NULL;
        }
        struct rte_mbuf_ext_shared_info* shinfo = (struct rte_mbuf_ext_shared_info*)ebuf;
        uint16_t len = offset + PER_EBUF_MBUF_SIZE <= (uint32_t)KNET_GetCfg(CONF_TCP_SGE_LEN)->intValue ?
        PER_EBUF_MBUF_SIZE : (uint32_t)KNET_GetCfg(CONF_TCP_SGE_LEN)->intValue - offset;
        uint64_t iova = rte_mempool_virt2iova(ebuf) + (uint64_t)sizeof(struct KNET_ExtBuf) + offset;
        KNET_MbufAttachExtBuf(mbuf, ptr + offset, iova, len, shinfo);
        ebuf->bufs[idx] = mbuf;
        offset += PER_EBUF_MBUF_SIZE;
    }
    ebuf->totalBufCnt = idx;
    ebuf->addr = ptr;
    return ptr;
}

static void EbufMpFree(uint32_t poolId, void *ptr)
{
    // 以下仅在tx没有设置用户自定义freecb时才会调用
    if (KNET_UNLIKELY(ptr == NULL)) {
        KNET_ERR("Extern buffer free failed, input ptr is null");
        return;
    }

    struct KNET_ExtBuf* ebuf = KnetPtrSub(ptr, sizeof(struct KNET_ExtBuf));
    // avoid dpdk free, keep KNET_ExtBuf dpdk consist with ebuf
    rte_mbuf_ext_refcnt_set((struct rte_mbuf_ext_shared_info*)ebuf, ebuf->totalBufCnt + 2);
    for (int i = 0; i < ebuf->totalBufCnt; i++) {
        KNET_PktFree(ebuf->bufs[i]);
    }
    (void)KNET_FmmFree(poolId, ebuf);
}

static void *EbufMpConstruct(uint32_t poolId, void *addr, uint64_t offset, uint16_t len)
{
    if (addr == NULL) {
        return NULL;
    }

    DP_Pbuf_t* pkt = NULL;
    struct rte_mbuf* mbuf = KNET_PktAlloc(poolId);
    if (unlikely(mbuf == NULL)) {
        return NULL;
    }
    pkt = KNET_Mbuf2Pkt(mbuf);
    DP_PbufRawReset(pkt, mbuf->buf_addr, mbuf->buf_len);
    return pkt;
}

/* ref pbuf pool 操作集合 */
struct ShinfoHead { // 用于 ref pbuf 对 extern buffer 的引用计数操作，在 ref pbuf 的起始地址之前存放 shinfo 指针
    struct rte_mbuf_ext_shared_info *shinfo;
};

/**
 * @brief RefPbufMpCreate为创建ref_pbuf内存的回调函数
 *
 * @param cfg 为dp传入的所需的内存的size和count，但是为了避免CONF_TCP_SGE_NUM配置给dp，dp又设置入参传给knet，此处并未使用cfg入参，
 *            而是直接获取CONF_TCP_SGE_NUM的值。
 * @param poolId 内存池的pool id
 * @return uint32_t KNET_OK：表示成功；KNET_ERROR表示失败
 */
static uint32_t RefPbufMpCreate(const DP_MempoolCfg_S *cfg, uint32_t *poolId)
{
    /* Pbuf 头部前预留存放 shinfo 指针的空间 */
    uint32_t size = (uint32_t)sizeof(struct ShinfoHead) + (uint32_t)sizeof(DP_Pbuf_t);
    uint32_t count = (uint32_t)KNET_GetCfg(CONF_TCP_SGE_NUM)->intValue;
    uint32_t ret = KNET_StkMpInit(size, count);
    if (ret != KNET_OK) {
        KNET_ERR("Ref pbuf mempool create failed, knet stack memppol init failed, ret %u", ret);
        return KNET_ERROR;
    }
    
    *poolId = KNET_STK_MP_POOLID;
    return KNET_OK;
}

static uint32_t RefPbufMpDestroy(uint32_t poolId)
{
    (void)poolId;
    return KNET_StkMpDeInit();
}

static void *RefPbufMpAlloc(uint32_t poolId)
{
    struct ShinfoHead* head = KNET_StkMpAlloc();
    if (unlikely(head == NULL)) {
        KNET_ERR("Ref pbuf mempool alloc failed, poolId %u", poolId);
        return NULL;
    }

    head->shinfo = NULL;
    void *ptr = KnetPtrAdd(head, sizeof(struct ShinfoHead));
    DP_PbufRawReset(ptr, 0, 0);
    return ptr;
}

static void RefPbufMpFree(uint32_t poolId, void *ptr)
{
    (void)poolId;

    if (KNET_UNLIKELY(ptr == NULL)) {
        KNET_ERR("Ref pbuf free failed, input ptr is null");
        return;
    }

    struct ShinfoHead *head = (struct ShinfoHead *)KnetPtrSub(ptr, sizeof(struct ShinfoHead));
    if (head->shinfo != NULL) {
        /* ref pbuf只在共线程使用，无需原子操作 */
        head->shinfo->refcnt--; // 令 extern buffer 的引用计数减 1
        head->shinfo = NULL;
    }

    KNET_StkMpFree((void *)head);
}

static void *RefPbufMpConstruct(uint32_t poolId, void *addr, uint64_t offset, uint16_t len)
{
    if (addr == NULL) {
        KNET_ERR("Pbuf mempool construct failed, input addr ptr is null");
        return NULL;
    }

    struct ShinfoHead* head = KNET_StkMpAlloc();
    if (unlikely(head == NULL)) {
        return NULL;
    }
    
    head->shinfo = KnetPtrSub(addr, sizeof(struct KNET_ExtBuf));
    /* ref pbuf只在共线程使用，无需原子操作 */
    head->shinfo->refcnt++; // 令 extern buffer 的引用计数加 1

    void *ptr = KnetPtrAdd(head, sizeof(struct ShinfoHead));
    DP_PbufRawReset(ptr, addr + offset, len);
    return ptr;
}

static void *MpConstructNULL(uint32_t poolId, void *addr, uint64_t offset, uint16_t len)
{
    (void)poolId;
    (void)addr;
    (void)offset;
    (void)len;
    return NULL;
}

KnetSalMpOpEntry g_knetSalentryable[DP_MEMPOOL_TYPE_MAX] = {
    [DP_MEMPOOL_TYPE_PBUF] = {KNET_MBUF_POOL_ID_OFFSET,
        PbufMpCreate, PbufMpDestroy, PbufMpAlloc, PbufMpFree, PbufMpConstruct},
    [DP_MEMPOOL_TYPE_FIXED_MEM] = {KNET_FMM_POOL_ID_OFFSET,
        FixedMpCreate, FixedMpDestroy, FixedMpAlloc, FixedMpFree, MpConstructNULL},
    [DP_MEMPOOL_TYPE_EBUF] = {KNET_FMM_POOL_ID_OFFSET,
        EbufMpCreate, EbufMpDestroy, EbufMpAlloc, EbufMpFree, EbufMpConstruct},
    [DP_MEMPOOL_TYPE_REF_PBUF] = {KNET_STK_POOL_ID_OFFSET,
        RefPbufMpCreate, RefPbufMpDestroy, RefPbufMpAlloc, RefPbufMpFree, RefPbufMpConstruct},
};

/* 根据 type 获取表项 */
static inline  KnetSalMpOpEntry *KnetGetMpOpEntry(uint32_t type)
{
    if (unlikely(type >= DP_MEMPOOL_TYPE_MAX || type <= DP_MEMPOOL_TYPE_MIN)) {
        KNET_ERR("K-NET get mempool option entry failed, invalid type %u", type);
        return NULL;
    }

    return &g_knetSalentryable[type];
}

int32_t KNET_ACC_CreateMbufMemPool(const DP_MempoolCfg_S *cfg, const DP_MempoolAttr_S *attr, DP_Mempool *handler)
{
    if (cfg == NULL || cfg->name == NULL || handler == NULL) {
        KNET_ERR("K-NET acc create mbuf mempool failed, input null ptr");
        return -1;
    }

    uint32_t ret;
    uint32_t poolId;
    uint32_t type = cfg->type;

    KnetSalMpOpEntry *entry = KnetGetMpOpEntry(type);
    if (entry == NULL) {
        KNET_ERR("K-NET acc create mbuf mempool failed, get option entry failed, type %u", type);
        return -1;
    }

    ret = entry->mpCreat(cfg, &poolId);
    if (ret != 0) {
        KNET_ERR("K-NET acc create mbuf mempool failed, mp create failed, ret %u, type %u", ret, type);
        return -1;
    }

    *handler = (DP_Mempool)(uintptr_t)poolId + entry->mpOffset;
    ret = MpHandleCtrlCreate(*handler, type);
    if (ret != 0) {
        KNET_ERR("K-NET acc create mbuf mempool failed, handle create failed, ret %u, type %u", ret, type);
        ret = entry->mpDestroy(poolId);
        if (ret != 0) {
            KNET_ERR("K-NET acc create mbuf mempool failed, mp destroy failed, ret %u, type %u", ret, type);
        }
        return -1;
    }

    return 0;
}

void KNET_ACC_DestroyMbufMemPool(DP_Mempool mp)
{
    uint32_t type = MbufMempoolGetType(mp);
    if (type == KNET_MP_HNDLE_TYPE_INVALID) {
        KNET_ERR("K-NET acc destory mbuf mempool failed, type invalid, type %u", type);
        return;
    }

    KnetSalMpOpEntry *entry = KnetGetMpOpEntry(type);
    if (entry == NULL) {
        KNET_ERR("K-NET acc destory mbuf mempool failed, get option entry failed, type %u", type);
        return;
    }

    uint32_t poolId = (uint32_t)(uintptr_t)mp - entry->mpOffset;
    uint32_t ret = entry->mpDestroy(poolId);
    if (ret != 0) {
        KNET_ERR("K-NET acc destroy mbuf mempool failed, mp destroy failed, ret %u, type %u", ret, type);
        return;
    }

    MpHandleCtrlDestroy(mp);
    return;
}

void KNET_ACC_MbufMemPoolFree(DP_Mempool mp, void *ptr)
{
    if (unlikely(ptr == NULL)) {
        KNET_ERR("K-NET acc mbuf mempool free failed, null pointer failed to free");
        return;
    }

    uint32_t type = MbufMempoolGetType(mp);
    if (unlikely(type == KNET_MP_HNDLE_TYPE_INVALID)) {
        KNET_ERR("K-NET acc mbuf mempool free failed, type invalid, type %u", type);
        return;
    }

    KnetSalMpOpEntry *entry = &g_knetSalentryable[type];

    uint32_t poolId = (uint32_t)(uintptr_t)mp - entry->mpOffset;
    entry->mpFree(poolId, ptr);
    return;
}

void *KNET_ACC_MbufMemPoolAlloc(DP_Mempool mp)
{
    uint32_t type = MbufMempoolGetType(mp);
    if (unlikely(type == KNET_MP_HNDLE_TYPE_INVALID)) {
        KNET_ERR("K-NET acc mbuf mempool alloc failed, type invalid, type %u", type);
        return NULL;
    }

    KnetSalMpOpEntry *entry = &g_knetSalentryable[type];

    uint32_t poolId = (uint32_t)(uintptr_t)mp - entry->mpOffset;
    return entry->mpAlloc(poolId);
}

void *KNET_ACC_MbufConstruct(DP_Mempool mp, void *addr, uint64_t offset, uint16_t len)
{
    uint32_t type = MbufMempoolGetType(mp);
    if (unlikely(type == KNET_MP_HNDLE_TYPE_INVALID)) {
        KNET_ERR("K-NET acc mbuf mempool construct failed, type invalid, type %u", type);
        return NULL;
    }

    KnetSalMpOpEntry *entry = &g_knetSalentryable[type];

    uint32_t poolId = (uint32_t)(uintptr_t)mp - entry->mpOffset;
    return entry->mpConstruct(poolId, addr, offset, len);
}