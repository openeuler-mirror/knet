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

#include <stdint.h>
#include "knet_atomic.h"
#include "knet_pkt.h"
#include "knet_sal_func.h"
#include "knet_rand.h"
#include "knet_transmission.h"
#include "knet_fmm.h"

#include "dp_mem_api.h"
#include "dp_mp_api.h"
#include "dp_rand_api.h"
#include "dp_show_api.h"
#include "dp_pbuf_api.h"
#include "dp_worker_api.h"
#include "dp_fib4tbl_api.h"
#include "dp_netdev_api.h"
#include "dp_addr_ext_api.h"
#include "dp_hashtbl_api.h"
#include "dp_debug_api.h"
#include "dp_clock_api.h"

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
#include <semaphore.h>
#include <signal.h>
#include <pthread.h>


#include "securec.h"
#include "knet_lock.h"
#include "knet_tcp_symbols.h"
#include "knet_hash_table.h"
#include "knet_sal_func.h"
#include "knet_sal_tcp.h"
#include "knet_dpdk_init.h"
#include "knet_sal_inner.h"
#include "common.h"
#include "mock.h"
#include "rte_hash.h"
#include "knet_mock.h"
#include "knet_tun.h"
#include "knet_sal_mp.h"
#include "knet_sal_func.h"
#include <dlfcn.h>

#define VALID_IP inet_addr("192.168.0.1")
#define VALID_PORT 8080
#define INVALID_TYPE 999
#define INVALID_PROTO (-1)

static union KNET_CfgValue g_cfg = {.intValue = 1};
static union KNET_CfgValue *MockKnetGetCfg(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    g_cfg.intValue = 1;
    return &g_cfg;
}

extern "C" {
extern int KnetConfigureStackNetdev(DP_Netdev_t *netdev, const char *ifname);
extern int KnetInitDpNetdev(void);
extern int KNET_ACC_HashTblDestroy(DP_HashTbl_t hTableId);
extern int KNET_ACC_HashTblInsertEntry(DP_HashTbl_t hTableId, const uint8_t *pu8Key, const void *pData);
extern int KNET_ACC_HashTblModifyEntry(DP_HashTbl_t hTableId, const uint8_t *pu8Key, const void *pData);
extern int KNET_ACC_HashTblDelEntry(DP_HashTbl_t hTableId, const uint8_t *pu8Key);
extern int KNET_ACC_HashTblLookupEntry(DP_HashTbl_t hTableId, const uint8_t *pu8Key, void *pData);
extern int KNET_ACC_HashTblEntryGetFirst(DP_HashTbl_t hTableId, uint8_t *pu8Key, void *pData);
extern int KNET_ACC_HashTblEntryGetNext(DP_HashTbl_t hTableId, uint8_t *pu8Key, void *pData, uint8_t *pu8NextKey);
extern uint32_t KNET_ACC_Rand(void);
extern uint32_t KNET_TimeHook(DP_ClockId_E clockId, int64_t *seconds, int64_t *nanoseconds);
extern int KNET_ACC_EventNotify(DP_AddrEventType_t type, const DP_AddrEvent_t *addrEvent);
extern int KNET_ACC_PreBind(void* userData, const struct DP_Sockaddr* addr, DP_Socklen_t addrlen);
extern int KNET_ACC_HashTblCreate(DP_HashTblCfg_t *pstHashTblCfg, DP_HashTbl_t *phTableId);
extern uint32_t SemWaitNonblock(DP_Sem_t sem);
extern uint32_t KNET_SemWaitHook(DP_Sem_t sem, int timeout);
extern uint32_t KNET_SemPostHook(DP_Sem_t sem);
extern int KNET_ACC_HashTblGetInfo(DP_HashTbl_t hTableId, DP_HashTblSummaryInfo_t *pstHashSummaryInfo);
extern int CheckFlowCfgValid(uint32_t dstIp, uint16_t dstPort, int32_t  proto);
extern int KNET_ACC_DelayInputEnque(void* pbuf, int cpdRingId);
extern int KnetSetInterFace(void);
extern int KnetSetDpRtCfg(void);
extern KNET_ACC_DelayInputDeque(void** pbuf, unsigned int burstSize, int cpdRingId);
int32_t MpHandleCtrlCreate(const DP_Mempool handler, const uint32_t type);
void MpHandleCtrlDestroy(const DP_Mempool handler);
extern int g_needFlowRule;
}

