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
#include <string.h>

#include "utils_log.h"
#include "utils_atomic.h"
#include "utils_spinlock.h"
#include "utils_cfg.h"

#define CFG_CNT_MAX (DP_CFG_MAX + DP_CFG_TCP_MAX + DP_CFG_IP_MAX + DP_CFG_ETH_MAX)

static int g_cfgInit = CFG_INITIAL;

atomic32_t g_tcpCbCnt = 0;
atomic32_t g_udpCbCnt = 0;
atomic32_t g_epollCbCnt = 0;

static Spinlock_t g_caAlgLock = SPIN_INITIALIZER;

PreCfgVal_t g_cfgVal[CFG_INNER_MAX] = { // {默认值, 最小值, 最大值}
    [DP_CFG_MBUF_MAX]      = { DP_DEFAULT_MBUF_MAX, DP_LOWLIMIT_MBUF_MAX, DP_HIGHLIMIT_MBUF_MAX, DP_DISABLE },
    [DP_CFG_WORKER_MAX]    = { DP_DEFAULT_WORKER_MAX, DP_LOWLIMIT_WORKER_MAX, DP_HIGHLIMIT_WORKER_MAX, DP_DISABLE },
    [DP_CFG_RT_MAX]        = { DP_DEFAULT_RT_MAX, DP_LOWLIMIT_RT_MAX, DP_HIGHLIMIT_RT_MAX, DP_DISABLE },
    [DP_CFG_ARP_MAX]       = { DP_DEFAULT_ARP_MAX, DP_LOWLIMIT_ARP_MAX, DP_HIGHLIMIT_ARP_MAX, DP_DISABLE },
    [DP_CFG_TCPCB_MAX]     = { DP_DEFAULT_TCPCB_MAX, DP_LOWLIMIT_TCPCB_MAX, DP_HIGHLIMIT_TCPCB_MAX, DP_DISABLE },
    [DP_CFG_UDPCB_MAX]     = { DP_DEFAULT_UDPCB_MAX, DP_LOWLIMIT_UDPCB_MAX, DP_HIGHLIMIT_UDPCB_MAX, DP_DISABLE },
    [DP_CFG_EPOLLCB_MAX]   = { DP_DEFAULT_EPOLL_MAX, DP_LOWLIMIT_EPOLL_MAX, DP_HIGHLIMIT_EPOLL_MAX, DP_DISABLE },
    [DP_CFG_CPD_PKT_TRANS] = { DP_ENABLE, DP_DISABLE, DP_ENABLE, DP_DISABLE },
    [DP_CFG_ZERO_COPY]     = { DP_DISABLE, DP_DISABLE, DP_ENABLE, DP_DISABLE },
    [DP_CFG_ZBUF_LEN_MAX]  = { DP_DEFAULT_ZIOV_LEN_MAX, DP_LOWLIMIT_ZIOV_LEN_MAX, DP_HIGHLIMIT_ZIOV_LEN_MAX,
                               DP_DISABLE },
    [DP_CFG_DEPLOYMENT]    = { DP_DEPLOYMENT_DEFAULT, DP_DEPLOYMENT_DEFAULT, DP_DEPLOYMENT_CO_THREAD, DP_DISABLE },
    [DP_CFG_CPD_VCPU_NUM]    = { DP_DEFAULT_CPD_VCPU_NUM,
        DP_LOWLIMIT_CPD_VCPU_NUM, DP_HIGHLIMIT_CPD_VCPU_NUM, DP_DISABLE },
    [DP_CFG_CPD_RING_PER_CPU_NUM]    = { DP_DEFAULT_CPD_RING_PER_CPU_NUM,
        DP_LOWLIMIT_CPD_RING_PER_CPU_NUM, DP_HIGHLIMIT_CPD_RING_PER_CPU_NUM, DP_DISABLE },

    [CFG_RAWCB_IP_MAX]           = { 1, 1, 1, DP_DISABLE },
    [CFG_RAWCB_IPV6_MAX]         = { 1, 1, 1, DP_DISABLE },
    [CFG_RAWCB_ETH_MAX]          = { 1, 1, 1, DP_DISABLE },
    [CFG_FD_OFFSET]              = { 0, 0, 1024, DP_DISABLE },
    [CFG_NDTBL_MISS_CACHED_SIZE] = { TBM_NDTBL_MISS_CACHED_SIZE, TBM_NDTBL_MISS_CACHED_SIZE, TBM_NDTBL_MISS_CACHED_SIZE,
                                     DP_DISABLE },
    [CFG_FAKE_NDITEM_ALIVE_TIME] = { TBM_FAKE_NDITEM_ALIVE_TIME, TBM_FAKE_NDITEM_ALIVE_TIME, TBM_FAKE_NDITEM_ALIVE_TIME,
                                     DP_DISABLE },
    [CFG_FAKE_NDTBL_SIZE]        = { TBM_FAKE_NDTBL_SIZE, TBM_FAKE_NDTBL_SIZE, TBM_FAKE_NDTBL_SIZE, DP_DISABLE },
    [CFG_ARP_MISS_INTERVAL]      = { TBM_ARP_MISS_INTERVAL, TBM_ARP_MISS_INTERVAL, TBM_ARP_MISS_INTERVAL, DP_DISABLE },
    [CFG_NOLOCK]                 = { DP_DISABLE, DP_DISABLE, DP_ENABLE, DP_DISABLE },
    [CFG_AUTO_DRIVE_WORKER]      = { DP_DISABLE, DP_DISABLE, DP_ENABLE, DP_DISABLE }
};

