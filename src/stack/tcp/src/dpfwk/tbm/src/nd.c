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
#include <securec.h>

#include "dp_tbm.h"

#include "dp_ethernet.h"

#include "nd.h"

#include "shm.h"
#include "utils_cfg.h"
#include "utils_log.h"
#include "utils_statistic.h"
#include "pmgr.h"
#include "tbm_utils.h"

static inline void WaitNdTblIdle(NdTbl_t* tbl)
{
    while (ATOMIC32_Load(&tbl->ref) != 0) {}
}

static inline uint32_t NdRef(NdItem_t* item)
{
    return ATOMIC32_Inc(&item->ref);
}

static inline uint32_t NdDeref(NdItem_t* item)
{
    return ATOMIC32_Dec(&item->ref);
}

static void NdItemFree(void *para)
{
    NdItem_t* item = (NdItem_t*)para;
    PutNd(item);
}

static void CreateLookUpTbl(NdTbl_t* tbl)
{
    int ret;
    DP_HashTblCfg_t hashTblCfg = {0};
    /* 如果没有注册HASH表功能，则不新建查找表 */
    if (TbmHashtblFuncReged == false) {
        return;
    }
    hashTblCfg.hashFunc = NULL;
    hashTblCfg.keySize = sizeof(TBM_IpAddr_t); /* 当前以IP的联合体作为key */
    hashTblCfg.entrySize = sizeof(int32_t);    /* 存储表项的索引 */
    hashTblCfg.entryNum = (uint32_t)tbl->maxCnt;

    ret = TbmHashtblCreate(&hashTblCfg, &tbl->ndLookUpTbl);
    if (ret != 0) {
        return;
    }
    tbl->LookUpTblUsed = true;
}

void* AllocNdTbl(void)
{
    NdTbl_t* tbl = NULL;
    size_t allocSize;

    allocSize = sizeof(NdTbl_t);
    tbl = SHM_MALLOC(allocSize, MOD_TBM, DP_MEM_FIX);
    if (tbl == NULL) {
        DP_LOG_ERR("Malloc memory failed for ndTbl.");
        return NULL;
    }

    (void)memset_s(tbl, allocSize, 0, allocSize);
    tbl->maxCnt = CFG_GET_VAL(DP_CFG_ARP_MAX);

    tbl->ndItemPool = MemPoolCreate((size_t)CFG_GET_VAL(DP_CFG_ARP_MAX), 0, NdItemFree);
    if (tbl->ndItemPool == NULL) {
        DP_LOG_ERR("Malloc memory failed for ndItemPool.");
        SHM_FREE(tbl, DP_MEM_FREE);
        return NULL;
    }

    tbl->maxFakeCnt = CFG_GET_VAL(CFG_FAKE_NDTBL_SIZE);
    allocSize = sizeof(NdFakeItem_t *) * tbl->maxFakeCnt;
    tbl->fakeItems = SHM_MALLOC(allocSize, MOD_TBM, DP_MEM_FREE);
    if (tbl->fakeItems == NULL) {
        DP_LOG_ERR("Malloc memory failed for fakeItem.");
        MemPoolDestroy(tbl->ndItemPool);
        SHM_FREE(tbl, DP_MEM_FIX);
        return NULL;
    }
    (void)memset_s(tbl->fakeItems, allocSize, 0, allocSize);
    SPINLOCK_Init(&tbl->lock);

    CreateLookUpTbl(tbl);

    return tbl;
}

static void DestroyLookUpTbl(NdTbl_t* tbl)
{
    if (tbl->LookUpTblUsed == false) {
        return;
    }
    tbl->LookUpTblUsed = false;
    TbmHashtblDestroy(tbl->ndLookUpTbl);
    tbl->ndLookUpTbl = NULL;
}

