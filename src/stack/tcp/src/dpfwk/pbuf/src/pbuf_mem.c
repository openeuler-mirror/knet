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

#include "shm.h"
#include "worker.h"
#include "utils_log.h"
#include "utils_cfg.h"
#include "pbuf_local.h"

#include "pbuf.h"

static PBUF_PbufMemHooks_t g_pbufMemHooks = { 0 };
static PBUF_EbufMemHooks_t g_ebufMemHooks = { 0 };
static PBUF_RefPbufMemHooks_t g_refPbufMemHooks = { 0 };

static inline void PBUF_Init(Pbuf_t* pbuf, uint16_t pktLen, uint16_t headroom)
{
    (void)memset_s(pbuf, sizeof(Pbuf_t), 0, sizeof(Pbuf_t));

    pbuf->nsegs      = 1;
    pbuf->payloadLen = pktLen;
    pbuf->offset     = headroom;
    pbuf->next       = NULL;
    pbuf->end        = pbuf;
    pbuf->ref        = 1;
}

Pbuf_t* PBUF_Alloc(uint16_t headroom, uint16_t dataroom)
{
    Pbuf_t* pbuf = NULL;
    Pbuf_t* nxtBuf = NULL;

    if (g_pbufMemHooks.pbufAlloc != NULL) {
        if (headroom > g_pbufMemHooks.payloadLen) {
            DP_LOG_ERR("Malloc memory failed for headroom %u too large.", headroom);
            return NULL;
        }
        pbuf = g_pbufMemHooks.pbufAlloc(g_pbufMemHooks.mp, headroom + dataroom);
        if (pbuf != NULL) {
            DP_PBUF_SET_OFFSET(pbuf, headroom);
            DP_PBUF_SET_VPNID(pbuf, 0);
        } else {
            DP_ADD_ABN_STAT(DP_PBUF_HOOK_ALLOC_ERR);
        }
        return pbuf;
    }

    uint32_t totLen = (uint32_t)headroom + (uint32_t)dataroom;
    uint16_t pktLen = totLen >= PBUF_MAX_SEG_LEN ? PBUF_MAX_SEG_LEN : (uint16_t)totLen;
    uint16_t nxtSegLen = totLen >= PBUF_MAX_SEG_LEN ? (uint16_t)(totLen - PBUF_MAX_SEG_LEN) : 0;

    pbuf = SHM_MALLOC(sizeof(Pbuf_t) + pktLen, MOD_PBUF, DP_MEM_FREE);
    if (pbuf == NULL) {
        DP_LOG_ERR("Malloc memory failed for pbuf.");
        return NULL;
    }

    PBUF_Init(pbuf, pktLen, headroom);

    if (nxtSegLen > 0) {
        nxtBuf = SHM_MALLOC(sizeof(Pbuf_t) + nxtSegLen, MOD_PBUF, DP_MEM_FREE);
        if (nxtBuf == NULL) {
            DP_LOG_ERR("Malloc memory failed for nxtBuf.");
            SHM_FREE(pbuf, DP_MEM_FREE);
            return NULL;
        }
        (void)memset_s(nxtBuf, sizeof(Pbuf_t), 0, sizeof(Pbuf_t));
        nxtBuf->payloadLen = nxtSegLen;
        pbuf->nsegs++;
        pbuf->next = nxtBuf;
        pbuf->end = nxtBuf;
        nxtBuf->next = NULL;
        nxtBuf->end = nxtBuf;
        nxtBuf->payload = (uint8_t *)(nxtBuf + 1);
    }

    pbuf->payload = (uint8_t*)(pbuf + 1);

    return pbuf;
}

void PBUF_Free(Pbuf_t* pbuf)
{
    Pbuf_t* cur = pbuf;

    if (PBUF_DEREF(pbuf) > 0) {
        return;
    }

    if (g_pbufMemHooks.pbufFree != NULL) {
        g_pbufMemHooks.pbufFree(g_pbufMemHooks.mp, pbuf);
        return;
    }

    do {
        Pbuf_t* next = cur->next;
        SHM_FREE(cur, DP_MEM_FREE);
        cur = next;
    } while (cur != NULL);
}

void DP_PbufFree(DP_Pbuf_t* pbuf)
{
    if (pbuf == NULL) {
        DP_LOG_INFO("DP_PbufFree pbuf is NULL.");
        return;
    }

    PBUF_Free(pbuf);
}

uint16_t PBUF_GetSegLen()
{
    if (g_pbufMemHooks.pbufAlloc != NULL) {
        return g_pbufMemHooks.payloadLen;
    }
    return PBUF_MAX_SEG_LEN;
}

Pbuf_t* PBUF_Construct(void* ebuf, uint64_t offset, uint16_t len)
{
    if (ebuf == NULL) {
        return NULL;
    }

    if (g_pbufMemHooks.pbufConstruct == NULL) {
        return NULL;
    }

    return g_pbufMemHooks.pbufConstruct(g_pbufMemHooks.mp, ebuf, offset, len);
}

