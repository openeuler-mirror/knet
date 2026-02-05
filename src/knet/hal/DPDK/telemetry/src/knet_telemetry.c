/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: DPDK Telemetry 初始化
 */

#include <dirent.h>
#include <limits.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "cJSON.h"
#include "rte_memzone.h"

#include "knet_config.h"
#include "knet_log.h"
#include "knet_telemetry_call.h"
#include "knet_telemetry_debug.h"
#include "knet_types.h"

#include "knet_telemetry.h"
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief telemetry新增维测接口的枚举类型
 * @brief 新增维测接口需要新增枚举变量
 */
typedef enum {
    FLOWTABLE_CB,
    QUEUEID_MAP_PIDTID_CB,
    ETHDEV_USAGE_CB,
    EPOLL_DETAILS_CB,
    GET_FD_COUNT,
    GET_NET_STAT,
    GET_SOCK_INFO,
    MAX_CB_NUM
} KnetTelemetryCbsEnum;

typedef struct {
    telemetry_cb cb_single;    // 单进程回调
    telemetry_cb cb_multi;     // 多进程回调
    const char *registeredCmd; // telemetry命令
    const char *helpCmd;
} TelemetryCmdInfo;

static TelemetryCmdInfo g_telemetryCmdInfos[MAX_CB_NUM] = {
    [FLOWTABLE_CB] = {.cb_single = KnetTelemetryFlowTableCallback,
                      .cb_multi = KnetTelemetryFlowTableCallback,
                      .registeredCmd = "/knet/flow/list",
                      .helpCmd = "Return flowtable information. "
                                 "Usage: /knet/flow/list,<startIndex> <flowCount>"},
    [QUEUEID_MAP_PIDTID_CB] = {.cb_single = KnetTelemetryQueIdMapPidTidCallback,
                               .cb_multi = KnetTelemetryQueIdMapPidTidCallbackMp,
                               .registeredCmd = "/knet/ethdev/queue",
                               .helpCmd = "Return queueid map pid and tid information. "
                                          "Usage: /knet/ethdev/queue"},
    [ETHDEV_USAGE_CB] = {.cb_single = KnetTelemetryEthdevUsageCallback,
                         .cb_multi = KnetTelemetryEthdevUsageCallback,
                         .registeredCmd = "/knet/ethdev/usage",
                         .helpCmd = "Return ethdev real-time bandwidth. "
                                    "Usage: /knet/ethdev/usage,<port> <time>"},
    [EPOLL_DETAILS_CB] = {.cb_single = KnetTelemetryEpollDetailsCallback,
                          .cb_multi = KnetTelemetryEpollDetailsCallbackMp,
                          .registeredCmd = "/knet/stack/epoll_stat",
                          .helpCmd = "Return epoll detail statistics. "
                                     "Usage: /knet/stack/epoll_stat, <pid> <start_epoll_fd> "
                                     "<epoll_fd_cnt> <start_fd> <fd_cnt>"},
    [GET_FD_COUNT] = {.cb_single = KnetTelemetryGetFdCountCallback,
                      .cb_multi = KnetTelemetryGetFdCountCallbackMp,
                      .registeredCmd = "/knet/stack/fd_count",
                      .helpCmd = "Return fd count of input socket type. "
                                 "Usage: /knet/stack/fd_count,[pid] <tcp/udp/epoll>"},
    [GET_NET_STAT] = {.cb_single = KnetTelemetryGetNetStatCallback,
                      .cb_multi = KnetTelemetryGetNetStatCallbackMp,
                      .registeredCmd = "/knet/stack/net_stat",
                      .helpCmd = "Return all connected socket information. "
                                 "Usage: /knet/stack/net_stat,<pid> <start_fd> <fd_cnt>"},
    [GET_SOCK_INFO] = {.cb_single = KnetTelemetryGetSockInfoCallback,
                       .cb_multi = KnetTelemetryGetSockInfoCallbackMp,
                       .registeredCmd = "/knet/stack/socket_info",
                       .helpCmd = "Return socket details of input fd. "
                                  "Usage: /knet/stack/socket_info,[pid] <fd>"}};

