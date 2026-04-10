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
#include <stdio.h>
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
#include <sys/mman.h>

#include "securec.h"
#include "knet_lock.h"
#include "common.h"
#include "mock.h"
#include "rte_config.h"
#include "rte_errno.h"
#include "rte_mempool.h"
#include "rte_mbuf.h"
#include "rte_malloc.h"
#include "securec.h"
#include "knet_mock.h"
#include "knet_types.h"
#include "knet_log.h"
#include "knet_pktpool.h"

#define INVALID_NUMAID (-2)
extern "C" {
KNET_LogLevel g_sdvLogLevel;
extern uint32_t PktPoolGetInSecondary(const char *name, uint32_t *poolId);
extern void PktPoolObjInitCallback(struct rte_mempool *mp, void *para, void *obj, unsigned index);
}

DTEST_CASE_F(MBUF, TEST_PKT_GET_MEM_POOL_NORMAL, NULL, NULL)
{
    struct rte_mempool *ret;

    ret = KnetPktGetMemPool(KNET_PKTPOOL_POOL_MAX_NUM);
    DT_ASSERT_EQUAL(ret, NULL);

    ret = KnetPktGetMemPool(0);
    DT_ASSERT_EQUAL(ret, NULL);
}

DTEST_CASE_F(MBUF, TEST_PKT_GET_POOL_CTRL_NORMAL, NULL, NULL)
{
    KnetPktPoolCtrl *ret;

    ret = KnetPktGetPoolCtrl(0);
    DT_ASSERT_EQUAL(ret, NULL);
}

DTEST_CASE_F(MBUF, TEST_PKTPOOL_CREATE_DESTROY_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_zmalloc, mock_rte_zmalloc);
    Mock->Create(rte_malloc, mock_rte_malloc);
    Mock->Create(rte_free, mock_rte_free);
    Mock->Create(rte_mempool_obj_iter, mock_rte_mempool_obj_iter);
    Mock->Create(rte_pktmbuf_pool_create_by_ops, mock_rte_pktmbuf_pool_create_by_ops);
    Mock->Create(rte_mempool_free, mock_rte_mempool_free);

    KNET_LogLevelSet(KNET_LOG_WARN);

    uint32_t ret = KNET_PktModInit();
    DT_ASSERT_EQUAL(ret, KNET_OK);
    KNET_INFO("knet pkt mod init success");

#define KNET_PKT_POOL_DEFAULT_BUFNUM 65536
#define KNET_PKT_POOL_DEFAULT_CACHENUM 128
#define KNET_PKT_POOL_DEFAULT_CACHESIZE 512
#define KNET_PKT_POOL_DEFAULT_PRIVATE_SIZE 256
#define KNET_PKT_POOL_DEFAULT_HEADROOM_SIZE 128
#define KNET_PKT_POOL_DEFAULT_CREATE_ALG KNET_PKTPOOL_ALG_RING_MP_MC

    KNET_PktPoolCfg pktPoolCfg = { 0 };
    (void)memcpy_s(pktPoolCfg.name, KNET_PKTPOOL_NAME_LEN, KNET_PKT_POOL_NAME, strlen(KNET_PKT_POOL_NAME));
    pktPoolCfg.bufNum = KNET_PKT_POOL_DEFAULT_BUFNUM;
    pktPoolCfg.cacheNum = KNET_PKT_POOL_DEFAULT_CACHENUM;
    pktPoolCfg.cacheSize = KNET_PKT_POOL_DEFAULT_CACHESIZE;
    pktPoolCfg.privDataSize = KNET_PKT_POOL_DEFAULT_PRIVATE_SIZE;
    pktPoolCfg.headroomSize = KNET_PKT_POOL_DEFAULT_HEADROOM_SIZE;
    pktPoolCfg.dataroomSize = KNET_PKT_POOL_DEFAULT_DATAROOM_SIZE;
    pktPoolCfg.createAlg = KNET_PKT_POOL_DEFAULT_CREATE_ALG;
    pktPoolCfg.numaId = -1;
    pktPoolCfg.init = NULL;

    ret = KNET_PktPoolCreate(NULL, NULL);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);

    uint32_t poolId;
    ret = KNET_PktPoolCreate(&pktPoolCfg, &poolId);
    DT_ASSERT_EQUAL(ret, KNET_OK);
    KNET_INFO("knet pktpool create success, poolId %u", poolId);

    KnetPktPoolCtrl *ctrl = KnetPktGetPoolCtrl(poolId);
    DT_ASSERT_NOT_EQUAL(ctrl, NULL);

    ret = KNET_PktPoolCreate(&pktPoolCfg, &poolId);
    DT_ASSERT_EQUAL(ret, KNET_OK);

    ret = KNET_PktPoolCreate(&pktPoolCfg, &poolId);
    DT_ASSERT_EQUAL(ret, KNET_OK);

    KNET_PktPoolDestroy(poolId);

    Mock->Delete(rte_zmalloc);
    Mock->Delete(rte_malloc);
    Mock->Delete(rte_free);
    Mock->Delete(rte_mempool_obj_iter);
    Mock->Delete(rte_pktmbuf_pool_create_by_ops);
    Mock->Delete(rte_mempool_free);

    DeleteMock(Mock);

