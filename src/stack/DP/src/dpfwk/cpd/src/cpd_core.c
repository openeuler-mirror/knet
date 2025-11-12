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
#include "pbuf.h"
#include "cpd_core.h"

CpdArpOpNodeHead g_cpdArpOpList;
CpdTapInfo g_tapInfoList[DEV_TBL_SIZE];   // tap口与netdev一一对应

static int g_cpdInit = CPD_INITIAL;
static TBM_Notify_t g_tbmNotify;

static Pbuf_t* CpdInput(Pbuf_t* pbuf)
{
    int ifindex = 0;
    DP_INC_PKT_STAT(pbuf->wid, DP_PKT_UP_TO_CTRL_PLANE);
    PBUF_PUT_HEAD(pbuf, sizeof(DP_EthHdr_t));
    ifindex = ((Netdev_t*)PBUF_GET_DEV(pbuf))->ifindex;
    CpdPktTranfer((uint32_t)ifindex, pbuf, PBUF_GET_PKT_LEN(pbuf));
    PBUF_Free(pbuf);
    return NULL;
}

/* 创建tap口 */
static void CPD_Adapt_Init(void)
{
    DP_Netdev_t* dev;
    int fd = -1;
    for (int index = 0; index < DEV_TBL_SIZE; ++index) {
        dev = DP_GetNetdevByIndex(index);
        // 遍历
        if (dev == NULL) {
            continue;
        }
        fd = CPD_TapAlloc(dev);
        if (fd <= 0) {
            DP_LOG_ERR("Cpd alloc tap failed.");
        }
        g_tapInfoList[index].ifindex = dev->ifindex;
        g_tapInfoList[index].fd = fd;
    }
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
        DP_LOG_ERR("SysCallInit failed.");
        return -1;
    }

    // 创建netlink socket
    LIST_INIT_HEAD(&g_cpdArpOpList);
    ret = CpdCpInit();
    if (ret != 0) {
        DP_LOG_ERR("CpdCpInit failed.");
        return -1;
    }

    g_tbmNotify.pid = 0;
    g_tbmNotify.cb = CpdTblMissHandle;
    g_tbmNotify.groups = TBM_NOTIFY_TYPE_ND;
    SPINLOCK_Init(&g_tbmNotify.lock);

    TBM_AddNotify(NULL, &g_tbmNotify);
    for (int i = 0; i < DEV_TBL_SIZE; ++i) {
        g_tapInfoList[i].fd = -1;
        g_tapInfoList[i].ifindex = -1;
    }
    if (CFG_GET_VAL(DP_CFG_CPD_PKT_TRANS) != 0) {
        CPD_Adapt_Init();
        PMGR_AddEntry(PMGR_ENTRY_ARP_IN, CpdInput);
        PMGR_AddEntry(PMGR_ENTRY_ICMP_IN, CpdInput);
    }
    g_cpdInit = CPD_INITED;
    return 0;
}

int DP_CpdRunOnce(void)
{
    if (g_cpdInit != CPD_INITED) {
        DP_LOG_ERR("Cpd is not initialized.");
        return -1;
    }
    // 同步表项
    CpdTblSync();
    // 控制报文响应
    CpdPktHandle();
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

    CpdArpOpList_t *arpOpItem  = LIST_FIRST(&g_cpdArpOpList);
    CpdArpOpList_t *nextArpOpItem = NULL;
    while (arpOpItem != NULL) {
        nextArpOpItem = LIST_NEXT(arpOpItem, node);
        SHM_FREE(arpOpItem, DP_MEM_FREE);
        arpOpItem = nextArpOpItem;
    }

    for (uint32_t index = 0; index < DEV_TBL_SIZE; ++index) {
        CPD_TapFree(g_tapInfoList[index].fd);
    }
    g_cpdInit = CPD_INITIAL;
}

