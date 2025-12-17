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
#include "utils_log.h"

static DP_AddrHooks_t g_addrHooks = { .eventNotify = NULL };
static DP_AddrBindHooks_t g_addrBindHooks = { .preBind = NULL };


int DP_AddrHooksReg(DP_AddrHooks_t* addrHooks)
{
    if (g_addrHooks.eventNotify != NULL) {
        DP_LOG_ERR("DP_AddrHooksReg failed, the hooks is reged.");
        return -1;
    }
    if (addrHooks == NULL || addrHooks->eventNotify == NULL) {
        DP_LOG_ERR("DP_AddrHooksReg failed, invalid addrHooks.");
        return -1;
    }

    g_addrHooks = *addrHooks;
    return 0;
}

int DP_AddrBindHooksReg(DP_AddrBindHooks_t* addrBindHooks)
{
    if (g_addrBindHooks.preBind != NULL) {
        DP_LOG_ERR("DP_AddrBindHooksReg failed, the hooks is reged.");
        return -1;
    }

    if (addrBindHooks == NULL || addrBindHooks->preBind == NULL) {
        DP_LOG_ERR("DP_AddrBindHooksReg failed, invalid addrBindHooks.");
        return -1;
    }

    g_addrBindHooks = *addrBindHooks;
    return 0;
}

int SOCK_AddrEventNotify(DP_AddrEventType_t type, DP_AddrEvent_t* event)
{
    if (g_addrHooks.eventNotify == NULL) {
        return 0;
    }

    return g_addrHooks.eventNotify(type, event);
}

int SOCK_AddrPreBind(void* userData, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen)
{
    if (g_addrBindHooks.preBind == NULL) {
        return 0;
    }

    return g_addrBindHooks.preBind(userData, addr, addrlen);
}

void SOCK_AddrHooksUnreg(void)
{
    g_addrHooks.eventNotify = NULL;
    g_addrBindHooks.preBind = NULL;
}