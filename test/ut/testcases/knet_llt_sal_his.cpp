/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 
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
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>

#include "rte_mbuf.h"
#include "rte_hash.h"
#include "rte_ethdev.h"
#include "stdarg.h"
#include "dp_log_api.h"
#include "dp_show_api.h"
#include "dp_pbuf_api.h"
#include "dp_cfg_api.h"
#include "dp_debug_api.h"
#include "knet_pkt.h"
#include "knet_lock.h"
#include "knet_types.h"
#include "knet_thread.h"
#include "knet_log.h"
#include "knet_transmission.h"
#include "knet_dpdk_init.h"
#include "knet_sal_func.h"
#include "knet_sal_tcp.h"
#include "knet_sal_inner.h"
#include "knet_sal_mp.h"
#include "securec.h"
#include "common.h"
#include "mock.h"

extern "C" {
    int DpdkGetQueId(uint16_t portId, uint32_t srcIp, uint32_t dstIp, uint16_t srcPort, uint16_t dstPort);
    int GetRetaSize(uint16_t portId);
}

#define MAX_SAL_NUM 1024
#define MAX_BUF_NUM 2048
extern KNET_LogLevel g_sdvLogLevel;
static union KNET_CfgValue g_cfg = {.intValue = 1};
static union KNET_CfgValue *MockKnetGetCfg(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    g_cfg.intValue = 1;
    return &g_cfg;
}

static union KNET_CfgValue *MockKnetGetCfg1(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    g_cfg.intValue = 2; // mock 2
    return &g_cfg;
}

static union KNET_CfgValue *MockKnetGetCfg256(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    g_cfg.intValue = 256; // 假设区间port的步长为256
    return &g_cfg;
}

DTEST_CASE_F(SAL_HIS, TEST_SAL_KNET_GET_STACK_LOG_LEVEL_NORMAL, NULL, NULL)
{
    uint32_t ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(DP_LogHookReg, TEST_GetFuncRetPositive(0));
    KNET_LogLevelSet(KNET_LOG_ERR);
    KNET_SAL_Init();

    KNET_LogLevelSet(KNET_LOG_WARN);
    KNET_SAL_Init();

    KNET_LogLevelSet(KNET_LOG_DEFAULT);
    KNET_SAL_Init();

    KNET_LogLevelSet(KNET_LOG_INFO);
    KNET_SAL_Init();

    KNET_LogLevelSet(KNET_LOG_DEBUG);
    KNET_SAL_Init();

    KNET_LogLevelSet(KNET_LOG_MAX);
    KNET_SAL_Init();

    Mock->Delete(DP_LogHookReg);
    DeleteMock(Mock);

#ifdef SDV_LOG_LEVEL
    KNET_LogLevelSet(g_sdvLogLevel);
#endif
}

static int MOCK_KNET_TxBurstSuccess(uint16_t queId, struct rte_mbuf **rteMbuf, int cnt, uint32_t portId)
{
    return cnt;
}
int DP_Cfg(DP_CfgKv_t* kv, int cnt);

// 依赖手动将最新libdpstack.so放到/usr/lib64
DTEST_CASE_F(SAL_HIS, TEST_SAL_HISINIT_NORMAL, NULL, NULL)
{
    uint32_t ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(DP_LogHookReg, TEST_GetFuncRetPositive(1));
    ret = KNET_SAL_Init();
    DT_ASSERT_EQUAL(ret, KNET_ERROR);

    Mock->Create(DP_LogHookReg, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetRegFunc, TEST_GetFuncRetPositive(1));
    ret = KNET_SAL_Init();
    DT_ASSERT_EQUAL(ret, KNET_ERROR);

    Mock->Create(KnetRegFunc, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetHandleInit, TEST_GetFuncRetPositive(1));
    ret = KNET_SAL_Init();
    DT_ASSERT_EQUAL(ret, KNET_ERROR);

    Mock->Create(KnetHandleInit, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetRegWorkderId, TEST_GetFuncRetPositive(1));
    ret = KNET_SAL_Init();
    DT_ASSERT_EQUAL(ret, KNET_ERROR);

    Mock->Create(KnetRegWorkderId, TEST_GetFuncRetPositive(0));
    ret = KNET_SAL_Init();
    DT_ASSERT_EQUAL(ret, KNET_OK);

    Mock->Delete(DP_LogHookReg);
    Mock->Delete(KnetRegFunc);
    Mock->Delete(KnetHandleInit);
    Mock->Delete(KnetRegWorkderId);
    DeleteMock(Mock);
}