#ifdef SDV_LOG_LEVEL
    KNET_LogLevelSet(g_sdvLogLevel);
#endif
}

DTEST_CASE_F(MBUF, TEST_PKTPOOL_CREATE_ABNORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_zmalloc, mock_rte_zmalloc);
    Mock->Create(rte_malloc, mock_rte_malloc);
    Mock->Create(rte_free, mock_rte_free);
    Mock->Create(rte_mempool_obj_iter, mock_rte_mempool_obj_iter);
    Mock->Create(rte_pktmbuf_pool_create_by_ops, mock_rte_pktmbuf_pool_create_by_ops);
    Mock->Create(rte_mempool_free, mock_rte_mempool_free);

    KNET_LogLevelSet(KNET_LOG_WARN);

    uint32_t ret = KNET_PktModInit();
    DT_ASSERT_EQUAL(ret, KNET_OK);
    KNET_INFO("knet pkt mod init success");

#define KNET_PKT_POOL_DEFAULT_BUFNUM 65536
#define KNET_PKT_POOL_DEFAULT_CACHENUM 128
#define KNET_PKT_POOL_DEFAULT_CACHESIZE 512
#define KNET_PKT_POOL_DEFAULT_PRIVATE_SIZE 256
#define KNET_PKT_POOL_DEFAULT_HEADROOM_SIZE 128
#define KNET_PKT_POOL_DEFAULT_CREATE_ALG KNET_PKTPOOL_ALG_RING_MP_MC

    KNET_PktPoolCfg pktPoolCfg = { 0 };
    (void)memcpy_s(pktPoolCfg.name, KNET_PKTPOOL_NAME_LEN, KNET_PKT_POOL_NAME, strlen(KNET_PKT_POOL_NAME));
    pktPoolCfg.bufNum = KNET_PKT_POOL_DEFAULT_BUFNUM;
    pktPoolCfg.cacheNum = KNET_PKT_POOL_DEFAULT_CACHENUM;
    pktPoolCfg.cacheSize = KNET_PKT_POOL_DEFAULT_CACHESIZE;
    pktPoolCfg.privDataSize = KNET_PKT_POOL_DEFAULT_PRIVATE_SIZE;
    pktPoolCfg.headroomSize = KNET_PKT_POOL_DEFAULT_HEADROOM_SIZE;
    pktPoolCfg.dataroomSize = KNET_PKT_POOL_DEFAULT_DATAROOM_SIZE;
    pktPoolCfg.createAlg = KNET_PKT_POOL_DEFAULT_CREATE_ALG;
    pktPoolCfg.numaId = -1;
    pktPoolCfg.init = NULL;

    uint32_t poolId = 0;
    Mock->Create(strlen, TEST_GetFuncRetPositive(0));
    ret = KNET_PktPoolCreate(&pktPoolCfg, &poolId);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);
    Mock->Delete(strlen);

    pktPoolCfg.bufNum = 0;
    ret = KNET_PktPoolCreate(&pktPoolCfg, &poolId);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);
    pktPoolCfg.bufNum = KNET_PKT_POOL_DEFAULT_BUFNUM;

    pktPoolCfg.cacheNum = 0;
    ret = KNET_PktPoolCreate(&pktPoolCfg, &poolId);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);
    pktPoolCfg.cacheNum = KNET_PKT_POOL_DEFAULT_CACHENUM;

    pktPoolCfg.cacheSize = 0;
    ret = KNET_PktPoolCreate(&pktPoolCfg, &poolId);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);
    pktPoolCfg.cacheSize = KNET_PKT_POOL_DEFAULT_CACHESIZE;

    pktPoolCfg.privDataSize = 0;
    ret = KNET_PktPoolCreate(&pktPoolCfg, &poolId);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);
    pktPoolCfg.privDataSize = KNET_PKT_POOL_DEFAULT_PRIVATE_SIZE;

    pktPoolCfg.headroomSize = 1;
    ret = KNET_PktPoolCreate(&pktPoolCfg, &poolId);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);
    pktPoolCfg.headroomSize = KNET_PKT_POOL_DEFAULT_HEADROOM_SIZE;

    pktPoolCfg.dataroomSize = 0;
    ret = KNET_PktPoolCreate(&pktPoolCfg, &poolId);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);
    pktPoolCfg.dataroomSize = KNET_PKT_POOL_DEFAULT_DATAROOM_SIZE;

    pktPoolCfg.createAlg = KNET_PKTPOOL_ALG_BUTT;
    ret = KNET_PktPoolCreate(&pktPoolCfg, &poolId);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);
    pktPoolCfg.createAlg = KNET_PKT_POOL_DEFAULT_CREATE_ALG;

    pktPoolCfg.numaId = INVALID_NUMAID;
    ret = KNET_PktPoolCreate(&pktPoolCfg, &poolId);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);
    pktPoolCfg.numaId = -1;

    Mock->Delete(rte_zmalloc);
    Mock->Delete(rte_malloc);
    Mock->Delete(rte_free);
    Mock->Delete(rte_mempool_obj_iter);
    Mock->Delete(rte_pktmbuf_pool_create_by_ops);
    Mock->Delete(rte_mempool_free);

    DeleteMock(Mock);