static int KNET_PktPoolCreateMock(const KNET_PktPoolCfg *cfg, uint32_t *poolId)
{
    if (poolId != NULL) {
        *poolId = 1;
    }
    return 0;
}

struct rte_mbuf mbuf_mock;

static struct rte_mbuf *KNET_PktAllocMock(uint32_t poolId)
{
    mbuf_mock.buf_addr = NULL;
    mbuf_mock.buf_len = 0;
    mbuf_mock.refcnt = 1;
    return &mbuf_mock;
}

DTEST_CASE_F(SAL_FUN, TEST_SAL_REG_MEM_NORMAL, NULL, NULL)
{
    uint32_t ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(DP_MemHookReg, TEST_GetFuncRetPositive(0));

    ret = KnetRegMem();
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(DP_MemHookReg);
    DeleteMock(Mock);
}

DTEST_CASE_F(SAL_FUN, TEST_SAL_REG_MBUF_NORMAL, NULL, NULL)
{
    uint32_t ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(DP_MempoolHookReg, TEST_GetFuncRetPositive(0));

    ret = KnetRegMbufMemPool();
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(DP_MempoolHookReg);
    DeleteMock(Mock);
}

KNET_STATIC int32_t MpHandleCtrlCreate(const DP_Mempool handler, const uint32_t type);
KNET_STATIC void MpHandleCtrlDestroy(const DP_Mempool handler);

DTEST_CASE_F(SAL_FUN, TEST_SAL_ACC_CREATE_NORMAL, NULL, NULL)
{
    int32_t ret = 0;
    int bufsize = 32;
    DP_MempoolCfg_S cfg = {0};
    cfg.type = DP_MEMPOOL_TYPE_PBUF;
    cfg.name = "cfg";
    DP_Mempool handler = {0};

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(strncpy_s, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_PktPoolCreate, TEST_GetFuncRetPositive(1));
    Mock->Create(rte_mempool_lookup, mock_rte_mempool_lookup);
    ret = KNET_ACC_CreateMbufMemPool(&cfg, NULL, NULL);
    DT_ASSERT_EQUAL(ret, -1);

    Mock->Create(KNET_PktPoolCreate, KNET_PktPoolCreateMock);
    ret = KNET_ACC_CreateMbufMemPool(&cfg, NULL, &handler);
    DT_ASSERT_EQUAL(ret, 0);
    
    Mock->Create(KNET_FmmCreatePool, TEST_GetFuncRetPositive(1));
    cfg.type = DP_MEMPOOL_TYPE_FIXED_MEM;
    ret = KNET_ACC_CreateMbufMemPool(&cfg, NULL, &handler);
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_FmmCreatePool);

    cfg.type = DP_MEMPOOL_TYPE_EBUF;
    cfg.size = bufsize;
    cfg.count = 1;
    Mock->Create(KNET_FmmCreatePool, TEST_GetFuncRetPositive(0));
    Mock->Create(MpHandleCtrlCreate, TEST_GetFuncRetPositive(0));
    ret = KNET_ACC_CreateMbufMemPool(&cfg, NULL, &handler);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(MpHandleCtrlCreate);
    Mock->Delete(KNET_PktPoolCreate);
    Mock->Delete(strncpy_s);
    Mock->Delete(rte_mempool_lookup);
    Mock->Delete(KNET_FmmCreatePool);
    DeleteMock(Mock);
}

DTEST_CASE_F(SAL_FUN, TEST_ACC_DESTROY_MBUF_MEMPOOL_NORMAL, NULL, NULL)
{
    uint32_t type = DP_MEMPOOL_TYPE_PBUF;
    DP_Mempool mp = {0};

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_PktPoolDestroy, TEST_GetFuncRetPositive(0));

    KNET_ACC_DestroyMbufMemPool(mp);
    DT_ASSERT_EQUAL(mp, NULL);

    Mock->Delete(KNET_PktPoolDestroy);
    DeleteMock(Mock);
}

uint32_t KNET_FmmAllocMock(uint32_t poolId, void **obj)
{
    static char buf[1024] = {0};
    *obj = buf;
    return KNET_OK;
}
/**
 * @brief KNET_ACC_MbufMemPoolAlloc 函数，内存池分配正常测试
 * 测试步骤：
 * 1.入参mp为0，预期结果为空
 * 2.入参mp为128，大于KNET_MEM_POOL_ID_OFFSET，
    打桩KNET_FmmAlloc提供足够地址操作空间，预期结果非空
 */
