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
#include "knet_dp_fd.h"

#include <sys/resource.h>

#include "knet_log.h"
#include "knet_config.h"

static struct KnetFd *g_fdMap = NULL;
static int g_fdMax = 0;

#define SOCKET_EXTRAS 1024

/* 内部接口，调用的地方保证多线程安全 */
void KnetFdDeinit(void)
{
    free(g_fdMap);
    g_fdMap = NULL;
    g_fdMax = 0;
}

/* 内部接口，调用的地方保证多线程安全 */
int KnetFdInit(void)
{
    struct rlimit limit = {0};
    int32_t cfgValue;

    if (g_fdMax > 0) {
        KNET_DEBUG("Hijack module reinit");
        return 0;
    }

    // 由于socketfd会被大页等使用，设置内置偏移量SOCKET_EXTRAS，不会影响具体链接数量
    cfgValue = KNET_GetCfg(CONF_DP_MAX_TCPCB).intValue + KNET_GetCfg(CONF_DP_MAX_UDPCB).intValue
        + SOCKET_EXTRAS;
    if (cfgValue <= 0) {
        KNET_ERR("Get config fd limit fail");
        return -1;
    }

    // 获取当前的资源 limit
    if (getrlimit(RLIMIT_NOFILE, &limit) != 0) {
        KNET_ERR("Get Linux fd limit fail");
        return -1;
    }
    if (limit.rlim_cur < (uint32_t)cfgValue) {
        KNET_ERR("Linux fd cur limit %u is small, please set 'ulimit -n %d' !", limit.rlim_cur, cfgValue);
        return -1;
    }

    g_fdMap = (struct KnetFd *)calloc(cfgValue, sizeof(struct KnetFd));
    if (g_fdMap == NULL) {
        KNET_ERR("Alloc fd mng failed!");
        return -1;
    }

    g_fdMax = cfgValue;
    KNET_INFO("Fd max %d", g_fdMax);

    return 0;
}

inline bool IsFdValid(int osFd)
{
    return (osFd >= 0) && (osFd < g_fdMax);
}

int OsFdToDpFd(int osFd)
{
    return g_fdMap[osFd].dpFd;
}

union FdPrivateData *GetFdPrivateData(int osFd)
{
    return &g_fdMap[osFd].privateData;
}

bool IsFdHijack(int osFd)
{
    return IsFdValid(osFd) && g_fdMap[osFd].state == FD_STATE_HIJACK;
}

void SetFdStateAndType(enum FdState state, int osFd, int dpFd, enum FdType type)
{
    g_fdMap[osFd].state = state;
    g_fdMap[osFd].dpFd = dpFd;
    g_fdMap[osFd].fdType = type;
}

void SetFdSocketState(enum FdState state, int osFd, int dpFd)
{
    SetFdStateAndType(state, osFd, dpFd, FD_TYPE_SOCKET);
}

void ResetFdState(int osFd)
{
    g_fdMap[osFd].state = FD_STATE_INVALID;
    g_fdMap[osFd].dpFd = INVALID_FD;
    g_fdMap[osFd].fdType = FD_TYPE_INVALID;
    g_fdMap[osFd].privateData.epollData.notify = (DP_EpollNotify_t){NULL, NULL};
    g_fdMap[osFd].privateData.epollData.data = (struct EpollNotifyData){0, {0}};
}

enum FdType GetFdType(int osFd)
{
    return g_fdMap[osFd].fdType;
}

int FdMaxGet(void)
{
    return g_fdMax;
}