#define POINTER_OFFSET 128
uintptr_t AlignUp(uintptr_t ptrAddr)
{
    return (ptrAddr + POINTER_OFFSET - 1) & ~(POINTER_OFFSET - 1);
}

DTEST_CASE_F(SAL_HIS, TEST_SAL_ACC_TX_NORMAL, NULL, NULL)
{
#define MBUF_CHAIN_CNT 2  // mbuf链中mbuf的个数
#define TX_BURST_CNT 1    // 要发送的MBUF链的个数
    int ret = 0;
    KNET_DpdkNetdevCtx dpCtx;
    dpCtx.xmitPortId = 1;
    void *ctx = &dpCtx;
    char mbuf[MBUF_CHAIN_CNT][4096 + 128] = {0};     // 打桩假设整个mbuf的大小为4096
    char expInfo[MBUF_CHAIN_CNT][4096 + 128] = {0};  // 打桩整个pstExpandInfo的大小为4096
    struct rte_mbuf *rteMbuf[MBUF_CHAIN_CNT] = {0};
    rteMbuf[0] = (struct rte_mbuf *)AlignUp((uintptr_t)mbuf[0]);
    rteMbuf[1] = (struct rte_mbuf *)AlignUp((uintptr_t)mbuf[1]);
    uint32_t totalPktLen = 0;
    DP_Pbuf_t *tcpMbuf[MBUF_CHAIN_CNT] = {0};
    for (int i = 0; i < MBUF_CHAIN_CNT; ++i) {
        tcpMbuf[i] = KNET_Mbuf2Pkt(rteMbuf[i]);
        tcpMbuf[i]->nsegs = MBUF_CHAIN_CNT;
        tcpMbuf[i]->segLen = i;  // 随意取一个长度，注意后续总长度不要溢出即可
        totalPktLen += tcpMbuf[i]->nsegs;
    }
    for (int i = 0; i < MBUF_CHAIN_CNT - 1; ++i) {
        tcpMbuf[i]->next = tcpMbuf[i + 1];
    }
    tcpMbuf[0]->totLen = totalPktLen;

    void **buf = tcpMbuf;
    int cnt = -1;
    uint16_t queId = 0;

    ret = KNET_ACC_TxBurst(ctx, queId, buf, cnt);
    DT_ASSERT_EQUAL(ret, -1);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_TxBurst, MOCK_KNET_TxBurstSuccess);

    cnt = TX_BURST_CNT;
    ret = KNET_ACC_TxBurst(ctx, queId, buf, cnt);
    DT_ASSERT_EQUAL(ret, cnt);

    /* 经过mbuf转换后，须确保以下字段正确 */
    DT_ASSERT_EQUAL(rteMbuf[0]->nb_segs, MBUF_CHAIN_CNT);
    DT_ASSERT_EQUAL(rteMbuf[0]->pkt_len, totalPktLen);

    Mock->Create(KNET_GetCfg, MockKnetGetCfg1);
    Mock->Create(DP_Cfg, TEST_GetFuncRetPositive(1));
    ret = KnetSetDpCfg();  // 测试MbufCheckSum、PbufCheckSum
    DT_ASSERT_NOT_EQUAL(ret, 0);
    ret = KNET_ACC_TxBurst(ctx, queId, buf, cnt);
    DT_ASSERT_EQUAL(ret, cnt);
    Mock->Delete(KNET_GetCfg);

    Mock->Delete(DP_Cfg);
    Mock->Delete(KNET_TxBurst);
    DeleteMock(Mock);
}