DTEST_CASE_F(SAL_FUN, TEST_ACC_MBUF_MEMPOOL_ALLOC_NORMAL, NULL, NULL)
{
    uint32_t type = DP_MEMPOOL_TYPE_PBUF;
    DP_Mempool mp = {0};

    DT_ASSERT_EQUAL(MpHandleCtrlCreate(mp, type), KNET_OK);
    void *retPtr = NULL;
    int poolIdGT32 = 128;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_PktAlloc, TEST_GetFuncRetPositive(0));
    retPtr = KNET_ACC_MbufMemPoolAlloc(mp);
    DT_ASSERT_EQUAL(retPtr, NULL);
    Mock->Delete(KNET_PktAlloc);

    Mock->Create(KNET_PktAlloc, KNET_PktAllocMock);
    retPtr = KNET_ACC_MbufMemPoolAlloc(mp);
    DT_ASSERT_NOT_EQUAL(retPtr, NULL);
    MpHandleCtrlDestroy(mp);

    mp = (void *)poolIdGT32;
    DT_ASSERT_EQUAL(MpHandleCtrlCreate(mp, type), KNET_OK);
    Mock->Create(KNET_FmmAlloc, KNET_FmmAllocMock);
    retPtr = KNET_ACC_MbufMemPoolAlloc(mp);
    DT_ASSERT_NOT_EQUAL(retPtr, NULL);
    MpHandleCtrlDestroy(mp);
    Mock->Delete(KNET_FmmAlloc);
    Mock->Delete(KNET_PktAlloc);
    DeleteMock(Mock);
}

void KNET_PktFreeMock(struct rte_mbuf *mbuf)
{
    DP_Pbuf_t *pbuf = KNET_Mbuf2Pkt(mbuf);
    pbuf->offset = 0;
}

DTEST_CASE_F(SAL_FUN, TEST_ACC_MBUF_MEMPOOL_FREE_NORMAL, NULL, NULL)
{
    uint32_t type = DP_MEMPOOL_TYPE_PBUF;
    DP_Mempool mp = {0};
    DT_ASSERT_EQUAL(MpHandleCtrlCreate(mp, type), KNET_OK);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    uint32_t mbuf_len = sizeof(struct rte_mbuf) + KNET_PKT_DBG_SIZE + sizeof(DP_Pbuf_t);
    struct rte_mbuf *mbuf = malloc(mbuf_len);
    memset(mbuf, 0, mbuf_len);
    DP_Pbuf_t *pbuf = KNET_Mbuf2Pkt(mbuf);
    pbuf->ref = 1;
    pbuf->offset = 1;

    Mock->Create(KNET_PktFree, KNET_PktFreeMock);

    KNET_ACC_MbufMemPoolFree(0, pbuf);
    DT_ASSERT_EQUAL(pbuf->offset, 0);

    MpHandleCtrlDestroy(mp);
    Mock->Delete(KNET_PktFree);

    free(mbuf);

    DeleteMock(Mock);
}

// 模拟的句柄
typedef struct {
    void* handle;
    int ref_count;
} Handle;

static Handle g_handles[10] = {0};
static int g_handle_index = 0;

// dlopen 打桩
void* dlopen_mock(const char* filename, int flag) {
    if (g_handle_index >= 10) {
        return NULL;
    }
    Handle* handle = &g_handles[g_handle_index];
    handle->handle = (void*)(uintptr_t)(0x1000 + g_handle_index);
    handle->ref_count = 1;
    g_handle_index++;
    return handle->handle;
}

// dlsym 打桩
void* dlsym_mock(void* handle, const char* symbol) {
    return 0xdeadbeaf;
}

// dlclose 打桩
int dlclose_mock(void* handle) {
    for (int i = 0; i < g_handle_index; i++) {
        if (g_handles[i].handle == handle) {
            g_handles[i].ref_count--;
            if (g_handles[i].ref_count <= 0) {
                // 模拟释放资源
                g_handles[i].handle = NULL;
            }
            return 0;
        }
    }
    return -1;
}

DTEST_CASE_F(SAL_FUN, TEST_SYMBOLS_INIT_DEINIT, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(dlopen, dlopen_mock);
    Mock->Create(dlsym, dlsym_mock);
    Mock->Create(dlclose, dlclose_mock);


    int ret = KnetInitDpSymbols();
    // 检查符号是否正确初始化
    DT_ASSERT_EQUAL(ret, 0);
    KnetDeinitDpSymbols();

    Mock->Delete(dlclose);
    Mock->Delete(dlsym);
    Mock->Delete(dlopen);
    DeleteMock(Mock);
    /* 真正dlopen */
    KnetInitDpSymbols();
    KnetRegRand();
}

