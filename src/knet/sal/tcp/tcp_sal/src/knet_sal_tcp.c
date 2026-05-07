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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>

#include "rte_ethdev.h"
#include "rte_ether.h"
#include "rte_ip.h"
#include "rte_tcp.h"
#include "rte_mbuf.h"
#include "stdarg.h"
#include "rte_thash.h"

#include "dp_log_api.h"
#include "dp_show_api.h"
#include "dp_debug_api.h"

#include "knet_log.h"
#include "knet_tcp_symbols.h"
#include "knet_pkt.h"
#include "knet_transmission.h"
#include "knet_dpdk_init.h"
#include "knet_config.h"
#include "knet_thread.h"
#include "knet_sal_func.h"
#include "knet_sal_inner.h"
#include "knet_sal_tcp.h"

#define MAX_TX_BURST_NUM 16
#define MAX_RX_BURST_NUM 64 // 同协议栈接收大小一致
#define RSS_KEY_SIZE 40 // 同步hns HNS3_RSS_KEY_SIZE与hinic SP6_RSS_KEY_SIZE驱动中大小
#define TCP_PBUFCNT 512 // 约束每个sock写入pbuf数量的约束，防止写满缓冲区在发送导致一次性写入太多，目前协议栈仅约束共线程

static bool g_hwChecksumEnable = false;
static uint16_t g_qid = 0;

static DP_LogLevel_E KnetGetStackLogLevel(void)
{
    switch (KNET_LogLevelGet()) {
        case KNET_LOG_ERR:
            return DP_LOG_LEVEL_ERROR;
        case KNET_LOG_WARN:
            return DP_LOG_LEVEL_WARNING;
        case KNET_LOG_INFO:
            return DP_LOG_LEVEL_INFO;
        case KNET_LOG_DEBUG:
            return DP_LOG_LEVEL_DEBUG;
        default:
            KNET_ERR("Log level %u not supported", KNET_LogLevelGet());
            return DP_LOG_LEVEL_WARNING;
    }
}

uint32_t KNET_SAL_Init()
{
    int re = KnetInitDpSymbols();
    if (re != 0) {
        KNET_ERR("Dpdp symbol load failed, ret %d", re);
        return KNET_ERROR;
    }
    DP_LogHook logHook = KNET_LogFixLenOutputHook;
    uint32_t ret = DP_LogHookReg(logHook);
    if (ret != 0) {
        KNET_ERR("Reg stack log failed, ret %d", ret);
        return KNET_ERROR;
    }
    DP_LogLevelSet(KnetGetStackLogLevel());

    ret = KnetRegFunc();
    if (ret != KNET_OK) {
        KNET_ERR("Reg func failed, ret %d", ret);
        return KNET_ERROR;
    }

    ret = KnetHandleInit();
    if (ret != KNET_OK) {
        KNET_ERR("Mem handle init failed, ret %d", ret);
        return KNET_ERROR;
    }

    /* tcp workerId功能注册 */
    ret = (uint32_t)KnetRegWorkderId();
    if (ret != 0) {
        KNET_ERR("RegWorkderId failed, ret %d", ret);
        return KNET_ERROR;
    }

    KNET_INFO("K-NET sal Init success");

    return KNET_OK;
}

/**
 * @brief 将 tcp 中的 `DP_Pbuf_t` 的校验和卸载信息分配到 DPDK 的 `rte_mbuf` 中。
 * @par 此函数根据 `DP_Pbuf_t` 中的标志位设置相应的 `rte_mbuf` 的校验和相关字段。
 */