void FreeNdTbl(void* tbl)
{
    NdTbl_t *mFree = (NdTbl_t *)tbl;

    DestroyLookUpTbl(mFree);
    MemPoolDestroy(mFree->ndItemPool);
    mFree->ndItemPool = NULL;
    for (int i = 0; i < mFree->maxFakeCnt; ++i) {
        if (mFree->fakeItems[i] != NULL) {
            FreeFakeNdItem(mFree->fakeItems[i]);
            mFree->fakeItems[i] = NULL;
        }
    }
    SPINLOCK_Deinit(&mFree->lock);
    SHM_FREE(mFree->fakeItems, DP_MEM_FREE);
    SHM_FREE(mFree, DP_MEM_FIX);
}

NdItem_t* AllocNdItem(void)
{
    NdItem_t* ndItem = SHM_MALLOC(sizeof(NdItem_t), MOD_TBM, DP_MEM_FREE);
    if (ndItem == NULL) {
        DP_LOG_ERR("Malloc memory failed for ndItem.");
        return NULL;
    }

    (void)memset_s(ndItem, sizeof(NdItem_t), 0, sizeof(NdItem_t));
    return ndItem;
}

NdFakeItem_t* AllocNdFakeItem(void)
{
    NdFakeItem_t* fakeItem = SHM_MALLOC(sizeof(NdFakeItem_t), MOD_TBM, DP_MEM_FREE);
    if (fakeItem == NULL) {
        DP_LOG_ERR("Malloc memory failed for fakeItem.");
        return NULL;
    }

    (void)memset_s(fakeItem, sizeof(NdFakeItem_t), 0, sizeof(NdFakeItem_t));
    fakeItem->maxCachedCnt = CFG_GET_VAL(CFG_NDTBL_MISS_CACHED_SIZE);
    size_t allocSize = sizeof(Pbuf_t *) * fakeItem->maxCachedCnt;
    fakeItem->cached = SHM_MALLOC(allocSize, MOD_TBM, DP_MEM_FREE);
    if (fakeItem->cached == NULL) {
        DP_LOG_ERR("Malloc memory failed for fakeItem cached.");
        SHM_FREE(fakeItem, DP_MEM_FREE);
        return NULL;
    }

    (void)memset_s(fakeItem->cached, allocSize, 0, allocSize);

    return fakeItem;
}

void FreeNdItem(NdItem_t* ndItem)
{
    SHM_FREE(ndItem, DP_MEM_FREE);
}

void FreeFakeNdItem(NdFakeItem_t* fakeItem)
{
    for (int i = 0; i < fakeItem->cachedCnt; ++i) {
        ASSERT(fakeItem->cached[i] != NULL);

        PBUF_Free(fakeItem->cached[i]);
        fakeItem->cached[i] = NULL;
    }

    SPINLOCK_Deinit(&fakeItem->lock);
    SHM_FREE(fakeItem->cached, DP_MEM_FREE);
    SHM_FREE(fakeItem, DP_MEM_FREE);
}

struct LookupNdContext {
    TBM_IpAddr_t dst;
    int32_t id;
};

static uint32_t LookUpHook(void* para, int32_t id, void *out)
{
    TBM_NdItem_t *item = (TBM_NdItem_t *)para;
    struct LookupNdContext *ctx = (struct LookupNdContext *)out;
    if (TBM_IPADDR_IS_EQUAL(&ctx->dst, &item->dst)) {
        ctx->id = id;
        return 0;
    }
    return 1;
}

static int LookupNdIdxFast(NdTbl_t* tbl, TBM_IpAddr_t dst)
{
    int32_t idx;
    int ret;

    ret = TbmHashtblLookupEntry(tbl->ndLookUpTbl, (uint8_t*)&dst, &idx);
    if (ret != 0) {
        return -1;
    }
    return idx;
}

static int LookupNdIdx(NdTbl_t* tbl, TBM_IpAddr_t dst)
{
    struct LookupNdContext ctx;
    ctx.dst = dst;
    ctx.id = 0;

    if (tbl->LookUpTblUsed == true) {
        return LookupNdIdxFast(tbl, dst);
    }

    if (MemPoolWalker(tbl->ndItemPool, LookUpHook, &ctx) == 0) {
        return ctx.id;
    }

    return -1;
}