PreCfgVal_t g_cfgTcpVal[CFG_INNER_TCP_MAX] = {
    [DP_CFG_TCP_SELECT_ACK] = { DP_ENABLE, DP_DISABLE, DP_ENABLE, DP_DISABLE },
    [DP_CFG_TCP_DELAY_ACK]    = { DP_ENABLE, DP_DISABLE, DP_ENABLE, DP_ENABLE },
    [DP_CFG_TCP_MSL_TIME]     = { DP_TCP_DEFAULT_MSL_TIME, DP_TCP_LOWLIMIT_MSL_TIME, DP_TCP_HIGHLIMIT_MSL_TIME,
                                  DP_DISABLE },
    [DP_CFG_TCP_FIN_TIMEOUT]  = { DP_TCP_DEFAULT_FIN_TIMEOUT, DP_TCP_LOWLIMIT_FIN_TIMEOUT, DP_TCP_HIGHLIMIT_FIN_TIMEOUT,
                                  DP_ENABLE },
    [DP_CFG_TCP_COOKIE]       = { DP_DISABLE, DP_DISABLE, DP_ENABLE, DP_ENABLE },
    [DP_CFG_TCP_RND_PORT_MIN] = { DP_TCP_DEFAULT_PORT_MIN, DP_TCP_LOWLIMIT_PORT_MIN, DP_TCP_HIGHLIMIT_PORT_MIN,
                                  DP_ENABLE },
    [DP_CFG_TCP_RND_PORT_MAX] = { DP_TCP_DEFAULT_PORT_MAX, DP_TCP_LOWLIMIT_PORT_MAX, DP_TCP_HIGHLIMIT_PORT_MAX,
                                  DP_ENABLE },
    [DP_CFG_TCP_WMEM_MAX]     = { DP_TCP_DEFAULT_WMEM_MAX, DP_TCP_LOWLIMIT_WMEM_MAX, DP_TCP_HIGHLIMIT_WMEM_MAX,
                                  DP_DISABLE },
    [DP_CFG_TCP_WMEM_DEFAULT] = { DP_TCP_DEFAULT_WMEM, DP_TCP_LOWLIMIT_WMEM_MAX, DP_TCP_HIGHLIMIT_WMEM_MAX,
                                  DP_DISABLE },
    [DP_CFG_TCP_RMEM_MAX]     = { DP_TCP_DEFAULT_RMEM_MAX, DP_TCP_LOWLIMIT_RMEM_MAX, DP_TCP_HIGHLIMIT_RMEM_MAX,
                                  DP_DISABLE },
    [DP_CFG_TCP_RMEM_DEFAULT] = { DP_TCP_DEFAULT_RMEM, DP_TCP_LOWLIMIT_RMEM_MAX, DP_TCP_HIGHLIMIT_RMEM_MAX,
                                  DP_DISABLE },
    [DP_CFG_TCP_INIT_CWND]    = { DP_TCP_DEFAULT_INIT_CWND, DP_TCP_LOWLIMIT_INIT_CWND, DP_TCP_HIGHLIMIT_INIT_CWND,
                                  DP_ENABLE },
    [DP_CFG_TCP_SYNACK_RETRIES] = { DP_TCP_DEFAULT_SYNACK_RETRIES, DP_TCP_LOWLIMIT_SYNACK_RETRIES,
                                    DP_TCP_HIGHLIMIT_SYNACK_RETRIES, DP_ENABLE },
    [DP_CFG_TCP_RND_PORT_STEP]  = { DP_TCP_DEFAULT_RND_PORT_STEP, DP_TCP_LOWLIMIT_RND_PORT_STEP,
                                    DP_TCP_HIGHLIMIT_RND_PORT_STEP, DP_DISABLE },
    [DP_CFG_TCP_USR_TIMEOUT] = { DP_TCP_DEFAULT_USR_TIMEOUT, DP_TCP_LOWLIMIT_USR_TIMEOUT, DP_TCP_HIGHLIMIT_USR_TIMEOUT,
                                 DP_DISABLE },
    [DP_CFG_TCP_FRTO]        = { DP_DISABLE, DP_DISABLE, DP_ENABLE, DP_ENABLE },
    [DP_CFG_TCP_ADJUST_DELAY_ACK]  = { DP_DISABLE, DP_DISABLE, DP_ENABLE, DP_DISABLE },
    [DP_CFG_TCP_DELAY_ACK_INTER]   = { DP_TCP_DEFAULT_DELAYACK_INTER, DP_TCP_LOWLIMIT_DELAYACK_INTER,
                                       DP_TCP_HIGHLIMIT_DELAYACK_INTER, DP_ENABLE },
    [DP_CFG_TCP_SNDBUF_PBUFCNT_MAX] = { DP_TCP_DEFAULT_SNDBUF_PBUFCNT, DP_TCP_LOWLIMIT_SNDBUF_PBUFCNT,
                                        DP_TCP_HIGHLIMIT_SNDBUF_PBUFCNT, DP_DISABLE },
    [DP_CFG_TCP_SMALL_PACKET_ZCOPY] = { DP_ENABLE, DP_DISABLE, DP_ENABLE, DP_DISABLE },
    [DP_CFG_TCP_MSS_USE_DEFAULT]   = { DP_ENABLE, DP_DISABLE, DP_ENABLE, DP_ENABLE },
    [DP_CFG_TCP_KEEPALIVE_INTVL] = {DP_TCP_DEFAULT_KEEPALIVE_INTVL, DP_TCP_LOWLIMIT_KEEPALIVE_INTVL,
                                    DP_TCP_HIGHLIMIT_KEEPALIVE_INTVL, DP_ENABLE},
    [DP_CFG_TCP_KEEPALIVE_PROBES] = {DP_TCP_DEFAULT_KEEPALIVE_PROBES, DP_TCP_LOWLIMIT_KEEPALIVE_PROBES,
                                     DP_TCP_HIGHLIMIT_KEEPALIVE_PROBES, DP_ENABLE},
    [DP_CFG_TCP_KEEPALIVE_TIME] = {DP_TCP_DEFAULT_KEEPALIVE_TIME, DP_TCP_LOWLIMIT_KEEPALIVE_TIME,
                                   DP_TCP_HIGHLIMIT_KEEPALIVE_TIME, DP_ENABLE},
    [DP_CFG_TCP_SYN_RETRIES] = {DP_TCP_DEFAULT_SYN_RETRIES, DP_TCP_LOWLIMIT_SYN_RETRIES,
                                DP_TCP_HIGHLIMIT_SYN_RETRIES, DP_ENABLE},
    [DP_CFG_TCP_TIMESTAMP] = {DP_ENABLE, DP_DISABLE, DP_ENABLE, DP_ENABLE},
    [DP_CFG_TCP_WINDOW_SCALING] = {DP_ENABLE, DP_DISABLE, DP_ENABLE, DP_ENABLE},
    [DP_CFG_TCP_RFC1337] = {DP_DISABLE, DP_DISABLE, DP_ENABLE, DP_ENABLE},

    [CFG_TCP_NO_CKSUM_VERIFY] = { 0, 0, 0, DP_DISABLE },
    [CFG_TCP_NO_CKSUM_CALC]   = { 0, 0, 0, DP_DISABLE },
    [CFG_TCP_NO_TIMESTAMP]    = { 0, 0, 0, DP_DISABLE },
    [CFG_TCP_CA_ALG]          = { DP_TCP_DEFAULT_CA_ALG, DP_TCP_LOWLIMIT_CA_ALG, DP_TCP_HIGHTLIMIT_CA_ALG, DP_ENABLE },
    [CFG_TCP_MSS_MAX]         = { TCP_DEFAULT_MSS_MAX, TCP_DEFAULT_MSS_MAX, TCP_DEFAULT_MSS_MAX, DP_DISABLE },
    [CFG_TCP_TSQ_PASSIVE]     = { DP_DISABLE, DP_DISABLE, DP_ENABLE, DP_DISABLE },
    [CFG_TCP_WIN_SCALE]       = { DP_TCP_DEFAULT_WIN_SCALE, DP_TCP_LOWLIMIT_WIN_SCALE, DP_TCP_HIGHLIMIT_WIN_SCALE,
                                  DP_DISABLE },
    [CFG_TCP_CLOSE_NO_RST]    = { DP_DISABLE, DP_DISABLE, DP_ENABLE, DP_DISABLE },
    [CFG_TCP_MAX_REXMIT_CNT_NO_EST] = { TCP_DEFAULT_MAX_REXMIT_CNT_NO_EST, TCP_LOWLIMIT_MAX_REXMIT_CNT_NO_EST,
        TCP_HIGHLIMIT_MAX_REXMIT_CNT_NO_EST, DP_DISABLE },
};

