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

#include "dp_log_api.h"
#include "dp_show_api.h"
#include "dp_debug_api.h"

#include "knet_log.h"
#include "knet_symbols.h"
#include "knet_pkt.h"
#include "knet_io_init.h"
#include "knet_config.h"
#include "knet_thread.h"
#include "knet_sal_func.h"
#include "knet_sal_dp.h"

#define MAX_TX_BURST_NUM 1024
#define MAX_RX_BURST_NUM 1024

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
    DP_LogHook logHook = KNET_FixLenOutputHook;
    uint32_t ret = DP_LogHookReg(logHook);
    if (ret != 0) {
        KNET_ERR("Reg stack log failed, ret %d", ret);
        return KNET_ERROR;
    }
    DP_LogLevelSet(KnetGetStackLogLevel());

    ret = KNET_RegFunc();
    if (ret != KNET_OK) {
        KNET_ERR("Reg func failed, ret %d", ret);
        return KNET_ERROR;
    }

    ret = KNET_HandleInit();
    if (ret != KNET_OK) {
        KNET_ERR("Mem handle init failed, ret %d", ret);
        return KNET_ERROR;
    }

    /* dp workerId功能注册 */
    ret = (uint32_t)KNET_RegWorkderId();
    if (ret != 0) {
        KNET_ERR("RegWorkderId failed, ret %d", ret);
        return KNET_ERROR;
    }

    KNET_INFO("K-NET sal Init success");

    return KNET_OK;
}

/**
 * @brief 将 `DP_Pbuf_t` 的校验和卸载信息分配到 DPDK 的 `rte_mbuf` 中。
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
    mbuf->next = NULL;
}

static KNET_ALWAYS_INLINE struct rte_mbuf *KNET_DpMbuf2rteMbuf(DP_Pbuf_t *buf)
{
    DP_Pbuf_t *pbuf = buf;
    DP_Pbuf_t *nextPbuf = NULL;

    struct rte_mbuf *mbuf = KnetPkt2Mbuf(pbuf);
    struct rte_mbuf *headMbuf = mbuf;

    /**
    * 1、tx 方向，协议栈保证了单线程，发送和释放不并发；
    * 2、协议栈保证了整个pbuf链上的引用计数一致，所以使用同一个mbufRef；
    */
    DP_PBUF_SET_REF(buf, DP_PBUF_GET_REF(buf) - 1);
    uint16_t mbufRef = DP_PBUF_GET_REF(buf);

    // 首片转换
    PbufSeg2Mbuf(pbuf, mbuf, mbufRef);

    // 其它分片转换
    nextPbuf = DP_PBUF_GET_NEXT(pbuf);
    while (nextPbuf != NULL) {
        mbuf->next = KnetPkt2Mbuf(nextPbuf);
        mbuf = mbuf->next;
        pbuf = nextPbuf;
        PbufSeg2Mbuf(pbuf, mbuf, mbufRef);
        nextPbuf = DP_PBUF_GET_NEXT(pbuf);
    }

    if (g_hwChecksumEnable) {
        KnetMbufCksumOffloadInfoAssign(headMbuf, buf);
    }

    headMbuf->pkt_len = DP_PBUF_GET_TOTAL_LEN(buf);
    headMbuf->nb_segs = DP_PBUF_GET_SEG_NUM(buf);
    return headMbuf;
}

static KNET_ALWAYS_INLINE void MbufSeg2Pbuf(struct rte_mbuf *mbuf, DP_Pbuf_t *pbuf)
{
    (void)memset_s(pbuf, sizeof(DP_Pbuf_t), 0, sizeof(DP_Pbuf_t));

    DP_PBUF_SET_REF(pbuf, rte_mbuf_refcnt_read(mbuf));
    DP_PBUF_SET_SEG_NUM(pbuf, mbuf->nb_segs);
    DP_PBUF_SET_TOTAL_LEN(pbuf, mbuf->pkt_len);
    DP_PBUF_SET_PAYLOAD_LEN(pbuf, mbuf->buf_len);
    DP_PBUF_SET_PAYLOAD(pbuf, mbuf->buf_addr);
    DP_PBUF_SET_SEG_LEN(pbuf, mbuf->data_len);
    DP_PBUF_SET_OFFSET(pbuf, mbuf->data_off);
    DP_PBUF_SET_END(pbuf, pbuf);
    DP_PBUF_SET_NEXT(pbuf, NULL);
}

