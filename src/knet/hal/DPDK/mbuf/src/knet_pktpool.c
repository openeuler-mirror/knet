/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 报文池相关操作
 */

#include <sys/mman.h>

#include "rte_config.h"
#include "rte_errno.h"
#include "rte_mempool.h"
#include "rte_mbuf.h"
#include "securec.h"
#include "knet_types.h"
#include "knet_log.h"
#include "knet_mem.h"
#include "knet_pktpool.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 报文池创建算法名称 */
char *g_knetPktPoolAlgName[KNET_PKTPOOL_ALG_BUTT] = {
    "ring_mp_mc",
};

/* PKT模块控制块 */
typedef struct {
    pthread_mutex_t mutex;                              /**< 访问KnetPktModCtrl的互斥锁 */
    uint32_t memHandleId;                               /**< 内存句柄 */
    uint32_t heapMemPtId;                               /**< 堆内存ID */
    KnetPktPoolCtrl poolCtrl[KNET_PKTPOOL_POOL_MAX_NUM];  /**< 报文池控制块数组 */
} KnetPktModCtrl;

static KnetPktModCtrl g_knetPktModCtrl;

static uint32_t g_pktPoolId = KNET_PKTPOOL_INVALID_ID;

static bool PktIsPoolExist(const char *poolName)
{
    uint32_t i;
    KnetPktPoolCtrl *poolCtrl = g_knetPktModCtrl.poolCtrl;

    for (i = 0; i < KNET_PKTPOOL_POOL_MAX_NUM; i++) {
        if (poolCtrl[i].mempool == NULL) {
            continue;
        }

        if (strncmp(poolName, poolCtrl[i].poolName, KNET_PKTPOOL_NAME_LEN) == 0) {
            return true;
        }
    }

    return false;
}

static uint32_t PktGetFreePoolId(uint32_t *poolId)
{
    uint32_t i;
    KnetPktPoolCtrl *poolCtrl = g_knetPktModCtrl.poolCtrl;

    for (i = 0; i < KNET_PKTPOOL_POOL_MAX_NUM; i++) {
        if (poolCtrl[i].mempool == NULL) {
            *poolId = i;
            return KNET_OK;
        }
    }

    return KNET_ERROR;
}

struct rte_mempool *KnetPktGetMemPool(uint32_t poolId)
{
    if (poolId >= KNET_PKTPOOL_POOL_MAX_NUM) {
        return NULL;
    }

    return g_knetPktModCtrl.poolCtrl[poolId].mempool;
}

KnetPktPoolCtrl *KnetPktGetPoolCtrl(uint32_t poolId)
{
    if ((poolId >= KNET_PKTPOOL_POOL_MAX_NUM) ||
        (g_knetPktModCtrl.poolCtrl[poolId].mempool == NULL)) {
        KNET_ERR("K-NET pktPoolId %u not exist", poolId);
        return NULL;
    }

    return &(g_knetPktModCtrl.poolCtrl[poolId]);
}

static uint32_t PktPoolCfgSanityCheck(const KNET_PktPoolCfg *cfg)
{
    size_t nameLen = strlen(cfg->name);
    if ((nameLen == 0) || (nameLen >= KNET_PKTPOOL_NAME_LEN)) {
        KNET_ERR("Pkt pool sanity check, invalid pool name. (length %u, name %.*s)",
            nameLen, KNET_PKTPOOL_NAME_LEN - 1, cfg->name);
        return KNET_ERROR;
    }

    if (cfg->bufNum == 0 || cfg->bufNum < cfg->cacheSize * CACHE_FLUSHTHRESH_SIZE_MULTIPLIER) {
        KNET_ERR("Pkt pool sanity check, buf number is invalid. (bufNum %u, cacheSize %u)",
            cfg->bufNum, cfg->cacheSize);
        return KNET_ERROR;
    }

    if (((cfg->cacheNum == 0) && (cfg->cacheSize != 0)) || (cfg->cacheNum > KNET_PKTPOOL_CACHE_MAX_NUM)) {
        KNET_ERR("Pkt pool sanity check, cache num invalid. (num %u, size %u)",
            cfg->cacheNum, cfg->cacheSize);
        return KNET_ERROR;
    }

    if (((cfg->cacheNum != 0) && (cfg->cacheSize == 0)) || (cfg->cacheSize > KNET_PKTPOOL_CACHE_MAX_SIZE)) {
        KNET_ERR("Pkt pool sanity check, cache size invalid. (num %u, size %u)",
            cfg->cacheNum, cfg->cacheSize);
        return KNET_ERROR;
    }

    if ((cfg->privDataSize == 0) || (cfg->privDataSize & 0x7)) {  // 7用于检查8字节对齐
        KNET_ERR("Pkt pool sanity check, privDataRoom size invalid. (privDataSize %u)",
            cfg->privDataSize);
        return KNET_ERROR;
    }

    if (cfg->headroomSize & 0x7) {  // 7用于检查8字节对齐
        KNET_ERR("Pkt pool sanity check, headroom size invalid. (headroomSize %u)",
            cfg->headroomSize);
        return KNET_ERROR;
    }

    if ((cfg->dataroomSize < KNET_PKTPOOL_DATAROOM_MIN_SIZE ||
        (cfg->dataroomSize > UINT16_MAX - RTE_PKTMBUF_HEADROOM))) {
        KNET_ERR("Pkt pool sanity check, dataroom size invalid. (dataroomsize %u)",
            cfg->dataroomSize);
        return KNET_ERROR;
    }

    if (cfg->createAlg >= KNET_PKTPOOL_ALG_BUTT) {
        KNET_ERR("Pkt pool sanity check, invalid pool create algorithm. (createAlg %u)",
            cfg->createAlg);
        return KNET_ERROR;
    }

    if ((cfg->numaId > RTE_MAX_NUMA_NODES) || (cfg->numaId < SOCKET_ID_ANY)) {
        KNET_ERR("Pkt pool sanity check, numaId invalid. numaId %d, RTE_MAX_NUMA_NODES %d, SOCKET_ID_ANY %d",
            cfg->numaId, RTE_MAX_NUMA_NODES, SOCKET_ID_ANY);
        return KNET_ERROR;
    }

    return KNET_OK;
}

