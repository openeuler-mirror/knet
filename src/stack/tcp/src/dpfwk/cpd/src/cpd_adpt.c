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

#include <sys/uio.h>

#include "dp_tbm_api.h"
#include "dp_tbm.h"

#include "dp_ethernet.h"
#include "dp_tcp.h"
#include "dp_ip.h"

#include "inet_sk.h"

#include "netdev.h"
#include "shm.h"
#include "utils_cfg.h"
#include "utils_log.h"
#include "pbuf.h"
#include "cpd.h"
#include "pmgr.h"

#include "cpd_linux.h"
#include "cpd_core.h"
#include "cpd_adpt.h"

#define CPD_MAX_SYNC_NUM_ONCE 16u   /* 一次获取表项个数 */
#define CPD_MAX_PKT_SIZE 64         /* 一次获取报文个数 */
#define CPD_MAX_BUF_SIZE 65535       /* 最大报文长度 */
#define CPD_MAX_IOV_LEN 128         /* 存储报文分片的iov数组长度 */

CPIOCallBack g_cpioCbFunc = {
    CPD_SyncTable,
    CPD_SendPkt,
    CPD_SendPktV,
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
        .family = item->family,
        .ifindex = (int)item->ifindex,
        .state = item->state,
        .flags = 0,
        .type = 0,
    };
    DP_TbmAttr_t* rtAttr[DP_NDA_MAX] = { 0 };
    uint8_t buf[64];

    rtAttr[0] = (DP_TbmAttr_t*)buf;
    DP_TBM_ATTR_SET_TYPE(rtAttr[0], DP_NDA_DST);
    if (msg.family == DP_AF_INET) {
        DP_TBM_ATTR_SET_LEN(rtAttr[0], sizeof(DP_InAddr_t));
        DP_TBM_ATTR_SET_DATA(rtAttr[0], item->ndEntry.dst.ipv4, DP_InAddr_t);
    } else {        // 能够保证msg.family取值仅为DP_AF_INET、DP_AF_INET6（CpdAnalysisNdInfo中填写）
        DP_TBM_ATTR_SET_LEN(rtAttr[0], sizeof(DP_In6Addr_t));
        DP_TBM_ATTR_SET_DATA(rtAttr[0], item->ndEntry.dst.ipv6, DP_In6Addr_t);
    }

    rtAttr[1] = DP_TBM_ATTR_NEXT(rtAttr[0]);
    DP_TBM_ATTR_SET_TYPE(rtAttr[1], DP_NDA_LLADDR);
    DP_TBM_ATTR_SET_LEN(rtAttr[1], sizeof(DP_EthAddr_t));
    DP_TBM_ATTR_SET_DATA(rtAttr[1], item->ndEntry.mac, DP_EthAddr_t);

    if (item->type == CPD_NEW_NEIGH) {
        // 表项变成incomplete，删除表项
        if ((item->state & DP_NUD_INCOMPLETE) != 0) {
            DP_LOG_DBG("Now arp tbl item state incomplete");
            return DP_NdCfg(DP_DEL_ND, &msg, rtAttr, 2); // 需要同步rtAttr数组中2位 ip、mac
        }
        if (DP_MAC_IS_DUMMY(&item->ndEntry.mac)) {
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

void CpdTblMissHandle(void* tn, int type, int op, uint8_t family, void* item)
{
    (void)tn;
    (void)op;
    (void)type;
    // 当前仅支持TBM_NOTIFY_TYPE_ND类型
    ASSERT(type == TBM_NOTIFY_TYPE_ND);

    int ret;
    TBM_IpAddr_t dstAddr;
    TBM_IpAddr_t srcAddr;
    if (family == DP_AF_INET) {
        dstAddr.ipv4 = ((TBM_NdItem_t*)item)->dst.ipv4;
        srcAddr.ipv4 = ((TBM_NdItem_t*)item)->dev->in.ifAddr->local;
        ret = g_cpioCbFunc.handleTblMiss(DP_IP_VERSION_IPV4, ((TBM_NdItem_t*)item)->dev->ifindex,
                                         (void*)&srcAddr, (void*)&dstAddr);
        if (ret != 0) {
            DP_LOG_DBG("Cpd handle tbl miss Ip4 failed.");
        }
        return;
    }
    DP_LOG_ERR("Cpd handle tbl miss with unknown op.");
}

static void CpdRxTcpChecksumCal(DP_Pbuf_t *pbuf)
{
    Netdev_t *dev = (Netdev_t *)PBUF_GET_DEV(pbuf);
    if (!NETDEV_RX_TCP_CKSUM_ENABLED(dev)) {  // 不开checksum offload，DP协议栈已软算，无需重新计算checksum
        return;
    }

    uint16_t pbufDataLen = DP_PBUF_GET_SEG_LEN(pbuf);   // 单片长度
    DP_EthHdr_t *ethAddr = (DP_EthHdr_t *)(DP_PBUF_GET_PAYLOAD(pbuf) + DP_PBUF_GET_OFFSET(pbuf));
    DP_IpHdr_t *ipHdr = (DP_IpHdr_t *)((uint8_t *)ethAddr + sizeof(DP_EthHdr_t));
    uint8_t ipHdrLen = DP_GET_IP_HDR_LEN(ipHdr);
    /* 如果ip头长度大于单片长度，或者小于最小头，或者等于整包长度，Ippreproc已经丢弃 */
    DP_TcpHdr_t *tcpHdr = (DP_TcpHdr_t *)((uint8_t *)ethAddr + sizeof(DP_EthHdr_t) + ipHdrLen);
    uint8_t tcpHdrLen = tcpHdr->off << 2;

    if (pbufDataLen >= sizeof(DP_EthHdr_t) + ipHdrLen + tcpHdrLen) {
        if (ethAddr->type != UTILS_HTONS(DP_ETH_P_IP)) {
            return;
        }
        if (ipHdr->type == DP_IPHDR_TYPE_TCP) {
            INET_Hashinfo_t hashinfo;
            hashinfo.laddr = ipHdr->src;
            hashinfo.paddr = ipHdr->dst;
            hashinfo.protocol = DP_IPPROTO_TCP;

            uint32_t pseudoHdrCksum = INET_CalcPseudoCksum(&hashinfo);

            tcpHdr->chksum = 0;
            uint32_t cksum = pseudoHdrCksum +
                             UTILS_HTONS((uint16_t)PBUF_GET_PKT_LEN(pbuf) - sizeof(DP_EthHdr_t) - ipHdrLen);
            PBUF_CUT_HEAD(pbuf, sizeof(DP_EthHdr_t));
            PBUF_CUT_HEAD(pbuf, ipHdrLen);
            cksum += PBUF_CalcCksumAcc(pbuf);
            PBUF_PUT_HEAD(pbuf, ipHdrLen);
            PBUF_PUT_HEAD(pbuf, sizeof(DP_EthHdr_t));
            tcpHdr->chksum = UTILS_CksumSwap(cksum);
        }
    }
}

uint16_t PbufReadIov(Pbuf_t* pbuf, struct iovec* dataIov, int *iovLen)
{
    ASSERT(pbuf != NULL);
    uint16_t     ret    = 0;
    uint16_t     totLen = (uint16_t)PBUF_GET_PKT_LEN(pbuf);
    Pbuf_t* cur    = pbuf;

    while (cur != NULL && cur->segLen == 0) {
        cur = cur->next;
    }
    int tmpIovLen = 0;
    while (cur != NULL  && totLen > 0) {
        uint16_t cpyLen = cur->segLen;
        if (cpyLen == 0) {
            cur = cur->next;
            continue;
        }

        dataIov[tmpIovLen].iov_base = PBUF_MTOD(cur, uint8_t*);
        dataIov[tmpIovLen].iov_len = cpyLen;
        tmpIovLen += 1;

        ret += cpyLen;
        totLen -= cpyLen;
        cur->segLen -= cpyLen;
        cur->offset += cpyLen;
    }
    *iovLen = tmpIovLen;
    return ret;
}

uint16_t PbufCopyIov(DP_Pbuf_t* pbuf, struct iovec* dataIov, int *IovLen)
{
    if (pbuf == NULL || dataIov == NULL || IovLen == NULL) {
        return 0;
    }
    return PbufReadIov(pbuf, dataIov, IovLen);
}

/* 转发协议栈收到的控制报文到内核 */
int CpdPktTranfer(uint32_t ifindex, void* pbuf, uint32_t dataLen, int cpdQueueId)
{
    if (PMGR_Get(PMGR_ENTRY_DELAY_KERNEL_IN) != NULL) {
        CpdRxTcpChecksumCal(pbuf); // TM280网卡会置checksum字段，无需计算；sp670网卡不会置，只会设置标志，需要计算
    }

    int ret;
    int writeLen;
    struct iovec iov[CPD_MAX_IOV_LEN];
    int iovCnt = 0;
    /* 在DP_PbufCopy中内存拷贝，无需初始化 */
    if (UTILS_UNLIKELY(((DP_Pbuf_t *)pbuf)->nsegs > CPD_MAX_IOV_LEN)) {
        uint8_t* dataHeap = SHM_MALLOC(dataLen, MOD_CPD, DP_MEM_FREE);
        if (dataHeap == NULL) {
            DP_ADD_ABN_STAT(DP_CPD_TRANS_MALLOC_ERR);
            DP_LOG_ERR("Malloc memory failed for cpd pkt transfer.");
            return -1;
        }
        writeLen = DP_PbufCopy(pbuf, dataHeap, dataLen);
        ret = g_cpioCbFunc.writePkt(ifindex, dataHeap, (uint32_t)writeLen, cpdQueueId);
        SHM_FREE(dataHeap, DP_MEM_FREE);
    } else {
        writeLen = PbufCopyIov(pbuf, iov, &iovCnt);
        ret = g_cpioCbFunc.writePktV(ifindex, iov, iovCnt, (uint32_t)writeLen, cpdQueueId);
    }

    if (ret != 0) {
        DP_LOG_ERR("Cpd write to kernel ret %d errno %d", ret, errno);
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
        ret = CpdAnalysisArpTbl(&tempTblItem[i]);
        if (ret != 0) {
            DP_LOG_INFO("Cpd analysis table current item unusefully, continue.");
        }
    }
}

static void CpdTxCksumAdd(Netdev_t *dev, DP_Pbuf_t* pbuf)
{
    DP_EthHdr_t *ethAddr = (DP_EthHdr_t *)(DP_PBUF_GET_PAYLOAD(pbuf) + DP_PBUF_GET_OFFSET(pbuf));
    DP_IpHdr_t *ipHdr = (DP_IpHdr_t *)((uint8_t *)ethAddr + sizeof(DP_EthHdr_t));
    uint8_t ipHdrLen = DP_GET_IP_HDR_LEN(ipHdr);  // 如果ip头长度大于单片长度，或者小于最小头，或者等于整包长度，在外层已经被丢弃
    DP_TcpHdr_t *tcpHdr = (DP_TcpHdr_t *)((uint8_t *)ethAddr + sizeof(DP_EthHdr_t) + ipHdrLen);
    uint8_t tcpHdrLen = tcpHdr->off << 2;

    INET_Hashinfo_t hashinfo;
    hashinfo.laddr = ipHdr->src;
    hashinfo.paddr = ipHdr->dst;
    hashinfo.protocol = DP_IPPROTO_TCP;
    uint32_t pseudoHdrCksum = INET_CalcPseudoCksum(&hashinfo);

    if (NETDEV_TX_TCP_CKSUM_ENABLED(dev)) {
        DP_PBUF_SET_OLFLAGS_BIT(pbuf, DP_PBUF_OLFLAGS_TX_TCP_CKSUM);
        ipHdr->chksum = 0;   // TM280 ip checksum必须置0，sp670可以不置0
        tcpHdr->chksum = 0;  // 防止内核tap口原先有值

        if (NETDEV_TX_L4_CKSUM_PARTIAL(dev)) {
            uint16_t len = (uint16_t)PBUF_GET_PKT_LEN(pbuf) - sizeof(DP_EthHdr_t) - ipHdrLen;
            if (NETDEV_TSO_ENABLED(dev) && pbuf->totLen > dev->mtu + sizeof(DP_EthHdr_t)) { /* 1448 + 66 */
                // 开启 TSO 下，伪校验和不能包括报文长度
                len = 0;

                DP_PBUF_SET_OLFLAGS_BIT(pbuf, DP_PBUF_OLFLAGS_TX_TSO);
                pbuf->tsoFragSize = dev->mtu - ipHdrLen - tcpHdrLen; /* 1448 */
            }
            uint32_t cksum = pseudoHdrCksum + UTILS_HTONS(len);
            tcpHdr->chksum = UTILS_CksumAdd(cksum);
        }
    }
}

static void CpdTxTcpChecksumAndTsoSet(Netdev_t *dev, DP_Pbuf_t *pbuf)
{
    uint8_t ethHdr = sizeof(DP_EthHdr_t);
    // 只有很小包头的异常包，不开checksum offload，内核已软算，无需重新计算checksum
    if (!NETDEV_TX_TCP_CKSUM_ENABLED(dev) || PBUF_GET_PKT_LEN(pbuf) < ethHdr + sizeof(DP_IpHdr_t)) {
        return;
    }

    uint16_t pbufDataLen = DP_PBUF_GET_SEG_LEN(pbuf);   // 单片长度
    uint32_t pktLen = PBUF_GET_PKT_LEN(pbuf);           // 整个包多片总长度
    DP_EthHdr_t *ethAddr = (DP_EthHdr_t *)(DP_PBUF_GET_PAYLOAD(pbuf) + DP_PBUF_GET_OFFSET(pbuf));
    DP_IpHdr_t *ipHdr = (DP_IpHdr_t *)((uint8_t *)ethAddr + ethHdr);
    uint8_t ipHdrLen = DP_GET_IP_HDR_LEN(ipHdr);
    /* 如果ip头长度大于单片长度，或者小于最小头，或者等于整包长度，或者取tcp头长度都不够, 忽略计算直接返回 */
    if (ipHdrLen > pbufDataLen || ipHdrLen < sizeof(DP_IpHdr_t) || ipHdrLen == pktLen ||
        pktLen < ethHdr + ipHdrLen + sizeof(DP_TcpHdr_t)) {
        return;
    }
    DP_TcpHdr_t *tcpHdr = (DP_TcpHdr_t *)((uint8_t *)ethAddr + ethHdr + ipHdrLen);
    uint8_t tcpHdrLen = tcpHdr->off << 2;

    if (pbufDataLen >= sizeof(DP_EthHdr_t) + ipHdrLen + tcpHdrLen) {
        if (ethAddr->type != UTILS_HTONS(DP_ETH_P_IP)) {
            return;
        }

        if (ipHdr->type == DP_IPHDR_TYPE_TCP) {
            CpdTxCksumAdd(dev, pbuf);
            /* tap口转发报文，只需要相对值，所以offset暂时设置为0；dpdk offload需要pbuf以下字段 */
            pbuf->offset = 0;
            pbuf->l3Off = sizeof(DP_EthHdr_t);
            pbuf->l4Off = sizeof(DP_EthHdr_t) + ipHdrLen;
            pbuf->l4Len = tcpHdrLen;  // 32 = sizeof(DP_TcpHdr_t) + 12字节
        }
    }
}

/* 响应（转发）内核控制报文 */
void CpdPktHandle(int cpdQueueId)
{
    Netdev_t* dev = NULL;
    DP_Pbuf_t* pbuf;
    /* 大包数据放到data区, 长度由dataLen控制 */
    int recvLen;
    int cnt = 0;
    if (CFG_GET_VAL(DP_CFG_CPD_PKT_TRANS) == 0 || cpdQueueId >= DEV_MAX_QUEUE_SIZE) {
        return;
    }

    uint8_t* data = SHM_MALLOC(CPD_MAX_BUF_SIZE, MOD_CPD, DP_MEM_FREE);
    if (data == NULL) {
        DP_LOG_ERR("Malloc memory failed for cpd pkt handle.");
        return;
    }
    uint32_t dataLen = CPD_MAX_BUF_SIZE;
    for (int index = 0; index <= g_devMaxIndex; ++index) {
        dev = DP_GetNetdevByIndexLockFree(index);
        if (dev == NULL) {
            continue;
        }
        cnt = 0;
        while (cnt < CPD_MAX_PKT_SIZE) {
            recvLen = g_cpioCbFunc.readPkt((uint32_t)dev->ifindex, data, dataLen, cpdQueueId);
            if (recvLen <= 0) {
                break;
            }
            cnt++;
            pbuf = DP_PbufBuild(data, (uint16_t)recvLen, 0);
            if (pbuf == NULL) {
                DP_LOG_INFO("Get pbuf allocfailed when handle cpd pkt.");
                continue;
            }
            DP_PBUF_SET_WID(pbuf, (uint8_t)-1);    // 保证与que的wid不一致，进入队列缓存发送，防止并发
            // 设置进入的worker队列
            DP_EthHdr_t *ethAddr = (DP_EthHdr_t *)(DP_PBUF_GET_PAYLOAD(pbuf) + DP_PBUF_GET_OFFSET(pbuf));
            uint32_t hash = CpdCalcTcpHash(ethAddr) % dev->txQueCnt;
            PBUF_SET_QUE_ID(pbuf, (uint8_t)hash);
            PBUF_SET_DEV(pbuf, dev);
            if (PMGR_Get(PMGR_ENTRY_DELAY_KERNEL_IN) != NULL) {
                CpdTxTcpChecksumAndTsoSet(dev, pbuf);
            }
            NETDEV_XmitPbuf(pbuf);
        }
    }
    SHM_FREE(data, DP_MEM_FREE);
}
