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
#include <pthread.h>
#include <sys/syscall.h>

#include "securec.h"
#include "knet_types.h"
#include "knet_log.h"
#include "knet_lock.h"
#include "knet_pktpool.h"
#include "knet_pkt.h"
#include "knet_config.h"

enum KnetPktThreadId {
    PKT_THREAD_ID_UNINITED,
    PKT_THREAD_ID_MAIN,
    PKT_THREAD_ID_OTHER,
};
static __thread int g_threadMain = PKT_THREAD_ID_UNINITED;

#define MBUF_BATCH_NUM 32

typedef struct {
    struct rte_mbuf *allocCache[MBUF_BATCH_NUM];
    struct rte_mbuf *freeCache[MBUF_BATCH_NUM];
    uint32_t allocCachedCnt;
    uint32_t freeCachedCnt;
    int32_t processMode;
    KNET_SpinLock lock;
} KnetMbufBatch;

#define MBUF_BATCH_THREAD_NUM 32
static KnetMbufBatch g_mbufBatch[MBUF_BATCH_THREAD_NUM] = {0};

/*
    因为以下原因需要特殊处理：
        1、协议栈会使用业务线程申请mbuf
        2、主线程是业务线程
        3、主线程也会被dpdk初始化成rte线程
        4、rte线程申请mbuf会从无锁的cache中获取，导致多进程场景下多个业务的主线程并发出错。
    解决方案：
        1、主线程不使用cache
        2、其他业务线程因为不是rte线程获取不到cache，也没问题
*/
static bool KnetIsCurrentMainThread(void)
{
    if (g_threadMain == PKT_THREAD_ID_MAIN) {
        return true;
    } else if (g_threadMain == PKT_THREAD_ID_OTHER) {
        return false;
    }

    pid_t tid = syscall(SYS_gettid);
    pid_t pid = getpid();
    if (tid == pid) { // 主线程的tid和进程的pid相同
        g_threadMain = PKT_THREAD_ID_MAIN;
        return true;
    } else {
        g_threadMain = PKT_THREAD_ID_OTHER;
        return false;
    }
}

static inline int PktAllocBulkWithoutCache(struct rte_mempool *pool, struct rte_mbuf **mbufs, unsigned count)
{
    int rc = rte_mempool_generic_get(pool, (void **)mbufs, count, NULL);
    if (unlikely(rc != 0)) {
        return rc;
    }

    for (uint32_t idx = 0; idx < count; ++idx) {
        __rte_mbuf_raw_sanity_check(mbufs[idx]);
        rte_pktmbuf_reset(mbufs[idx]);
    }

    return 0;
}

struct rte_mbuf *KNET_PktAlloc(uint32_t poolId)
{
    struct rte_mempool *mp = KnetPktGetMemPool(poolId);
    if (unlikely(mp == NULL)) {
        KNET_ERR("Pkt alloc failed, invalid para. (poolId=%u)", poolId);
        return NULL;
    }

    static __thread KnetMbufBatch *batch = NULL;
    if (batch == NULL) {
        pid_t tid = syscall(SYS_gettid);
        batch = &g_mbufBatch[tid % MBUF_BATCH_THREAD_NUM];
        batch->processMode = KNET_GetCfg(CONF_COMMON_MODE).intValue;
    }
    struct rte_mbuf *mbuf = NULL;

    KNET_SpinlockLock(&batch->lock);

    if (batch->freeCachedCnt > 0) {
        mbuf = batch->freeCache[--batch->freeCachedCnt];
        goto out;
    }

    if (batch->allocCachedCnt > 0) {
        mbuf = batch->allocCache[--batch->allocCachedCnt];
        goto out;
    }