PreCfgVal_t g_cfgIpVal[CFG_INNER_IP_MAX] = {
    [DP_CFG_IP_REASS_MAX]     = { DP_IP_DEFAULT_REASS_MAX, DP_IP_LOWLIMIT_REASS_MAX, DP_IP_HIGHLIMIT_REASS_MAX,
                                  DP_DISABLE },
    [DP_CFG_IP_REASS_TIMEOUT] = { DP_IP_DEFAULT_REASS_TIMEO, DP_IP_LOWLIMIT_REASS_TIMEO, DP_IP_HIGHLIMIT_REASS_TIMEO,
                                  DP_ENABLE },
    [DP_CFG_IP6_FLABEL_REFLECT] = { DP_DISABLE, DP_DISABLE, DP_ENABLE, DP_ENABLE },
    [DP_CFG_IP_FRAG_DIST_MAX] = { DP_IP_DEFAULT_FRAG_DIST_MAX, DP_IP_LOWLIMIT_FRAG_DIST_MAX,
                                  DP_IP_HIGHLIMIT_FRAG_DIST_MAX, DP_ENABLE },

    [CFG_IP_NO_CKSUM_VERIFY] = { 0, 0, 1, DP_DISABLE },
    [CFG_IP_NO_CKSUM_CALC]   = { 0, 0, 1, DP_DISABLE },
};

