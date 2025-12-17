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

#include "rt.h"

#include "dp_inet.h"

#include "shm.h"
#include "utils_cfg.h"
#include "utils_log.h"
#include "tbm_utils.h"

#define RT_KEY_EQUAL(l, r) (((l)->addr & (l)->mask) == ((r)->addr & (r)->mask))

static inline uint32_t RtRef(RtItem_t* item)
{
    return ATOMIC32_Inc(&item->ref);
}

static inline uint32_t RtDeref(RtItem_t* item)
{
    return ATOMIC32_Dec(&item->ref);
}

static void RtItemFree(void *para)
{
    RtItem_t* item = (RtItem_t*)para;
    PutRt(item);
}

static void CreateFib4Tbl(RtTbl_t* tbl)
{
    int ret;
    DP_Fib4TblCfg_t fib4TblCfg = {0};

    /* 如果没有注册FIB表功能，则不新建查找表 */
    if (TbmFib4tblFuncReged == false) {
        return;
    }

    fib4TblCfg.entrySize = sizeof(uint32_t);
    fib4TblCfg.entryNum = (uint32_t)tbl->maxCnt;
    fib4TblCfg.vpnNum = 4096; /* 默认配置4096，暂时保持不变 */
    fib4TblCfg.flag = 1;
    fib4TblCfg.createType = 0;
    fib4TblCfg.updateFreq = 0;
    fib4TblCfg.delayTime = 0;

    ret = TbmFib4Create(&fib4TblCfg, &tbl->fibTbl);
    if (ret != 0) {
        return;
    }
    tbl->fibTblUsed = true;
}

void* AllocRtTbl(void)
{
    RtTbl_t* tbl;
    size_t   allocSize;

    allocSize = sizeof(RtTbl_t);

    tbl = SHM_MALLOC(allocSize, MOD_TBM, DP_MEM_FIX);
    if (tbl == NULL) {
        DP_LOG_ERR("Malloc memory failed for rtTbl.");
        return NULL;
    }

    (void)memset_s(tbl, allocSize, 0, allocSize);
    tbl->maxCnt = CFG_GET_VAL(DP_CFG_RT_MAX);
    tbl->rtItemPool = MemPoolCreate((size_t)CFG_GET_VAL(DP_CFG_RT_MAX), 0, RtItemFree);
    if (tbl->rtItemPool == NULL) {
        DP_LOG_ERR("Malloc memory failed for rtItemPool.");
        SHM_FREE(tbl, DP_MEM_FIX);
        return NULL;
    }

    CreateFib4Tbl(tbl);

    return tbl;
}

static void DestroyFib4Tbl(RtTbl_t* tbl)
{
    if (tbl->fibTblUsed == false) {
        return;
    }
    tbl->fibTblUsed = false;
    TbmFib4Destroy(tbl->fibTbl);
    tbl->fibTbl = NULL;
}

void FreeRtTbl(void* tbl)
{
    RtTbl_t *rtTbl = (RtTbl_t *)tbl;

    DestroyFib4Tbl(rtTbl);
    MemPoolDestroy(rtTbl->rtItemPool);

    if (rtTbl->dftRt != NULL) {
        NETDEV_FreeIfAddr(rtTbl->dftRt->ifaddr);
        SHM_FREE(rtTbl->dftRt, DP_MEM_FREE);
    }
    SHM_FREE(rtTbl, DP_MEM_FREE);
}

RtItem_t* AllocRtItem(void)
{
    RtItem_t* rtItem = SHM_MALLOC(sizeof(RtItem_t), MOD_TBM, DP_MEM_FREE);

    if (rtItem == NULL) {
        DP_LOG_ERR("Malloc memory failed for rtItem.");
        return NULL;
    }

    (void)memset_s(rtItem, sizeof(RtItem_t), 0, sizeof(RtItem_t));

    return rtItem;
}

void FreeRtItem(RtItem_t* rtItem)
{
    SHM_FREE(rtItem, DP_MEM_FREE);
}

struct LookupRtContext {
    RtKey_t* rtKey;
    int32_t id;
    int maskLen;
};