void PktPoolObjInitCallback(struct rte_mempool *mp, void *para, void *obj, unsigned index)
{
    (void)mp;
    (void)index;

    void *pkt = KNET_Mbuf2Pkt((struct rte_mbuf *)obj);
    KNET_PktInit init = (KNET_PktInit)para;
    if (init != NULL) {
        init(pkt);
    }
}

static struct rte_mempool *MemPoolCreate(const KNET_PktPoolCfg *cfg)
{
    uint32_t privDataSize = (uint32_t)cfg->privDataSize + KNET_PKT_DBG_SIZE;
    /* 若配置的headroom比mbuf固定预留的headroom长，则在mbuf私有数据区多申请空间 */
    if (cfg->headroomSize > RTE_PKTMBUF_HEADROOM) {
        privDataSize += (uint32_t)cfg->headroomSize - RTE_PKTMBUF_HEADROOM;
    }

    if (RTE_ALIGN(privDataSize, (uint32_t)RTE_MBUF_PRIV_ALIGN) != privDataSize) {
        privDataSize = RTE_ALIGN(privDataSize, (uint32_t)RTE_MBUF_PRIV_ALIGN);
    }
    if (privDataSize > UINT16_MAX) {
        KNET_ERR("Private data size out of range %u > %u", privDataSize, UINT16_MAX);
        return NULL;
    }

    uint32_t dataroomSize = (uint32_t)cfg->dataroomSize + RTE_PKTMBUF_HEADROOM;
    if (dataroomSize > UINT16_MAX) {
        KNET_ERR("Data room size out of range %u > %u", dataroomSize, UINT16_MAX);
        return NULL;
    }

    struct rte_mempool *mp = rte_pktmbuf_pool_create_by_ops(cfg->name, cfg->bufNum, cfg->cacheSize, privDataSize,
        dataroomSize, cfg->numaId, g_knetPktPoolAlgName[cfg->createAlg]);
    if (mp == NULL) {
        KNET_ERR("Pkt pool create failed, empty mp create failed. (errno %d, name %s, bufnum %u, "
            "cacheSize %u, numaId %u)", rte_errno, cfg->name, cfg->bufNum,
            cfg->cacheSize, cfg->numaId);
        return NULL;
    }

    return mp;
}

static uint32_t PktPoolCreate(uint32_t poolId, const KNET_PktPoolCfg *cfg)
{
    struct rte_mempool *mp = MemPoolCreate(cfg);
    if (mp == NULL) {
        KNET_ERR("mem pool create failed. poolId %u", poolId);
        return KNET_ERROR;
    }

    KnetPktPoolCtrl *poolCtrl = &(g_knetPktModCtrl.poolCtrl[poolId]);
    (void)strncpy_s(poolCtrl->poolName, KNET_PKTPOOL_NAME_LEN, cfg->name, KNET_PKTPOOL_NAME_LEN - 1);
    poolCtrl->mempool = mp;

    rte_mempool_obj_iter(mp, PktPoolObjInitCallback, cfg->init);

    return KNET_OK;
}

