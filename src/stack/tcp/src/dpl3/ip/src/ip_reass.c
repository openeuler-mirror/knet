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
#include "ip_reass.h"

#include <securec.h>

#include "utils_cfg.h"
#include "utils_log.h"
#include "utils_base.h"
#include "utils_debug.h"
#include "utils_mem_pool.h"
#include "utils_statistic.h"
#include "netdev.h"
#include "worker.h"

#define MAX_UNORDER_FRAG_NUM (CFG_GET_IP_VAL(DP_CFG_IP_FRAG_DIST_MAX))

typedef struct IpReassTbl IpReassTbl_t;

typedef struct IpReassNode {
    PBUF_Chain_t  reassChain;
    uint8_t       rcvLastFrag; // 如果收到最后一片，则更新totLen
    uint8_t       proto;
    uint16_t      totLen;
    uint16_t      ipid;
    uint16_t      pktSum;      // 收到的报文数量
    DP_InAddr_t   dst;
    DP_InAddr_t   src;
    Netdev_t*     dev;
    uint32_t      expired; // 重组超时时间
    IpReassTbl_t* tbl;
    LIST_ENTRY(IpReassNode) node;
} IpReassNode_t;

struct IpReassTbl {
    LIST_HEAD(, IpReassNode) rnLists; // reass nodes list

    int max;
    int cnt;
};

typedef struct {
    Pbuf_t* cur;
    Pbuf_t* prev;
    Pbuf_t* next;
    uint16_t curOffset;
    uint16_t curEndOffset;
    uint16_t prevOffset;
    uint16_t prevEndOffset;
    uint16_t nextOffset;
    uint16_t nextEndOffset;

    bool curIsLastFrag;
} ReassInfo_t;

typedef enum {
    REASS_POLICY_NEXT_PHASE,
    REASS_POLICY_STOP,
} ReassPolicy_t;

static IpReassTbl_t* g_reassTbls;

static char* g_ipNodeMpName = "DP_IP_NODE_MP";
static DP_Mempool g_ipNodeMemPool = {0};

static inline IpReassTbl_t* GetReassTbl(int wid)
{
    return &g_reassTbls[wid];
}

static inline uint16_t GetReassOffset(Pbuf_t* pbuf)
{
    return *PBUF_GET_CB(pbuf, uint16_t*);
}

static inline void SetReassOffset(Pbuf_t* pbuf, uint16_t offset)
{
    *PBUF_GET_CB(pbuf, uint16_t*) = offset;
}

static IpReassNode_t* NewReassNode(IpReassTbl_t* tbl, DP_IpHdr_t* ipHdr, DP_Pbuf_t* pbuf)
{
    IpReassNode_t* reassNode = NULL;
    uint32_t tick = (uint32_t)UTILS_MAX(CFG_GET_IP_VAL(DP_CFG_IP_REASS_TIMEOUT) * 1000, 1);
    uint32_t ipReassTimeo = WORKER_TIME2_TICK(tick);

    if (tbl->cnt >= tbl->max) {
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_NET_REASS_OVER_TBL_LIMIT);
        return NULL;
    }

    reassNode = MEMPOOL_ALLOC(g_ipNodeMemPool);
    if (reassNode == NULL) {
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_NET_REASS_MALLOC_FAIL);
        DP_LOG_ERR("Malloc memory failed for ip reassNode.");
        return NULL;
    }
    (void)memset_s(reassNode, sizeof(*reassNode), 0, sizeof(*reassNode));
    reassNode->proto   = ipHdr->type;
    reassNode->dst     = ipHdr->dst;
    reassNode->src     = ipHdr->src;
    reassNode->ipid    = ipHdr->ipid;
    reassNode->expired = WORKER_GetTick(DP_PBUF_GET_WID(pbuf)) + ipReassTimeo;
    reassNode->dev     = (Netdev_t*)PBUF_GET_DEV(pbuf);
    reassNode->tbl     = tbl;

    LIST_INSERT_TAIL(&tbl->rnLists, reassNode, node);
    tbl->cnt++;

    return reassNode;
}

static void DeleteReassNode(IpReassNode_t* reassNode)
{
    IpReassTbl_t* tbl = reassNode->tbl;

    ASSERT(tbl != NULL);

    LIST_REMOVE(&tbl->rnLists, reassNode, node);
    tbl->cnt--;

    MEMPOOL_FREE(g_ipNodeMemPool, reassNode);
}

static bool MatchReassNode(IpReassNode_t* reassNode, DP_IpHdr_t* ipHdr, DP_Pbuf_t* pbuf)
{
    return reassNode->ipid == ipHdr->ipid &&
           ipHdr->dst == reassNode->dst &&
           ipHdr->src == reassNode->src &&
           ipHdr->type == reassNode->proto &&
           PBUF_GET_DEV(pbuf) == reassNode->dev;
}