DTEST_CASE_F(SAL_HIS, TEST_SAL_ACC_RX_NORMAL, NULL, NULL)
{
    int ret = 0;
    KNET_DpdkNetdevCtx dpCtx;
    dpCtx.xmitPortId = 1;
    int arr[MAX_BUF_NUM] = {0};
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_RxBurst, TEST_GetFuncRetPositive(0));

    ret = KNET_ACC_RxBurst(&dpCtx, 0, (void **)&arr, -1);
    DT_ASSERT_EQUAL(ret, -1);

    ret = KNET_ACC_RxBurst(&dpCtx, 0, (void **)arr, MAX_BUF_NUM);
    DT_ASSERT_EQUAL(ret, -1);

    ret = KNET_ACC_RxBurst(&dpCtx, 0, (void **)arr, 1);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KNET_RxBurst);
    DeleteMock(Mock);
}

int KNET_RxBurstMock(uint16_t queId, struct rte_mbuf **rxBuf, int cnt, uint32_t portId)
{
    static char mbuf[4096] = {0};
    rxBuf[0] = (struct rte_mbuf *)mbuf;
    return 1;
}

DTEST_CASE_F(SAL_HIS, TEST_SAL_RTE_MBUF_TO_TCP_MBUF_NORMAL, NULL, NULL)
{
    int ret = 0;
    KNET_DpdkNetdevCtx dpCtx = {0};
    dpCtx.xmitPortId = 1;
    uint32_t mbuf_len = sizeof(struct rte_mbuf) + KNET_PKT_DBG_SIZE + sizeof(DP_Pbuf_t);
    struct rte_mbuf *mbuf = malloc(mbuf_len);
    memset(mbuf, 0, mbuf_len);
    DP_Pbuf_t *arr[MAX_BUF_NUM] = {KNET_Mbuf2Pkt(mbuf)};
    int cnt = 1;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_RxBurst, KNET_RxBurstMock);
    Mock->Create(memset_s, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_mbuf_refcnt_read, TEST_GetFuncRetPositive(0));

    ret = KNET_ACC_RxBurst(&dpCtx, 0, arr, cnt);
    DT_ASSERT_EQUAL(ret, cnt);

    Mock->Delete(rte_mbuf_refcnt_read);
    Mock->Delete(memset_s);
    Mock->Delete(KNET_RxBurst);
    DeleteMock(Mock);

    free(mbuf);
}