uint32_t PktPoolCreateInPrimary(const KNET_PktPoolCfg *cfg, uint32_t *poolId)
{
    /* 编译阶段确认偏移值是否发生变化 */
    RTE_BUILD_BUG_ON(MBUF_ATTACHED != (RTE_MBUF_F_INDIRECT | RTE_MBUF_F_EXTERNAL));
    RTE_BUILD_BUG_ON(24 != (RTE_MBUF_L2_LEN_BITS + RTE_MBUF_L3_LEN_BITS + RTE_MBUF_L4_LEN_BITS)); /* 24为比特数目 */
    RTE_BUILD_BUG_ON((16 - RTE_MBUF_TSO_SEGSZ_BITS) != 0);  /* 16为比特数目 */
    RTE_BUILD_BUG_ON(16 != (RTE_MBUF_OUTL3_LEN_BITS + RTE_MBUF_OUTL2_LEN_BITS));  /* 16为比特数目 */

    uint32_t ret;
    ret = PktPoolCfgSanityCheck(cfg);
    if (ret != KNET_OK) {
        KNET_ERR("Pkt pool create, cfg sanity check failed, ret %u.", ret);
        return ret;
    }

    pthread_mutex_lock(&(g_knetPktModCtrl.mutex));
    do {
        if (PktIsPoolExist(cfg->name)) {
            KNET_ERR("Pkt pool create, pool already exist, name %s.", cfg->name);
            ret = KNET_ERROR;
            break;
        }

        ret = PktGetFreePoolId(&g_pktPoolId);
        if (ret != KNET_OK) {
            KNET_ERR("Pkt pool create, get free poolid failed, name %s.", cfg->name);
            break;
        }

        ret = PktPoolCreate(g_pktPoolId, cfg);
        if (ret != KNET_OK) {
            KNET_ERR("Pkt pool create, create pool failed, poolId %u, ret %u.", g_pktPoolId, ret);
            g_pktPoolId = KNET_PKTPOOL_INVALID_ID;
            break;
        }

        *poolId = g_pktPoolId;
    } while (0);
    pthread_mutex_unlock(&(g_knetPktModCtrl.mutex));

    return ret;
}

uint32_t PktPoolGetInSecondary(const char *name, uint32_t *poolId)
{
    uint32_t ret;

    pthread_mutex_lock(&(g_knetPktModCtrl.mutex));
    do {
        ret = PktGetFreePoolId(&g_pktPoolId);
        if (ret != KNET_OK) {
            KNET_ERR("Pkt pool get, get free poolid failed. (name %s)", name);
            break;
        }
        
        struct rte_mempool *mp = NULL;
        mp = rte_mempool_lookup(name);
        if (mp == NULL) {
            KNET_ERR("Pkt pool get, pool dose not exist, slave process can not creat pool. (name %s)", name);
            ret = KNET_ERROR;
            break;
        }

        KnetPktPoolCtrl *poolCtrl = &(g_knetPktModCtrl.poolCtrl[g_pktPoolId]);
        (void)strncpy_s(poolCtrl->poolName, KNET_PKTPOOL_NAME_LEN, name, KNET_PKTPOOL_NAME_LEN - 1);
        poolCtrl->mempool = mp;

        *poolId = g_pktPoolId;
    } while (0);
    pthread_mutex_unlock(&(g_knetPktModCtrl.mutex));

    return ret;
}

uint32_t KNET_PktPoolCreate(const KNET_PktPoolCfg *cfg, uint32_t *poolId)
{
    if (poolId == NULL || cfg == NULL) {
        KNET_ERR("Pkt pool create, null ptr param");
        return KNET_ERROR;
    }

    /* pktPoolId目前仅在KNET constructor函数中创建一次，之后都返回第一次创建的pktPoolId */
    if (g_pktPoolId != KNET_PKTPOOL_INVALID_ID) {
        KNET_DEBUG("K-NET pktPoolId %u already exist", g_pktPoolId);
        *poolId = g_pktPoolId;
        return KNET_OK;
    }

    /* 若为主进程则创建pkt pool */
    if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
        return PktPoolCreateInPrimary(cfg, poolId);
    }

    /* 从进程使用主进程创建的pkt pool */
    return PktPoolGetInSecondary(KNET_PKT_POOL_NAME, poolId);
}

void PktPoolDestroy(uint32_t poolId)
{
    if (poolId >= KNET_PKTPOOL_POOL_MAX_NUM) {
        KNET_ERR("Pkt pool destroy, poolId invalid. (poolId %u)", poolId);
        return;
    }

    KnetPktPoolCtrl *poolCtrl = &(g_knetPktModCtrl.poolCtrl[poolId]);

    pthread_mutex_lock(&(g_knetPktModCtrl.mutex));
    if (poolCtrl->mempool != NULL) {
        rte_mempool_free(poolCtrl->mempool);
        poolCtrl->mempool = NULL;
        (void)memset_s(poolCtrl->poolName, KNET_PKTPOOL_NAME_LEN, 0, KNET_PKTPOOL_NAME_LEN);
    }
    pthread_mutex_unlock(&(g_knetPktModCtrl.mutex));

    g_pktPoolId = KNET_PKTPOOL_INVALID_ID;

    return;
}

void KNET_PktPoolDestroy(uint32_t poolId)   // 从进程不进行pool的销毁
{
    if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
        return PktPoolDestroy(poolId);
    }
}

uint32_t KNET_PktModInit(void)
{
    (void)memset_s(&g_knetPktModCtrl, sizeof(g_knetPktModCtrl), 0, sizeof(g_knetPktModCtrl));

    int32_t iRet = pthread_mutex_init(&g_knetPktModCtrl.mutex, NULL);
    if (iRet != 0) {
        KNET_ERR("Pkt mutex init failed.(ret %d)", iRet);
        return KNET_ERROR;
    }

    return KNET_OK;
}

#ifdef __cplusplus
}
#endif /* __cpluscplus */