#ifdef SDV_LOG_LEVEL
    KNET_LogLevelSet(g_sdvLogLevel);
#endif
}

DTEST_CASE_F(MBUF, TEST_PKT_MOD_INIT_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    uint32_t ret = 0;

    Mock->Create(pthread_mutex_init, TEST_GetFuncRetPositive(1));
    ret = KNET_PktModInit();
    DT_ASSERT_EQUAL(ret, KNET_ERROR);
    Mock->Delete(pthread_mutex_init);

    DeleteMock(Mock);
}

DTEST_CASE_F(MBUF, TEST_PKT_GET_IN_SECONDARY_NORMAL, NULL, NULL)
{
    uint32_t ret;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_mempool_lookup, TEST_GetFuncRetPositive(1));

    char name[KNET_PKTPOOL_NAME_LEN] = "FMM";
    uint32_t poolId;

    ret = PktPoolGetInSecondary(name, &poolId);
    DT_ASSERT_EQUAL(ret, KNET_OK);

    Mock->Delete(rte_mempool_lookup);

    Mock->Create(rte_mempool_lookup, TEST_GetFuncRetPositive(0));
    ret = PktPoolGetInSecondary(name, &poolId);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);
    Mock->Delete(rte_mempool_lookup);
    
    DeleteMock(Mock);
}

DTEST_CASE_F(MBUF, TEST_PKT_POOL_OBJ_INIT_CALLBACK_NORMAL, NULL, NULL)
{
    PktPoolObjInitCallback(NULL, NULL, NULL, 0);
}