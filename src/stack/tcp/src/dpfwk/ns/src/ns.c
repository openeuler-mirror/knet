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

static void FreeNetns(NS_Net_t* net)
{
    if (net == NULL) {
        return;
    }

    for (int i = 0; i < NS_NET_BUTT; i++) {
        if (net->tbls[i] != NULL && g_ops[i].freeTbl != NULL) {
            g_ops[i].freeTbl(net->tbls[i]);
        }
    }

    SPINLOCK_Deinit(&net->lock);
    SHM_FREE(net, DP_MEM_FIX);
}

static NS_Net_t* AllocNetns(void)
{
    NS_Net_t* net;
    size_t    netSize = sizeof(NS_Net_t);

    net = SHM_MALLOC(netSize, MOD_NS, DP_MEM_FIX);
    if (net == NULL) {
        DP_LOG_ERR("Malloc memory failed for NS net.");
        return NULL;
    }

    (void)memset_s(net, netSize, 0, netSize);

    if (SPINLOCK_Init(&net->lock) != 0) {
        SHM_FREE(net, DP_MEM_FIX);
        return NULL;
    }

    for (int i = 0; i < NS_NET_BUTT; i++) {
        if (g_ops[i].allocTbl == NULL) {
            continue;
        }

        net->tbls[i] = g_ops[i].allocTbl();
        if (net->tbls[i] == NULL) {
            goto err;
        }
    }

    return net;
err:
    FreeNetns(net);
    return NULL;
}

void NS_SetNetOps(int id, void *alloc, void *freeFunc)
{
    if (id < 0 || id >= NS_NET_BUTT) {
        return;
    }
    g_ops[id].allocTbl = alloc;
    g_ops[id].freeTbl = freeFunc;
}

void NS_CleanNetOps(int id)
{
    if (id < 0 || id >= NS_NET_BUTT) {
        return;
    }
    g_ops[id].allocTbl = NULL;
    g_ops[id].freeTbl  = NULL;
}

int NS_Init(int slave)
{
    SHM_REG("dftnet", g_defaultNet);

    if (slave != 0) {
        return 0;
    }

    if (g_defaultNet != NULL) {
        return -1;
    }

    g_defaultNet = AllocNetns();
    if (g_defaultNet == NULL) {
        return -1;
    }

    return 0;
}

void NS_Deinit(int slave)
{
    if (slave != 0) {
        return;
    }
    FreeNetns(g_defaultNet);
    g_defaultNet = NULL;
}