static KNET_ALWAYS_INLINE void KnetMbufCksumOffloadInfoAssign(struct rte_mbuf *mbuf, DP_Pbuf_t *pbuf)
{
    /* 填写rteMbuf中checksum offload需要的字段 */
    if ((DP_PBUF_GET_OLFLAGS(pbuf) & DP_PBUF_OLFLAGS_TX_IP_CKSUM) > 0) {
        mbuf->ol_flags |= RTE_MBUF_F_TX_IPV4;
        mbuf->ol_flags |= RTE_MBUF_F_TX_IP_CKSUM;
    }

    if ((DP_PBUF_GET_OLFLAGS(pbuf) & DP_PBUF_OLFLAGS_TX_TCP_CKSUM) > 0) {
        mbuf->ol_flags |= RTE_MBUF_F_TX_IPV4;
        mbuf->ol_flags |= RTE_MBUF_F_TX_IP_CKSUM;
        mbuf->ol_flags |= RTE_MBUF_F_TX_TCP_CKSUM;

        if ((DP_PBUF_GET_OLFLAGS(pbuf) & DP_PBUF_OLFLAGS_TX_TSO) > 0) {
            mbuf->ol_flags |= RTE_MBUF_F_TX_TCP_SEG;
            mbuf->tso_segsz = DP_PBUF_GET_TSO_FRAG_SIZE(pbuf);
        }
    }

    mbuf->l2_len = DP_PBUF_GET_L2_LEN(pbuf);
    mbuf->l3_len = DP_PBUF_GET_L3_LEN(pbuf);
    mbuf->l4_len = DP_PBUF_GET_L4_LEN(pbuf);
}

static KNET_ALWAYS_INLINE void DpPbufCksumOffloadInfoAssign(DP_Pbuf_t *pbuf, struct rte_mbuf *mbuf)
{
    uint16_t olflags = 0;
    if ((mbuf->ol_flags & RTE_MBUF_F_RX_IP_CKSUM_GOOD) != 0) {
        olflags |= DP_PBUF_OLFLAGS_RX_IP_CKSUM_GOOD;
    }

    if ((mbuf->ol_flags & RTE_MBUF_F_RX_IP_CKSUM_BAD) != 0) {
        olflags |= DP_PBUF_OLFLAGS_RX_IP_CKSUM_BAD;
    }

    if ((mbuf->ol_flags & RTE_MBUF_F_RX_L4_CKSUM_GOOD) != 0) {
        olflags |= DP_PBUF_OLFLAGS_RX_L4_CKSUM_GOOD;
    }

    if ((mbuf->ol_flags & RTE_MBUF_F_RX_L4_CKSUM_BAD) != 0) {
        olflags |= DP_PBUF_OLFLAGS_RX_L4_CKSUM_BAD;
    }
    DP_PBUF_SET_OLFLAGS(pbuf, olflags);
}

static KNET_ALWAYS_INLINE void PbufSeg2Mbuf(DP_Pbuf_t *pbuf, struct rte_mbuf *mbuf, uint16_t mbufRef)
{
    mbuf->pkt_len = DP_PBUF_GET_TOTAL_LEN(pbuf);
    mbuf->data_len = DP_PBUF_GET_SEG_LEN(pbuf);
    mbuf->nb_segs = DP_PBUF_GET_SEG_NUM(pbuf);
    mbuf->buf_addr = DP_PBUF_GET_PAYLOAD(pbuf);
    mbuf->data_off = DP_PBUF_GET_OFFSET(pbuf);
    // 不能直接赋值，会导致协议栈和驱动并发操作时计数出错；而是应该使用偏移。
    if (mbufRef != 0) {
        rte_mbuf_refcnt_update(mbuf, mbufRef);
    }
}

static KNET_ALWAYS_INLINE struct rte_mbuf *KNET_DpMbuf2rteMbuf(DP_Pbuf_t *buf)
{
    DP_Pbuf_t *pbuf = buf;
    DP_Pbuf_t *nextPbuf = NULL;

    struct rte_mbuf *mbuf = KNET_Pkt2Mbuf(pbuf);
    struct rte_mbuf *headMbuf = mbuf;

    /**
    * 1、tx 方向，协议栈保证了单线程，发送和释放不并发；
    * 2、协议栈保证了整个pbuf链上的引用计数一致，所以使用同一个mbufRef；
    */
    uint16_t mbufRef = DP_PBUF_GET_REF(buf) - 1;
    DP_PBUF_SET_REF(buf, mbufRef);

    // 首片转换
    PbufSeg2Mbuf(pbuf, mbuf, mbufRef);

    // 其它分片转换
    nextPbuf = DP_PBUF_GET_NEXT(pbuf);
    while (nextPbuf != NULL) {
        mbuf->next = KNET_Pkt2Mbuf(nextPbuf);
        mbuf = mbuf->next;
        pbuf = nextPbuf;
        PbufSeg2Mbuf(pbuf, mbuf, mbufRef);
        nextPbuf = DP_PBUF_GET_NEXT(pbuf);
    }
    mbuf->next = NULL;

    if (g_hwChecksumEnable) {
        KnetMbufCksumOffloadInfoAssign(headMbuf, buf);
    }

    headMbuf->pkt_len = DP_PBUF_GET_TOTAL_LEN(buf);
    headMbuf->nb_segs = DP_PBUF_GET_SEG_NUM(buf);
    return headMbuf;
}