DTEST_CASE_F(SAL_FUN, TEST_SAL_ACC_RAND_NORMAL, NULL, NULL)
{
    uint32_t ret = 0;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KNET_GetRandomNum, TEST_GetFuncRetNegative(1));
    ret = KNET_ACC_Rand();
    DT_ASSERT_EQUAL(ret, KNET_INVALID_RAND);

    Mock->Create(KNET_GetRandomNum, TEST_GetFuncRetPositive(0));
    ret = KNET_ACC_Rand();
    DT_ASSERT_EQUAL(ret, KNET_INVALID_RAND);

    Mock->Delete(KNET_GetRandomNum);
    DeleteMock(Mock);
}

void SalCallBackTest(void)
{
    printf("CallBackTest\n");
    return;
}

DTEST_CASE_F(SAL_FUN, TEST_SAL_HANDLE_INIT_NORMAL, NULL, NULL)
{
    uint32_t ret = 0;
    ret = KnetHandleInit();
    DT_ASSERT_EQUAL(ret, KNET_OK);
}

DTEST_CASE_F(SAL_FUN, TEST_SAL_REG_FUNC_NORMAL, NULL, NULL)
{
    uint32_t ret = 0;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KnetRegMem, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetRegMbufMemPool, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetRegRand, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetRegTime, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetRegHashTable, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetRegDebug, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetRegFdir, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetRegBind, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetRegSem, TEST_GetFuncRetPositive(0));
    Mock->Create(KnetRegDelayCpd, TEST_GetFuncRetPositive(0));
    ret = KnetRegFunc();
    DT_ASSERT_EQUAL(ret, KNET_OK);
    Mock->Delete(KnetRegDelayCpd);
    Mock->Delete(KnetRegSem);
    Mock->Delete(KnetRegBind);
    Mock->Delete(KnetRegFdir);
    Mock->Delete(KnetRegDebug);
    Mock->Delete(KnetRegHashTable);
    Mock->Delete(KnetRegTime);
    Mock->Delete(KnetRegRand);
    Mock->Delete(KnetRegMbufMemPool);
    Mock->Delete(KnetRegMem);

    DeleteMock(Mock);
}

DTEST_CASE_F(SAL_FUN, TEST_SAL_REG_WORKDER_ID, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(DP_RegGetSelfWorkerIdHook, TEST_GetFuncRetPositive(0));
    int32_t ret = KnetRegWorkderId();
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(DP_RegGetSelfWorkerIdHook);

    DeleteMock(Mock);
}

DTEST_CASE_F(SAL_FUN, TEST_SAL_KNET_TIME_HOOK_NORMAL, NULL, NULL)
{
    uint32_t ret = 0;
    int64_t seconds;
    int64_t nanoseconds;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    ret = KNET_TimeHook(DP_CLOCK_REALTIME, &seconds, &nanoseconds);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);

    ret = KNET_TimeHook(DP_CLOCK_MONOTONIC_COARSE, NULL, &nanoseconds);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);

    ret = KNET_TimeHook(DP_CLOCK_MONOTONIC_COARSE, &seconds, NULL);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);

    Mock->Create(clock_gettime, TEST_GetFuncRetPositive(1));
    ret = KNET_TimeHook(DP_CLOCK_MONOTONIC_COARSE, &seconds, &nanoseconds);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);
    Mock->Delete(clock_gettime);

    ret = KNET_TimeHook(DP_CLOCK_MONOTONIC_COARSE, &seconds, &nanoseconds);
    DT_ASSERT_EQUAL(ret, KNET_OK);

    DeleteMock(Mock);
}
/**
 * @brief KNET_ACC_EventNotify 函数，事件通知测试
 * 测试步骤：
 * 入参type均为 DP_ADDR_EVENT_CREATE
 * 1.入参addrEvent为NULL，预期结果为0
 * 2.构造入参addrEvent，打桩流标操作和事件通知，预期结果为0
 */