static int InsertNdLookupTbl(NdTbl_t* tbl, NdItem_t* item, int ndidx)
{
    int ret;
    ret = TbmHashtblInsertEntry(tbl->ndLookUpTbl, (uint8_t *)&item->dst, &ndidx);
    if (ret != 0) {
        return -1;
    }
    return 0;
}

int InsertNd(NdTbl_t* tbl, NdItem_t* item)
{
    NdItem_t* temp;
    int ndidx;

    if (tbl->cnt >= tbl->maxCnt) {
        return -1;
    }

    ndidx = LookupNdIdx(tbl, item->dst);
    if (ndidx < 0) {
        ndidx = MemNodeAlloc(tbl->ndItemPool);
        if (ndidx < 0) {
            return -1;
        }
        if (MemNodeSetPointer(tbl->ndItemPool, ndidx, item) != 0) {
            MemNodeFree(tbl->ndItemPool, ndidx);
            return -1;
        }
        if (tbl->LookUpTblUsed == true) {
            /* 添加索引失败, 删除数据 */
            if (InsertNdLookupTbl(tbl, item, ndidx) < 0) {
                MemNodeFree(tbl->ndItemPool, ndidx);
                return -1;
            }
        }
        tbl->cnt++;
        return 0;
    } else {
        /* 已存在，索引不变，不需要处理查找表 */
        temp = MemNodeGetPointer(tbl->ndItemPool, ndidx);
        if (MemNodeSetPointer(tbl->ndItemPool, ndidx, item) != 0) {
            return -1;
        }

        WaitNdTblIdle(tbl);
        if (temp != NULL) {
            /* 更新老旧item的状态为invlid 在下次发送报文时更新 */
            temp->valid = 0;
            PutNd(temp);
        }
        return 0;
    }
}

static int DeleteNdLookupTbl(NdTbl_t* tbl, NdItem_t* item)
{
    int ret;
    if (tbl->LookUpTblUsed == false) {
        return 0;
    }
    ret = TbmHashtblDelEntry(tbl->ndLookUpTbl, (uint8_t *)&item->dst);
    if (ret != 0) {
        return -1;
    }
    return 0;
}

int RemoveNd(NdTbl_t* tbl, TBM_IpAddr_t dst, Netdev_t* dev)
{
    int       ndidx;
    NdItem_t* item;

    ndidx = LookupNdIdx(tbl, dst);
    if (ndidx < 0) {
        return -1;
    }

    item = MemNodeGetPointer(tbl->ndItemPool, ndidx);
    if (item == NULL || item->dev != dev) {
        return -1;
    }

    tbl->cnt--;

    if (DeleteNdLookupTbl(tbl, item) != 0) {
        // 在前面已查找成功，此处索引删除失败说明出现未知故障，也要完成实际数据删除，此处只记录日志
        DP_LOG_INFO("Remove nd hash index failed.");
    }
    MemNodeFree(tbl->ndItemPool, ndidx);

    item->valid = 0;
    WaitNdTblIdle(tbl);
    PutNd(item);
    item = NULL;

    return 0;
}

static NdItem_t* LookupNd(NdTbl_t* tbl, TBM_IpAddr_t dst)
{
    int ndidx;

    ndidx = LookupNdIdx(tbl, dst);

    return ndidx < 0 ? NULL : MemNodeGetPointer(tbl->ndItemPool, ndidx);
}

int GetNdCnt(NdTbl_t* tbl)
{
    return tbl->cnt;
}

NdItem_t* GetNd(NdTbl_t* tbl, TBM_IpAddr_t dst)
{
    NdItem_t* item;

    (void)ATOMIC32_Inc(&tbl->ref);

    item = LookupNd(tbl, dst);
    if (item != NULL) {
        (void)NdRef(item);
    }

    (void)ATOMIC32_Dec(&tbl->ref);
    return item;
}