static KNET_ALWAYS_INLINE void MbufSeg2Pbuf(struct rte_mbuf *mbuf, DP_Pbuf_t *pbuf)
{
    DP_PBUF_SET_REF(pbuf, rte_mbuf_refcnt_read(mbuf));
    DP_PBUF_SET_SEG_NUM(pbuf, mbuf->nb_segs);
    DP_PBUF_SET_TOTAL_LEN(pbuf, mbuf->pkt_len);
    DP_PBUF_SET_PAYLOAD_LEN(pbuf, mbuf->buf_len);
    DP_PBUF_SET_PAYLOAD(pbuf, mbuf->buf_addr);
    DP_PBUF_SET_SEG_LEN(pbuf, mbuf->data_len);
    DP_PBUF_SET_OFFSET(pbuf, mbuf->data_off);
    DP_PBUF_SET_END(pbuf, pbuf);
    DP_PBUF_SET_NEXT(pbuf, NULL);
    DP_PBUF_SET_VPNID(pbuf, 0);
}

/**
 * @brief DPDK `rte_mbuf` 链表转换为 tcp 中的 `DP_Pbuf_t` 链表结构。
 * @par 每个 `rte_mbuf` 会被转换为一个 `DP_Pbuf_t`，并组成单向链表返回。
 *
 * @param    rteMbuf [IN] 参数类型 #struct rte_mbuf *。要转换的 DPDK `rte_mbuf` 链表的头指针。
 * @param    portId  [IN] 参数类型 #uint32_t。与数据包相关联的端口 ID。
 *
 * @retval 返回转换后的 `DP_Pbuf_t` 链表的头指针。
 *         如果链表中的 `mbuf` 个数不足，函数内部会记录错误信息。
 */
static KNET_ALWAYS_INLINE DP_Pbuf_t *KNET_RteMbuf2tcpMbuf(struct rte_mbuf *rteMbuf, uint32_t portId)
{
    struct rte_mbuf *mbuf = rteMbuf;
    struct rte_mbuf *nextMbuf = NULL;

    DP_Pbuf_t *pbuf = KNET_Mbuf2Pkt(mbuf);
    DP_Pbuf_t *headPbuf = pbuf;

    // 首片转换
    MbufSeg2Pbuf(mbuf, pbuf);

    // 其它分片转换
    nextMbuf = mbuf->next;
    while (nextMbuf != NULL) {
        DP_PBUF_SET_NEXT(pbuf, KNET_Mbuf2Pkt(nextMbuf));
        pbuf = pbuf->next;
        mbuf = nextMbuf;
        MbufSeg2Pbuf(mbuf, pbuf);
        nextMbuf = mbuf->next;
    }
    headPbuf->end = pbuf;

    DP_PBUF_SET_TOTAL_LEN(headPbuf, rte_pktmbuf_pkt_len(rteMbuf));
    DP_PBUF_SET_SEG_NUM(headPbuf, rteMbuf->nb_segs);
    if (g_hwChecksumEnable) {
        DpPbufCksumOffloadInfoAssign(headPbuf, rteMbuf);
    }

    return headPbuf;
}

static KNET_ALWAYS_INLINE void KnetDropPkt(void **pkt, int bufCnt)
{
    DP_Pbuf_t *curBuf = NULL;
    void *nextBuf = NULL;
    struct rte_mbuf *mbuf = NULL;
    for (int index = 0; index < bufCnt; index++) {
        curBuf = pkt[index];
        while (curBuf != NULL) {
            nextBuf = DP_PBUF_GET_NEXT(curBuf);
            mbuf = KNET_Pkt2Mbuf(curBuf);
            KNET_PktFree(mbuf);
            curBuf = nextBuf;
        }
    }
}