DTEST_CASE_F(SAL_FUN, TEST_SAL_KNET_ACC_EVENT_NOTIFY_ABNORMAL, NULL, NULL)
{
    int ret = 0;
    g_needFlowRule = 0;
    ret = KNET_ACC_EventNotify(DP_ADDR_EVENT_CREATE, NULL);
    DT_ASSERT_EQUAL(ret, 0);

    struct sockaddr_in AddrIn = {0};
    AddrIn.sin_family = AF_INET;
    AddrIn.sin_port = htons(VALID_PORT);
    AddrIn.sin_addr.s_addr = htonl(0xC0A80101);  // 对应192.168.1.1
    DP_AddrEvent_t addrEvent;
    addrEvent.localAddr = (DP_Sockaddr *)&AddrIn;
    addrEvent.portMask = 0xFF00;
    addrEvent.protocol = 0x06;
    for (int i = 0; i < DP_ADDR_QUE_MAP_MAX; ++i) {  // 覆盖SetFdirDpQueInfo
        addrEvent.queMap[i] = i;
    }

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    Mock->Create(KNET_IsNeedFlowRule, TEST_GetFuncRetPositive(1));
    Mock->Create(KNET_EventNotify, TEST_GetFuncRetPositive(0));
    ret = KNET_ACC_EventNotify(DP_ADDR_EVENT_CREATE, &addrEvent);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(KNET_GetCfg);
    Mock->Delete(KNET_EventNotify);
    Mock->Delete(KNET_IsNeedFlowRule);

    DeleteMock(Mock);
}

DTEST_CASE_F(SAL_FUN, TEST_ACC_PRE_BIND_UN_NORMAL, NULL, NULL)
{
    void *userData = 0;
    struct DP_Sockaddr addr;
    DP_Socklen_t addrlen = sizeof(struct DP_Sockaddr);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    int ret = KNET_ACC_PreBind(userData, &addr, addrlen);
    DT_ASSERT_EQUAL(ret, -1);

    userData = 1;
    ret = KNET_ACC_PreBind(userData, NULL, addrlen);
    DT_ASSERT_EQUAL(ret, -1);

    ret = KNET_ACC_PreBind(userData, &addr, 0);
    DT_ASSERT_EQUAL(ret, -1);

    DeleteMock(Mock);
}

DTEST_CASE_F(SAL_FUN, TEST_ACC_HASH_TBL_CREATE, NULL, NULL)
{
    DP_HashTblCfg_t pstHashTblCfg;
    DP_HashTbl_t phTableId;

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    int ret = KNET_ACC_HashTblCreate(NULL, NULL);
    DT_ASSERT_NOT_EQUAL(ret, 0);

    Mock->Create(KNET_CreateHashTbl, TEST_GetFuncRetPositive(0));
    ret = KNET_ACC_HashTblCreate(&pstHashTblCfg, &phTableId);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KNET_CreateHashTbl);
    DeleteMock(Mock);
}

DTEST_CASE_F(SAL_FUN, TEST_KNET_INIT_TCP_DEV, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(memcpy_s, TEST_GetFuncRetPositive(0));
    Mock->Create(DP_CreateNetdev, TEST_GetFuncRetPositive(1));
    Mock->Create(KnetConfigureStackNetdev, TEST_GetFuncRetPositive(0));
    int ret = KnetInitDpNetdev();
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(memcpy_s);
    Mock->Delete(DP_CreateNetdev);
    Mock->Delete(KnetConfigureStackNetdev);
    DeleteMock(Mock);
}

DTEST_CASE_F(SAL_FUN, TEST_SEM_WAIT_NONBLOCK, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(sem_trywait, TEST_GetFuncRetPositive(0));
    sem_t sem;
    uint32_t ret = SemWaitNonblock(&sem);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(sem_trywait);
    DeleteMock(Mock);
}

DTEST_CASE_F(SAL_FUN, TEST_SEM_POST, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(sem_trywait, TEST_GetFuncRetPositive(0));
    sem_t sem;
    uint32_t ret = KNET_SemWaitHook(&sem, 0);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(sem_trywait);
    DeleteMock(Mock);
}