    if ((batch->processMode != 0) && KnetIsCurrentMainThread()) {
        int ret = PktAllocBulkWithoutCache(mp, (struct rte_mbuf **)batch->allocCache, MBUF_BATCH_NUM);
        if (ret != 0) {
            KNET_ERR("pkt alloc bulk without cache failed, ret %d", ret);
            goto err;
        }
    } else {
        int ret = rte_pktmbuf_alloc_bulk(mp, (struct rte_mbuf **)batch->allocCache, MBUF_BATCH_NUM);
        if (ret < 0) {
            KNET_ERR("mbuf alloc bulk failed, ret %d", ret);
            goto err;
        }
    }
    batch->allocCachedCnt = MBUF_BATCH_NUM - 1;
    mbuf = batch->allocCache[batch->allocCachedCnt];

out:
    KNET_SpinlockUnlock(&batch->lock);
    rte_pktmbuf_reset(mbuf);
    return mbuf;

err:
    KNET_SpinlockUnlock(&batch->lock);
    return NULL;
}

/**
 * @attention 前提：rte_mempool_generic_put和rte_mempool_put_bulk使用前提是mbuf ref为1
 *            若不为1就释放会导致后续申请的时候dpdk alloc接口的mbuf ref == 1的check失败，目前dp协议栈调用mbuf free接口时mbuf ref一定为1
 * @note 实测发现rte_mempool_put_bulk接口批处理性能远高于单个mbuf释放rte_pktmbuf_free_seg接口，所以实现mbuf批量alloc和free方案。
 *       方案实现：为保证多线程情况下不同线程高效率批处理，采用线程id hash映射到不同的批处理mbuf数组。
 */
void KNET_PktFree(void *pkt)
{
    if (unlikely(pkt == NULL)) {
        KNET_ERR("Pkt free failed, invalid para");
        return;
    }
    struct rte_mbuf *mbuf = KnetPkt2Mbuf(pkt);
    if (rte_mbuf_refcnt_update(mbuf, -1) != 0) {
        return;
    }
    rte_mbuf_refcnt_set(mbuf, 1);
    static __thread KnetMbufBatch *batch = NULL;
    if (batch == NULL) {
        pid_t tid = syscall(SYS_gettid);
        batch = &g_mbufBatch[tid % MBUF_BATCH_THREAD_NUM];
        batch->processMode = KNET_GetCfg(CONF_COMMON_MODE).intValue;
    }

    KNET_SpinlockLock(&batch->lock);
    batch->freeCache[batch->freeCachedCnt] = mbuf;
    batch->freeCachedCnt++;
    if (batch->freeCachedCnt < MBUF_BATCH_NUM) {
        KNET_SpinlockUnlock(&batch->lock);
        return;
    }
    if ((batch->processMode != 0) && KnetIsCurrentMainThread()) {
        mbuf = batch->freeCache[0];
        rte_mempool_generic_put(mbuf->pool, (void **)batch->freeCache, batch->freeCachedCnt, NULL);
    } else {
        mbuf = batch->freeCache[0];
        rte_mempool_put_bulk(mbuf->pool, (void **)batch->freeCache, batch->freeCachedCnt);
    }

    batch->freeCachedCnt = 0;
    KNET_SpinlockUnlock(&batch->lock);
}

static void MbufFree(KnetMbufBatch *batch, struct rte_mbuf **cache, uint32_t *cacheCnt)
{
    KNET_SpinlockLock(&batch->lock);
    uint32_t mbufNum = *cacheCnt;

    if (mbufNum == 0) {
        KNET_SpinlockUnlock(&batch->lock);
        return;
    }

    if ((batch->processMode != 0) && KnetIsCurrentMainThread()) {
        rte_mempool_generic_put(cache[0]->pool, (void **)cache, mbufNum, NULL);
    } else {
        rte_mempool_put_bulk(cache[0]->pool, (void **)cache, mbufNum);
    }
    *cacheCnt = 0;
    KNET_SpinlockUnlock(&batch->lock);
}

void KNET_PktBatchFree(void)
{
    for (uint32_t batchIndex = 0; batchIndex < MBUF_BATCH_THREAD_NUM; ++batchIndex) {
        KnetMbufBatch *batch = &g_mbufBatch[batchIndex];
        MbufFree(batch, batch->allocCache, &batch->allocCachedCnt);
        MbufFree(batch, batch->freeCache, &batch->freeCachedCnt);
    }
}
