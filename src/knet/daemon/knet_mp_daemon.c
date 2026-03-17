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
#include "knet_dpdk_init.h"
#include "knet_fmm.h"
#include "knet_config.h"
#include "knet_rpc.h"
#include "knet_log.h"
#include "knet_hash_table.h"
#include "knet_pdump.h"
#include "knet_telemetry.h"

#ifndef KNET_VERSION
#define KNET_VERSION "0"
#endif

bool KNET_IsMpDaemonInit(void)
{
    return true;
}

int DaemonInitPublicResource(void)
{
    int ret;

    ret = (int) KNET_InitDpdk(KNET_PROC_TYPE_PRIMARY, KNET_RUN_MODE_MULTIPLE);
    if (ret != 0) {
        KNET_ERR("K-NET init dpdk failed");
        return -1;
    }

    ret = KNET_InitHash(KNET_PROC_TYPE_PRIMARY);
    if (ret != 0) {
        KNET_UninitDpdk(KNET_PROC_TYPE_PRIMARY, KNET_RUN_MODE_MULTIPLE);
        KNET_ERR("K-NET init Hash failed");
        return -1;
    }

    ret = KNET_InitFmm(KNET_PROC_TYPE_PRIMARY);
    if (ret != 0) {
        KNET_UninitHash(KNET_PROC_TYPE_PRIMARY);
        KNET_UninitDpdk(KNET_PROC_TYPE_PRIMARY, KNET_RUN_MODE_MULTIPLE);
        KNET_ERR("K-NET init Fmm failed");
        return -1;
    }
    return 0;
}

int DaemonInitResource(void)
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

    KNET_LogLevelSetByStr(KNET_GetCfg(CONF_COMMON_LOG_LEVEL)->strValue);
    if (KNET_GetCfg(CONF_COMMON_MODE)->intValue != KNET_RUN_MODE_MULTIPLE) {
        KNET_ERR("K-NET conf is Single process");
        return -1;
    }

    if (KNET_GetCfg(CONF_INTERFACE_BOND_ENABLE)->intValue == 1) {
        KNET_ERR("K-NET multi-process mode not support bond, please not use bond or use the single-process mode");
        return -1;
    }

    ret = DaemonInitPublicResource();
    if (ret != 0) {
        KNET_ERR("K-NET init public resource failed");
        return -1;
    }

    /* 创建telemetry持久化线程 */
    ret = KNET_TelemetryStartPersistThread(KNET_PROC_TYPE_PRIMARY, KNET_RUN_MODE_MULTIPLE);
    if (ret != 0) {
        KNET_ERR("K-NET daemon init telemetry persist thread failed");
        return -1;
    }
    
    return 0;
}

int DaemonMainLooper(void)
{
    int ret;
    ret = KNET_RpcRun();
    if (ret != 0) {
        KNET_WARN("K-NET rpc run failed");
        return -1;
    }
    return 0;
}

int DaemonUninitPublicResource(void)
{
    int ret;
    int flag = 0;
    // 异常退出时，需要释放资源
    ret = (int) KNET_UninitDpdk(KNET_PROC_TYPE_PRIMARY, KNET_RUN_MODE_MULTIPLE);
    if (ret != 0) {
        flag = 1;
        KNET_WARN("K-NET uninit dpdk failed");
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

int DaemonUninitResource(void)
{
    int ret;
    int flag = 0;

    ret = DaemonUninitPublicResource();
    if (ret != 0) {
        flag = 1;
        KNET_WARN("K-NET uninit public resource failed");
    }

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
    ret = DaemonInitResource();
    if (ret != 0) {
        KNET_ERR("K-NET init resource failed");
        return -1;
    }

    KNET_INFO("Process Init started");

    int flag = 0; // 异常退出标记

    ret = DaemonMainLooper();
    if (ret != 0) {
        flag = 1;
        KNET_WARN("K-NET main looper failed");
    }

    ret = DaemonUninitResource();
    if (ret != 0) {
        flag = 1;
        KNET_WARN("K-NET uninit resource failed");
    }
    if (flag == 1) {
        return -1;
    }
    return 0;
}