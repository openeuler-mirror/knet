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
#include "utils_log.h"

#include "pbuf.h"
#include "pbuf_local.h"

#define PBUF_DATAROOM_SIZE 2176 /* 内存池中每个内存块可用的数据报文空间大小。当前约定为2048 + 128 */

static char* g_pbufMpName = "DP_PBUF_MP";
static char* g_ebufMpName = "DP_EBUF_MP";
static char* g_refPbufMpName = "DP_REF_PBUF_MP";
static DP_Mempool g_pbufMp = NULL;
static DP_Mempool g_ebufMp = NULL;
static DP_Mempool g_refPbufMp = NULL;

// 确保 DP_Pbuf_t结构体 cache对齐
#define STATIC_ASSERT_PBUF()                                                  \
    do {                                                                      \
        STATIC_ASSERT(sizeof(DP_Pbuf_t) % CACHE_LINE == 0);                   \
    } while (0)

static void PbufMpFree(void* mp, Pbuf_t* pbuf)
{
    uint8_t ref = pbuf->ref;
    Pbuf_t* cur = pbuf;
    Pbuf_t* next = NULL;
    while (cur != NULL) {
        next = cur->next;
        cur->ref = ref;
        if ((cur->flags & DP_PBUF_FLAGS_EXTERNAL) == DP_PBUF_FLAGS_EXTERNAL) {
            if (EBUF_REFCNTUPDATE(cur, -1) == 1) {
                EBUF_CALLBACK(cur);
            }
        } else if ((cur->flags & DP_PBUF_FLAGS_EXT_HEAD) == DP_PBUF_FLAGS_EXT_HEAD) {
            // 偏移掉mbuf头大小，mbuf头大小192
            *(void**)((uintptr_t)cur - 192) = NULL;
            cur->next = NULL;
            MEMPOOL_FREE(mp, cur);
        }
        else {
            MEMPOOL_FREE(mp, cur);
        }
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
    DP_PBUF_SET_TOTAL_LEN(ret, 0);
    DP_PBUF_SET_OFFSET(ret, 0);
    DP_PBUF_SET_VPNID(ret, 0);
    DP_PBUF_SET_SEG_LEN(ret, 0);
    ret->flags = 0;

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
        DP_PBUF_SET_OLFLAGS(nxt, 0);
        DP_PBUF_SET_TOTAL_LEN(nxt, 0);
        DP_PBUF_SET_OFFSET(nxt, 0);
        DP_PBUF_SET_VPNID(ret, 0);
        DP_PBUF_SET_SEG_LEN(ret, 0);
        nxt->flags = 0;
        allocedLen += DP_PBUF_GET_PAYLOAD_LEN(nxt);
    }

    return ret;
}

static DP_Pbuf_t* PbufMpConstruct(void* mp, void* ebuf, uint64_t offset, uint16_t len)
{
    DP_Pbuf_t* ret = NULL;

    ret = MEMPOOL_CONSTRUCT(mp, ebuf, offset, len);
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

    ret->offset = 0;
    ret->payload = (void*)((uint8_t*)ebuf + offset);
    ret->totLen = len;
    ret->segLen = len;
    ret->payloadLen = len;
    ret->flags |= DP_PBUF_FLAGS_EXTERNAL;
    return ret;
}

static void EbufMpFree(void* ep, void* ebuf)
{
    MEMPOOL_FREE(ep, ebuf);
}

static void* EbufMpAlloc(void* ep)
{
    void* ret = NULL;

    ret = MEMPOOL_ALLOC(ep);

    return ret;
}

static void RefPbufMpFree(void* mp, Pbuf_t* pbuf)
{
    MEMPOOL_FREE(mp, pbuf);
}

static Pbuf_t* RefPbufMpAlloc(void* mp)
{
    Pbuf_t* ret = NULL;

    ret = MEMPOOL_ALLOC(mp);

    return ret;
}

static DP_Pbuf_t* RefPbufMpConstruct(void* mp, void* ebuf, uint64_t offset, uint16_t len)
{
    DP_Pbuf_t* ret = NULL;

    ret = MEMPOOL_CONSTRUCT(mp, ebuf, offset, len);
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

    ret->offset = 0;
    ret->payload = (void*)((uint8_t*)ebuf + offset);
    ret->totLen = len;
    ret->segLen = len;
    ret->payloadLen = len;
    ret->flags |= DP_PBUF_FLAGS_REFERENCED;
    return ret;
}