static IpReassNode_t* FindReassNode(IpReassTbl_t* tbl, DP_IpHdr_t* ipHdr, DP_Pbuf_t* pbuf)
{
    IpReassNode_t* reassNode = NULL;

    LIST_FOREACH(&tbl->rnLists, reassNode, node)
    {
        if (MatchReassNode(reassNode, ipHdr, pbuf)) {
            return reassNode;
        }
    }
    return NULL;
}


static IpReassNode_t* IpReassGetNode(DP_IpHdr_t* ipHdr, DP_Pbuf_t* pbuf)
{
    IpReassNode_t* reassNode = NULL;

    IpReassTbl_t* tbl = GetReassTbl(DP_PBUF_GET_WID(pbuf));

    ASSERT(tbl != NULL);

    reassNode = FindReassNode(tbl, ipHdr, pbuf);
    if (reassNode != NULL) {
        return reassNode;
    }

    return NewReassNode(tbl, ipHdr, pbuf);
}

static ReassPolicy_t ReassLocationPhase(IpReassNode_t* node, ReassInfo_t* info)
{
    DP_Pbuf_t* prev = NULL;
    DP_Pbuf_t* next = PBUF_CHAIN_FIRST(&node->reassChain);

    /*
      1. 查找当前 pbuf 能够插入的位置，这个位置在 C 的尾部偏移小于 B 的尾部偏移

      |---- A ----|          |----- B -----|
           ..................... C -----|
     */

    while (next != NULL) {
        info->nextOffset = GetReassOffset(next);
        info->nextEndOffset = info->nextOffset + (uint16_t)PBUF_GET_PKT_LEN(next);

        if (info->curEndOffset < info->nextEndOffset) {
            break;
        }

        info->prevOffset = info->nextOffset;
        info->prevEndOffset = info->nextEndOffset;
        prev               = next;
        next               = PBUF_CHAIN_NEXT(next);
    }
    /*
      |---- A ----|          |----- B -----| |-- D --|
            ..................... C -----|

    最后一片应该在最后
    如果 C 是最后一片，移除 B 及后面的报文后再进行后面的处理
     */
    if (info->curIsLastFrag && next != NULL) {
        uint16_t pktCnt = PBUF_ChainRemoveAfter(&node->reassChain, next);
        // 报文总数减少已经释放的报文数量
        node->pktSum -= pktCnt;
        next = NULL;
    }
    /*
      对于下面这种情况，需要将 A 丢弃，找到正确的 prev

      ..|              |--- A ---|    |--- B ---|
           |------- C -------------..

     */

    while (prev != NULL && info->curOffset < info->prevOffset) {
        Pbuf_t* tmp = PBUF_CHAIN_PREV(prev);
        PBUF_ChainRemove(&node->reassChain, prev);
        node->pktSum -= prev->nsegs; // 报文总数减少prev的pbuf数量
        PBUF_Free(prev);
        prev = tmp;
        if (prev != NULL) {
            info->prevOffset = GetReassOffset(prev);
            info->prevEndOffset = info->prevOffset + (uint16_t)PBUF_GET_PKT_LEN(prev);
        }
    }

    info->prev = prev;
    info->next = next;

    return REASS_POLICY_NEXT_PHASE;
}

static ReassPolicy_t ReassCutPhase(IpReassNode_t* node, ReassInfo_t* info)
{
    Pbuf_t* cur = info->cur;
    Pbuf_t* next = info->next;
    uint16_t dupLen = 0;

    /*
      |--- A ---|           |--- B ---|
                  |-- C-- |

      或

      |--- A ---|
                  |-- C -- |

      这两种情况直接插入重组队列
     */
    if (((next == NULL) && (info->curOffset > info->prevEndOffset)) ||
        ((next != NULL) && (info->curOffset > info->prevEndOffset) &&
         (info->curEndOffset < info->nextOffset))) {
        SetReassOffset(cur, info->curOffset);
        PBUF_ChainInsertBefore(&node->reassChain, next, cur);
        node->pktSum += cur->nsegs; // 新报文加入报文计数增加
        return REASS_POLICY_STOP;
    }

        /*
      裁剪掉后部重复的部分

      |----- A ----|           |---- B -----|
                      ---- C ----|

      或

      |----- A ----|           |---- B -----|
                                 |-- C --|
     */
    if (next != NULL && info->nextOffset < info->curEndOffset) {
        dupLen = info->curEndOffset - info->nextOffset;
        if (dupLen >= PBUF_GET_PKT_LEN(cur)) {
            // 第二种情况，完全重复，直接丢弃
            PBUF_Free(cur);
            return REASS_POLICY_STOP;
        }
        // 第一种情况，Cut 尾部 dup 数据
        PBUF_CutTailData(cur, dupLen);
        info->curEndOffset = info->nextOffset;
    }
    /*
      裁剪掉前部重复的部分

      |----- A -----|            |------- B -------|

              |------ C  ----...

      或
      |----- A -----|            |------- B -------|

            |-- C --|

     */
    if (info->prevEndOffset > info->curOffset) {
        dupLen = info->prevEndOffset - info->curOffset;
        if (dupLen >= PBUF_GET_PKT_LEN(cur)) {
            // 第二种情况，完全重复，直接丢弃
            PBUF_Free(cur);
            return REASS_POLICY_STOP;
        }
        // 第一种情况，Cut 前部数据
        PBUF_CUT_DATA(cur, dupLen);
        info->curOffset = info->prevEndOffset;
    }

    return REASS_POLICY_NEXT_PHASE;
}