uint16_t KnetGetFdirQid(unsigned __int128 queMap, uint16_t *qid)
{
    uint16_t qidSize = 0;
    for (int i = 0; i < MAX_QUEUE_NUM; i++) {
        if ((queMap & (unsigned __int128)1 << i) != 0) {
            qid[qidSize] = i + g_qid;
            qidSize++;
        }
    }
    return qidSize;
}

uint16_t GetRetaSize(uint16_t portId)
{
    struct rte_eth_dev_info devInfo = {0};
    /* 获取指定端口的设备信息 */
    int32_t ret = rte_eth_dev_info_get(portId, &devInfo);
    if (ret != 0) {
        KNET_ERR("The rte eth dev info get failed, portId %u, ret %d", portId, ret);
        return -1;
    }
    return devInfo.reta_size;
}

static int GetQidFromReta(uint16_t portId, struct rte_ipv4_tuple *tuple)
{
    int ret = -1;
    uint32_t hash = 0;
    uint8_t rss_key[RSS_KEY_SIZE] = { 0 };
    struct rte_eth_rss_conf rss_conf = { rss_key, RSS_KEY_SIZE };
    ret = rte_eth_dev_rss_hash_conf_get(portId, &rss_conf);
    if (ret == 0) {
        hash = (uint32_t)rte_softrss((uint32_t*)tuple, RTE_THASH_V4_L4_LEN, rss_key);

        uint16_t retaSize = GetRetaSize(portId);
        if (retaSize == 0) {
            KNET_ERR("Get reta size failed, portId %u", portId);
            return -1;
        }

        struct rte_eth_rss_reta_entry64 retaConf[retaSize];
        for (int i = 0; i < retaSize / RTE_ETH_RETA_GROUP_SIZE; i++) {
            retaConf[i].mask = ~0LL;
        }
        ret = rte_eth_dev_rss_reta_query(portId, retaConf, retaSize);
        if (ret != 0) {
            KNET_ERR("Rss reta query failed, portId %u, ret %d", portId, ret);
            return -1;
        }
        // retaSize在sp卡为256，tm卡为512, RTE_ETH_RETA_GROUP_SIZE为64，所以index和shift不会越界
        int index = (hash % retaSize) / RTE_ETH_RETA_GROUP_SIZE;
        int shift = (hash % retaSize) % RTE_ETH_RETA_GROUP_SIZE;
        ret  =  retaConf[index].reta[shift];
        KNET_INFO("Rss hash %u, int index %d, int shift %d, qid %d, qid - g_qid %d",
            hash, index, shift, ret, ret - g_qid);
        return ret;
    }
    KNET_ERR("The rte eth dev rss hash conf get failed in reta table, portId %u, ret %d", portId, ret);
    return -1;
}

/**
 * @brief 根据4元组寻找收包的queue id
 * tuple设置时考虑收包场景进行hash：sip是对端ip，dip为本端ip；发包时设置为与收包一样的队列即可以保证同队列
 * 具体规则为：找到dip dport是否有流表，如果没有流表，直接使用reta表进行寻找；如果找到流表，使用流表寻找queue id。
 * @param portId
 * @param srcIp
 * @param dstIp
 * @param srcPort
 * @param dstPort
 * @return int -1: 失败，其他值为queue id
 */
