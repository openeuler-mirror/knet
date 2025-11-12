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

#include "dp_tbm_api.h"
#include "dp_tbm.h"

#include "dp_ethernet.h"

#include "netdev.h"
#include "shm.h"
#include "utils_cfg.h"
#include "utils_log.h"
#include "pbuf.h"
#include "cpd.h"

#include "cpd_linux.h"
#include "cpd_adpt.h"

#define CPD_MAX_SYNC_NUM_ONCE 16u   /* 一次获取表项个数 */
#define CPD_MAX_PKT_SIZE 16         /* 一次获取报文个数 */
#define CPD_MAX_BUF_SIZE 2048       /* 最大报文长度 */
#define DP_NUD_INCOMPLETE 0x01

CPIOCallBack g_cpioCbFunc = {
    CPD_SyncTable,
    CPD_SendPkt,
    CPD_RcvPkt,
    CPD_TblMissHandle
};

// 预留g_cpioCbFunc注册能力
void CpIoHookReg(CPIOCallBack *cb)
{
    if (cb != NULL) {
        g_cpioCbFunc = *cb;
    }
}

/* 从 中转表 向 ND 表同步信息 */
static int CpdAnalysisArpTbl(SycnTableEntry *item)
{
    DP_NdMsg_t msg = {
        .ifindex = (int)item->ifindex,
        .state = item->state,
        .flags = 0,
        .type = 0,
    };
    DP_TbmAttr_t* rtAttr[DP_NDA_MAX] = { 0 };
    uint8_t buf[64];

    rtAttr[0] = (DP_TbmAttr_t*)buf;
    DP_TBM_ATTR_SET_TYPE(rtAttr[0], DP_NDA_DST);
    DP_TBM_ATTR_SET_LEN(rtAttr[0], (size_t)sizeof(DP_InAddr_t));
    DP_TBM_ATTR_SET_DATA(rtAttr[0], item->tableEntry.arpEntry.dst, DP_InAddr_t);

    rtAttr[1] = DP_TBM_ATTR_NEXT(rtAttr[0]);
    DP_TBM_ATTR_SET_TYPE(rtAttr[1], DP_NDA_LLADDR);
    DP_TBM_ATTR_SET_LEN(rtAttr[1], (uint16_t)sizeof(DP_EthAddr_t));
    DP_TBM_ATTR_SET_DATA(rtAttr[1], item->tableEntry.arpEntry.mac, DP_EthAddr_t);

    if (item->type == CPD_NEW_NEIGH) {
        // 表项变成incomplete，删除表项
        if ((item->state & DP_NUD_INCOMPLETE) != 0) {
            DP_LOG_DBG("Now arp tbl item state incomplete");
            return DP_NdCfg(DP_DEL_ND, &msg, rtAttr, 2); // 需要同步rtAttr数组中2位 ip、mac
        }
        if (DP_MAC_IS_DUMMY(&item->tableEntry.arpEntry.mac)) {
            DP_LOG_DBG("Cpd analysis arp tbl item ignore dummy mac");
            return 0;
        }
        msg.state = DP_ND_STATE_REACHABLE;
        DP_LOG_DBG("Get new nd msg");
        return DP_NdCfg(DP_NEW_ND, &msg, rtAttr, 2);    // 需要同步rtAttr数组中2位 ip、mac
    }
    if (item->type == CPD_DEL_NEIGH) {
        DP_LOG_DBG("Del nd msg");
        return DP_NdCfg(DP_DEL_ND, &msg, rtAttr, 2);    // 需要同步rtAttr数组中2位 ip、mac
    }
    return 0;
}

void CpdTblMissHandle(TBM_Notify_t* tn, int type, int op, void* item)
{
    (void)tn;
    (void)op;
    if (type == TBM_NOTIFY_TYPE_ND) { // IPV4
        DP_InAddr_t dstAddr = ((TBM_NdItem_t*)item)->dst;
        DP_InAddr_t srcAddr = ((TBM_NdItem_t*)item)->dev->in.ifAddr->local;
        int ret = g_cpioCbFunc.handleTblMiss(type, (void*)&srcAddr, (void*)&dstAddr);
        if (ret != 0) {
            DP_LOG_ERR("Cpd handle tbl miss failed.");
        }
    }
}

/* 转发协议栈收到的控制报文到内核 */
int CpdPktTranfer(uint32_t ifindex, void* pbuf, uint32_t dataLen)
{
    int ret;
    /* 在DP_PbufCopy中内存拷贝，无需初始化 */
    uint8_t* data = SHM_MALLOC(dataLen, MOD_CPD, DP_MEM_FREE);
    if (data == NULL) {
        DP_LOG_ERR("Malloc memory failed for cpd pkt transfer.");
        return -1;
    }
    int writeLen = DP_PbufCopy(pbuf, data, dataLen);
    ret = g_cpioCbFunc.writePkt(ifindex, data, (uint32_t)writeLen);
    SHM_FREE(data, DP_MEM_FREE);
    if (ret != 0) {
        return -1;
    }
    return 0;
}

void CpdTblSync(void)
{
    int ret = -1;
    uint32_t syncNum = CPD_MAX_SYNC_NUM_ONCE;
    SycnTableEntry tempTblItem[CPD_MAX_SYNC_NUM_ONCE] = {0};

    g_cpioCbFunc.syncTable(tempTblItem, &syncNum);  // 拿最多16个表项 到tempTblItem
    for (uint32_t i = 0; i < syncNum; ++i) {
        if (tempTblItem[i].family == DP_AF_INET) {
            ret = CpdAnalysisArpTbl(&tempTblItem[i]);
        }
        if (tempTblItem[i].family == DP_AF_INET6) {
            DP_LOG_INFO("Cpd analysis table item with family ipv6 is not supported.");
            continue;
        }
        if (ret != 0) {
            DP_LOG_INFO("Cpd analysis table current item unusefully, continue.");
        }
    }
}

/* 响应（转发）内核控制报文 */
void CpdPktHandle(void)
{
    Netdev_t* dev = NULL;
    DP_Pbuf_t* pbuf;
    uint8_t data[CPD_MAX_BUF_SIZE];
    uint32_t dataLen = CPD_MAX_BUF_SIZE;
    int recvLen;
    int cnt = 0;
    if (CFG_GET_VAL(DP_CFG_CPD_PKT_TRANS) == 0) {
        return;
    }

    for (int index = 0; index < DEV_TBL_SIZE; ++index) {
        dev = DP_GetNetdevByIndex(index);
        if (dev == NULL) {
            continue;
        }
        cnt = 0;
        while (cnt < CPD_MAX_PKT_SIZE) {
            recvLen = g_cpioCbFunc.readPkt((uint32_t)dev->ifindex, data, dataLen);
            if (recvLen <= 0) {
                break;
            }
            cnt++;
            pbuf = DP_PbufBuild(data, (uint16_t)recvLen, 0);
            if (pbuf == NULL) {
                DP_LOG_INFO("Get pbuf allocfailed when handle cpd pkt.");
                continue;
            }
            PBUF_SET_WID(pbuf, (uint8_t)-1);    // 保证与que的wid不一致，进入队列缓存发送，防止并发
            PBUF_SET_QUE_ID(pbuf, 0);
            PBUF_SET_DEV(pbuf, dev);
            NETDEV_XmitPbuf(pbuf);
        }
    }
}
