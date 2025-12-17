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

#include <string.h>
#include <securec.h>
#include "dp_tcp_cc_api.h"

#include "utils_cfg.h"
#include "tcp_bbr.h"
#include "tcp_cc.h"

static inline uint32_t TcpNewRenoSsthresh(TcpSk_t* tcp)
{
    uint32_t ssthresh;
    uint32_t initCwnd;

    // sstrensh = MIN(inflight / 2, INIT_CWND * MSS)
    initCwnd = TcpGetInitCwnd(tcp);
    // 2: 计算当前已发送但未确认的数据量（inflight）的一半，作为慢启动阈值（ssthresh）
    ssthresh = (tcp->sndMax - tcp->sndUna) / 2; // inflight:在途数据

    return ssthresh > initCwnd ? ssthresh : initCwnd;
}

static void TcpNewRenoInit(void* tcpSk)
{
    TcpSk_t* tcp = (TcpSk_t*)tcpSk;
    tcp->cwnd     = TcpGetInitCwnd(tcp);
    tcp->ssthresh = INT_MAX;
    tcp->caState  = TCP_CA_OPEN;
}

static void TcpNewRenoAcked(void* tcpSk, uint32_t acked, uint32_t rtt)
{
    (void)rtt;
    TcpSk_t* tcp = (TcpSk_t*)tcpSk;
    uint32_t incWnd;

    if (TCP_IS_IN_RECOVERY(tcp)) { // 在recovery阶段
        // recovery -> open
        if (TcpSeqGeq(tcp->sndUna, tcp->seqRecover)) {
            tcp->cwnd    = tcp->ssthresh;
            tcp->caState = TCP_CA_OPEN;
        } else {
            // 按照mss增长，期望能够发送新的报文
            tcp->cwnd += tcp->mss;
        }
        return;
    }

    // 慢启动或者拥塞避免
    if (tcp->cwnd > tcp->ssthresh) { // 拥塞避免
        incWnd = tcp->mss * tcp->mss / tcp->cwnd;
        incWnd = (incWnd == 0) ? 1 : incWnd;
    } else { // 慢启动，一次窗口增长的大小不超过2个mss
        incWnd = UTILS_MIN(acked, ((uint32_t)tcp->mss) << 1);
    }

    // UTILS_MIN左参转换u64避免回绕，右参最大不超过30位，取最小值无风险
    tcp->cwnd = UTILS_MIN((uint64_t)(tcp->cwnd + incWnd), ((uint32_t)DP_TCP_MAXWIN << tcp->sndWs));
}

static void TcpNewRenoDupAck(void* tcpSk)
{
    TcpSk_t* tcp = (TcpSk_t*)tcpSk;
    if (tcp->dupAckCnt < tcp->reorderCnt) { // 乱序状态不增加cwnd
        return;
    }

    if (TCP_IS_IN_OPEN(tcp)) {
        tcp->seqRecover = tcp->sndMax;
        tcp->caState  = TCP_CA_RECOVERY;
        tcp->ssthresh = TcpNewRenoSsthresh(tcp);
        tcp->cwnd     = tcp->ssthresh + tcp->mss * tcp->dupAckCnt; // 3: NewReno算法
    }
}

static void TcpNewRenoLoss(void* tcpSk)
{
    TcpSk_t* tcp = (TcpSk_t*)tcpSk;
    tcp->caState    = TCP_CA_OPEN; // new reno在这里直接使用open状态，恢复到慢启动状态
    tcp->seqRecover = tcp->sndMax;
    tcp->ssthresh   = TcpNewRenoSsthresh(tcp);
    tcp->cwnd       = TcpGetInitCwnd(tcp);
}

static void TcpNewRenoRestart(void* tcpSk)
{
    TcpSk_t* tcp = (TcpSk_t*)tcpSk;
    tcp->cwnd    = TcpGetInitCwnd(tcp);
    tcp->caState = TCP_CA_OPEN;
}

static DP_TcpCaMeth_t g_newreno = {
    .algId     = TCP_CAMETH_NEWRENO,
    .algName   = "newreno",
    .caInit    = TcpNewRenoInit,
    .caDeinit  = NULL,
    .caAcked   = TcpNewRenoAcked,
    .caDupAck  = TcpNewRenoDupAck,
    .caTimeout = TcpNewRenoLoss,
    .caRestart = TcpNewRenoRestart,
};

typedef struct TcpCongCtlAlg {
    uint8_t valid;      // 未使用数组节点为0
    int8_t algId;
    uint8_t flag;       // 内部注册的设置为1，不允许外部去注册
    int8_t reserve;
    DP_TcpCaMeth_t *caMeth; /* 保存指针而不是复制内部的所有method，生命周期由注册者管理 */
} TcpCongCtlAlg_t;

static Spinlock_t g_caArrayLock = SPIN_INITIALIZER;
static int32_t g_caCnt = 0;
static TcpCongCtlAlg_t g_caArray[TCP_CA_MAX_NUM] = {0};

const DP_TcpCaMeth_t* TcpCaGet(int8_t algId)
{
    int8_t tempAlgId;
    int8_t defaultAlgId = (int8_t)CFG_GET_TCP_VAL(CFG_TCP_CA_ALG);
    int cnt = 0;
    if (algId < 0) {
        tempAlgId = defaultAlgId;
    } else {
        tempAlgId = algId;
    }

    SPINLOCK_DoLock(&g_caArrayLock);

    for (int index = 0; index < TCP_CA_MAX_NUM; index++) {
        TcpCongCtlAlg_t *node = &g_caArray[index];
        if (node->valid == 0) {
            continue;
        }
        cnt++;

        if (node->caMeth->algId == tempAlgId) {
            SPINLOCK_DoUnlock(&g_caArrayLock);
            return node->caMeth;
        }
        if (cnt >= g_caCnt) {
            break;
        }
    }
    SPINLOCK_DoUnlock(&g_caArrayLock);

    return &g_newreno; // 使用默认的newreno算法
}

