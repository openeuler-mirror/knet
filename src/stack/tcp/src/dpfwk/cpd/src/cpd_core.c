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

#include "dp_ethernet.h"
#include "dp_cpd_api.h"
#include "utils_cfg.h"
#include "utils_log.h"
#include "utils_statistic.h"
#include "cpd_adpt.h"
#include "cpd_linux.h"
#include "netdev.h"
#include "tbm.h"
#include "shm.h"
#include "pmgr.h"
#include "dp_ip.h"
#include "pbuf.h"
#include "dp_ethernet.h"
#include "dp_ip.h"
#include "dp_tcp.h"
#include "cpd_core.h"

#define BURST_SIZE 32
/* RUNONCE_PER_PERIOD为2的幂次-1 */
#define RUNONCE_PER_PERIOD 16383

CpdNdOpNodeHead g_cpdNdOpList;
CpdTapInfo g_tapInfoList[DEV_TBL_SIZE][DEV_MAX_QUEUE_SIZE];   // tap口与netdev一一对应
static int g_CpdRingStartIndex[DEV_MAX_QUEUE_SIZE] = {0};
int g_devMaxIndex = 0;

static int g_cpdInit = CPD_INITIAL;
static TBM_Notify_t g_tbmNotify;
static DP_CpdqueOps_t g_cpdQueOps;

static Pbuf_t* CpdInput(Pbuf_t* pbuf)
{
    int ifindex = 0;
    DP_INC_PKT_STAT(pbuf->wid, DP_PKT_UP_TO_CTRL_PLANE);
    PBUF_PUT_HEAD(pbuf, sizeof(DP_EthHdr_t));
    ifindex = ((Netdev_t*)PBUF_GET_DEV(pbuf))->ifindex;
    CpdPktTranfer((uint32_t)ifindex, pbuf, PBUF_GET_PKT_LEN(pbuf), 0);
    PBUF_Free(pbuf);
    return NULL;
}

static Pbuf_t* CpdInputById(Pbuf_t* pbuf, int cpdQueueId)
{
    int ifindex = 0;
    DP_INC_PKT_STAT(pbuf->wid, DP_PKT_UP_TO_CTRL_PLANE);
    PBUF_PUT_HEAD(pbuf, sizeof(DP_EthHdr_t));
    ifindex = ((Netdev_t*)PBUF_GET_DEV(pbuf))->ifindex;
    CpdPktTranfer((uint32_t)ifindex, pbuf, PBUF_GET_PKT_LEN(pbuf), cpdQueueId);
    PBUF_Free(pbuf);
    return NULL;
}

uint32_t CpdCalcTcpHash(DP_EthHdr_t *ethAddr)
{
    uint32_t hash = 0;
    if (ethAddr->type == UTILS_HTONS(DP_ETH_P_IP)) {
        DP_IpHdr_t *ipHdr = (DP_IpHdr_t *)((uint8_t *)ethAddr + sizeof(DP_EthHdr_t));
        uint8_t ipHdrLen = DP_GET_IP_HDR_LEN(ipHdr);
        if (ipHdr->type == DP_IPHDR_TYPE_TCP && (((ipHdr)->off & (~UTILS_HTONS(DP_IP_FRAG_DF))) == 0)) {
            DP_TcpHdr_t *tcpHdr = (DP_TcpHdr_t *)((uint8_t *)ethAddr + sizeof(DP_EthHdr_t) + ipHdrLen);
            hash = ipHdr->src ^ ipHdr->dst ^ tcpHdr->dport ^ tcpHdr->sport;
        }
    }
    return hash;
}

static Pbuf_t* DelayInput(Pbuf_t* pbuf)
{
    // 数据线程处理过二层头，所以需要减去sizeof(DP_EthHdr_t)
    DP_EthHdr_t *ethAddr = (DP_EthHdr_t *)(DP_PBUF_GET_PAYLOAD(pbuf) + DP_PBUF_GET_OFFSET(pbuf) - sizeof(DP_EthHdr_t));
    uint32_t hash = CpdCalcTcpHash(ethAddr);
    int cpdQueueNum = CFG_GET_VAL(DP_CFG_CPD_RING_PER_CPU_NUM) * CFG_GET_VAL(DP_CFG_CPD_VCPU_NUM);
    if (g_cpdQueOps.cpdEnque(pbuf, hash % cpdQueueNum) <= 0) {
        DP_ADD_ABN_STAT(DP_CPD_DELAY_ENQUE_ERR);
        PBUF_Free(pbuf);
    }
    return NULL;
}

