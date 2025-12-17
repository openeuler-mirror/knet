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
#include "ns.h"

NS_Net_t*   g_defaultNet;
NS_NetOps_t g_ops[NS_NET_BUTT];

typedef struct {
    NS_Net_t* defaultNet;
    NS_Net_t* nets;
    int maxNetCnt;
    int reserve;
} NS_Mgr_t;

static NS_Mgr_t* g_nsMgr;

static void DeinitNetns(NS_Net_t* net)
{
    for (int i = 0; i < NS_NET_BUTT; i++) {
        if (net->tbls[i] != NULL && g_ops[i].freeTbl != NULL) {
            g_ops[i].freeTbl(net->tbls[i]);
        }
    }

    SPINLOCK_Deinit(&net->lock);
    net->id = NS_INVALID_ID;
    net->isUsed = 0;
}

static int InitNetns(NS_Net_t* net, int id)
{
    SPINLOCK_Init(&net->lock);

    for (int i = 0; i < NS_NET_BUTT; i++) {
        if (g_ops[i].allocTbl == NULL) {
            DP_LOG_INFO("No allocFn for initNetns net->tbls[%d].", i);
            continue;
        }

        net->tbls[i] = g_ops[i].allocTbl();
        if (net->tbls[i] == NULL) {
            DP_LOG_ERR("InitNetns failed for net->tbls[%d].", i);
            goto err;
        }
    }
    net->id = id;
    net->isUsed = 1;
    return 0;
err:
    DeinitNetns(net);
    return -1;
}

static NS_Mgr_t* AllocNsMgr(void)
{
    NS_Mgr_t* mgr = NULL;
    size_t allocSize;
    int maxNetCnt = NS_NET_MAX;

    allocSize = sizeof(NS_Mgr_t) + sizeof(NS_Net_t) * (maxNetCnt + 1); // 1: 默认的 ns
    mgr = SHM_MALLOC(allocSize, MOD_NS, DP_MEM_FIX);
    if (mgr == NULL) {
        DP_LOG_ERR("malloc memory failed for ns mgr.");
        return NULL;
    }

    (void)memset_s(mgr, allocSize, 0, allocSize);

    mgr->maxNetCnt = maxNetCnt;
    mgr->defaultNet = (NS_Net_t*)(mgr + 1);
    mgr->nets = mgr->defaultNet + 1;

    if (InitNetns(mgr->defaultNet, -1) != 0) {
        SHM_FREE(mgr, DP_MEM_FIX);
        return NULL;
    }

    for (int i = 0; i < mgr->maxNetCnt; i++) {
        mgr->nets[i].isUsed = 0;
        mgr->nets[i].id = NS_INVALID_ID;
    }

    return mgr;
}

static void FreeNsMgr(NS_Mgr_t* mgr)
{
    if (mgr == NULL) {
        return;
    }

    for (int i = 0; i < mgr->maxNetCnt; i++) {
        NS_Net_t* net = &mgr->nets[i];
        if (net->isUsed == 0) {
            continue;
        }
        DeinitNetns(net);
    }
    DeinitNetns(mgr->defaultNet);
    SHM_FREE(mgr, DP_MEM_FIX);
}

void NS_SetNetOps(int id, void *mAlloc, void *mFree)
{
    ASSERT(id >= 0);
    ASSERT(id < NS_NET_BUTT);
    g_ops[id].allocTbl = mAlloc;
    g_ops[id].freeTbl = mFree;
}

int NS_Create(void)
{
    if (g_nsMgr == NULL) {
        DP_LOG_ERR("NS_Create failed for g_nsMgr NULL.");
        return -1;
    }

    for (int i = 0; i < g_nsMgr->maxNetCnt; i++) {
        NS_Net_t* net = &g_nsMgr->nets[i];
        if (net->isUsed != 0) {
            continue;
        }
        if (InitNetns(net, i) != 0) {
            return -1;
        }
        return i;
    }
    DP_LOG_ERR("NS_Create failed for g_nsMgr find no unused net.");
    return -1;
}

NS_Net_t* NS_GetNet(int id)
{
    if (g_nsMgr == NULL || id < -1 || id >= g_nsMgr->maxNetCnt) {
        return NULL;
    }

    if (id == -1) {
        return g_nsMgr->defaultNet;
    }

    NS_Net_t* net = &g_nsMgr->nets[id];
    return net->isUsed == 0 ? NULL : net;
}

int NS_Init(int slave)
{
    SHM_REG("nsMgr", g_nsMgr);
    if (slave != 0) {
        g_defaultNet = g_nsMgr->defaultNet;
        return 0;
    }

    if (g_nsMgr != NULL) {
        DP_LOG_ERR("NS_Init failed for g_nsMgr not NULL.");
        return -1;
    }

    g_nsMgr = AllocNsMgr();
    if (g_nsMgr == NULL) {
        return -1;
    }
    g_defaultNet = g_nsMgr->defaultNet;
    return 0;
}

void NS_Deinit(int slave)
{
    g_defaultNet = NULL;
    if (slave != 0) {
        return;
    }
    FreeNsMgr(g_nsMgr);
    g_nsMgr = NULL;
}
