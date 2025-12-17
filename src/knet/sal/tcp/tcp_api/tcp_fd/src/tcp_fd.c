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
#include "tcp_fd.h"

#include <sys/resource.h>

#include "knet_log.h"
#include "knet_config.h"

static struct KNET_Fd *g_fdMap = NULL;
static int g_fdMax = 0;

#define SOCKET_EXTRAS 1024

/* 内部接口，调用的地方保证多线程安全 */
void KNET_FdDeinit(void)
{
    free(g_fdMap);
    g_fdMap = NULL;
    g_fdMax = 0;
}

static void ResetFdMap(void)
{
    for (int i = 0; i < g_fdMax; i++) {
        g_fdMap[i].dpFd = KNET_INVALID_FD;
    }
}

/* 内部接口，调用的地方保证多线程安全 */
int KNET_FdInit(void)
{
    struct rlimit limit = {0};
    int32_t cfgValue;

    if (g_fdMax > 0) {
        KNET_DEBUG("Hijack module reinit");
        return 0;
    }

    // 由于socketfd会被大页等使用，设置内置偏移量SOCKET_EXTRAS，不会影响具体链接数量
    cfgValue = KNET_GetCfg(CONF_TCP_MAX_TCPCB)->intValue + KNET_GetCfg(CONF_TCP_MAX_UDPCB)->intValue
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

    g_fdMap = (struct KNET_Fd *)calloc(cfgValue, sizeof(struct KNET_Fd));
    if (g_fdMap == NULL) {
        KNET_ERR("Alloc fd mng failed!");
        return -1;
    }

    g_fdMax = cfgValue;
    (void)ResetFdMap();
    KNET_INFO("Fd max %d", g_fdMax);

    return 0;
}

inline bool KNET_IsFdValid(int osFd)
{
    return (osFd >= 0) && (osFd < g_fdMax);
}

int KNET_OsFdToDpFd(int osFd)
{
    return g_fdMap[osFd].dpFd;
}

union KNET_FdPrivateData *KNET_GetFdPrivateData(int osFd)
{
    return &g_fdMap[osFd].privateData;
}

bool KNET_IsFdHijack(int osFd)
{
    return KNET_IsFdValid(osFd) && g_fdMap[osFd].state == KNET_FD_STATE_HIJACK;
}

void KNET_SetFdStateAndType(enum KNET_FdState state, int osFd, int dpFd, enum KNET_FdType type)
{
    g_fdMap[osFd].state = state;
    g_fdMap[osFd].dpFd = dpFd;
    g_fdMap[osFd].fdType = type;
}

void KNET_SetFdSocketState(enum KNET_FdState state, int osFd, int dpFd)
{
    KNET_SetFdStateAndType(state, osFd, dpFd, KNET_FD_TYPE_SOCKET);
}

void KNET_ResetFdState(int osFd)
{
    g_fdMap[osFd].state = KNET_FD_STATE_INVALID;
    g_fdMap[osFd].dpFd = KNET_INVALID_FD;
    g_fdMap[osFd].fdType = KNET_FD_TYPE_INVALID;
    g_fdMap[osFd].privateData.epollData.notify = (DP_EpollNotify_t){NULL, NULL};
    g_fdMap[osFd].privateData.epollData.data = (struct KNET_EpollNotifyData){0, {0}};
    g_fdMap[osFd].establishedFdFlag = KNET_UNESTABLISHED_FD;
    g_fdMap[osFd].epFdHasOsFd = false;
    g_fdMap[osFd].tid = 0;
}

enum KNET_FdType KNET_GetFdType(int osFd)
{
    return g_fdMap[osFd].fdType;
}

int KNET_FdMaxGet(void)
{
    return g_fdMax;
}

void KNET_SetEstablishedFdState(int osFd)
{
    g_fdMap[osFd].establishedFdFlag = KNET_ESTABLISHED_FD;
}
 
int KNET_GetEstablishedFdState(int osFd)
{
    return g_fdMap[osFd].establishedFdFlag;
}

void KNET_SetEpollFdTid(int osFd, uint64_t tid)
{
    g_fdMap[osFd].tid = tid;
}

uint64_t KNET_GetEpollFdTid(int osFd)
{
    return g_fdMap[osFd].tid;
}

void KNET_EpHasOsFdSet(int osFd)
{
    g_fdMap[osFd].epFdHasOsFd = true; // 仅共线程操作
}

bool KNET_EpfdHasOsfdGet(int osFd)
{
    return g_fdMap[osFd].epFdHasOsFd;
}