int PBUF_PbufMemHooksReg(PBUF_PbufMemHooks_t* pbufMemHooks)
{
    if (pbufMemHooks == NULL || pbufMemHooks->pbufAlloc == NULL || pbufMemHooks->pbufFree == NULL ||
        pbufMemHooks->pbufConstruct == NULL || pbufMemHooks->payloadLen == 0) {
        return -1;
    }
    g_pbufMemHooks = *pbufMemHooks;
    return 0;
}

void PBUF_MemHooksUnreg(void)
{
    g_pbufMemHooks.pbufAlloc = NULL;
    g_pbufMemHooks.payloadLen = 0;
    g_pbufMemHooks.pbufFree = NULL;
    g_pbufMemHooks.mp = NULL;

    g_ebufMemHooks.ebufAlloc = NULL;
    g_ebufMemHooks.ebufFree = NULL;
    g_ebufMemHooks.ep = NULL;

    g_refPbufMemHooks.refPbufAlloc = NULL;
    g_refPbufMemHooks.refPbufFree = NULL;
    g_refPbufMemHooks.refPbufConstruct = NULL;
    g_refPbufMemHooks.mp = NULL;
}

void* PBUF_ExtBufAlloc(void)
{
    void* ebuf = NULL;
    int wid = 0;

    if (g_ebufMemHooks.ebufAlloc != NULL) {
        ebuf = g_ebufMemHooks.ebufAlloc(g_ebufMemHooks.ep);
    }

    if (ebuf == NULL) {
        return NULL;
    }

    if (CFG_GET_VAL(DP_CFG_DEPLOYMENT) == DP_DEPLOYMENT_CO_THREAD) {
        wid = WORKER_GetSelfId();
    }

    if (wid >= 0) {
        DP_ZcopyMemCntAdd((uint32_t)wid, (size_t)CFG_GET_VAL(DP_CFG_ZBUF_LEN_MAX), DP_MEM_ZCOPY_SEND);
    }

    return ebuf;
}

void PBUF_ExtBufFree(void* ebuf)
{
    int wid = 0;
    if (ebuf == NULL) {
        return;
    }

    if (CFG_GET_VAL(DP_CFG_DEPLOYMENT) == DP_DEPLOYMENT_CO_THREAD) {
        wid = WORKER_GetSelfId();
    }

    if (g_ebufMemHooks.ebufFree != NULL) {
        if (wid >= 0) {
            DP_ZcopyMemCntSub((uint32_t)wid, (size_t)CFG_GET_VAL(DP_CFG_ZBUF_LEN_MAX), DP_MEM_ZCOPY_SEND);
        }
        g_ebufMemHooks.ebufFree(g_ebufMemHooks.ep, ebuf);
    }
}

int PBUF_EbufMemHooksReg(PBUF_EbufMemHooks_t* ebufMemHooks)
{
    if (ebufMemHooks == NULL || ebufMemHooks->ebufAlloc == NULL || ebufMemHooks->ebufFree == NULL) {
        return -1;
    }
    g_ebufMemHooks = *ebufMemHooks;
    return 0;
}

Pbuf_t* PBUF_RefPbufAlloc(void)
{
    Pbuf_t* pbuf = NULL;

    if (g_refPbufMemHooks.refPbufAlloc != NULL) {
        pbuf = g_refPbufMemHooks.refPbufAlloc(g_refPbufMemHooks.mp);
    }

    return pbuf;
}

void PBUF_RefPbufFree(Pbuf_t* pbuf)
{
    if (pbuf == NULL) {
        return;
    }

    if ((CFG_GET_VAL(DP_CFG_DEPLOYMENT) != DP_DEPLOYMENT_CO_THREAD)) {
        PBUF_Free(pbuf);
        return;
    }

    if (g_refPbufMemHooks.refPbufFree != NULL) {
        g_refPbufMemHooks.refPbufFree(g_refPbufMemHooks.mp, pbuf);
    }
}

Pbuf_t* PBUF_RefPbufConstruct(void* ebuf, uint64_t offset, uint16_t len)
{
    if (ebuf == NULL) {
        return NULL;
    }

    if ((CFG_GET_VAL(DP_CFG_DEPLOYMENT) != DP_DEPLOYMENT_CO_THREAD)) {  // 非共线程场景，使用 pbuf pool
        return PBUF_Construct(ebuf, offset, len);
    }

    if (g_refPbufMemHooks.refPbufConstruct == NULL) {
        return NULL;
    }

    return g_refPbufMemHooks.refPbufConstruct(g_refPbufMemHooks.mp, ebuf, offset, len);
}

int PBUF_RefPbufMemHooksReg(PBUF_RefPbufMemHooks_t* refPbufMemHooks)
{
    if (refPbufMemHooks == NULL || refPbufMemHooks->refPbufAlloc == NULL || refPbufMemHooks->refPbufFree == NULL
        || refPbufMemHooks->refPbufConstruct == NULL) {
        return -1;
    }
    g_refPbufMemHooks = *refPbufMemHooks;
    return 0;
}