/* 创建tap口 */
static int CPD_Adapt_Init(void)
{
    DP_Netdev_t* dev;
    int fd = -1;
    for (int index = 0; index < DEV_TBL_SIZE; ++index) {
        dev = DP_GetNetdevByIndex(index);
        // 遍历
        if (dev == NULL) {
            continue;
        }
        g_devMaxIndex = index;
        int cpdQueueSize = CFG_GET_VAL(DP_CFG_CPD_VCPU_NUM) * CFG_GET_VAL(DP_CFG_CPD_RING_PER_CPU_NUM);
        if (cpdQueueSize > DEV_MAX_QUEUE_SIZE) {
            DP_LOG_ERR("Cpd alloc tap failed, cpd queue size is %d, larger than max queue size", cpdQueueSize);
            continue;
        }
        for (int queueIndex = 0; queueIndex < cpdQueueSize; queueIndex++) {
            fd = CPD_TapAlloc(dev);
            if (fd <= 0) {
                DP_LOG_ERR("Cpd alloc tap failed.");
                goto err;
            }
            g_tapInfoList[index][queueIndex].ifindex = dev->ifindex;
            g_tapInfoList[index][queueIndex].fd = fd;
        }
    }
    return 0;
err:
    for (uint32_t index = 0; index < DEV_TBL_SIZE; ++index) {
        for (uint32_t queueIndex = 0; queueIndex < DEV_MAX_QUEUE_SIZE; queueIndex++) {
            if (g_tapInfoList[index][queueIndex].fd == -1) {
                continue;
            }
            CPD_TapFree(g_tapInfoList[index][queueIndex].fd);
            g_tapInfoList[index][queueIndex].fd = -1;
        }
    }
    g_devMaxIndex = -1;

    return -1;
}

int DP_CpdQueHooksReg(void* queOps)
{
    if (queOps == NULL || ((DP_CpdqueOps_t*)queOps)->cpdEnque == NULL ||
    ((DP_CpdqueOps_t*)queOps)->cpdDeque == NULL) {
        DP_LOG_ERR("CpdQueHooksReg failed by invalid param.");
        return -1;
    }

    if (PMGR_Get(PMGR_ENTRY_DELAY_KERNEL_IN) != NULL) {
        DP_LOG_ERR("CpdQueHooksReg failed by repeat reg.");
        return -1;
    }
    g_cpdQueOps.cpdEnque = ((DP_CpdqueOps_t*)queOps)->cpdEnque;
    g_cpdQueOps.cpdDeque = ((DP_CpdqueOps_t*)queOps)->cpdDeque;
    PMGR_AddEntry(PMGR_ENTRY_DELAY_KERNEL_IN, DelayInput);
    return 0;
}

int DP_CpdInit(void)
{
    int ret = 0;

    if (g_cpdInit != CPD_INITIAL) {
        DP_LOG_ERR("Cpd cannot be initialized repeatedly.");
        return -1;
    }
    // 系统钩子初始化
    ret = SysCallInit();
    if (ret != 0) {
        DP_LOG_ERR("Cpd init failed by sysCallInit failed.");
        return -1;
    }

    // 创建netlink socket
    LIST_INIT_HEAD(&g_cpdNdOpList);
    ret = CpdCpInit();
    if (ret != 0) {
        DP_LOG_ERR("CpdCpInit failed.");
        return -1;
    }

    g_tbmNotify.pid = 0;
    g_tbmNotify.cb = CpdTblMissHandle;
    g_tbmNotify.groups = TBM_NOTIFY_TYPE_ND;
    SPINLOCK_Init(&g_tbmNotify.lock);

    (void)TBM_AddNotify(NULL, &g_tbmNotify);
    for (int i = 0; i < DEV_TBL_SIZE; ++i) {
        for (int j = 0; j < DEV_MAX_QUEUE_SIZE; j++) {
            g_tapInfoList[i][j].fd = -1;
            g_tapInfoList[i][j].ifindex = -1;
        }
    }
    if (CFG_GET_VAL(DP_CFG_CPD_PKT_TRANS) != 0) {
        ret = CPD_Adapt_Init();
        if (ret != 0) {
            DP_LOG_ERR("Cpd adapt Init failed.");
            return -1;
        }
        PMGR_AddEntry(PMGR_ENTRY_ARP_IN, CpdInput);
        PMGR_AddEntry(PMGR_ENTRY_ICMP_IN, CpdInput);
    }
    g_cpdInit = CPD_INITED;
    return 0;
}

