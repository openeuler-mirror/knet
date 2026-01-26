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
#include "dp_init_api.h"
#include "dp_deinit.h"

#include "utils.h"
#include "utils_cfg.h"
#include "utils_log.h"
#include "utils_statistic.h"
#include "utils_mem_pool.h"
#include "shm.h"
#include "ns.h"
#include "worker.h"
#include "netdev.h"
#include "tbm.h"
#include "pmgr.h"
#include "sock.h"
#include "pbuf.h"
#include "dp_notify.h"
#include "dp_proto_init.h"
#include "dp_fd.h"

static int ResourceInit(int slave)
{
    (void)slave;

    if (UTILS_StatInit() != 0) {
        DP_LOG_ERR("Dp init statistic debug module failed.");
        return -1;
    }

    return 0;
}

static void ResourceDeinit(int slave)
{
    (void)slave;
    UTILS_StatDeinit();
}

static int NotifyInit(int slave)
{
    (void)slave;
    SOCK_NotifyFn_t notifyFns[] = {
        NULL,
        EPOLL_Notify,
        POLL_Notify,
        SELECT_Notify,
    };

    for (int i = 0; i < (int)DP_ARRAY_SIZE(notifyFns); i++) {
        if (notifyFns[i] == NULL) {
            continue;
        }
        if (SOCK_SetNotifyFn(i, notifyFns[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

static void ProtoDeinit(int slave)
{
    void (*deinitFns[])(int slave) = {
        ETH_Deinit,
        IP_Deinit,
        RAW_Deinit,
        UDP_Deinit,
        TCP_Deinit,
    };

    for (int i = 0; i < (int)DP_ARRAY_SIZE(deinitFns); i++) {
        if (deinitFns[i] == NULL) {
            continue;
        }
        deinitFns[i](slave);
    }
}

static int ProtoInit(int slave)
{
    int (*initFns[])(int slave) = {
        ETH_Init,
        IP_Init,
        RAW_Init,
        UDP_Init,
        TCP_Init,
    };

    for (int i = 0; i < (int)DP_ARRAY_SIZE(initFns); i++) {
        if (initFns[i] == NULL) {
            continue;
        }
        if (initFns[i](slave) != 0) {
            DP_LOG_ERR("DP_ProtoInit failed for initFns[%d].", i);
            ProtoDeinit(slave);
            return -1;
        }
    }
    return 0;
}

static int ShmMemInit(int slave)
{
    (void)slave;
    return SHM_MEM_INIT(slave);
}

static int ShmInit(int slave)
{
    (void)slave;
    return SHM_INIT(slave);
}

static int PbufMpInit(int slave)
{
    (void)slave;

    if ((UTILS_GetMpFunc()->mpCreate != NULL) && (PBUF_MpInit() != 0)) {
        DP_LOG_ERR("Dp init pbuf mempool failed with registered mpFunc.");
        return -1;
    }

    return 0;
}

static void PbufMpDeinit(int slave)
{
    (void)slave;
    PBUF_MpDeinit();  // 去初始化内存池需要全部资源释放之后
}

static int DpFuncInit(int slave)
{
    (void)slave;
    return UTILS_FuncInit();
}

static void DpFuncDeinit(int slave)
{
    (void)slave;
    UTILS_FuncDeInit();
}

static int DpWorkerInit(int slave)
{
    (void)slave;
    return WORKER_Init();
}

static void DpWorkerDeinit(int slave)
{
    (void)slave;
    WORKER_Deinit();
}

static void DpTbmDeinit(int slave)
{
    (void)slave;
    TBM_Deinit();
}

uint32_t g_threadId[DP_HIGHLIMIT_WORKER_MAX] = {0};
int      g_workerId[DP_HIGHLIMIT_WORKER_MAX] = {0};

// void DP_StopDriveWorker(int workerCnt)
// {
//     for (int i = 0; i < workerCnt; i++) {
//         DP_StopWorker(i);
//         (void)DP_ThreadJoin(g_threadId[i]);
//     }
//     (void)DP_ThreadDeinit();
//     WORKER_DeregGetWorkerId();
// }

// static void DP_ThreadWorkerRun(uintptr_t arg0, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3)
// {
//     (void)arg1;
//     (void)arg2;
//     (void)arg3;
//     DP_RunWorker((int)arg0);
//     return;
// }

// static int AutoDriveGetSelfId(void)
// {
//     uint32_t tid = DP_ThreadSelfId();  // 获取当前所在的tid
//     if (tid == DP_THREAD_INVALID_ID || tid > (uint32_t)CFG_GET_VAL(DP_CFG_WORKER_MAX)) {
//         return -1;
//     }

//     return g_workerId[(tid & (DP_HIGHLIMIT_WORKER_MAX - 1))];
// }

static int AutoDriveWorkerInit(int slave)
{
    (void)slave;
    if (CFG_GET_VAL(CFG_AUTO_DRIVE_WORKER) != DP_ENABLE) { // 仅处理使能自主调度线程驱动worker
        return 0;
    }

    // int workerCnt = CFG_GET_VAL(DP_CFG_WORKER_MAX);
    // if (DP_ThreadInit((uint16_t)workerCnt) != 0) {
    //     DP_LOG_ERR("Auto drive worker init dp thread failed.");
    //     return -1;
    // }

    // if (DP_RegGetSelfWorkerIdHook(AutoDriveGetSelfId) != 0) {  // 注册获取当前wid的钩子
    //     return -1;
    // }

    // DP_ThreadCreateParam_S param = {"worker", DP_THREAD_SCHED_OTHER, 0, 0, DP_ThreadWorkerRun, {0, 0, 0, 0}, 0};
    // for (int i = 0; i < workerCnt; i++) {
    //     param.args[0] = (uintptr_t)i;  // runworker的入参为当前workerId
    //     if (DP_ThreadCreate(&param, &g_threadId[i]) != 0) {
    //         DP_LOG_ERR("Auto drive worker create thread %d failed.", i);
    //         (void)DP_StopDriveWorker(i);
    //         return -1;
    //     }
    //     g_workerId[(g_threadId[i] & (DP_HIGHLIMIT_WORKER_MAX - 1))] = i;  // 保存当前tid对应的wid
    // }

    return 0;
}

typedef struct {
    int (*init)(int slave);
    void (*deinit)(int slave);
    const char *name;
} CONFIG_InitFns_t;

static const CONFIG_InitFns_t g_cfgInitFns[] = {
    {.init = DpFuncInit, .deinit = DpFuncDeinit, .name = "utils_func"},
    {.init = ShmMemInit, .deinit = NULL, .name = "shm_mem"},
    {.init = ShmInit, .deinit = NULL, .name = "shm"},
    {.init = ResourceInit, .deinit = ResourceDeinit, .name = "resource"},
    {.init = PbufMpInit, .deinit = PbufMpDeinit, .name = "pbuf_mempool"},
    {.init = NETDEV_Init, .deinit = NETDEV_Deinit, .name = "netdev"},
    {.init = TBM_Init, .deinit = DpTbmDeinit, .name = "tbm"},
    {.init = SOCK_Init, .deinit = SOCK_Deinit, .name = "sock"},
    {.init = NotifyInit, .deinit = NULL, .name = "notify"},
    {.init = INET_Init, .deinit = INET_Deinit, .name = "inet"},

#ifdef DPITF_NETLINK
    {.init = SOCK_InitNetlink, .deinit = NULL, .name = "netlink"},
#endif
    {.init = PMGR_Init, .deinit = PMGR_Deinit, .name = "pmgr"},
    {.init = ProtoInit, .deinit = ProtoDeinit, .name = "proto"},
    {.init = NS_Init, .deinit = NS_Deinit, .name = "ns"},
    {.init = DpWorkerInit, .deinit = DpWorkerDeinit, .name = "worker"},
    {.init = AutoDriveWorkerInit, .deinit = NULL, .name = "autodrive"},
};

int DP_Init(int slave)
{
    if (UTILS_IsCfgInited() != 0) {
        DP_LOG_ERR("Dp init failed, dp has inited already.");
        return -1;
    }

    int initFnsSize = (int)(sizeof(g_cfgInitFns) / sizeof(g_cfgInitFns)[0]);
    int ret = 0;
    int idx = 0;
    for (idx = 0; idx < initFnsSize; idx++) {
        if (g_cfgInitFns[idx].init != NULL) {
            ret = g_cfgInitFns[idx].init(slave);
        }
        if (ret != 0) {
            DP_LOG_ERR("Dp init module %s failed.", g_cfgInitFns[idx].name);
            goto deinit;
        }
    }

    UTILS_SetCfgInit(CFG_INITED);
    DP_LOG_INFO("Dp init successfully.");

    return 0;

deinit:
    for (idx = idx - 1; idx >= 0; idx--) {
        if (g_cfgInitFns[idx].deinit != NULL) {
            g_cfgInitFns[idx].deinit(slave);
        }
    }

    return ret;
}

void DP_Deinit(int slave)
{
    if (UTILS_IsCfgInited() != 1) {
        DP_LOG_WARN("Dp deinit without inited config.");
        return;
    }

    int initFnsSize = (int)(sizeof(g_cfgInitFns) / sizeof(g_cfgInitFns)[0]);

    for (int idx = initFnsSize - 1; idx >= 0; idx--) {
        if (g_cfgInitFns[idx].deinit != NULL) {
            g_cfgInitFns[idx].deinit(slave);
        }
    }

    UTILS_SetCfgInit(CFG_INITIAL);
}