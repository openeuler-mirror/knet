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

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "utils_log.h"
#include "utils_cfg.h"

static int g_cfgInit = CFG_INITIAL;

PreCfgVal_t  g_cfgVal[CFG_INNER_MAX] = { // {默认值, 最小值, 最大值}
    [DP_CFG_MBUF_MAX]        = {DP_DEFAULT_MBUF_MAX, DP_LOWLIMIT_MBUF_MAX, DP_HIGHLIMIT_MBUF_MAX},
    [DP_CFG_WORKER_MAX]      = {DP_DEFAULT_WORKER_MAX, DP_LOWLIMIT_WORKER_MAX, DP_HIGHLIMIT_WORKER_MAX},
    [DP_CFG_RT_MAX]          = {DP_DEFAULT_RT_MAX, DP_LOWLIMIT_RT_MAX, DP_HIGHLIMIT_RT_MAX},
    [DP_CFG_ARP_MAX]         = {DP_DEFAULT_ARP_MAX, DP_LOWLIMIT_ARP_MAX, DP_HIGHLIMIT_ARP_MAX},
    [DP_CFG_TCPCB_MAX]       = {DP_DEFAULT_TCPCB_MAX, DP_LOWLIMIT_TCPCB_MAX, DP_HIGHLIMIT_TCPCB_MAX},
    [DP_CFG_UDPCB_MAX]       = {DP_DEFAULT_UDPCB_MAX, DP_LOWLIMIT_UDPCB_MAX, DP_HIGHLIMIT_UDPCB_MAX},
    [DP_CFG_CPD_PKT_TRANS]   = {DP_ENABLE, DP_DISABLE, DP_ENABLE},

    [CFG_RAWCB_IP_MAX]       = {1, 1, 1},
    [CFG_RAWCB_IPV6_MAX]     = {1, 1, 1},
    [CFG_RAWCB_ETH_MAX]      = {1, 1, 1},
    [CFG_FD_OFFSET]          = {0, 0, 0},
    [CFG_NDTBL_MISS_CACHED_SIZE] = {TBM_NDTBL_MISS_CACHED_SIZE, TBM_NDTBL_MISS_CACHED_SIZE, TBM_NDTBL_MISS_CACHED_SIZE},
    [CFG_FAKE_NDITEM_ALIVE_TIME] = {TBM_FAKE_NDITEM_ALIVE_TIME, TBM_FAKE_NDITEM_ALIVE_TIME, TBM_FAKE_NDITEM_ALIVE_TIME},
    [CFG_FAKE_NDTBL_SIZE]        = {TBM_FAKE_NDTBL_SIZE, TBM_FAKE_NDTBL_SIZE, TBM_FAKE_NDTBL_SIZE},
    [CFG_ARP_MISS_INTERVAL]      = {TBM_ARP_MISS_INTERVAL, TBM_ARP_MISS_INTERVAL, TBM_ARP_MISS_INTERVAL}
};

PreCfgVal_t g_cfgTcpVal[CFG_INNER_TCP_MAX] = {
    [DP_CFG_TCP_SELECT_ACK]      = {DP_ENABLE, DP_DISABLE, DP_ENABLE},
    [DP_CFG_TCP_DELAY_ACK]       = {DP_ENABLE, DP_DISABLE, DP_ENABLE},
    [DP_CFG_TCP_MSL_TIME]        = {DP_TCP_DEFAULT_MSL_TIME, DP_TCP_LOWLIMIT_MSL_TIME, DP_TCP_HIGHLIMIT_MSL_TIME},
    [DP_CFG_TCP_FIN_TIMEOUT]     = {DP_TCP_DEFAULT_FIN_TIMEOUT, DP_TCP_LOWLIMIT_FIN_TIMEOUT,
                                    DP_TCP_HIGHLIMIT_FIN_TIMEOUT},
    [DP_CFG_TCP_COOKIE]          = {DP_DISABLE, DP_DISABLE, DP_ENABLE},
    [DP_CFG_TCP_RND_PORT_MIN]    = {DP_TCP_DEFAULT_PORT_MIN, DP_TCP_LOWLIMIT_PORT_MIN, DP_TCP_HIGHLIMIT_PORT_MIN},
    [DP_CFG_TCP_RND_PORT_MAX]    = {DP_TCP_DEFAULT_PORT_MAX, DP_TCP_LOWLIMIT_PORT_MAX, DP_TCP_HIGHLIMIT_PORT_MAX},
    [DP_CFG_TCP_WMEM_MAX]        = {DP_TCP_DEFAULT_WMEM_MAX, DP_TCP_LOWLIMIT_WMEM_MAX, DP_TCP_HIGHLIMIT_WMEM_MAX},
    [DP_CFG_TCP_WMEM_DEFAULT]    = {DP_TCP_DEFAULT_WMEM, DP_TCP_LOWLIMIT_WMEM_MAX, DP_TCP_HIGHLIMIT_WMEM_MAX},
    [DP_CFG_TCP_RMEM_MAX]        = {DP_TCP_DEFAULT_RMEM_MAX, DP_TCP_LOWLIMIT_RMEM_MAX, DP_TCP_HIGHLIMIT_RMEM_MAX},
    [DP_CFG_TCP_RMEM_DEFAULT]    = {DP_TCP_DEFAULT_RMEM, DP_TCP_LOWLIMIT_RMEM_MAX, DP_TCP_HIGHLIMIT_RMEM_MAX},

    [CFG_TCP_NO_CKSUM_VERIFY]    = {0, 0, 0},
    [CFG_TCP_NO_CKSUM_CALC]      = {0, 0, 0},
    [CFG_TCP_NO_TIMESTAMP]       = {0, 0, 0},
    [CFG_TCP_CA_ALG]             = {0, 0, 0},
    [CFG_TCP_MSS_MAX]            = {TCP_DEFAULT_MSS_MAX, TCP_DEFAULT_MSS_MAX, TCP_DEFAULT_MSS_MAX},
};

