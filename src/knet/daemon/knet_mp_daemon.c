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

#include <rte_config.h>
#include <rte_memzone.h>
#include <rte_memory.h>
#include <rte_ethdev.h>
#include <rte_pdump.h>
#include "securec.h"
#include "knet_thread.h"
#include "knet_io_init.h"
#include "knet_fmm.h"
#include "knet_config.h"
#include "knet_rpc.h"
#include "knet_log.h"
#include "knet_hash_rpc.h"
#include "knet_transmission.h"
#include "knet_pdump.h"
#include "knet_dpdk_telemetry.h"

#ifndef KNET_VERSION
#define KNET_VERSION "0"
#endif

const static struct rte_memzone *g_pdumpRequestMz = NULL;  // socket设计方案中pdump_request的共享内存

int KnetInitPublicResource(void)
{
    int ret;

    ret = KNET_InitTrans(KNET_PROC_TYPE_PRIMARY);
    if (ret != 0) {
        KNET_ERR("K-NET init Trans failed");
        return -1;
    }

    ret = KNET_InitHash(KNET_PROC_TYPE_PRIMARY);
    if (ret != 0) {
        KNET_ERR("K-NET init Hash failed");
        return -1;
    }

    ret = KNET_InitFmm(KNET_PROC_TYPE_PRIMARY);
    if (ret != 0) {
        KNET_ERR("K-NET init Fmm failed");
        return -1;
    }
    return 0;
}

int KnetInitResource(void)
{
    int ret;

    /* initialise the system */
    KNET_LogInit();

    printf("knet_mp_daemon version : %s\n", KNET_VERSION);
    KNET_LogNormal("knet_mp_daemon version : %s", KNET_VERSION);

    ret = KNET_InitCfg(KNET_PROC_TYPE_PRIMARY);
    if (ret != 0) {
        KNET_ERR("K-NET init cfg failed");
        return -1;
    }

    KNET_LogLevelConfigure();

    if (KNET_GetCfg(CONF_COMMON_MODE).intValue != KNET_RUN_MODE_MULTIPLE) {
        KNET_ERR("K-NET conf is Single process");
        return -1;
    }

    ret = KNET_CtrlVcpuCheck();
    if (ret != 0) {
        KNET_ERR("K-NET ctrl vcpu check failed");
        return -1;
    }

    ret = (int) KNET_InitDpdk(KNET_PROC_TYPE_PRIMARY, KNET_RUN_MODE_MULTIPLE);
    if (ret != 0) {
        KNET_ERR("K-NET init dpdk failed");
        return -1;
    }

    ret = KnetInitPublicResource();
    if (ret != 0) {
        KNET_ERR("K-NET init public resource failed");
        return -1;
    }
    ret = KNET_MultiPdumpInit(&g_pdumpRequestMz);
    if (ret != 0) {
        KNET_ERR("Multi pudmp memzone init failed");
        return -1;
    }

    return 0;
}

int KnetMainLooper(void)
{
    int ret;
    ret = KNET_RpcRun();
    if (ret != 0) {
        KNET_WARN("K-NET rpc run failed");
        return -1;
    }
    return 0;
}

int KnetUninitPublicResource(void)
{
    int ret;
    int flag;
    // 异常退出时，需要释放资源
    ret = KNET_UninitTrans(KNET_PROC_TYPE_PRIMARY);
    if (ret != 0) {
        flag = 1;
        KNET_WARN("K-NET uninit trans failed");
    }

    ret = KNET_UninitHash(KNET_PROC_TYPE_PRIMARY);
    if (ret != 0) {
        flag = 1;
        KNET_WARN("K-NET uninit hash failed");
    }

    ret = KNET_UnInitFmm(KNET_PROC_TYPE_PRIMARY);
    if (ret != 0) {
        flag = 1;
        KNET_WARN("K-NET uninit fmm failed");
    }

    if (flag == 1) {
        return -1;
    }
    return 0;
}

int KnetUninitResource(void)
{
    int ret;
    int flag;

    ret = KnetUninitPublicResource();
    if (ret != 0) {
        flag = 1;
        KNET_WARN("K-NET uninit public resource failed");
    }

    ret = (int) KNET_UninitDpdk(KNET_PROC_TYPE_PRIMARY, KNET_RUN_MODE_MULTIPLE);
    if (ret != 0) {
        flag = 1;
        KNET_WARN("K-NET uninit dpdk failed");
    }

    ret = (int) KNET_UninitDpdkTelemetry();
    if (ret != 0) {
        flag = 1;
        KNET_WARN("K-NET uninit dpdk telemetry failed, ret %d", ret);
    }
    ret = KNET_MultiPdumpUninit(g_pdumpRequestMz);
    if (ret != 0) {
        flag = 1;
        KNET_ERR("K-NET clean memzone failed");
    }
    g_pdumpRequestMz = NULL;
 
    KNET_LogUninit();

    if (flag == 1) {
        return -1;
    }
    return 0;
}

#ifndef KNET_MP_DAEMON
int main(int argc, char *argv[])
#else
int MainDaemon(int argc, char *argv[])
#endif
{
    int ret;
    /* initialise the system */
    ret = KnetInitResource();
    if (ret != 0) {
        KNET_ERR("K-NET init resource failed");
        return -1;
    }

    KNET_INFO("Process Init started");

    int flag = 0; // 异常退出标记

    ret = KnetMainLooper();
    if (ret != 0) {
        flag = 1;
        KNET_WARN("K-NET main looper failed");
    }

    ret = KnetUninitResource();
    if (ret != 0) {
        flag = 1;
        KNET_WARN("K-NET uninit resource failed");
    }
    if (flag == 1) {
        return -1;
    }
    return 0;
}