static ReassPolicy_t ReassMergePhase(IpReassNode_t* node, ReassInfo_t* info)
{
    Pbuf_t* cur = info->cur;
    Pbuf_t* prev = info->prev;
    Pbuf_t* next = info->next;

    /*
    到这里可能有以下几种情况：

    1. prev == NULL && next != NULL

                  | --- A --- |
      | -- C -- ......

    2. prev != NULL && next == NULL

      | --- A --- |
                  ... -- C -- |

    3. next != NULL && prev != NULL

    |---- A ---- |            |----- B ------|
                 ... -- C -- ..

    4. next == NULL && prev == NULL

     */

    // 先将pbuf插入到reasschain中
    SetReassOffset(cur, info->curOffset);
    PBUF_ChainInsertBefore(&node->reassChain, next, cur);
    node->pktSum += cur->nsegs; // 新加入报文计数增加

    // 前一片和本片可以合并，如果前一片不存在，则插入到head
    if (prev != NULL && info->curOffset == info->prevEndOffset) {
        PBUF_ChainMergeNext(&node->reassChain, prev);
        cur = prev;
    }

    // 后一片和本片可以合并
    if (next != NULL && info->curEndOffset == info->nextOffset) {
        PBUF_ChainMergeNext(&node->reassChain, cur);
    }

    return REASS_POLICY_NEXT_PHASE;
}

static Pbuf_t* DoReass(IpReassNode_t* node, ReassInfo_t* info)
{
    Pbuf_t* ret = NULL;

    ReassPolicy_t (*reassPhases[])(IpReassNode_t*, ReassInfo_t*) = {
        ReassLocationPhase,
        ReassCutPhase,
        ReassMergePhase,
    };

    for (int i = 0; i < (int)DP_ARRAY_SIZE(reassPhases); i++) {
        if (reassPhases[i](node, info) != REASS_POLICY_NEXT_PHASE) {
            return NULL;
        }
    }

    ret = PBUF_CHAIN_FIRST(&node->reassChain);
    if (node->rcvLastFrag != 0 && PBUF_GET_PKT_LEN(ret) == node->totLen) {
        DeleteReassNode(node);
        PBUF_SET_HEAD(ret, PBUF_GET_L3_OFF(ret));
        return ret;
    }
    return NULL;
}

DP_Pbuf_t* IpReass(DP_Pbuf_t* pbuf, DP_IpHdr_t* ipHdr, uint8_t hdrLen)
{
    uint16_t ipHdrOff = UTILS_NTOHS(ipHdr->off);
    bool isLastFrag = DP_IS_LAST_FRAG(ipHdrOff) ? true : false;
    uint16_t curOffset = DP_IP_FRAG_OFFSET(ipHdrOff);
    uint16_t curEndOffset = 0;
    ReassInfo_t info = { 0 };
    IpReassNode_t* node = NULL;

    node = IpReassGetNode(ipHdr, pbuf);
    if (node == NULL) {
        goto drop;
    }

    if (node->pktSum >= (uint16_t)MAX_UNORDER_FRAG_NUM) {
        DP_INC_PKT_STAT(pbuf->wid, DP_PKT_NET_REASS_NODE_OVER_LIMIT);
        PBUF_ChainClean(&node->reassChain);
        DeleteReassNode(node);
        goto drop;
    }

    PBUF_CUT_HEAD(pbuf, hdrLen);
    curEndOffset = curOffset + (uint16_t)PBUF_GET_PKT_LEN(pbuf);

    if (isLastFrag) {
        if (node->rcvLastFrag != 0) {
            // 重复收到最后一片，简单处理，直接丢掉
            goto drop;
        }
        node->rcvLastFrag = 1;
        node->totLen = curEndOffset;
    }

    info.curOffset = curOffset;
    info.curEndOffset = curEndOffset;
    info.curIsLastFrag = isLastFrag;
    info.cur = pbuf;

    DP_INC_PKT_STAT(pbuf->wid, DP_PKT_REASS_IN_FRAG);
    return DoReass(node, &info);

drop:
    PBUF_Free(pbuf);
    return NULL;
}