#define KNET_DPDK_SOCKET_BUFFER_SIZE 1024

static char g_knetTelemetrySocket[PATH_MAX] = {0};
static char g_knetTelemetrySocketNew[PATH_MAX] = {0};

KNET_STATIC int32_t DpdkRuntimeDirInit(void)
{
    const char *runtimeDir = getenv("XDG_RUNTIME_DIR");
    /* 非 root 模式下，没有环境变量，默认异常 */
    if (getuid() != 0 && runtimeDir == NULL) {
        KNET_ERR("Common user failed to init DPDK without XDG_RUNTIME_DIR environment variable");
        return -1;
    }

    /* root 模式下, 走系统路径 */
    if (getuid() == 0) {
        runtimeDir = "/var/run";
    }

    /* 非root模式，有环境变量，使用环境变量路径 */
    int ret = snprintf_s(g_knetTelemetrySocket, PATH_MAX, PATH_MAX - 1, "%s/dpdk/knet/dpdk_telemetry.v2", runtimeDir);
    if (ret < 0) {
        KNET_ERR("Snprintf knet telemetry failed, ret %d", ret);
        return -1;
    }

    ret = snprintf_s(g_knetTelemetrySocketNew, PATH_MAX, PATH_MAX - 1, "%s/dpdk/knet/dpdk_telemetry.v2:1", runtimeDir);
    if (ret < 0) {
        KNET_ERR("Snprintf knet telemetryNew failed, ret %d", ret);
        return -1;
    }

    return 0;
}

KNET_STATIC bool DpdkTelemetryJsonPidCheck(const char *buffer)
{
    /* 解析 JSON 数据 */
    cJSON *json = cJSON_Parse(buffer);
    if (json == NULL) {
        KNET_ERR("K-NET dpdk telemetry parsing JSON failed");
        return false;
    }

    /* 获取 pid 项 */
    cJSON *pidItem = cJSON_GetObjectItem(json, "pid");
    if (pidItem == NULL || !cJSON_IsNumber(pidItem)) {
        KNET_ERR("K-NET dpdk telemetry get object item 'pid' failed, 'pid' not found or not a number");
        cJSON_Delete(json);
        return false;
    }

    /* 比较 pid */
    int pidFromJson = pidItem->valueint;
    pid_t currentPid = getpid();

    cJSON_Delete(json);

    return (pidFromJson == currentPid);
}

KNET_STATIC bool DpdkTelemetrySocketPidCheck(void)
{
    int sockfd = -1;
    struct sockaddr_un addr = {0};
    char buffer[KNET_DPDK_SOCKET_BUFFER_SIZE] = {0};
    ssize_t numBytes;

    sockfd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (sockfd == -1) {
        KNET_ERR("K-NET dpdk telemetry socket get sockfd failed");
        return false;
    }

    (void)memset_s(&addr, sizeof(addr), 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strncpy_s(addr.sun_path, sizeof(addr.sun_path), g_knetTelemetrySocket, strlen(g_knetTelemetrySocket)) != 0) {
        KNET_ERR("K-NET dpdk telemetry socket strncpy_s failed");
        close(sockfd);
        return false;
    }

    if (connect(sockfd, (void *)&addr, sizeof(addr)) == -1) {
        KNET_ERR("K-NET dpdk telemetry socket connect failed");
        close(sockfd);
        return false;
    }
    KNET_DEBUG("K-NET telemetry connect successfully, fd %d", sockfd);

    /* 从 socket 读取数据 */
    (void)memset_s(&buffer, sizeof(buffer), 0, sizeof(buffer));
    numBytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (numBytes == -1) {
        KNET_ERR("K-NET dpdk telemetry socket recv data failed");
        close(sockfd);
        return false;
    }

    if (!DpdkTelemetryJsonPidCheck(buffer)) {
        KNET_ERR("K-NET dpdk telemetry socket pid check init failed");
        close(sockfd);
        return false;
    }

    close(sockfd);
    return true;
}