PreCfgVal_t g_cfgEthVal[CFG_INNER_ETH_MAX] = {
    [CFG_ETH_PADDING] = { 0, 0, 1, DP_DISABLE },
};

typedef struct {
    PreCfgVal_t* cfg;
    int          maxKey;
} CfgMap_t;

static CfgMap_t g_cfgMap[] = {
    [DP_CFG_TYPE_SYS] = { g_cfgVal, DP_CFG_MAX },
    [DP_CFG_TYPE_TCP] = { g_cfgTcpVal, DP_CFG_TCP_MAX },
    [DP_CFG_TYPE_IP]  = { g_cfgIpVal, DP_CFG_IP_MAX },
    [DP_CFG_TYPE_ETH] = { g_cfgEthVal, DP_CFG_ETH_MAX },
};

// 当前 DP 内置的拥塞算法使用情况
static CfgCaInfo_t g_TcpCaInfo[CFG_TCP_CAMETH_MAX] = {
    [CFG_TCP_CAMETH_NEWRENO] = {.algName = "newreno", .algId = CFG_TCP_CAMETH_NEWRENO, .valid = 1, },
    [CFG_TCP_CAMETH_BBR]     = {.algName = "bbr", .algId = CFG_TCP_CAMETH_BBR, .valid = 1, },
};