int DpdkGetQueId(uint16_t portId, uint32_t srcIp, uint32_t dstIp, uint16_t srcPort, uint16_t dstPort)
{
    uint16_t fdirQueue[MAX_QUEUE_NUM] = {0};
    uint16_t fdirDstPort = dstPort;
    /* 存下portStep, 数据路径避免每次GetCfg返回长union，从函数栈空间拷贝长数据到现场 */
    static int portStep = INVALID_CONF_INT_VALUE;
    if (portStep == INVALID_CONF_INT_VALUE) {
        portStep = KNET_GetCfg(CONF_INNER_PORT_STEP)->intValue;
    }
    if (portStep > 1) {
        // 把port转换为下流表的区间左值
        fdirDstPort = dstPort & ~(portStep - 1);
    }
    int queueSize = KNET_FindFdirQue(dstIp, fdirDstPort, fdirQueue);
 
    struct rte_ipv4_tuple tuple = {0};
    tuple.src_addr = srcIp;
    tuple.dst_addr = dstIp;
    tuple.sport = srcPort;
    tuple.dport = dstPort;
    int qid = -1;
    if (queueSize == -1) {
        // 流表没有命中，直接rss
        qid = GetQidFromReta(portId, &tuple); // 函数中会打印日志
    } else if (queueSize == 1) {
        qid = fdirQueue[0];
    } else if (queueSize > 1) { // rss流表,需要散开
        uint32_t hash = 0;
        uint8_t rss_key[RSS_KEY_SIZE] = { 0 };
        struct rte_eth_rss_conf rss_conf = { rss_key, RSS_KEY_SIZE };
        int ret = rte_eth_dev_rss_hash_conf_get(portId, &rss_conf);
        if (ret == 0) {
            hash = (uint32_t)rte_softrss((uint32_t*)&tuple, RTE_THASH_V4_L4_LEN, rss_key);
            qid = fdirQueue[hash % queueSize];
            return qid;
        }
        KNET_ERR("The rte eth dev rss hash conf get failed in rss flow, portId %u, ret %d", portId, ret);
        return -1;
    } else {
        KNET_ERR("No queue in flow table, portId %u, dip %u, dport %u", portId, dstIp, dstPort);
    }

    return qid;
}

/**
 * @brief 主动建链时，获取发送队列，保证收发同队列
 * 如果当前worker仅有一个队列，协议栈会拦截，不会在knet进行hash；
 * 如果worker有多队列，会在knet进行hash获取队列。
 * @param ctx 网卡信息
 * @param srcAddr 对端ip
 * @param srcAddrLen
 * @param dstAddr 本端ip
 * @param dstAddrLen
 * @return int -1：失败，其他：成功
 */
int KNET_ACC_TxHash(void* ctx, const struct DP_Sockaddr* srcAddr, DP_Socklen_t srcAddrLen,
    const struct DP_Sockaddr *dstAddr, DP_Socklen_t dstAddrLen)
{
    // 协议栈保证入参不为空
    const struct sockaddr_in *srcAddrIn = (const struct sockaddr_in *)srcAddr;
    uint32_t srcIp = ntohl(srcAddrIn->sin_addr.s_addr);
    uint16_t srcPort = ntohs(srcAddrIn->sin_port);
 
    const struct sockaddr_in *dstAddrIn = (const struct sockaddr_in *)dstAddr;
    uint32_t dstIp = ntohl(dstAddrIn->sin_addr.s_addr);
    if (dstIp == 0) {
        KNET_ERR("The dst ip is 0 in rx hash, srcIp %x, srcPort %u", srcIp, srcPort);
        return -1;
    }
    uint16_t dstPort = ntohs(dstAddrIn->sin_port);
    KNET_DpdkNetdevCtx *dpdkNetdevCtx = (KNET_DpdkNetdevCtx *)ctx;
    uint16_t portId = dpdkNetdevCtx->xmitPortId;
 
    int qid = DpdkGetQueId(portId, srcIp, dstIp, srcPort, dstPort);
    if (qid < g_qid) {
        KNET_ERR("K-NET get queue id failed, srcIp %x, srcPort %u, dstIp %x, dstPort %u, qid %d, g_qid %d",
            srcIp, srcPort, dstIp, dstPort, qid, g_qid);
        return -1;
    }
    KNET_DEBUG("K-NET get queue id success, srcIp %x, srcPort %u, dstIp %x, dstPort %u, qid %d, g_qid %d",
        srcIp, srcPort, dstIp, dstPort, qid, g_qid);
    return qid - g_qid;
}