PreCfgVal_t g_cfgIpVal[CFG_INNER_IP_MAX] = {
    [DP_CFG_IP_REASS_MAX]       = {DP_IP_DEFAULT_REASS_MAX, DP_IP_LOWLIMIT_REASS_MAX, DP_IP_HIGHLIMIT_REASS_MAX},
    [DP_CFG_IP_REASS_TIMEOUT]   = {DP_IP_DEFAULT_REASS_TIMEO, DP_IP_LOWLIMIT_REASS_TIMEO, DP_IP_HIGHLIMIT_REASS_TIMEO},

    [CFG_IP_NO_CKSUM_VERIFY]    = {0, 0, 1},
    [CFG_IP_NO_CKSUM_CALC]      = {0, 0, 1},
};

PreCfgVal_t g_cfgEthVal[CFG_INNER_ETH_MAX] = {
    [CFG_ETH_PADDING] = {0, 0, 1},
};

typedef struct {
    PreCfgVal_t* cfg;
    int  maxKey;
} CfgMap_t;

static CfgMap_t g_cfgMap[] = {
    [DP_CFG_TYPE_SYS] = {g_cfgVal, DP_CFG_MAX},
    [DP_CFG_TYPE_TCP] = {g_cfgTcpVal, DP_CFG_TCP_MAX},
    [DP_CFG_TYPE_IP]  = {g_cfgIpVal, DP_CFG_IP_MAX},
    [DP_CFG_TYPE_ETH] = {g_cfgEthVal, DP_CFG_ETH_MAX },
};

static bool CheckCfgKvSpecial(DP_CfgKv_t* kv)
{
    PreCfgVal_t* tempMap = g_cfgMap[kv->type].cfg;

    if (kv->type == DP_CFG_TYPE_TCP && kv->key == DP_CFG_TCP_WMEM_DEFAULT &&
        kv->val > tempMap[DP_CFG_TCP_WMEM_MAX].currentVal) {
        return false;
    }
    if (kv->type == DP_CFG_TYPE_TCP && kv->key == DP_CFG_TCP_RMEM_DEFAULT &&
        kv->val > tempMap[DP_CFG_TCP_RMEM_MAX].currentVal) {
        return false;
    }
    return true;
}

static bool CheckCfgKv(DP_CfgKv_t* kv)
{
    PreCfgVal_t* tempMap = g_cfgMap[kv->type].cfg;
    if (!CheckCfgKvSpecial(kv)) {
        return false;
    }

    if (kv->val > tempMap[kv->key].maxVal || kv->val < tempMap[kv->key].minVal) {
        return false;
    }
    return true;
}

int DP_Cfg_Inter(DP_CfgKv_t* kv, int cnt)
{
    if (kv == NULL || cnt <= 0) {
        DP_LOG_ERR("DP Config failed, invalid parameter.");
        return -1;
    }

    for (int i = 0; i < cnt; i++) {
        if ((uint32_t)(kv[i].type) >= DP_CFG_TYPE_MAX) {
            DP_LOG_ERR("DP Config failed, kv type invalid.");
            return -1;
        }

        if (kv[i].key < 0 || kv[i].key >= g_cfgMap[kv[i].type].maxKey) {
            DP_LOG_ERR("DP Config failed, kv key invalid.");
            return -1;
        }

        if (!CheckCfgKv(&kv[i])) {
            DP_LOG_ERR("DP Config failed, kv val invalid.");
            return -1;
        }

        g_cfgMap[kv[i].type].cfg[kv[i].key].currentVal = kv[i].val;
    }

    return 0;
}

int DP_Cfg(DP_CfgKv_t* kv, int cnt)
{
    if (UTILS_IsCfgInited() != 0) {
        DP_LOG_ERR("DP Config has inited already before.");
        return -1;
    }

    return DP_Cfg_Inter(kv, cnt);
}

int DP_CfgGet(DP_CfgKv_t* kv, int cnt)
{
    if (kv == NULL || cnt <= 0) {
        return -1;
    }

    for (int i = 0; i < cnt; i++) {
        if ((uint32_t)(kv[i].type) >= DP_CFG_TYPE_MAX) {
            return -1;
        }

        if (kv[i].key < 0 || kv[i].key >= g_cfgMap[kv[i].type].maxKey) {
            return -1;
        }

        kv[i].val = g_cfgMap[kv[i].type].cfg[kv[i].key].currentVal;
    }

    return 0;
}

void UTILS_SetCfgInit(int init)
{
    g_cfgInit = init;
}

int UTILS_IsCfgInited(void)
{
    if (g_cfgInit == CFG_INITED) {
        return 1;
    }

    return 0;
}