typedef bool (*CfgKvCheckFunc)(DP_CfgKv_t* kv);

typedef struct {
    DP_CfgType_t type;
    int key;
    CfgKvCheckFunc cfgCheckFunc;
} CfgCheck_t;

static bool CfgTcpWmemMaxCheck(DP_CfgKv_t* kv)
{
    PreCfgVal_t* tempMap = g_cfgMap[kv->type].cfg;
    if (kv->val > tempMap[DP_CFG_TCP_WMEM_MAX].currentVal) {
        DP_LOG_ERR("DP Config Value special check failed, DP_CFG_TCP_WMEM_DEFAULT val = %d, WMEM_MAX val = %d.",
            kv->val, tempMap[DP_CFG_TCP_WMEM_MAX].currentVal);
        return false;
    }
    return true;
}

static bool CfgTcpRmemMaxCheck(DP_CfgKv_t* kv)
{
    PreCfgVal_t* tempMap = g_cfgMap[kv->type].cfg;
    if (kv->val > tempMap[DP_CFG_TCP_RMEM_MAX].currentVal) {
        DP_LOG_ERR("DP Config Value special check failed, DP_CFG_TCP_RMEM_DEFAULT val = %d, RMEM_MAX val = %d.",
            kv->val, tempMap[DP_CFG_TCP_RMEM_MAX].currentVal);
        return false;
    }
    return true;
}

static bool CfgTcpRndPortStepCheck(DP_CfgKv_t* kv)
{
    uint32_t step = (uint32_t)kv->val;
    // 随机端口步长必须是2的幂次
    if ((step & (step - 1)) != 0) {
        DP_LOG_ERR("DP Config Value special check failed, "
            "DP_CFG_TCP_RND_PORT_STEP val need to be a power of 2.");
        return false;
    }
    return true;
}

static bool CfgTcpDelayAckInterCheck(DP_CfgKv_t* kv)
{
    if (kv->val % 10 != 0) { // 必须是10的倍数
        DP_LOG_ERR("DP Config Value special check failed, "
            "DP_CFG_TCP_DELAY_ACK_INTER val need to be a multiple of 10.");
        return false;
    }
    return true;
}

static bool CfgTcpRndPortCheck(DP_CfgKv_t* kv)
{
    (void)kv;
    if ((CFG_GET_VAL(DP_CFG_DEPLOYMENT) == DP_DEPLOYMENT_CO_THREAD) &&
        (CFG_GET_TCP_VAL(DP_CFG_TCP_RND_PORT_STEP) != 1) && (UTILS_IsCfgInited() != 0)) {
        DP_LOG_ERR("DP Config Value special check failed, "
            "DP_CFG_TCP_RND_PORT_MIN or DP_CFG_TCP_RND_PORT_MAX can't be set in co_thread mode.");
        return false;
    }

    return true;
}

static bool CheckCfgKvSpecial(DP_CfgKv_t* kv)
{
    CfgCheck_t cfgCheck[] = {
        {.type = DP_CFG_TYPE_TCP, .key = DP_CFG_TCP_WMEM_DEFAULT, .cfgCheckFunc = CfgTcpWmemMaxCheck},
        {.type = DP_CFG_TYPE_TCP, .key = DP_CFG_TCP_RMEM_DEFAULT, .cfgCheckFunc = CfgTcpRmemMaxCheck},
        {.type = DP_CFG_TYPE_TCP, .key = DP_CFG_TCP_RND_PORT_STEP, .cfgCheckFunc = CfgTcpRndPortStepCheck},
        {.type = DP_CFG_TYPE_TCP, .key = DP_CFG_TCP_DELAY_ACK_INTER, .cfgCheckFunc = CfgTcpDelayAckInterCheck},
        {.type = DP_CFG_TYPE_TCP, .key = DP_CFG_TCP_RND_PORT_MIN, .cfgCheckFunc = CfgTcpRndPortCheck},
        {.type = DP_CFG_TYPE_TCP, .key = DP_CFG_TCP_RND_PORT_MAX, .cfgCheckFunc = CfgTcpRndPortCheck},
    };

    for (uint32_t i = 0; i < sizeof(cfgCheck) / sizeof(cfgCheck[0]); i++) {
        if (cfgCheck[i].type == kv->type && cfgCheck[i].key == kv->key) {
            if (!cfgCheck[i].cfgCheckFunc(kv)) {
                return false;
            }
        }
    }

    return true;
}