/**
 * @brief 创建硬链接到 /var/run/dpdk/knet/ 目录中，适配 dpdk telemetry 中的 create_socket
 */
KNET_STATIC int32_t DpdkTelemetryLinkCreate(void)
{
    int sockfd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (sockfd < 0) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "K-NET dpdk telemetry socket create failed, sockfd %d", sockfd);
        return -1;
    }

    struct sockaddr_un addr = {0};
    (void)memset_s(&addr, sizeof(addr), 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    int32_t ret =
        strncpy_s(addr.sun_path, sizeof(addr.sun_path), g_knetTelemetrySocketNew, strlen(g_knetTelemetrySocketNew));
    if (ret != 0) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "K-NET dpdk telemetry socket strncpy_s failed, ret %d", ret);
        close(sockfd);
        return -1;
    }

    /* 检查当前 socket 是否处于活动状态 */
    if (connect(sockfd, (void *)&addr, sizeof(addr)) == 0) {
        KNET_INFO("Socket at path knet telemetry socket is already in use");
        close(sockfd);
        return -EADDRINUSE;
    }

    /* socket 未激活，删除并尝试重新创建 */
    KNET_LOG_LINE_LIMIT(KNET_LOG_INFO, "K-NET dpdk telemetry attempting unlink and retrying bind");

    char knetTelemetrySocketNewRealPath[PATH_MAX] = {0};
    if (realpath(g_knetTelemetrySocketNew, knetTelemetrySocketNewRealPath) != NULL) {
        unlink(knetTelemetrySocketNewRealPath);
    }

    char knetTelemetrySocketRealPath[PATH_MAX] = {0};
    if (realpath(g_knetTelemetrySocket, knetTelemetrySocketRealPath) == NULL) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "K-NET dpdk telemetry checkout knet dir failed, not real");
        close(sockfd);
        return -1;
    }

    ret = link(knetTelemetrySocketRealPath, knetTelemetrySocketNewRealPath);

    close(sockfd);

    return ret;
}

KNET_STATIC int32_t DpdkTelemetryFindSocket(void)
{
    struct stat fileStat = {0};
    if (lstat(g_knetTelemetrySocket, &fileStat) == -1 || S_ISSOCK(fileStat.st_mode) == 0) {
        KNET_ERR("K-NET dpdk telemetry file %s is not socket", g_knetTelemetrySocket);
        return -1;
    }

    if (DpdkTelemetrySocketPidCheck()) {
        return 0;
    }

    return -1;
}

KNET_STATIC int32_t TelemetryMzInit(void)
{
    const struct rte_memzone *mz = NULL;
    mz = rte_memzone_reserve(KNET_TELEMETRY_MZ_NAME, sizeof(KNET_TelemetryInfo), SOCKET_ID_ANY, 0);
    if (mz == NULL) {
        KNET_ERR("Allocate memory for telemetry debug info failed");
        return KNET_ERROR;
    }

    KNET_TelemetryInfo *telemetryInfo = mz->addr;
    (void)memset_s(telemetryInfo, sizeof(KNET_TelemetryInfo), 0, sizeof(KNET_TelemetryInfo));

    return KNET_OK;
}