int KNET_ACC_TxBurst(void *ctx, uint16_t queueId, void **buf, int cnt)
{
    if (unlikely(cnt <= 0 || cnt > MAX_TX_BURST_NUM)) {
        KNET_ERR("Cnt %u less than 0 or larger than %u", cnt, MAX_TX_BURST_NUM);
        return -1;
    }

    uint16_t queId = queueId + g_qid;
    KNET_DpdkNetdevCtx *dpdkNetdevCtx = (KNET_DpdkNetdevCtx *)ctx;
    uint16_t portId = dpdkNetdevCtx->xmitPortId;
    struct rte_mbuf *rteMbuf[MAX_TX_BURST_NUM];

    for (int j = 0; j < cnt; j++) {
        rteMbuf[j] = KNET_DpMbuf2rteMbuf(buf[j]);
    }
    int total = KNET_TxBurst(queId, rteMbuf, cnt, portId);
    if (unlikely(total < cnt)) {
        // pbuf的计数在转换时已经减去，这里不用重复操作
        KnetDropPkt(&buf[total], cnt - total);
        KNET_LOG_LINE_LIMIT(KNET_LOG_WARN, "Tx burst total %d of cnt %d", total, cnt);
    }

    return total;
}

int KNET_ACC_RxBurst(void *ctx, uint16_t queueId, void **buf, int cnt)
{
    if (unlikely(cnt <= 0 || cnt > MAX_RX_BURST_NUM)) {
        KNET_ERR("Cnt %u less than 0 or larger than %u", cnt, MAX_RX_BURST_NUM);
        return -1;
    }

    uint16_t queId = queueId + g_qid;
    KNET_DpdkNetdevCtx *dpdkNetdevCtx = (KNET_DpdkNetdevCtx *)ctx;
    uint32_t portId = dpdkNetdevCtx->xmitPortId;
    struct rte_mbuf *rteMbuf[MAX_RX_BURST_NUM];

    int num = KNET_RxBurst(queId, rteMbuf, cnt, portId);
    for (int j = 0; j < num; j++) {
        buf[j] = KNET_RteMbuf2tcpMbuf(rteMbuf[j], portId);
        if (j + 1 < num) {
            rte_prefetch0(rteMbuf[j + 1]);
            rte_prefetch0(rteMbuf[j + 1] + KNET_PKT_DBG_SIZE + KNET_PKT_DBG_SIZE);
        }
    }

    return num;
}

