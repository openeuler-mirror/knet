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
#include <unistd.h>
#include <rte_lcore.h>

#include "knet_config.h"
#include "knet_log.h"
#include "knet_stk_mp.h"

#define KNET_STK_MP_MAX_WORKER_NUM 32           // 与 worker num 的最大值保持一致
#define MALLOC_BLOCK_COUNT 1048576  // 每次malloc ref_pbuf的最大count：1MB

struct KnetStkMp {
    void** bufs;
    uint32_t count;
    uint32_t off;
    uint32_t mallocCount;
};

KNET_STATIC bool g_knetStkMpIsInit = false;
KNET_STATIC unsigned g_knetStkMpNum = 0;
KNET_STATIC struct KnetStkMp g_knetStkMps[KNET_STK_MP_MAX_WORKER_NUM] = {0};   // 每个 worker 都持有一个 mp

static void *KnetStkMpGet(struct KnetStkMp* mp)
{
    if (KNET_UNLIKELY(mp->off == 0)) {
        KNET_ERR("K-NET stack mempool get failed, no memory");
        return NULL;
    }

    return mp->bufs[--mp->off];
}

static void KnetStkMpPut(struct KnetStkMp* mp, void *buf)
{
    if (KNET_UNLIKELY(mp->off >= mp->count)) {
        KNET_ERR("K-NET stack mempool put failed, pool is full");
        return;
    }

    mp->bufs[mp->off++] = buf;
}

static void OneMpClearInner(struct KnetStkMp* mp)
{
    // 申请时，mp->bufs前面是malloc申请的头指针
    for (uint32_t i = 0; i < mp->mallocCount; ++i) {
        if (mp->bufs[i] != NULL) {
            free(mp->bufs[i]);
        }
    }

    // 置空整个mp->bufs
    for (uint32_t i = 0; i < mp->count; ++i) {
        mp->bufs[i] = NULL;
    }

    free(mp->bufs);
    mp->bufs = NULL;
    mp->count = 0;
    mp->off = 0;
    mp->mallocCount = 0;
}

static uint32_t KnetOneMpClear(struct KnetStkMp* mp)
{
    if (KNET_UNLIKELY(mp->off != mp->count)) {
        KNET_ERR("K-NET stack mempool clear failed, active memory exists, count %u", mp->off);
        return KNET_ERROR;
    }

    OneMpClearInner(mp);
    return KNET_OK;
}

static uint32_t KnetStkMpClear(void)
{
    uint32_t ret;
    struct KnetStkMp *mp = NULL;
    for (unsigned i  = 0; i < g_knetStkMpNum; ++i) {
        mp = &g_knetStkMps[i];
        ret = KnetOneMpClear(mp);
        if (ret != KNET_OK) {
            KNET_ERR("K-NET stack mempool clear failed, clear mempool %d failed", i);
            return KNET_ERROR;
        }
    }

    g_knetStkMpNum = 0;
    return KNET_OK;
}

static uint32_t OneMpFill(struct KnetStkMp* mp, const uint32_t size, const uint32_t count)
{
    void *mallocBuf = NULL;
    uint32_t putCount = MALLOC_BLOCK_COUNT;
    for (uint32_t i = 0; i < mp->mallocCount; ++i) {
        if (i == (mp->mallocCount - 1)) {
            // 最后一次malloc的空间单独处理：根据是否有余数确定块数，如果没有余数，说明申请的是MALLOC_BLOCK_COUNT的整数倍，
            // 则剩余buf数就是MALLOC_BLOCK_COUNT，否则为余数
            uint32_t remainder = count % MALLOC_BLOCK_COUNT;
            putCount = (remainder == 0) ? MALLOC_BLOCK_COUNT : remainder;
        }
        mallocBuf = mp->bufs[i];
        for (uint32_t j = 1; j < putCount; ++j) {
            ++mp->count;
            void *buf = mallocBuf + j * size;
            KnetStkMpPut(mp, buf);
        }
    }

    if (KNET_UNLIKELY(mp->count != count)) {
        KNET_ERR("K-NET stack mempool init failed, expected count %u, actual init count %u", count, mp->count);
        return KNET_ERROR;
    }
    return KNET_OK;
}

