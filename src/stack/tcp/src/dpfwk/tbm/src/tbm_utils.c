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
/* 
 * TBM支持注册HASH和FIB表实现，无默认实现，暂时不放到UTIL模块中
 * */

#include "tbm_utils.h"

#include <stdbool.h>
#include <securec.h>

#include "dp_fib4tbl_api.h"
#include "dp_hashtbl_api.h"

#include "utils_log.h"
#include "shm.h"

static DP_HashTblHooks_t g_hashFuncGrp = {0};
static DP_Fib4TblHooks_t g_fib4Hook = {0};
static bool g_tbmUtilInited = false;

int TbmUtilInit(void)
{
    g_tbmUtilInited = true;
    return 0;
}

int TbmUtilDeinit(void)
{
    g_tbmUtilInited = false;
    return 0;
}

int DP_HashTblHooksReg(DP_HashTblHooks_t *hashTblHooks)
{
    /* 已初始化，不允许再注册钩子实现 */
    if (g_tbmUtilInited == true) {
        DP_LOG_ERR("Hash hookreg failed, tbm util has been inited.");
        return EPERM;
    }

    if (g_hashFuncGrp.createTable != NULL) {
        DP_LOG_ERR("Hash hookreg failed, tbm g_hashFuncGrp has been registed.");
        return EEXIST;
    }

    if ((hashTblHooks == NULL) ||
        (hashTblHooks->createTable == NULL) ||
        (hashTblHooks->destroyTable == NULL) ||
        (hashTblHooks->insertEntry == NULL) ||
        (hashTblHooks->modifyEntry == NULL) ||
        (hashTblHooks->delEntry == NULL) ||
        (hashTblHooks->lookupEntry == NULL) ||
        (hashTblHooks->getInfo == NULL) ||
        (hashTblHooks->hashtblEntryGetFirst == NULL) ||
        (hashTblHooks->hashtblEntryGetNext == NULL)) {
        DP_LOG_ERR("Hash hookreg failed, invalid hashTblHooks.");
        return EINVAL;
    }

    g_hashFuncGrp = *hashTblHooks;

    return 0;
}

DP_HashTblHooks_t *GetHashtblHook(void)
{
    return &g_hashFuncGrp;
}

void ClearHashtblHook(void)
{
    (void)memset_s(&g_hashFuncGrp, sizeof(g_hashFuncGrp), 0, sizeof(g_hashFuncGrp));
}

int DP_Fib4TblHooksReg(DP_Fib4TblHooks_t *fib4TblHooks)
{
    if (g_tbmUtilInited == true) {
        DP_LOG_ERR("Fib4 hookreg failed, tbm util has been inited.");
        return EPERM;
    }

    if (g_fib4Hook.createTable != NULL) {
        DP_LOG_ERR("Fib4 hookreg failed, fib4 hook has been registed.");
        return EEXIST;
    }

    if ((fib4TblHooks == NULL) ||
        (fib4TblHooks->createTable == NULL) ||
        (fib4TblHooks->destroyTable == NULL) ||
        (fib4TblHooks->insertEntry == NULL) ||
        (fib4TblHooks->modifyEntry == NULL) ||
        (fib4TblHooks->delEntry == NULL) ||
        (fib4TblHooks->exactMatchEntry == NULL) ||
        (fib4TblHooks->lmpEntry == NULL) ||
        (fib4TblHooks->getInfo == NULL) ||
        (fib4TblHooks->fib4EntryGetFirst == NULL) ||
        (fib4TblHooks->fib4EntryGetNext == NULL)) {
        DP_LOG_ERR("Fib4 hookreg failed, invalid fib4TblHooks.");
        return EINVAL;
    }

    g_fib4Hook = *fib4TblHooks;
    return 0;
}

DP_Fib4TblHooks_t *GetFib4tblHook(void)
{
    return &g_fib4Hook;
}

void ClearFib4tblHook(void)
{
    (void)memset_s(&g_fib4Hook, sizeof(g_fib4Hook), 0, sizeof(g_fib4Hook));
}

#define MEM_POOL_NODE_INDEX_MASK 0x7FFFFFFF
#define MEM_POOL_NODE_INVAL_BIT 0x80000000

typedef struct MemPoolNode {
    void *para; /* 保存free链表指针或者用于本身保存小于指针长度的数据 */
    union {
        int32_t index; /* 单独维护暴露给外部的节点ID，用于快速索引和判断节点是否有效，小于0表示无效，范围0~INTMAX */
        uint32_t id; /* 用作方便位运算 */
    };
} MemPoolNode_t;

typedef struct FixedMemPool {
    MemPoolNode_t *mem; /* 保存内存池的起始地址 */
    MemPoolNode_t *freeList;
    MemPoolNode_t *freeTail; /* 记录freeList尾部节点 */
    uint32_t maxCnt;
    uint32_t curCnt; /* 记录已经分配了多少个节点 */
    void (*nodeFreeHook)(void*);
} FixedMemPool_t;