void PutNd(NdItem_t* item)
{
    if (NdDeref(item) == 0) {
        if (item->dev != NULL) {
            NETDEV_PutDev(item->dev);
        }
        FreeNdItem(item);
    }
}

void PutFakeNd(NdFakeItem_t* fakeItem)
{
    if (NdDeref(&fakeItem->item) == 0) {
        FreeFakeNdItem(fakeItem);
    }
}

static int LookupFakeNdIdx(NdTbl_t* tbl, TBM_IpAddr_t dst)
{
    for (int i = 0; i < tbl->maxFakeCnt; i++) {
        if (tbl->fakeItems[i] == NULL) {
            continue;
        }
        if (TBM_IPADDR_IS_EQUAL(&tbl->fakeItems[i]->item.dst, &dst)) {
            return i;
        }
    }

    return -1;
}

static void XmitNdMissCache(NdFakeItem_t* fakeItem)
{
    Pbuf_t**  pkts = NULL;
    int cnt = 0;

    SPINLOCK_Lock(&fakeItem->lock);
    cnt = fakeItem->cachedCnt;
    /* 在该函数中全部赋值，无需初始化 */
    pkts = SHM_MALLOC(sizeof(Pbuf_t*) * cnt, MOD_TBM, DP_MEM_FREE);
    if (pkts == NULL) {
        DP_LOG_ERR("Malloc memory failed for pbuf pkts.");
        SPINLOCK_Unlock(&fakeItem->lock);
        return;
    }

    for (int i = 0; i < cnt; i++) {
        pkts[i] = fakeItem->cached[i];
        fakeItem->cached[i] = NULL;
        fakeItem->cachedCnt--;
    }
    SPINLOCK_Unlock(&fakeItem->lock);

    for (int i = 0; i < cnt; i++) {
        PMGR_Dispatch(pkts[i]);
    }
    SHM_FREE(pkts, DP_MEM_FREE);
}

static int FreeExpireFakeItem(NdTbl_t* tbl)
{
    // 删除老化的假表项
    TBM_NdFakeItem_t* fakeItem = NULL;
    int delNum = 0;
    uint32_t aliveTime = (uint32_t)CFG_GET_VAL(CFG_FAKE_NDITEM_ALIVE_TIME);
    for (int i = 0; i < tbl->maxFakeCnt; i++) {
        fakeItem = tbl->fakeItems[i];
        if (fakeItem == NULL || TIME_CMP(UTILS_TimeNow(), fakeItem->item.insertTime + aliveTime) < 0) {
            continue;
        }

        delNum++;
        tbl->fakeCnt--;
        tbl->fakeItems[i] = NULL;
        WaitNdTblIdle(tbl);
        PutFakeNd(fakeItem);
    }
    return delNum;
}

NdFakeItem_t* InsertFakeNd(NdTbl_t* tbl, TBM_IpAddr_t dst, Netdev_t* dev)
{
    NdFakeItem_t* fakeItem = NULL;
    if (tbl->fakeCnt >= tbl->maxFakeCnt) {
        // 删除老化表项
        SPINLOCK_Lock(&tbl->lock);
        if (FreeExpireFakeItem(tbl) == 0) {
            SPINLOCK_Unlock(&tbl->lock);
            return NULL;
        }
        SPINLOCK_Unlock(&tbl->lock);
    }

    // 生成假表项
    fakeItem = AllocNdFakeItem();
    if (fakeItem == NULL) {
        return NULL;
    }

    TBM_IPADDR_COPY(&fakeItem->item.dst, &dst);
    fakeItem->item.state        = DP_ND_STATE_INCOMPLETE;
    fakeItem->item.flags        = 0;
    fakeItem->item.type         = 0;
    fakeItem->item.ref          = 2;    // 后续需要返回假表项供外部操作，引用计数设置为2
    fakeItem->item.dev          = dev;
    fakeItem->item.insertTime   = UTILS_TimeNow();
    fakeItem->item.updateTime   = fakeItem->item.insertTime;
    fakeItem->cachedCnt         = 0;
    SPINLOCK_Init(&fakeItem->lock);
    DP_MAC_SET_DUMMY(&fakeItem->item.mac);

    SPINLOCK_Lock(&tbl->lock);
    for (int i = 0; i < tbl->maxFakeCnt; i++) {
        if (tbl->fakeItems[i] == NULL) {
            tbl->fakeItems[i] = fakeItem;
            tbl->fakeCnt++;
            SPINLOCK_Unlock(&tbl->lock);
            return fakeItem;
        }
    }
    SPINLOCK_Unlock(&tbl->lock);
    FreeFakeNdItem(fakeItem);
    return NULL;
}

