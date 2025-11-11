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

#include "pmgr.h"

#include "utils_base.h"

PMGR_ProtoEntry_t g_entrys[] = {
    [PMGR_ENTRY_ETH_IN]     = NULL,
    [PMGR_ENTRY_ETH_OUT]    = NULL,
    [PMGR_ENTRY_IP_IN]      = NULL,
    [PMGR_ENTRY_IP_OUT]     = NULL,
    [PMGR_ENTRY_ICMP_IN]    = NULL,
    [PMGR_ENTRY_ARP_IN]     = NULL,
    [PMGR_ENTRY_TCP_IN]     = NULL,
    [PMGR_ENTRY_UDP_IN]     = NULL,
    [PMGR_ENTRY_UDP_ERR_IN] = NULL,
    [PMGR_ENTRY_ROUTE_OUT]  = NULL,
    [PMGR_ENTRY_ND_OUT]     = NULL,
    [PMGR_ENTRY_VLAN_OUT]   = NULL,

    [PMGR_ENTRY_BUTT]       = NULL,
};

void PMGR_AddEntry(PMGR_Entry_t id, PMGR_ProtoEntry_t entry)
{
    ASSERT(id >= PMGR_ENTRY_ETH_IN);
    ASSERT(id < PMGR_ENTRY_BUTT);

    g_entrys[id] = entry;
}

void PMGR_Dispatch(Pbuf_t* pbuf)
{
    PMGR_ProtoEntry_t entry;

    do {
        entry = g_entrys[PBUF_GET_ENTRY(pbuf)];
        if (entry == NULL) {
            PBUF_Free(pbuf);
            break;
        } else {
            pbuf = entry(pbuf);
        }
    } while (pbuf != NULL);
}

int PMGR_Init(int slave)
{
    int (*initFns[])(int slave) = {
        ETH_Init,
        IP_Init,
        IP6_Init,
        UDP_Init,
        TCP_Init,
    };

    for (int i = 0; i < (int)ARRAY_SIZE(initFns); i++) {
        if (initFns[i] == NULL) {
            continue;
        }
        if (initFns[i](slave) != 0) {
            PMGR_Deinit(slave);
            return -1;
        }
    }

    return 0;
}

void PMGR_Deinit(int slave)
{
    void (*deinitFns[])(int slave) = {
        ETH_Deinit,
        IP_Deinit,
        IP6_Deinit,
        UDP_Deinit,
        TCP_Deinit,
    };

    for (int i = 0; i < (int)ARRAY_SIZE(deinitFns); i++) {
        if (deinitFns[i] == NULL) {
            continue;
        }
        deinitFns[i](slave);
    }

    for (int i = 0; i < PMGR_ENTRY_BUTT; i++) {
        g_entrys[i] = NULL;
    }
}

PMGR_ProtoEntry_t* GetProtoEntrys(void)
{
    return g_entrys;
}