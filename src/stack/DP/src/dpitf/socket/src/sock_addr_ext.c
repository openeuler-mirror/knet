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

#include "dp_addr_ext_api.h"


static DP_AddrHooks_t g_addrHooks = { .eventNotify = NULL };


int DP_AddrHooksReg(DP_AddrHooks_t* addrHooks)
{
    if (g_addrHooks.eventNotify != NULL) {
        return -1;
    }
    if (addrHooks == NULL || addrHooks->eventNotify == NULL) {
        return -1;
    }

    g_addrHooks = *addrHooks;
    return 0;
}

int SOCK_AddrEventNotify(DP_AddrEventType_t type, int protocol, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen)
{
    if (g_addrHooks.eventNotify == NULL) {
        return 0;
    }

    DP_AddrEvent_t event = { 0 };
    event.protocol = protocol;
    event.localAddr = *addr;
    event.localAddrLen = addrlen;

    return g_addrHooks.eventNotify(type, &event);
}

void SOCK_AddrHooksUnreg(void)
{
    g_addrHooks.eventNotify = NULL;
}