static int PbufMpInit(void)
{
    DP_MempoolCfg_S mpCfg = {0};
    mpCfg.name = g_pbufMpName;
    mpCfg.count = (uint32_t)CFG_GET_VAL(DP_CFG_MBUF_MAX);
    mpCfg.size = PBUF_DATAROOM_SIZE + sizeof(Pbuf_t);
    mpCfg.type = DP_MEMPOOL_TYPE_PBUF;

    int ret = MEMPOOL_CREATE(&mpCfg, NULL, &g_pbufMp);
    if (ret != 0) {
        DP_LOG_ERR("PBUF_MpInit failed for create mempoll g_pbufMp.");
        return ret;
    }

    // 注册内存池相关的操作集
    PBUF_PbufMemHooks_t pbufMemHook = {
        .mp = g_pbufMp,
        .payloadLen = PBUF_DATAROOM_SIZE,
        .pbufAlloc = PbufMpAlloc,
        .pbufFree = PbufMpFree,
        .pbufConstruct = PbufMpConstruct,
    };

    ret = PBUF_PbufMemHooksReg(&pbufMemHook);
    if (ret != 0) {
        MEMPOOL_DESTROY(g_pbufMp);
    }
    return ret;
}

static int PbufZCopyMpInit(void)
{
    DP_MempoolCfg_S mpCfg = {0};
    // 创建 extern buffer pool
    mpCfg.name = g_ebufMpName;
    mpCfg.type = DP_MEMPOOL_TYPE_EBUF;
    mpCfg.count = (uint32_t)CFG_GET_VAL(DP_CFG_MBUF_MAX);
    mpCfg.size = (uint32_t)CFG_GET_VAL(DP_CFG_ZBUF_LEN_MAX);

    int ret = MEMPOOL_CREATE(&mpCfg, NULL, &g_ebufMp);
    if (ret != 0) {
        DP_LOG_ERR("PBUF_MpInit failed for create mempoll g_ebufMp.");
        return ret;
    }

    PBUF_EbufMemHooks_t ebufMemHook = {
        .ep = g_ebufMp,
        .ebufAlloc = EbufMpAlloc,
        .ebufFree = EbufMpFree,
    };

    ret = PBUF_EbufMemHooksReg(&ebufMemHook);
    if (ret != 0) {
        MEMPOOL_DESTROY(g_ebufMp);
    }
    return ret;
}

static int PbufRefMapInit(void)
{
    DP_MempoolCfg_S mpCfg = {0};
    mpCfg.name = g_refPbufMpName;
    mpCfg.type = DP_MEMPOOL_TYPE_REF_PBUF;
    mpCfg.count = (uint32_t)CFG_GET_VAL(DP_CFG_MBUF_MAX);
    mpCfg.size = sizeof(Pbuf_t);

    int ret = MEMPOOL_CREATE(&mpCfg, NULL, &g_refPbufMp);
    if (ret != 0) {
        DP_LOG_ERR("PBUF_MpInit failed for create mempoll g_refPbufMp.");
        return ret;
    }

    PBUF_RefPbufMemHooks_t refPbufHook = {
        .mp = g_refPbufMp,
        .refPbufAlloc = RefPbufMpAlloc,
        .refPbufFree = RefPbufMpFree,
        .refPbufConstruct = RefPbufMpConstruct,
    };

    ret = PBUF_RefPbufMemHooksReg(&refPbufHook);
    if (ret != 0) {
        MEMPOOL_DESTROY(g_refPbufMp);
    }
    return ret;
}

int PBUF_MpInit(void)
{
    STATIC_ASSERT_PBUF();
    int ret = 0;
    if (g_pbufMp != NULL || g_ebufMp != NULL || g_refPbufMp != NULL) {
        DP_LOG_ERR("PBUF_MpInit failed for g_ebufMp or g_pbufMp not NULL.");
        return -1;
    }

    ret = PbufMpInit();
    if (ret != 0) {
        return ret;
    }

    // 不支持零拷贝，无需初始化下列内存池
    if (CFG_GET_VAL(DP_CFG_ZERO_COPY) == DP_DISABLE) {
        return 0;
    }

    ret = PbufZCopyMpInit();
    if (ret != 0) {
        MEMPOOL_DESTROY(g_pbufMp);
        return ret;
    }

    // 非共线程，无需初始化ref内存池
    if (CFG_GET_VAL(DP_CFG_DEPLOYMENT) != DP_DEPLOYMENT_CO_THREAD) {
        return 0;
    }

    ret = PbufRefMapInit();
    if (ret != 0) {
        MEMPOOL_DESTROY(g_pbufMp);
        MEMPOOL_DESTROY(g_ebufMp);
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
    MEMPOOL_DESTROY(g_ebufMp);
    MEMPOOL_DESTROY(g_refPbufMp);
    g_pbufMp = NULL;
    g_ebufMp = NULL;
    g_refPbufMp = NULL;
}
