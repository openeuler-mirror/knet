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
#include "utils_log.h"
#include "pbuf_local.h"

#include "pbuf.h"

static DP_PbufMemHooks_t g_pbufMemHooks = { 0 };

Pbuf_t* PBUF_Alloc(uint16_t headroom, uint16_t dataroom)
{
    Pbuf_t* pbuf = NULL;
    Pbuf_t* nxtBuf = NULL;

    if (g_pbufMemHooks.pbufAlloc != NULL) {
        pbuf = g_pbufMemHooks.pbufAlloc(g_pbufMemHooks.mp, headroom + dataroom);
        if (pbuf != NULL) {
            pbuf->offset = headroom;
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

    (void)memset_s(pbuf, sizeof(Pbuf_t), 0, sizeof(Pbuf_t));

    pbuf->nsegs      = 1;
    pbuf->payloadLen = pktLen;
    pbuf->offset     = headroom;
    pbuf->next       = NULL;
    pbuf->end        = pbuf;
    pbuf->ref        = 1;

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

    if (g_pbufMemHooks.pbufFree != NULL) {
        g_pbufMemHooks.pbufFree(g_pbufMemHooks.mp, pbuf);
        return;
    }

    if (PBUF_DEREF(pbuf) > 0) {
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
        return;
    }

    PBUF_Free(pbuf);
}

int DP_PbufMemHooksReg(DP_PbufMemHooks_t* pbufMemHooks)
{
    if (pbufMemHooks == NULL || pbufMemHooks->pbufAlloc == NULL || pbufMemHooks->pbufFree == NULL) {
        return -1;
    }
    g_pbufMemHooks = *pbufMemHooks;
    return 0;
}

void PBUF_MemHooksUnreg(void)
{
    g_pbufMemHooks.pbufAlloc = NULL;
    g_pbufMemHooks.pbufFree = NULL;
    g_pbufMemHooks.mp = NULL;
}