void *MemPoolCreate(size_t count, size_t itemSize, NodeFreeHook freeHook)
{
    (void)itemSize; /* 当前只用于保存指针，不使用此参数，后续根据需要扩展 */
    FixedMemPool_t *pool;
    size_t allocsize;

    /* 在该函数中全字段赋值，无需初始化 */
    pool = SHM_MALLOC(sizeof(FixedMemPool_t), MOD_TBM, DP_MEM_FREE);
    if (pool == NULL) {
        DP_LOG_ERR("Malloc memory failed for fixd mempool.");
        return NULL;
    }

    allocsize = count * sizeof(MemPoolNode_t);
    /* 在该函数中全字段赋值，无需初始化 */
    pool->mem = SHM_MALLOC(allocsize, MOD_TBM, DP_MEM_FREE);
    if (pool->mem == NULL) {
        DP_LOG_ERR("Malloc memory failed for pool mem.");
        SHM_FREE(pool, DP_MEM_FREE);
        return NULL;
    }
    (void)memset_s(pool->mem, allocsize, 0, allocsize);
    pool->freeList = NULL;
    pool->freeTail = NULL;
    pool->maxCnt = (uint32_t)count;
    pool->curCnt = 0;
    pool->nodeFreeHook = freeHook;

    for (uint32_t i = 0; i < pool->maxCnt; i++) {
        pool->mem[i].id = (i | MEM_POOL_NODE_INVAL_BIT);
    }

    return pool;
}

int32_t MemNodeAlloc(void *pool)
{
    MemPoolNode_t *item;
    FixedMemPool_t *mempool = (FixedMemPool_t *)pool;

    if (mempool->freeList != NULL) {
        item = (MemPoolNode_t *)mempool->freeList;
        mempool->freeList = (MemPoolNode_t *)item->para;
        if (mempool->freeList == NULL) {
            mempool->freeTail = NULL;
        }
        item->para = NULL;
        item->id &= (~MEM_POOL_NODE_INVAL_BIT);
        return item->index;
    }

    if (mempool->curCnt >= mempool->maxCnt) {
        DP_LOG_ERR("MemNodeAlloc failed, mempool is full");
        return -1;
    }
    item = &mempool->mem[mempool->curCnt];
    mempool->curCnt++;
    item->para = NULL;
    item->id &= (~MEM_POOL_NODE_INVAL_BIT);

    return item->index;
}


static inline bool NodeIsUsed(int32_t id)
{
    return id >= 0;
}

static MemPoolNode_t* NodeId2Node(FixedMemPool_t *mempool, int32_t index)
{
    MemPoolNode_t *item;

    if ((index < 0) || ((uint32_t)index >= mempool->curCnt)) {
        return NULL;
    }
    item = &mempool->mem[index];

    return item;
}

void MemNodeFree(void *pool, int32_t id)
{
    MemPoolNode_t *item;
    FixedMemPool_t *mempool = (FixedMemPool_t *)pool;

    item = NodeId2Node(mempool, id);
    if (item == NULL) {
        return;
    }
    item->id |= MEM_POOL_NODE_INVAL_BIT;
    /* 第一次产生节点删除 */
    if (mempool->freeList == NULL) {
        mempool->freeList = item;
        mempool->freeTail = item;
        mempool->freeTail->para = NULL;
    } else {
        mempool->freeTail->para = item; /* 新释放的节点插入到链表尾部 */
        mempool->freeTail = item;
        item->para = NULL;
    }
}

uint32_t MemNodeSetPointer(void *pool, int32_t id, void* pointer)
{
    MemPoolNode_t *item;
    FixedMemPool_t *mempool = (FixedMemPool_t *)pool;

    item = NodeId2Node(mempool, id);
    if (item == NULL) {
        return EINVAL;
    }

    /* index不相等，表明节点未使用 */
    if (item->index != id) {
        return EINVAL;
    }

    item->para = pointer;
    return 0;
}

void* MemNodeGetPointer(void *pool, int32_t id)
{
    MemPoolNode_t *item;
    FixedMemPool_t *mempool = (FixedMemPool_t *)pool;

    item = NodeId2Node(mempool, id);
    if (item == NULL) {
        return NULL;
    }

    /* index不相等，表明节点未使用 */
    if (!NodeIsUsed(item->index)) {
        return NULL;
    }
    return item->para;
}

uint32_t MemPoolWalker(void *pool, NodeWalkerHook nodeWalker, void *out)
{
    MemPoolNode_t *item;
    FixedMemPool_t *mempool = (FixedMemPool_t *)pool;
    for (uint32_t i = 0; i < mempool->curCnt; i++) {
        item = &mempool->mem[i];
        if (!NodeIsUsed(item->index)) {
            continue;
        }
        if (nodeWalker(item->para, item->index, out) == 0) {
            return 0;
        }
    }
    return 1;
}

void MemPoolDestroy(void *pool)
{
    MemPoolNode_t *item;
    FixedMemPool_t *mempool = (FixedMemPool_t *)pool;
    for (uint32_t i = 0; i < mempool->curCnt; i++) {
        item = &mempool->mem[i];
        if (NodeIsUsed(item->index)) {
            mempool->nodeFreeHook(item->para);
        }
    }
    SHM_FREE(mempool->mem, DP_MEM_FREE);
    SHM_FREE(mempool, DP_MEM_FREE);
}