DTEST_CASE_F(SAL_FUN, TEST_HASH_TBL_OP, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_DestroyHashTbl, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_HashTblAddEntry, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_HashTblModifyEntry, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_HashTblDelEntry, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_HashTblLookupEntry, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_GetHashTblFirstEntry, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_GetHashTblNextEntry, TEST_GetFuncRetPositive(0));

    uintptr_t hTableId;
    int ret = KNET_ACC_HashTblDestroy(hTableId);
    DT_ASSERT_EQUAL(ret, 0);

    ret = KNET_ACC_HashTblInsertEntry(hTableId, NULL, NULL);
    DT_ASSERT_EQUAL(ret, 0);

    ret = KNET_ACC_HashTblModifyEntry(hTableId, NULL, NULL);
    DT_ASSERT_EQUAL(ret, 0);

    ret = KNET_ACC_HashTblDelEntry(hTableId, NULL);
    DT_ASSERT_EQUAL(ret, 0);

    ret = KNET_ACC_HashTblLookupEntry(hTableId, NULL, NULL);
    DT_ASSERT_EQUAL(ret, 0);

    ret = KNET_ACC_HashTblEntryGetFirst(hTableId, NULL, NULL);
    DT_ASSERT_EQUAL(ret, 0);

    ret = KNET_ACC_HashTblEntryGetNext(hTableId, NULL, NULL, NULL);
    DT_ASSERT_EQUAL(ret, 0);

    Mock->Delete(KNET_GetHashTblNextEntry);
    Mock->Delete(KNET_GetHashTblFirstEntry);
    Mock->Delete(KNET_HashTblLookupEntry);
    Mock->Delete(KNET_HashTblDelEntry);
    Mock->Delete(KNET_HashTblModifyEntry);
    Mock->Delete(KNET_DestroyHashTbl);
    Mock->Delete(KNET_HashTblAddEntry);
    DeleteMock(Mock);
}
/**
 * @brief KNET_SemPostHook 函数，信号量发布钩子测试
 * 测试步骤：
 * 定义信号量入参sem，sem_post无法正常返回，打桩返回值为0，预期结果为0
 */
DTEST_CASE_F(SAL_FUN, TEST_SEM_POST_HOOK, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(sem_post, TEST_GetFuncRetPositive(0));
    sem_t sem;
    uint32_t ret = KNET_SemPostHook(&sem);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(sem_post);
    DeleteMock(Mock);
}

/**
 * @brief DP_SemHookReg 函数，信号量注册测试
 * 测试步骤：
 * 入参为空，DP_SemHookReg无法正常返回，打桩返回值为0，预期结果为0
 */
DTEST_CASE_F(SAL_FUN, TEST_KNET_REG_SEM, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(DP_SemHookReg, TEST_GetFuncRetPositive(0));
    uint32_t ret = KnetRegSem();
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(DP_SemHookReg);

    DeleteMock(Mock);
}

/**
 * @brief KNET_ACC_MbufConstruct 函数，内存块构造测试
 * 测试步骤：
 * 1.入参mp为0，入参addr为申请地址偏移512字节，确保地址操作不会溢出，
    入参offset和入参len测试数据为16
 * 2.KNET_PktAlloc打桩返回为空，预期结果为NULL
 * 3.KNET_PktAlloc打桩返回内存地址，预期结果非空
 */
DTEST_CASE_F(SAL_FUN, TEST_ACC_MBUF_CONSTRUCT, NULL, NULL)
{
    uint32_t type = DP_MEMPOOL_TYPE_PBUF;
    DP_Mempool mp = {0};
    DT_ASSERT_EQUAL(MpHandleCtrlCreate(mp, type), KNET_OK);

    void *retPtr = NULL;
    void *addr = malloc(1024);
    DT_ASSERT_NOT_EQUAL(addr, NULL);
    const int originOffset = 512;
    const int testOffset = 16;
    const int testLen = 16;
    
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_PktAlloc, TEST_GetFuncRetPositive(0));
    retPtr = KNET_ACC_MbufConstruct(mp, addr + originOffset, testOffset, testLen);
    DT_ASSERT_EQUAL(retPtr, NULL);
    Mock->Delete(KNET_PktAlloc);

    Mock->Create(KNET_PktAlloc, KNET_PktAllocMock);
    Mock->Create(KNET_MbufAttachExtBuf, TEST_GetFuncRetPositive(0));
    retPtr = KNET_ACC_MbufConstruct(mp, addr + originOffset, testOffset, testLen);
    DT_ASSERT_NOT_EQUAL(retPtr, NULL);
    Mock->Delete(KNET_PktAlloc);
    Mock->Delete(KNET_MbufAttachExtBuf);

    MpHandleCtrlDestroy(mp);

    DeleteMock(Mock);
    free(addr);
}