NdFakeItem_t* GetFakeNd(NdTbl_t* tbl, TBM_IpAddr_t dst)
{
    int ndidx;
    NdFakeItem_t* fakeItem = NULL;

    (void)ATOMIC32_Inc(&tbl->ref);

    ndidx = LookupFakeNdIdx(tbl, dst);
    if (ndidx >= 0) {
        fakeItem = tbl->fakeItems[ndidx];
        (void)NdRef(&fakeItem->item);
    }

    (void)ATOMIC32_Dec(&tbl->ref);
    return fakeItem;
}

bool IsNeedNotify(NdFakeItem_t* fakeItem)
{
    uint32_t interval = (uint32_t)CFG_GET_VAL(CFG_ARP_MISS_INTERVAL);
    SPINLOCK_Lock(&fakeItem->lock);
    if (TIME_CMP(UTILS_TimeNow(), fakeItem->item.updateTime + interval) > 0) {
        fakeItem->item.updateTime = UTILS_TimeNow();
        SPINLOCK_Unlock(&fakeItem->lock);
        return false;
    }
    SPINLOCK_Unlock(&fakeItem->lock);
    return true;
}

int PushNdMissCache(NdFakeItem_t* fakeItem, Pbuf_t* pbuf)
{
    Pbuf_t *clone = NULL;
    if (fakeItem == NULL) {
        return -1;
    }

    SPINLOCK_Lock(&fakeItem->lock);
    if (fakeItem->cachedCnt >= fakeItem->maxCachedCnt) {
        SPINLOCK_Unlock(&fakeItem->lock);
        return -1;
    }

    clone = PBUF_Clone(pbuf);
    if (clone == NULL) {
        SPINLOCK_Unlock(&fakeItem->lock);
        return -1;
    }

    PBUF_SET_DEV(clone, PBUF_GET_DEV(pbuf));
    DP_INC_PKT_STAT(pbuf->wid, DP_PKT_ARP_MISS_RESV);
    fakeItem->cached[fakeItem->cachedCnt] = clone;
    fakeItem->cachedCnt++;
    SPINLOCK_Unlock(&fakeItem->lock);
    return 0;
}

void RemoveFakeNdItem(NdTbl_t* tbl, TBM_IpAddr_t dst)
{
    NdFakeItem_t* fakeItem = NULL;
    int           ndidx    = -1;

    SPINLOCK_Lock(&tbl->lock);
    ndidx = LookupFakeNdIdx(tbl, dst);
    if (ndidx < 0) {
        SPINLOCK_Unlock(&tbl->lock);
        return;
    }

    fakeItem = tbl->fakeItems[ndidx];
    XmitNdMissCache(fakeItem);

    // 删除表项
    tbl->fakeCnt--;
    tbl->fakeItems[ndidx] = NULL;
    SPINLOCK_Unlock(&tbl->lock);
    WaitNdTblIdle(tbl);
    PutFakeNd(fakeItem);
}

void ClearFakeNd(NdTbl_t *tbl)
{
    SPINLOCK_Lock(&tbl->lock);
    for (int i = 0; i < tbl->maxFakeCnt; ++i) {
        if (tbl->fakeItems[i] != NULL) {
            FreeFakeNdItem(tbl->fakeItems[i]);
            tbl->fakeItems[i] = NULL;
        }
    }
    tbl->fakeCnt = 0;
    SPINLOCK_Unlock(&tbl->lock);
}