static bool CfgKeyIsValid(DP_CfgKv_t* kv)
{
    if ((uint32_t)(kv->type) >= DP_CFG_TYPE_MAX) {
        DP_LOG_ERR("DP Config failed, kv type invalid, type = %d, key = %d.", kv->type, kv->key);
        return false;
    }

    if (kv->key < 0 || kv->key >= g_cfgMap[kv->type].maxKey) {
        DP_LOG_ERR("DP Config failed, kv key invalid, type = %d, key = %d.", kv->type, kv->key);
        return false;
    }
    return true;
}

static bool CfgValueIsValid(DP_CfgKv_t* kv)
{
    PreCfgVal_t* preCfgVal = &g_cfgMap[kv->type].cfg[kv->key];

    if (preCfgVal->dynamic != DP_ENABLE && UTILS_IsCfgInited() != 0) {
        DP_LOG_ERR("DP Config failed, kv key don't support dynamic change.");
        return false;
    }

    if (!CheckCfgKvSpecial(kv)) {
        return false;
    }

    if (kv->val > preCfgVal->maxVal || kv->val < preCfgVal->minVal) {
        DP_LOG_ERR("DP Config Value check failed, type = %d, key = %d, val = %d, maxval = %d, minval = %d.",
            kv->type, kv->key, kv->val, preCfgVal->maxVal, preCfgVal->minVal);
        return false;
    }

    return true;
}

int DP_Cfg_Inter(DP_CfgKv_t* kv, int cnt)
{
    for (int i = 0; i < cnt; i++) {
        g_cfgMap[kv[i].type].cfg[kv[i].key].currentVal = kv[i].val;
    }

    return 0;
}

int DP_Cfg(DP_CfgKv_t* kv, int cnt)
{
    if ((kv == NULL) || (cnt <= 0) || (cnt > CFG_CNT_MAX)) {
        DP_LOG_ERR("DP Config failed, invalid parameter, cnt = %d.", cnt);
        return -1;
    }

    int randPortMin = CFG_GET_TCP_VAL(DP_CFG_TCP_RND_PORT_MIN);
    int randPortMax = CFG_GET_TCP_VAL(DP_CFG_TCP_RND_PORT_MAX);

    for (int i = 0; i < cnt; i++) {
        if (!CfgKeyIsValid(&kv[i])) {
            DP_LOG_ERR("DP Config check key failed.");
            return -1;
        }

        if (!CfgValueIsValid(&kv[i])) {
            return -1;
        }

        if (kv[i].type == DP_CFG_TYPE_TCP && kv[i].key == DP_CFG_TCP_RND_PORT_MIN) {
            randPortMin = kv[i].val;
        }

        if (kv[i].type == DP_CFG_TYPE_TCP && kv[i].key == DP_CFG_TCP_RND_PORT_MAX) {
            randPortMax = kv[i].val;
        }
    }

    if (randPortMin > randPortMax) {
        return -1;
    }

    return DP_Cfg_Inter(kv, cnt);
}

