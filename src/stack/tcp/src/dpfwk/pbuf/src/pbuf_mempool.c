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

#include "utils_cfg.h"
#include "utils_mem_pool.h"
#include "utils_base.h"

#include "pbuf.h"
#include "pbuf_local.h"

#define PBUF_DATAROOM_SIZE 2048 /* 内存池中每个内存块可用的数据报文空间大小。当前约定为2048 */

static char* g_pbufMpName = "DP_PBUF_MP";
static DP_Mempool g_pbufMp = NULL;

static void PbufMpFree(void* mp, Pbuf_t* pbuf)
{
    uint8_t ref = pbuf->ref;
    Pbuf_t* cur = pbuf;
    Pbuf_t* next = NULL;
    while (cur != NULL) {
        next = cur->next;
        cur->ref = ref; // 同步首片的引用计数，由底层决定是否真正释放
        MEMPOOL_FREE(mp, cur);
        cur = next;
    }
}

/**
 * @brief 内存块的结构说明图
 * @par 内存池基于dp的rte_mbuf内存池，其中每个内存块结构如下：
 *                   |<---privateSize--->|<-headroomSize->|<-dataroomSize->|
 * +-----------+-----+-------------------+----------------+----------------+
 * | rte_mbuf  | DBG |    privateData    |    headroom    |    dataroom    |
 * +-----------+-----+-------------------+----------------+----------------+
 * ^                 ^                   ^
 * mbuf              pbuf                pbuf->payload
 * 约束pbuf->payload已设置为数据段的起始位置，pbuf->payloadLen已设置为数据段的最大长度
 */
static Pbuf_t* PbufMpAlloc(void* mp, uint32_t payload)
{
    uint32_t allocedLen = 0;
    Pbuf_t* ret = NULL;
    Pbuf_t* nxt = NULL;

    // 分配首片
    ret = MEMPOOL_ALLOC(mp);
    if (ret == NULL) {
        return NULL;
    }
    DP_PBUF_SET_SEG_NUM(ret, 1);
    DP_PBUF_SET_NEXT(ret, NULL);
    DP_PBUF_SET_END(ret, ret);
    DP_PBUF_SET_REF(ret, 1);
    PBUF_SET_ND(ret, NULL);
    PBUF_SET_FLOW(ret, NULL);
    DP_PBUF_SET_OLFLAGS(ret, 0);

    allocedLen += DP_PBUF_GET_PAYLOAD_LEN(ret);

    // 分配更多片
    while (allocedLen < payload) {
        nxt = MEMPOOL_ALLOC(mp);
        if (nxt == NULL) {
            PbufMpFree(mp, ret);
            return NULL;
        }
        DP_PBUF_SET_NEXT(DP_PBUF_GET_END(ret), nxt);
        DP_PBUF_SET_END(ret, nxt);
        DP_PBUF_SET_SEG_NUM(ret, DP_PBUF_GET_SEG_NUM(ret) + 1);
        PBUF_SET_ND(nxt, NULL);
        PBUF_SET_FLOW(nxt, NULL);
        DP_PBUF_SET_OLFLAGS(ret, 0);
        allocedLen += DP_PBUF_GET_PAYLOAD_LEN(nxt);
    }

    return ret;
}

int PBUF_MpInit(void)
{
    if (g_pbufMp != NULL) {
        return -1;
    }

    DP_MempoolCfg_S mpCfg = {0};
    mpCfg.name = g_pbufMpName;
    mpCfg.count = (uint32_t)CFG_GET_VAL(DP_CFG_MBUF_MAX);
    mpCfg.size = PBUF_DATAROOM_SIZE + sizeof(Pbuf_t);
    mpCfg.type = DP_MEMPOOL_TYPE_PBUF;

    int ret = MEMPOOL_CREATE(&mpCfg, NULL, &g_pbufMp);
    if (ret != 0) {
        return ret;
    }

    DP_PbufMemHooks_t memHook = {
        .mp = g_pbufMp,
        .pbufAlloc = PbufMpAlloc,
        .pbufFree = PbufMpFree,
    };
    ret = DP_PbufMemHooksReg(&memHook);
    if (ret != 0) {
        MEMPOOL_DESTROY(g_pbufMp);
    }
    return ret;
}

void PBUF_MpDeinit(void)
{
    if (g_pbufMp == NULL) {
        return;
    }
    PBUF_MemHooksUnreg();

    MEMPOOL_DESTROY(g_pbufMp);
    g_pbufMp = NULL;
}