DTEST_CASE_F(SAL_HIS, TEST_SAL_KNET_ACC_TXHASH, NULL, NULL)
{
    KNET_DpdkNetdevCtx dpCtx;
    dpCtx.xmitPortId = 1;
    void *ctx = &dpCtx;

    struct sockaddr_in srcAddr;
    DP_Socklen_t srcAddrLen = sizeof(struct sockaddr_in);
    char* srcIp = "192.168.1.1"; // 假设原ip为192.168.1.1
    uint16_t srcPort = 8080;
    inet_pton(AF_INET, srcIp, &(srcAddr.sin_addr));
    srcAddr.sin_port = htons(srcPort);

    struct sockaddr_in dstAddr;
    DP_Socklen_t dstAddrLen = sizeof(struct sockaddr_in);
    char* dstIp = "0.0.0.0"; // 异常ip
    uint16_t dstPort = 8080;
    inet_pton(AF_INET, dstIp, &(dstAddr.sin_addr));
    dstAddr.sin_port = htons(dstPort);

    int ret = KNET_ACC_TxHash(ctx, (const DP_Sockaddr*)&srcAddr, srcAddrLen, (const DP_Sockaddr*)&dstAddr, dstAddrLen);
    DT_ASSERT_EQUAL(ret, -1);

    dstIp = "192.168.1.2"; // 假设目的ip为192.168.1.2
    inet_pton(AF_INET, dstIp, &(dstAddr.sin_addr));

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(DpdkGetQueId, TEST_GetFuncRetPositive(1));
    ret = KNET_ACC_TxHash(ctx, (const DP_Sockaddr*)&srcAddr, srcAddrLen, (const DP_Sockaddr*)&dstAddr, dstAddrLen);
    DT_ASSERT_EQUAL(ret, 1);
    Mock->Delete(DpdkGetQueId);

    Mock->Create(DpdkGetQueId, TEST_GetFuncRetNegative(1));
    ret = KNET_ACC_TxHash(ctx, (const DP_Sockaddr*)&srcAddr, srcAddrLen, (const DP_Sockaddr*)&dstAddr, dstAddrLen);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(DpdkGetQueId);

    DeleteMock(Mock);
}
int MockGetRetaSize(uint16_t portId)
{
    return RTE_ETH_RSS_RETA_SIZE_256; // sp卡reta为256
}
DTEST_CASE_F(SAL_HIS, TEST_SAL_DPDK_GET_QUEUEID, NULL, NULL)
{
    uint16_t portId = 0;
    uint32_t srcIp = 0x01020304;
    uint32_t dstIp = 0x05060708;
    uint16_t srcPort = 6380;
    uint16_t dstPort = 49152; // 此为假设通信的5元组

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfg256);
    Mock->Create(KNET_FindFdirQue, TEST_GetFuncRetNegative(1));
    Mock->Create(rte_eth_dev_rss_hash_conf_get, TEST_GetFuncRetNegative(1));
    int ret = DpdkGetQueId(portId, srcIp, dstIp, srcPort, dstPort);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(rte_eth_dev_rss_hash_conf_get);
    Mock->Delete(KNET_FindFdirQue);

    Mock->Create(KNET_FindFdirQue, TEST_GetFuncRetNegative(1));
    Mock->Create(rte_eth_dev_rss_hash_conf_get, TEST_GetFuncRetPositive(0));
    Mock->Create(GetRetaSize, MockGetRetaSize);
    Mock->Create(rte_eth_dev_rss_reta_query, TEST_GetFuncRetNegative(1));
    ret = DpdkGetQueId(portId, srcIp, dstIp, srcPort, dstPort);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(rte_eth_dev_rss_reta_query);
    Mock->Delete(GetRetaSize);
    Mock->Delete(rte_eth_dev_rss_hash_conf_get);
    Mock->Delete(KNET_FindFdirQue);

    Mock->Create(KNET_FindFdirQue, TEST_GetFuncRetNegative(1));
    Mock->Create(rte_eth_dev_rss_hash_conf_get, TEST_GetFuncRetPositive(0));
    Mock->Create(GetRetaSize, MockGetRetaSize);
    Mock->Create(rte_eth_dev_rss_reta_query, TEST_GetFuncRetPositive(0));
    ret = DpdkGetQueId(portId, srcIp, dstIp, srcPort, dstPort);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(rte_eth_dev_rss_reta_query);
    Mock->Delete(GetRetaSize);
    Mock->Delete(rte_eth_dev_rss_hash_conf_get);
    Mock->Delete(KNET_FindFdirQue);

    Mock->Create(KNET_FindFdirQue, TEST_GetFuncRetPositive(0));
    ret = DpdkGetQueId(portId, srcIp, dstIp, srcPort, dstPort);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_FindFdirQue);
    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

int rte_eth_dev_info_getMock(uint16_t port_id, struct rte_eth_dev_info *dev_info)
{
    dev_info->reta_size = RTE_ETH_RSS_RETA_SIZE_256;
    return 0;
}
/**
 * @brief KnetConfigureStackNetdev 获取指定端口的设备信息的RetaSize
 * 测试步骤：
 * 1.入参portId为0，预期结果为0
 */
DTEST_CASE_F(SAL_HIS, TEST_GET_TETA_SIZE, NULL, NULL)
{
    int ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_eth_dev_info_get, rte_eth_dev_info_getMock);
    ret = GetRetaSize(0);
    DT_ASSERT_EQUAL(ret, RTE_ETH_RSS_RETA_SIZE_256);
    Mock->Delete(rte_eth_dev_info_get);
    DeleteMock(Mock);
}