void CpdDelayInput(int cpdQueueId)
{
    /* 未注册转发内核队列操作，无需延后处理内核数据到内核 */
    if (PMGR_Get(PMGR_ENTRY_DELAY_KERNEL_IN) == NULL) {
        return;
    }

    if (cpdQueueId >= DEV_MAX_QUEUE_SIZE || cpdQueueId < 0) {
        return;
    }

    /* 已注册内核队列操作，延后处理内核数据到内核 */
    int burstCnt = 0;
    static Pbuf_t* pbufs[DEV_MAX_QUEUE_SIZE][BURST_SIZE] = {0};
    burstCnt = g_cpdQueOps.cpdDeque((void**)pbufs[cpdQueueId], BURST_SIZE, cpdQueueId);
    if (burstCnt < 0) {
        DP_ADD_ABN_STAT(DP_CPD_DELAY_DEQUE_ERR);
        return;
    }

    for (int i = 0; i < burstCnt; i++) {
        (void)CpdInputById(pbufs[cpdQueueId][i], cpdQueueId);
    }
}

int DP_CpdRunOnce(int cpdId)
{
    if (g_cpdInit != CPD_INITED) {
        DP_LOG_ERR("Cpd run failed by cpd is not initialized.");
        return -1;
    }

    if (cpdId >= CFG_GET_VAL(DP_CFG_CPD_VCPU_NUM)) {
        DP_LOG_ERR("Cpd run failed cpdId %d is InValid.", cpdId);
        return -1;
    }

    g_CpdRingStartIndex[cpdId] = cpdId * CFG_GET_VAL(DP_CFG_CPD_RING_PER_CPU_NUM);
    int cpdQueuePerCpu = CFG_GET_VAL(DP_CFG_CPD_RING_PER_CPU_NUM);
    for (int cpdQueueId = g_CpdRingStartIndex[cpdId];
        cpdQueueId < g_CpdRingStartIndex[cpdId] + cpdQueuePerCpu; cpdQueueId++) {
        CpdDelayInput(cpdQueueId);

        if (cpdId == 0) {
            // 同步表项, 由于提高了cpd调度频率，降低表项同步频率, CpdRunOnce当前是单线程调度
            static unsigned int acc = 0;
            if (PMGR_Get(PMGR_ENTRY_DELAY_KERNEL_IN) == NULL) {
                CpdTblSync();
            } else if ((acc & RUNONCE_PER_PERIOD) == 0) {   // 未注册转发钩子，每RUNONCE_PER_PERIOD次同步一下表项
                CpdTblSync();
            }
            acc++;
        }

        // 控制报文响应
        CpdPktHandle(cpdQueueId);
    }
    return 0;
}

void DP_CPD_Deinit(void)
{
    if (g_cpdInit != CPD_INITED) {
        DP_LOG_ERR("Cpd is not initialized, cannot deinit.");
        return;
    }
    CloseNetlinkFd();

    TBM_DelNotify(NULL, &g_tbmNotify);

    CpdNdOpList_t *arpOpItem  = LIST_FIRST(&g_cpdNdOpList);
    CpdNdOpList_t *nextArpOpItem = NULL;
    while (arpOpItem != NULL) {
        nextArpOpItem = LIST_NEXT(arpOpItem, node);
        SHM_FREE(arpOpItem, DP_MEM_FREE);
        arpOpItem = nextArpOpItem;
    }

    for (uint32_t index = 0; index < DEV_TBL_SIZE; ++index) {
        for (uint32_t queueIndex = 0; queueIndex < DEV_MAX_QUEUE_SIZE; queueIndex++) {
            if (g_tapInfoList[index][queueIndex].fd == -1) {
                continue;
            }
            CPD_TapFree(g_tapInfoList[index][queueIndex].fd);
        }
    }
    g_cpdInit = CPD_INITIAL;
}