static int KNET_GetHashTblInfoMock(uint32_t tableId, KNET_HashTblInfo *info)
{
    if (info != NULL) {
        info->tableId = 0;
        info->pfHashFunc = NULL;
        info->keySize = 0;
        info->currEntryNum = 1;
        info->maxEntryNum = 1;
        info->entrySize = 1;
    }
    return 0;
}
/**
 * @brief KNET_ACC_HashTblGetInfo 函数，哈希表信息获取测试
 * 测试步骤：
 * 1.入参hTableId为0，入参pstHashSummaryInfo为空，参数非法，预期结果为KNET_ERROR
 * 2.入参hTableId为0，入参pstHashSummaryInfo非空，打桩填充结构体，预期结果为KNET_OK
 */
DTEST_CASE_F(SAL_FUN, TEST_ACC_HASHTB_GET_INFO, NULL, NULL)
{
    DP_HashTbl_t hTableId = 0;
    size_t ret = 0;
    DP_HashTblSummaryInfo_t *pstHashSummaryInfo = malloc(sizeof(DP_HashTblSummaryInfo_t));
    DT_ASSERT_NOT_EQUAL(pstHashSummaryInfo, NULL);
    ret = KNET_ACC_HashTblGetInfo(0, NULL);
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KNET_GetHashTblInfo, KNET_GetHashTblInfoMock);
    ret = KNET_ACC_HashTblGetInfo(0, pstHashSummaryInfo);
    DT_ASSERT_EQUAL(ret, KNET_OK);
    Mock->Delete(KNET_GetHashTblInfo);

    DeleteMock(Mock);
    free(pstHashSummaryInfo);
}

static union KNET_CfgValue KNET_GetCfgMock1(enum KNET_ConfKey key)
{
    union KNET_CfgValue ret = {0};
    if (key == CONF_COMMON_COTHREAD) {
        ret.intValue = 1;
    } else if (key == CONF_HW_BIFUR_ENABLE) {
        ret.intValue = KERNEL_FORWARD_ENABLE;
    }
    return ret;
}
/**
 * @brief CheckFlowCfgValid 函数，流配置有效性检查测试
 * 测试步骤：
 * 1.入参dstIp，入参dstPort，入参proto 均构造，满足检查条件，预期结果为0
 */
DTEST_CASE_F(SAL_FUN, TEST_CHECK_FLOW_CFG_VALID, NULL, NULL)
{
    uint32_t dstIp = VALID_IP;
    uint16_t dstPort = VALID_PORT;
    int32_t proto = IPPROTO_TCP;

    int ret = CheckFlowCfgValid(dstIp, dstPort, proto);
    DT_ASSERT_EQUAL(ret, 0);
}

void *KNET_GetDelayRxRingMock(int cpdRingId)
{
    static struct rte_ring ring = {0};
    return &ring;
}

/**
 * @brief KNET_ACC_DelayInputEnque 函数，延迟输入队列入队测试
 * 测试步骤：
 * 1.入参为空
 * 2.打桩ring操作函数，预期结果为0
 */
DTEST_CASE_F(SAL_FUN, TEST_ACC_DELAY_INPUT_ENQUE, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KNET_GetDelayRxRing, KNET_GetDelayRxRingMock);
    Mock->Create(rte_ring_enqueue_burst, TEST_GetFuncRetPositive(0));
    int ret = KNET_ACC_DelayInputEnque(NULL, 0);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(rte_ring_enqueue_burst);
    Mock->Delete(KNET_GetDelayRxRing);

    DeleteMock(Mock);
}
/**
 * @brief KNET_ACC_DelayInputDeque 函数，延迟输入队列出队测试
 * 测试步骤：
 * 1.入参为空
 * 2.打桩ring操作函数，预期结果为0
 */
DTEST_CASE_F(SAL_FUN, TEST_ACC_DELAY_INPUT_DEQUE, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KNET_GetDelayRxRing, KNET_GetDelayRxRingMock);
    Mock->Create(rte_ring_dequeue_burst, TEST_GetFuncRetPositive(0));
    int ret = KNET_ACC_DelayInputDeque(NULL, 0, 0);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(rte_ring_dequeue_burst);
    Mock->Delete(KNET_GetDelayRxRing);

    DeleteMock(Mock);
}

static union KNET_CfgValue *KNET_GetCfgMockBranch1(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    if (key == CONF_HW_BIFUR_ENABLE) {
        g_cfg.intValue = KERNEL_FORWARD_ENABLE;
    }
    return &g_cfg;
}
static union KNET_CfgValue *KNET_GetCfgMockBranch2(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    errno_t ret = strcpy_s(g_cfg.strValueArr[0], sizeof(g_cfg.strValueArr[0]), "0000:06:00.0");
    DT_ASSERT_EQUAL(ret, EOK);

    if (key == CONF_HW_BIFUR_ENABLE) {
        g_cfg.intValue = BIFUR_ENABLE;
    } else if (key == CONF_INNER_KERNEL_BOND_NAME) {
        g_cfg.strValue[0] = '\0';
    }
    return &g_cfg;
}