int DP_CfgGet(DP_CfgKv_t* kv, int cnt)
{
    if ((kv == NULL) || (cnt <= 0) || (cnt > CFG_CNT_MAX)) {
        DP_LOG_ERR("DP Config get failed, invalid parameter, cnt = %d.", cnt);
        return -1;
    }

    for (int i = 0; i < cnt; i++) {
        if (!CfgKeyIsValid(&kv[i])) {
            DP_LOG_ERR("DP Config get check key failed.");
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

int DP_GetAvalibleCaAlg(const char **caAlg, int size, int *used)
{
    if (caAlg == NULL || used == NULL || size <= 0) {
        DP_LOG_ERR("DP avalible ca alg get failed, invalid parameter");
        return -1;
    }
    int cnt = 0;
    for (int i = 0; i < CFG_TCP_CAMETH_MAX; i++) {
        if (i >= size) {
            break;
        }
        caAlg[cnt++] = g_TcpCaInfo[i].algName;
    }
    *used = cnt;
    return 0;
}

int DP_GetAllowedCaAlg(const char **caAlg, int size, int *used)
{
    if (caAlg == NULL || used == NULL || size <= 0) {
        DP_LOG_ERR("DP allowed ca alg get failed, invalid parameter");
        return -1;
    }

    SPINLOCK_DoLock(&g_caAlgLock);
    int cnt = 0;
    for (int i = 0; i < CFG_TCP_CAMETH_MAX; i++) {
        if (i >= size) {
            break;
        }
        if (g_TcpCaInfo[i].valid != 0) {
            caAlg[cnt++] = g_TcpCaInfo[i].algName;
        }
    }
    SPINLOCK_DoUnlock(&g_caAlgLock);

    *used = cnt;
    return 0;
}

int DP_SetAllowedCaAlg(const char **caAlg, int size)
{
    if (caAlg == NULL || size <= 0 || size > CFG_TCP_CAMETH_MAX) {
        DP_LOG_ERR("DP allowed ca alg set failed, invalid parameter");
        return -1;
    }

    // 与 linux 行为保持一致
    // 1. 检查是否存在非法值；2. 将已有的拥塞算法置为不可用；3. 将新设置的拥塞算法置为可用
    int validCaAlg[CFG_TCP_CAMETH_MAX] = {0};
    int cnt = 0;
    for (int i = 0; i < size; i++) {
        int vaild = 0;
        if (caAlg[i] == NULL) {
            DP_LOG_ERR("DP allowed ca alg set failed, caAlg[%d] is NULL", i);
            return -1;
        }
        for (int j = 0; j < CFG_TCP_CAMETH_MAX; j++) {
            if (strcmp(g_TcpCaInfo[j].algName, caAlg[i]) == 0) {
                vaild = 1;
                validCaAlg[cnt] = j;
                cnt++;
            }
        }
        if (vaild == 0) {
            DP_LOG_ERR("DP allowed ca alg set failed, invalid input ca alg %s", caAlg[i]);
            return -1;
        }
    }

    SPINLOCK_DoLock(&g_caAlgLock);
    for (int i = 0; i < CFG_TCP_CAMETH_MAX; i++) {
        g_TcpCaInfo[i].valid = 0;
    }

    for (int i = 0; i < cnt; i++) {
        g_TcpCaInfo[validCaAlg[i]].valid = 1;
    }
    SPINLOCK_DoUnlock(&g_caAlgLock);

    return 0;
}

int DP_SetCaAlg(const char *caAlg)
{
    if (caAlg == NULL) {
        DP_LOG_ERR("DP ca alg set failed, invalid parameter");
        return -1;
    }
    SPINLOCK_DoLock(&g_caAlgLock);
    for (int i = 0; i < CFG_TCP_CAMETH_MAX; i++) {
        if (strcmp(g_TcpCaInfo[i].algName, caAlg) == 0 && g_TcpCaInfo[i].valid == 1) {
            CFG_SET_TCP_VAL(CFG_TCP_CA_ALG, g_TcpCaInfo[i].algId);
            SPINLOCK_DoUnlock(&g_caAlgLock);
            return 0;
        }
    }
    SPINLOCK_DoUnlock(&g_caAlgLock);
    DP_LOG_ERR("DP ca alg set failed, invalid input ca alg");
    return -1;
}

void UTILS_SetDefaultAllowedCaAlg(void)
{
    SPINLOCK_DoLock(&g_caAlgLock);
    for (int i = 0; i < CFG_TCP_CAMETH_MAX; i++) {
        g_TcpCaInfo[i].valid = 1;
    }
    SPINLOCK_DoUnlock(&g_caAlgLock);
}

int UTILS_CheckCaAlgValid(const char *caAlg)
{
    if (caAlg == NULL) {
        return -1;
    }

    SPINLOCK_DoLock(&g_caAlgLock);
    for (int i = 0; i < CFG_TCP_CAMETH_MAX; i++) {
        if (strcmp(g_TcpCaInfo[i].algName, caAlg) == 0 && g_TcpCaInfo[i].valid == 0) {
            SPINLOCK_DoUnlock(&g_caAlgLock);
            return -1;
        }
    }
    SPINLOCK_DoUnlock(&g_caAlgLock);

    return 0;
}