/**
 * @brief DPDK `rte_mbuf` 链表转换为 `DP_Pbuf_t` 链表结构。
 * @par 每个 `rte_mbuf` 会被转换为一个 `DP_Pbuf_t`，并组成单向链表返回。
 *
 * @param    rteMbuf [IN] 参数类型 #struct rte_mbuf *。要转换的 DPDK `rte_mbuf` 链表的头指针。
 * @param    portId  [IN] 参数类型 #uint32_t。与数据包相关联的端口 ID。
 *
 * @retval 返回转换后的 `DP_Pbuf_t` 链表的头指针。
 *         如果链表中的 `mbuf` 个数不足，函数内部会记录错误信息。
 */
static KNET_ALWAYS_INLINE DP_Pbuf_t *KNET_RteMbuf2dpMbuf(struct rte_mbuf *rteMbuf, uint32_t portId)
{
    struct rte_mbuf *mbuf = rteMbuf;
    struct rte_mbuf *nextMbuf = NULL;

    DP_Pbuf_t *pbuf = KnetMbuf2Pkt(mbuf);
    DP_Pbuf_t *headPbuf = pbuf;

    // 首片转换
    MbufSeg2Pbuf(mbuf, pbuf);

    // 其它分片转换
    nextMbuf = mbuf->next;
    while (nextMbuf != NULL) {
        DP_PBUF_SET_NEXT(pbuf, KnetMbuf2Pkt(nextMbuf));
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

static inline void KnetDropPkt(void **pkt, int bufCnt)
{
    DP_Pbuf_t *curBuf = NULL;
    void *nextBuf = NULL;
    for (int index = 0; index < bufCnt; index++) {
        curBuf = pkt[index];
        while (curBuf != NULL) {
            nextBuf = DP_PBUF_GET_NEXT(curBuf);
            KNET_PktFree(curBuf);
            curBuf = nextBuf;
        }
    }
}

int KNET_ACC_TxBurst(void *ctx, uint16_t queueId, void **buf, int cnt)
{
    if (unlikely(cnt <= 0 || cnt > MAX_TX_BURST_NUM)) {
        KNET_ERR("Cnt %u less than 0 or larger than %u", cnt, MAX_TX_BURST_NUM);
        return -1;
    }

    uint16_t queId = queueId + g_qid;
    DpdkNetdevCtx *dpdkNetdevCtx = (DpdkNetdevCtx *)ctx;
    uint16_t portId = dpdkNetdevCtx->portId;
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
    DpdkNetdevCtx *dpdkNetdevCtx = (DpdkNetdevCtx *)ctx;
    uint32_t portId = dpdkNetdevCtx->portId;
    struct rte_mbuf *rteMbuf[MAX_RX_BURST_NUM];

    int num = KNET_RxBurst(queId, rteMbuf, cnt, portId);
    for (int j = 0; j < num; j++) {
        buf[j] = KNET_RteMbuf2dpMbuf(rteMbuf[j], portId);
    }

    return num;
}

int KNET_SetDpCfg(void)
{
    int ret;
    DP_CfgKv_t cfg[] = {
        {DP_CFG_TYPE_SYS, DP_CFG_MBUF_MAX, KNET_GetCfg(CONF_DP_MAX_MBUF).intValue},
        {DP_CFG_TYPE_SYS, DP_CFG_WORKER_MAX, KNET_GetCfg(CONF_DP_MAX_WORKER_NUM).intValue},
        {DP_CFG_TYPE_SYS, DP_CFG_RT_MAX, KNET_GetCfg(CONF_DP_MAX_ROUTE).intValue},
        {DP_CFG_TYPE_SYS, DP_CFG_ARP_MAX, KNET_GetCfg(CONF_DP_MAX_ARP).intValue},
        {DP_CFG_TYPE_SYS, DP_CFG_TCPCB_MAX, KNET_GetCfg(CONF_DP_MAX_TCPCB).intValue},
        {DP_CFG_TYPE_SYS, DP_CFG_UDPCB_MAX, KNET_GetCfg(CONF_DP_MAX_UDPCB).intValue},
        {DP_CFG_TYPE_SYS, DP_CFG_CPD_PKT_TRANS, 1}, // 根据默认没有开启流量分叉配置

        {DP_CFG_TYPE_TCP, DP_CFG_TCP_SELECT_ACK, KNET_GetCfg(CONF_DP_TCP_SACK).intValue},
        {DP_CFG_TYPE_TCP, DP_CFG_TCP_DELAY_ACK, KNET_GetCfg(CONF_DP_TCP_DACK).intValue},
        {DP_CFG_TYPE_TCP, DP_CFG_TCP_MSL_TIME, KNET_GetCfg(CONF_DP_MSL_TIME).intValue},
        {DP_CFG_TYPE_TCP, DP_CFG_TCP_FIN_TIMEOUT, KNET_GetCfg(CONF_DP_FIN_TIMEOUT).intValue},
        {DP_CFG_TYPE_TCP, DP_CFG_TCP_MIN_PORT, KNET_GetCfg(CONF_DP_MIN_PORT).intValue},
        {DP_CFG_TYPE_TCP, DP_CFG_TCP_MAX_PORT, KNET_GetCfg(CONF_DP_MAX_PORT).intValue},
        {DP_CFG_TYPE_TCP, DP_CFG_TCP_WMEM_MAX, KNET_GetCfg(CONF_DP_MAX_SENDBUF).intValue},
        {DP_CFG_TYPE_TCP, DP_CFG_TCP_WMEM_DEFAULT, KNET_GetCfg(CONF_DP_DEF_SENDBUF).intValue},
        {DP_CFG_TYPE_TCP, DP_CFG_TCP_RMEM_MAX, KNET_GetCfg(CONF_DP_MAX_RECVBUF).intValue},
        {DP_CFG_TYPE_TCP, DP_CFG_TCP_RMEM_DEFAULT, KNET_GetCfg(CONF_DP_DEF_RECVBUF).intValue},
        {DP_CFG_TYPE_TCP, DP_CFG_TCP_COOKIE, KNET_GetCfg(CONF_DP_TCP_COOKIE).intValue},

        {DP_CFG_TYPE_IP, DP_CFG_IP_REASS_MAX, KNET_GetCfg(CONF_DP_REASS_MAX).intValue},
        {DP_CFG_TYPE_IP, DP_CFG_IP_REASS_TIMEOUT, KNET_GetCfg(CONF_DP_REASS_TIMEOUT).intValue},
    };
    g_hwChecksumEnable = (KNET_GetCfg(CONF_HW_TCP_CHECKSUM).intValue > 0) ? true : false;

    for (uint32_t i = 0; i < sizeof(cfg) / sizeof(cfg[0]); ++i) {
        KNET_INFO("Dp cfg index %u type %u, key %d, value %d", i, cfg[i].type, cfg[i].key, cfg[i].val);
        ret = DP_Cfg(&cfg[i], 1);
        if (ret != 0) {
            KNET_ERR("Set dp cfg faild, ret %d, index %u, type %u, key %d, value %d",
                ret, i, cfg[i].type, cfg[i].key, cfg[i].val);
            return ret;
        }
    }

    g_qid = (uint16_t)KNET_GetCfg(CONF_INNER_QID).intValue;
    return ret;
}