static union KNET_CfgValue *KNET_GetCfgMockBranch3(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    errno_t ret = strcpy_s(g_cfg.strValue, sizeof(g_cfg.strValue), "0000:06:00.0");
    DT_ASSERT_EQUAL(ret, EOK);

    if (key == CONF_HW_BIFUR_ENABLE) {
        g_cfg.intValue = BIFUR_ENABLE;
    }
    return &g_cfg;
}

static char *RealPathMock(const char *name, char *resolved)
{
    char path[] = "/sys/bus/pci/devices";
    size_t pathLen = strlen(path) + 1;
    if (resolved == NULL) {
        return NULL;
    }
    if (strncpy_s(resolved, pathLen, path, pathLen) != 0) { // resolved至少MAX_PATH字节
        return NULL;
    }
    return resolved;
}
/**
 * @brief KnetSetInterFace 函数，设置接口函数测试
 * 测试步骤：
 * 1.入参为空，BIFUR_ENABLE失效，创建TAP，预期结果为0
 * 2.入参为空，网口名为空，获取Ifname，打桩路径，目录类型错误，预期结果为-1
 * 3.入参为空，网口名非空，拷贝网口名，预期结果为0
 */
DTEST_CASE_F(SAL_FUN, TEST_KNET_SET_INTERFACE, NULL, NULL)
{
    int ret;
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(KNET_GetCfg, KNET_GetCfgMockBranch1);
    Mock->Create(KNET_TAPCreate, TEST_GetFuncRetPositive(0));
    ret = KnetSetInterFace();
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(KNET_GetCfg);

    Mock->Create(KNET_GetCfg, KNET_GetCfgMockBranch2);
    Mock->Create(KNET_FetchIfIndex, TEST_GetFuncRetPositive(0));
    Mock->Create(realpath, RealPathMock);
    ret = KnetSetInterFace();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(realpath);
    Mock->Delete(KNET_FetchIfIndex);
    Mock->Delete(KNET_GetCfg);

    Mock->Create(KNET_GetCfg, KNET_GetCfgMockBranch3);
    Mock->Create(KNET_FetchIfIndex, TEST_GetFuncRetPositive(0));
    ret = KnetSetInterFace();
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(KNET_FetchIfIndex);
    Mock->Delete(KNET_TAPCreate);
    Mock->Delete(KNET_GetCfg);

    DeleteMock(Mock);
}

/**
 * @brief KnetConfigureStackNetdev 函数，配置协议栈网络设备测试
 * 测试步骤：
 * 1.入参netdev分配内存，入参ifname构造，预期结果为0
 */
DTEST_CASE_F(SAL_FUN, TEST_KNET_CONFIG_STACK_DEV, NULL, NULL)
{
    DP_Netdev_t *netdev = malloc(256);
    DT_ASSERT_NOT_EQUAL(netdev, NULL);
    char ifname[] = "0000:06:00.0";
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(DP_ProcIfreq, TEST_GetFuncRetPositive(0));
    int ret = KnetConfigureStackNetdev(netdev, ifname);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(DP_ProcIfreq);
    DeleteMock(Mock);
    free(netdev);
}

static union KNET_CfgValue *KNET_GetCfgMockNetInfo(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    if (key == CONF_INTERFACE_IP) {
        g_cfg.intValue = inet_addr("192.168.0.2");
    } else if (key == CONF_INTERFACE_NETMASK) {
        g_cfg.intValue = 0xFFFFFF00;
    } else if (key == CONF_INTERFACE_GATEWAY) {
        g_cfg.intValue = VALID_IP;
    }
    return &g_cfg;
}
/**
 * @brief KnetSetDpRtCfg 函数，数据平面路由配置设置测试
 * 测试步骤：
 * 1.入参为空，打桩构造配置信息，预期结果为0
 */
DTEST_CASE_F(SAL_FUN, TEST_KNET_SET_DPRT_CFG, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, KNET_GetCfgMockNetInfo);
    Mock->Create(DP_RtCfg, TEST_GetFuncRetPositive(0));
    int ret = KnetSetDpRtCfg();
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(DP_RtCfg);
    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}