KNET_STATIC int32_t RegDpShowStatisticCmd(void)
{
    telemetry_cb telemetryCallbackFunc = KnetTelemetryStatisticCallback;
    int ret = 0;
    
    if (KNET_GetCfg(CONF_COMMON_MODE)->intValue == KNET_RUN_MODE_MULTIPLE) {
        telemetryCallbackFunc = KnetTelemetryStatisticCallbackMp;
    }
    char *cmds[] = {"/knet/stack/tcp_stat",
                    "/knet/stack/conn_stat",
                    "/knet/stack/pkt_stat",
                    "/knet/stack/abn_stat",
                    "/knet/stack/mem_stat",
                    "/knet/stack/pbuf_stat",
                    NULL};
    for (int i = 0; cmds[i] != NULL; ++i) {
        ret = rte_telemetry_register_cmd(cmds[i], telemetryCallbackFunc, "Return stack statistics");
        if (ret < 0) {
            KNET_ERR("K-NET register telemetry cmd %s failed, ret %d", cmds[i], ret);
            return KNET_ERROR;
        }
    }
    return KNET_OK;
}

KNET_STATIC int32_t RegTelemetryCmd(void)
{
    if (KNET_GetCfg(CONF_COMMON_MODE)->intValue == KNET_RUN_MODE_MULTIPLE && TelemetryMzInit() != KNET_OK) {
        return KNET_ERROR; /* 内部已打印日志 */
    }
    for (int i = 0; i < MAX_CB_NUM; i++) {
        telemetry_cb cb =
            (KNET_GetCfg(CONF_COMMON_MODE)->intValue == KNET_RUN_MODE_MULTIPLE ? g_telemetryCmdInfos[i].cb_multi
                                                                               : g_telemetryCmdInfos[i].cb_single);
        int ret = rte_telemetry_register_cmd(g_telemetryCmdInfos[i].registeredCmd, cb, g_telemetryCmdInfos[i].helpCmd);
        if (ret < 0) {
            KNET_ERR("K-NET register telemetry cmd %s failed, ret %d", g_telemetryCmdInfos[i].registeredCmd, ret);
            return KNET_ERROR;
        }
    }

    return RegDpShowStatisticCmd();
}

/**
 * @brief 创建/var/run/dpdk/knet/telemetry.v2:1到/var/run/dpdk/knet/telemetry.v2的硬链接
 * @attention 必须在 ret_eal_init() 之后进行。ret_eal_init 中会调用 telemetry_v2_init
 * @note telemetry_v2_init 中的 atexit(unlink_sockets) 会在子进程退出时被执行到，
 * 导致 dpdk-telemetry 脚本无法通过 socket 文件 dpdk_telemetry.v2 获取到网卡统计信息
 */
int32_t KNET_InitDpdkTelemetry(void)
{
    int32_t ret = DpdkRuntimeDirInit();
    if (ret < 0) {
        KNET_ERR("K-NET dpdk runtime dir init failed, ret %d", ret);
        return KNET_ERROR;
    }

    ret = DpdkTelemetryFindSocket();
    if (ret < 0) {
        KNET_ERR("K-NET dpdk telemetry socket not found in knet dir directory");
        return KNET_ERROR;
    }

    ret = DpdkTelemetryLinkCreate();
    if (ret < 0) {
        KNET_ERR("K-NET dpdk telemetry link create failed");
        return -1;
    }

    ret = RegTelemetryCmd();
    if (ret < 0) {
        KNET_ERR("K-NET reg telemetry command failed, ret %d", ret);
        return KNET_ERROR;
    }

    return 0;
}

int32_t KNET_UninitDpdkTelemetry(void)
{
    if (KNET_GetCfg(CONF_DPDK_TELEMETRY)->intValue == 0) {
        return KNET_OK;
    }

    char rteTelemetrySocket[PATH_MAX] = {0};
    if (realpath(g_knetTelemetrySocketNew, rteTelemetrySocket) == NULL) {
        KNET_ERR("K-NET unlink dpdk telemetry checkout rte dir failed, not real");
        return -1;
    }

    int32_t ret = unlink(rteTelemetrySocket);
    if (ret < 0) {
        KNET_ERR("Rte telemetry socket rte telemetry socket unlink failed, ret %d", ret);
        return KNET_ERROR;
    }

    return KNET_OK;
}

#ifdef __cplusplus
}
#endif /* __cpluscplus */