static uint32_t KnetOneMpInit(struct KnetStkMp* mp, const uint32_t size, const uint32_t count)
{
    mp->bufs = malloc(sizeof(void*) * count);
    if (mp->bufs == NULL) {
        KNET_ERR("K-NET one mempool init failed, mempool bufs malloc failed");
        return KNET_ERROR;
    }

    mp->count = 0;
    mp->mallocCount = 0;

    // 每次申请按照1M个count申请一次，防止一次性申请太大的空间导致失败
    uint32_t totalBlocks = count / MALLOC_BLOCK_COUNT;
    uint32_t remainder = count % MALLOC_BLOCK_COUNT;
    void *mallocBuf = NULL;
    for (uint32_t i = 0; i < totalBlocks; ++i) {
        mallocBuf = malloc(size * MALLOC_BLOCK_COUNT);
        if (mallocBuf == NULL) {
            OneMpClearInner(mp);
            KNET_ERR("K-NET mempool init failed, mallocBuf %u malloc failed, count %u", i, count);
            return KNET_ERROR;
        }

        // 将malloc申请的头指针放到 bufs首部，便于释放
        ++mp->mallocCount;
        ++mp->count;
        KnetStkMpPut(mp, mallocBuf);
    }

    if (remainder > 0) {
        mallocBuf = malloc(size * remainder);
        if (mallocBuf == NULL) {
            OneMpClearInner(mp);
            KNET_ERR("K-NET mempool init failed, mallocBuf %u malloc failed, remainder %u", totalBlocks+1, remainder);
            return KNET_ERROR;
        }

        // 将malloc申请的头指针放到 bufs首部，便于释放
        ++mp->mallocCount;
        ++mp->count;
        KnetStkMpPut(mp, mallocBuf);
    }

    KNET_INFO("K-NET mempool malloc success, size %u, count %u", size, count);
    uint32_t ret = OneMpFill(mp, size, count);
    if (ret != KNET_OK) {
        OneMpClearInner(mp);
        KNET_ERR("K-NET mempool init failed, fill failed");
    }
    return ret;
}

uint32_t KNET_StkMpInit(const uint32_t size, const uint32_t count)
{
    if (g_knetStkMpIsInit) {
        KNET_INFO("Stack mempool exists");
        return KNET_OK;
    }

    uint32_t ret;
    int32_t workerNum = KNET_GetCfg(CONF_TCP_MAX_WORKER_NUM)->intValue;
    struct KnetStkMp *mp = NULL;
    for (int32_t i  = 0; i < workerNum; ++i) {      // 每个 worker 都初始化一个 mp
        mp = &g_knetStkMps[i];
        ret = KnetOneMpInit(mp, size, count);
        if (ret != KNET_OK) {
            (void)KnetStkMpClear();
            KNET_ERR("K-NET stack mempool init failed, one mempool init failed, size %u, count %u", size, count);
            return KNET_ERROR;
        }
        ++g_knetStkMpNum;
    }

    g_knetStkMpIsInit = true;
    return KNET_OK;
}

uint32_t KNET_StkMpDeInit(void)
{
    if (!g_knetStkMpIsInit) {
        KNET_INFO("Stack mempool not init");
        return KNET_OK;
    }

    uint32_t ret = KnetStkMpClear();
    if (ret != KNET_OK) {
        KNET_ERR("K-NET stack mempool deinit failed, clear failed");
        return KNET_ERROR;
    }

    g_knetStkMpIsInit = false;
    return KNET_OK;
}

void *KNET_StkMpAlloc(void)
{
    unsigned workerId = rte_lcore_id();
    if (KNET_UNLIKELY(workerId >= g_knetStkMpNum)) {
        KNET_ERR("K-NET stack mempool alloc failed, workerId %u is too large, max %u", workerId, g_knetStkMpNum);
        return NULL;
    }

    struct KnetStkMp *mp = &g_knetStkMps[workerId];
    return KnetStkMpGet(mp);
}

void KNET_StkMpFree(void *buf)
{
    unsigned workerId = rte_lcore_id();
    if (KNET_UNLIKELY(workerId >= g_knetStkMpNum)) {
        KNET_ERR("K-NET stack mempool free failed, workerId %u is too large, max %u", workerId, g_knetStkMpNum);
        return;
    }

    struct KnetStkMp *mp = &g_knetStkMps[workerId];
    KnetStkMpPut(mp, buf);
}