void IpReassTimer(int wid, uint32_t tickNow)
{
    IpReassTbl_t*  tbl = GetReassTbl(wid);
    IpReassNode_t* reassNode;
    IpReassNode_t* nxtReassNode;

    reassNode = LIST_FIRST(&tbl->rnLists);

    while (reassNode != NULL) {
        nxtReassNode = LIST_NEXT(reassNode, node);

        if (tickNow > reassNode->expired) {
            DP_ADD_ABN_STAT(DP_INET_REASS_TIME_OUT);
            PBUF_ChainClean(&reassNode->reassChain);
            DeleteReassNode(reassNode);
        }

        reassNode = nxtReassNode;
    }
}

static WORKER_Work_t g_reassTimer = {
    .type = WORKER_WORK_TYPE_TIMER,
    .task = {
        .timerWork = {
            .internalTick = IP_REASS_TIME_INTERVAL,
            .timerCb = IpReassTimer,
        },
    },
    .map = WORKER_BITMAP_ALL,
    .next = NULL,
};

static void IpReassTblInit(IpReassTbl_t* tbl)
{
    LIST_INIT_HEAD(&tbl->rnLists);
    tbl->cnt = 0;
    tbl->max = CFG_GET_IP_VAL(DP_CFG_IP_REASS_MAX) * 2; // 2 : 缓存的分片报文个数为 2 倍的系统缓存的重组节点个数
}

static void IpReassTblDeinit(IpReassTbl_t* tbl)
{
    IpReassNode_t* reassNode = NULL;
    IpReassNode_t* nextReassNode = NULL;

    reassNode = LIST_FIRST(&tbl->rnLists);

    while (reassNode != NULL) {
        nextReassNode = LIST_NEXT(reassNode, node);
        PBUF_ChainClean(&reassNode->reassChain);
        DeleteReassNode(reassNode);
        reassNode = nextReassNode;
    }
}

static int IpNodeMemPoolInit(void)
{
    /* 内存块最大数量为最大表数量与每个表IpNode最大数量的乘积 */
    DP_MempoolCfg_S mpCfg = {0};
    mpCfg.name = g_ipNodeMpName;
    mpCfg.count = (uint32_t)(CFG_GET_VAL(DP_CFG_WORKER_MAX) * g_reassTbls[0].max);
    mpCfg.size = sizeof(IpReassNode_t);
    mpCfg.type = DP_MEMPOOL_TYPE_FIXED_MEM;

    int32_t mod = MOD_IP;
    DP_MempoolAttr_S attr = (void*)&mod;
    return MEMPOOL_CREATE(&mpCfg, &attr, &g_ipNodeMemPool);
}

int IpReassInit()
{
    if (g_reassTbls != NULL) {
        return -1;
    }

    size_t allocSize = sizeof(IpReassTbl_t) * CFG_GET_VAL(DP_CFG_WORKER_MAX);

    g_reassTbls = MEM_MALLOC(allocSize, MOD_IP, DP_MEM_FIX);
    if (g_reassTbls == NULL) {
        DP_LOG_ERR("Malloc memory failed for ip reassTbl.");
        return -1;
    }
    (void)memset_s(g_reassTbls, allocSize, 0, allocSize);

    for (int i = 0; i < CFG_GET_VAL(DP_CFG_WORKER_MAX); i++) {
        IpReassTblInit(&g_reassTbls[i]);
    }

    /* DP_MEMPOOL_TYPE_FIXED_MEM 类型的内存池创建失败时会使用MEM_MALLOC代替 */
    if (IpNodeMemPoolInit() != 0) {
        DP_LOG_INFO("IpNodeMemPoolInit failed.");
    }

    WORKER_AddWork(&g_reassTimer);
    return 0;
}

void IpReassDeinit(void)
{
    if (g_reassTbls == NULL) {
        return;
    }

    for (int i = 0; i < CFG_GET_VAL(DP_CFG_WORKER_MAX); i++) {
        IpReassTblDeinit(&g_reassTbls[i]);
    }

    MEM_FREE(g_reassTbls, DP_MEM_FIX);
    g_reassTbls = NULL;

    if (g_ipNodeMemPool != NULL) {
        MEMPOOL_DESTROY(g_ipNodeMemPool);
        g_ipNodeMemPool = NULL;
    }
}