int KnetSetDpCfg(void)
{
    DP_CfgKv_t cfg[] = {
        {DP_CFG_TYPE_SYS, DP_CFG_MBUF_MAX, KNET_GetCfg(CONF_TCP_MAX_MBUF)->intValue},
        {DP_CFG_TYPE_SYS, DP_CFG_WORKER_MAX, KNET_GetCfg(CONF_TCP_MAX_WORKER_NUM)->intValue},
        {DP_CFG_TYPE_SYS, DP_CFG_RT_MAX, KNET_GetCfg(CONF_TCP_MAX_ROUTE)->intValue},
        {DP_CFG_TYPE_SYS, DP_CFG_ARP_MAX, KNET_GetCfg(CONF_TCP_MAX_ARP)->intValue},
        {DP_CFG_TYPE_SYS, DP_CFG_TCPCB_MAX, KNET_GetCfg(CONF_TCP_MAX_TCPCB)->intValue},
        {DP_CFG_TYPE_SYS, DP_CFG_UDPCB_MAX, KNET_GetCfg(CONF_TCP_MAX_UDPCB)->intValue},
        {DP_CFG_TYPE_SYS, DP_CFG_CPD_PKT_TRANS, (KNET_GetCfg(CONF_HW_BIFUR_ENABLE)->intValue == BIFUR_ENABLE) ? 0 : 1},
        {DP_CFG_TYPE_SYS, DP_CFG_ZERO_COPY,  KNET_GetCfg(CONF_COMMON_ZERO_COPY)->intValue},
        {DP_CFG_TYPE_SYS, DP_CFG_ZBUF_LEN_MAX,  KNET_GetCfg(CONF_TCP_SGE_LEN)->intValue},
        {DP_CFG_TYPE_SYS, DP_CFG_DEPLOYMENT, KNET_GetCfg(CONF_COMMON_COTHREAD)->intValue},
        {DP_CFG_TYPE_SYS, DP_CFG_CPD_VCPU_NUM, KNET_GetCfg(CONF_COMMON_CTRL_VCPU_NUMS)->intValue},
        {DP_CFG_TYPE_SYS, DP_CFG_CPD_RING_PER_CPU_NUM, KNET_GetCfg(CONF_COMMON_CTRL_RING_PER_VCPU)->intValue},

        {DP_CFG_TYPE_TCP, DP_CFG_TCP_SELECT_ACK, KNET_GetCfg(CONF_TCP_TCP_SACK)->intValue},
        {DP_CFG_TYPE_TCP, DP_CFG_TCP_DELAY_ACK, KNET_GetCfg(CONF_TCP_TCP_DACK)->intValue},
        {DP_CFG_TYPE_TCP, DP_CFG_TCP_MSL_TIME, KNET_GetCfg(CONF_TCP_MSL_TIME)->intValue},
        {DP_CFG_TYPE_TCP, DP_CFG_TCP_FIN_TIMEOUT, KNET_GetCfg(CONF_TCP_FIN_TIMEOUT)->intValue},
        {DP_CFG_TYPE_TCP, DP_CFG_TCP_MIN_PORT, KNET_GetCfg(CONF_TCP_MIN_PORT)->intValue},
        {DP_CFG_TYPE_TCP, DP_CFG_TCP_MAX_PORT, KNET_GetCfg(CONF_TCP_MAX_PORT)->intValue},
        {DP_CFG_TYPE_TCP, DP_CFG_TCP_WMEM_MAX, KNET_GetCfg(CONF_TCP_MAX_SENDBUF)->intValue},
        {DP_CFG_TYPE_TCP, DP_CFG_TCP_WMEM_DEFAULT, KNET_GetCfg(CONF_TCP_DEF_SENDBUF)->intValue},
        {DP_CFG_TYPE_TCP, DP_CFG_TCP_RMEM_MAX, KNET_GetCfg(CONF_TCP_MAX_RECVBUF)->intValue},
        {DP_CFG_TYPE_TCP, DP_CFG_TCP_RMEM_DEFAULT, KNET_GetCfg(CONF_TCP_DEF_RECVBUF)->intValue},
        {DP_CFG_TYPE_TCP, DP_CFG_TCP_COOKIE, KNET_GetCfg(CONF_TCP_TCP_COOKIE)->intValue},
        {DP_CFG_TYPE_TCP, DP_CFG_TCP_SYNACK_RETRIES, KNET_GetCfg(CONF_TCP_SYNACK_RETRIES)->intValue},
        {DP_CFG_TYPE_TCP, DP_CFG_TCP_RND_PORT_STEP, KNET_GetCfg(CONF_INNER_PORT_STEP)->intValue},
        {DP_CFG_TYPE_TCP, DP_CFG_TCP_SNDBUF_PBUFCNT_MAX, TCP_PBUFCNT},

        {DP_CFG_TYPE_IP, DP_CFG_IP_REASS_MAX, KNET_GetCfg(CONF_TCP_REASS_MAX)->intValue},
        {DP_CFG_TYPE_IP, DP_CFG_IP_REASS_TIMEOUT, KNET_GetCfg(CONF_TCP_REASS_TIMEOUT)->intValue},
    };
    g_hwChecksumEnable = (KNET_GetCfg(CONF_HW_TCP_CHECKSUM)->intValue > 0) ? true : false;

    int ret;
    for (uint32_t i = 0; i < sizeof(cfg) / sizeof(cfg[0]); ++i) {
        KNET_INFO("Dp cfg index %u type %u, key %d, value %d", i, cfg[i].type, cfg[i].key, cfg[i].val);
        ret = DP_Cfg(&cfg[i], 1);
        if (ret != 0) {
            KNET_ERR("Set tcp cfg failed, ret %d, index %u, type %u, key %d, value %d",
                ret, i, cfg[i].type, cfg[i].key, cfg[i].val);
            return ret;
        }
    }

    g_qid = (uint16_t)KNET_GetCfg(CONF_INNER_QID)->intValue;
    return ret;
}