static uint32_t LookUpRtHook(void* para, int32_t id, void *out)
{
    RtItem_t *item = (RtItem_t *)para;
    struct LookupRtContext *ctx = (struct LookupRtContext *)out;

    if (RT_KEY_EQUAL(ctx->rtKey, &item->key)) {
        ctx->id = id;
        return 0;
    }
    return 1;
}

static int32_t LookupRtIndexFast(RtTbl_t* tbl, RtKey_t* rtKey)
{
    uint32_t index;
    DP_Fib4Key_t key = {0};
    key.dip = UTILS_NTOHL(rtKey->addr);
    key.pfxlen = (uint32_t)DP_NetmaskLen(rtKey->mask);

    if (TbmFib4ExactMatchEntry(tbl->fibTbl, &key, &index) != 0) {
        return -1;
    }
    return (int32_t)index;
}

static int32_t LookupRtIndex(RtTbl_t* tbl, RtKey_t* rtKey)
{
    if (tbl->fibTblUsed == true) {
        return LookupRtIndexFast(tbl, rtKey);
    }

    struct LookupRtContext ctx;
    ctx.rtKey = rtKey;
    ctx.id = -1;

    if (MemPoolWalker(tbl->rtItemPool, LookUpRtHook, &ctx) == 0) {
        return ctx.id;
    }

    return -1;
}

RtItem_t* LookupRt(RtTbl_t* tbl, RtKey_t* rtKey)
{
    int32_t idx;

    idx = LookupRtIndex(tbl, rtKey);
    if (idx < 0) {
        return NULL;
    }

    return MemNodeGetPointer(tbl->rtItemPool, idx);
}

static int InsertDftRt(RtTbl_t* tbl, RtItem_t* rtItem)
{
    RtItem_t* temp = tbl->dftRt;
    tbl->dftRt = rtItem;

    if (temp != NULL) {
        WaitRtTblIdle(tbl);
        PutRt(temp);
    }

    return 0;
}

static int32_t InsertFib4IndexTbl(RtTbl_t* tbl, RtKey_t* rtKey, int32_t idx)
{
    DP_Fib4Key_t key = {0};

    if (tbl->fibTblUsed == false) {
        return 0;
    }

    key.dip = UTILS_NTOHL(rtKey->addr);
    key.pfxlen = (uint32_t)DP_NetmaskLen(rtKey->mask);

    if (TbmFib4InsertEntry(tbl->fibTbl, &key, (uint32_t)idx) != 0) {
        return -1;
    }
    return 0;
}

int InsertRt(RtTbl_t* tbl, RtItem_t* rtItem)
{
    if (rtItem->key.addr == DP_INADDR_ANY) {
        DP_LOG_INFO("Insert Default Rt item.");
        return InsertDftRt(tbl, rtItem);
    }

    if (tbl->cnt >= tbl->maxCnt) {
        DP_LOG_ERR("InsertRt failed, tbl is full");
        return -1;
    }

    if (LookupRt(tbl, &rtItem->key) != NULL) {
        DP_LOG_ERR("InsertRt failed, the same rtKey is exist in table.");
        return -1;
    }

    int32_t idx = MemNodeAlloc(tbl->rtItemPool);
    if (idx < 0) {
        return -1;
    }

    if (MemNodeSetPointer(tbl->rtItemPool, idx, rtItem) != 0) {
        DP_LOG_ERR("InsertRt failed, MemNodeSetPointer failed.");
        MemNodeFree(tbl->rtItemPool, idx);
        return -1;
    }

    if (InsertFib4IndexTbl(tbl, &rtItem->key, idx) < 0) {
        DP_LOG_ERR("InsertRt failed, InsertFib4IndexTbl failed.");
        MemNodeFree(tbl->rtItemPool, idx);
        return -1;
    }

    tbl->cnt++;

    return 0;
}

static int RemoveDftRt(RtTbl_t* tbl)
{
    RtItem_t* temp;

    if (tbl->dftRt == NULL) {
        return 0;
    }

    temp = tbl->dftRt;
    tbl->dftRt = NULL;

    WaitRtTblIdle(tbl);
    PutRt(temp);

    return 0;
}