const DP_TcpCaMeth_t* TcpCaGetByName(char* algName)
{
    int cnt = 0;
    SPINLOCK_DoLock(&g_caArrayLock);

    for (int index = 0; index < TCP_CA_MAX_NUM; index++) {
        TcpCongCtlAlg_t *node = &g_caArray[index];
        if (node->valid == 0) {
            continue;
        }
        cnt++;

        if (strcmp(node->caMeth->algName, algName) == 0) {
            SPINLOCK_DoUnlock(&g_caArrayLock);
            return node->caMeth;
        }
        if (cnt >= g_caCnt) {
            break;
        }
    }
    SPINLOCK_DoUnlock(&g_caArrayLock);
    return NULL; // 没有找到对应的算法
}

#define TCP_CA_MAX_NUM          (32) /* 最多存在32个拥塞算法 */
#define TCP_CA_ALGID_UNSPEC	0

int32_t TcpCaRegist(DP_TcpCaMeth_t* meth)
{
    int position = -1;

    if (g_caCnt >= TCP_CA_MAX_NUM) {
        return -ENOMEM;
    }
    /* 查询是否存在重复注册 */
    for (int index = TCP_CA_MAX_NUM - 1; index >= 0; index--) {
        TcpCongCtlAlg_t *node = &g_caArray[index];
        if (node->valid == 0) {
            position = index;
            continue;
        }

        if ((strcmp(node->caMeth->algName, meth->algName) == 0) ||
            (node->algId == meth->algId)) {
            return -EEXIST;
        }
    }

    /* 实际不会发生，数量未达到上限一定存在空位 */
    if (position < 0) {
        return -ENOMEM;
    }
    if (meth->algId < TCP_CA_REG_ALGID_START) {
        g_caArray[position].flag = 1;
    }

    g_caArray[position].caMeth = meth;
    g_caArray[position].algId = meth->algId;
    g_caArray[position].valid = 1;

    g_caCnt++;
    return 0;
}


int32_t TcpCaUnRegist(const char *algName)
{
    int cnt = 0;

    /* 查询是否存在重复注册 */
    for (int index = 0; index < TCP_CA_MAX_NUM; index++) {
        TcpCongCtlAlg_t *node = &g_caArray[index];
        if (node->valid == 0) {
            continue;
        }
        cnt++;

        if (strcmp(node->caMeth->algName, algName) == 0) {
            if (node->flag == 1) {
                break;
            }
            node->algId = -1;
            node->caMeth = NULL;
            node->valid = 0;
            g_caCnt--;
            return 0;
        }
        if (cnt >= g_caCnt) {
            break;
        }
    }

    return -ENOENT;
}

static void TcpNewRenoRegist(void)
{
    (void)TcpCaRegist(&g_newreno);
}

int32_t DP_TcpRegisterCcAlgo(DP_TcpCaMeth_t* meth)
{
    int32_t ret;
    size_t len = 0;

    if (meth == NULL) {
        return -EFAULT;
    }

    len = strnlen(meth->algName, DP_TCP_CA_NAME_MAX_LEN);
    if (len >= DP_TCP_CA_NAME_MAX_LEN || len == 0) {
        return -EINVAL;
    }

    if (meth->algId < TCP_CA_REG_ALGID_START) {
        return -EINVAL;
    }

    SPINLOCK_DoLock(&g_caArrayLock);
    // 预注册内部实现的拥塞算法，防止name冲突
    if (g_caCnt == 0) {
        TcpBBRRegist();
        TcpNewRenoRegist();
    }
    ret = TcpCaRegist(meth);
    SPINLOCK_Unlock(&g_caArrayLock);

    return ret;
}

int32_t DP_TcpUnregisterCcAlgo(const char *algName)
{
    int32_t ret;
    size_t len = 0;

    if (algName == NULL) {
        return -EFAULT;
    }

    len = strnlen(algName, DP_TCP_CA_NAME_MAX_LEN);
    if (len >= DP_TCP_CA_NAME_MAX_LEN || len == 0) {
        return -EINVAL;
    }

    SPINLOCK_DoLock(&g_caArrayLock);
    ret = TcpCaUnRegist(algName);
    SPINLOCK_Unlock(&g_caArrayLock);

    return ret;
}

int TcpGetCaMethCnt(void)
{
    return g_caCnt;
}

int TcpCaModuleInit(void)
{
    for (int index = 0; index < TCP_CA_MAX_NUM; index++) {
        if (g_caArray[index].valid == 0) {
            g_caArray[index].algId = -1;
        }
    }

    TcpBBRRegist();
    TcpNewRenoRegist();

    return 0;
}

void TcpCaModuleDeinit(void)
{
    for (int index = 0; index < TCP_CA_MAX_NUM; index++) {
        g_caArray[index].algId = -1;
        g_caArray[index].valid = 0;
        g_caArray[index].flag = 0;
        /* 如果存在动态注册的拥塞算法，由注册者提前做去注册，此处直接置为空 */
        g_caArray[index].caMeth = NULL;
    }

    g_caCnt = 0;
}