static int32_t RemoveFib4IndexTbl(RtTbl_t* tbl, RtKey_t* rtKey)
{
    uint32_t index;
    DP_Fib4Key_t key = {0};

    if (tbl->fibTblUsed == false) {
        return 0;
    }

    key.dip = UTILS_NTOHL(rtKey->addr);
    key.pfxlen = (uint32_t)DP_NetmaskLen(rtKey->mask);

    if (TbmFib4DelEntry(tbl->fibTbl, &key, &index) != 0) {
        return -1;
    }

    return 0;
}

int RemoveRt(RtTbl_t* tbl, RtKey_t* rtKey)
{
    RtItem_t* rtItem;
    int32_t idx;

    if (rtKey->addr == DP_INADDR_ANY) {
        DP_LOG_INFO("Remove Default Rt item.");
        return RemoveDftRt(tbl);
    }

    idx = LookupRtIndex(tbl, rtKey);
    if (idx < 0) {
        DP_LOG_ERR("RemoveRt failed, the rtKey is not exist in table.");
        return -1;
    }

    if (RemoveFib4IndexTbl(tbl, rtKey) != 0) {
        // 在前面已查找成功，此处索引删除失败说明出现未知故障，也要完成实际数据删除，此处只记录日志
        DP_LOG_INFO("Remove fib4 index failed.");
    }

    rtItem = MemNodeGetPointer(tbl->rtItemPool, idx);
    if (rtItem == NULL) {
        DP_LOG_ERR("RemoveRt failed, can't find the rtItem node.");
        return -1;
    }

    MemNodeFree(tbl->rtItemPool, idx);
    tbl->cnt--;

    rtItem->valid = 0;
    WaitRtTblIdle(tbl);
    PutRt(rtItem);

    return 0;
}

static RtItem_t* MatchFib4IndexTbl(RtTbl_t* tbl, DP_InAddr_t dst)
{
    uint32_t index;
    DP_Fib4Key_t key = {0};
    key.dip = UTILS_NTOHL(dst);
    key.pfxlen = DP_INET_MASK_LEN; /* 最长匹配 */

    if (TbmFib4LmpEntry(tbl->fibTbl, &key, &index) != 0) {
        return NULL;
    }

    return MemNodeGetPointer(tbl->rtItemPool, (int32_t)index);
}

struct MatchRtContext {
    RtKey_t* rtKey;
    RtItem_t* cur;
    int maskLen;
};

static uint32_t MatchRtHook(void* para, int32_t id, void *out)
{
    (void)id;
    RtItem_t *item = (RtItem_t *)para;
    struct MatchRtContext *ctx = (struct MatchRtContext *)out;

    ctx->rtKey->mask = item->key.mask;
    if (RT_KEY_EQUAL(ctx->rtKey, &item->key) && DP_NetmaskLen(item->key.mask) > ctx->maskLen) {
        ctx->cur = item;
        return 0;
    }
    return 1;
}

static RtItem_t* MatchRt(RtTbl_t* tbl, DP_InAddr_t dst)
{
    RtKey_t key = {
        .addr = dst,
    };

    int maskLen = 0;
    struct MatchRtContext ctx;

    if (tbl->fibTblUsed == true) {
        RtItem_t* item = MatchFib4IndexTbl(tbl, dst);
        return item == NULL ? tbl->dftRt : item;
    }

    ctx.rtKey = &key;
    ctx.maskLen = maskLen;
    ctx.cur = NULL;
    if (MemPoolWalker(tbl->rtItemPool, MatchRtHook, &ctx) == 0) {
        return ctx.cur;
    }

    return tbl->dftRt;
}

RtItem_t* GetRt(RtTbl_t* tbl, DP_InAddr_t dst)
{
    RtItem_t* item;

    (void)ATOMIC32_Inc(&tbl->ref);

    item = MatchRt(tbl, dst);
    if (item != NULL) {
        (void)RtRef(item);
    }

    (void)ATOMIC32_Dec(&tbl->ref);
    return item;
}

void PutRt(RtItem_t* item)
{
    if (RtDeref(item) == 0) {
        if (item->ifaddr->dev != NULL) {
            NETDEV_PutDev(item->ifaddr->dev);
        }
        NETDEV_FreeIfAddr(item->ifaddr);
        FreeRtItem(item);
    }
}
