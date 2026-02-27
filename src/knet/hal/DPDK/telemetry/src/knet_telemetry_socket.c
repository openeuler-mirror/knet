/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: telemetry socket相关操作
 */


#include <arpa/inet.h>
#include <unistd.h>

#include "rte_malloc.h"
#include "rte_memzone.h"

#include "dp_in_api.h"
#include "knet_log.h"
#include "knet_telemetry.h"
#include "knet_telemetry_call.h"
#include "knet_telemetry_debug.h"
#include "knet_transmission.h"
#include "knet_utils.h"

#include "tcp_fd.h"

#define FD_TYPE_COUNT 3
#define FD_COUNT_STR_MAX_LEN 30
#define FD_TYPE_STR_MAX_LEN 10
#define FD_STR_MAX_LEN 10
#define PID_STR_MAX_LEN 10
#define INET_IP_STR_LEN 16
#define MAX_FD_NUM_LIMIT 256
#define MAX_TID_LEN 20

typedef enum {
    NET_STAT_PARAM_PID = 0,
    NET_STAT_PARAM_START_FD,
    NET_STAT_PARAM_FD_CNT,
    NET_STAT_PARAMS_NUM
} NetStatIndex;

typedef enum {
    SOCK_INFO_PARAM_PID = 0,
    SOCK_INFO_PARAM_FD,
    SOCK_INFO_PARAM_NUM
} SockInfoIndex;
 KNET_STATIC int GetFdType(const char *fdTypeStr, int *fdType)
{
    if (fdTypeStr == NULL) {
        KNET_ERR("K-NET telemetry get fd type failed, fdTypeStr is null");
        return KNET_ERROR;
    }
    typedef struct {
        const char *param;
        int fdType;
    } FdTypeMapping;
    static const FdTypeMapping FD_TYPE_MAPPING[FD_TYPE_COUNT] = {{"tcp", 0}, {"udp", 1}, {"epoll", 2}};
    for (int i = 0; i < FD_TYPE_COUNT; i++) {
        if (strcmp(fdTypeStr, FD_TYPE_MAPPING[i].param) == 0) {
            *fdType = FD_TYPE_MAPPING[i].fdType;
            return KNET_OK;
        }
    }
    KNET_ERR("K-NET telemetry get fd type failed, invalid fdTypeStr");
    return KNET_ERROR;
}
// 多进程获取fd数目的回调参数为进程号和套接字类型 如"12345 tcp"
KNET_STATIC int GetPidStrAndFdTypeStrFromParams(const char *params, char *pidStr, char *fdTypeStr)
{
    char *spacePos = strchr(params, ' ');
    if (spacePos == NULL || spacePos != strrchr(params, ' ') || (spacePos - params) >= PID_STR_MAX_LEN ||
        strlen(spacePos + 1) >= FD_TYPE_STR_MAX_LEN) {
        KNET_ERR("K-NET telemetry get fd count failed, invalid params");
        return KNET_ERROR;
    }
    if (strncpy_s(pidStr, PID_STR_MAX_LEN - 1, params, spacePos - params) != 0 ||
        strncpy_s(fdTypeStr, FD_TYPE_STR_MAX_LEN - 1, spacePos + 1, strlen(spacePos + 1))) {
        KNET_ERR("K-NET telemetry get fd count failed, strncpy_s failed");
        return KNET_ERROR;
    }
    return KNET_OK;
}
KNET_STATIC int ValidateParamsAndGetFdType(const char *params, int *fdType)
{
    char pidStr[PID_STR_MAX_LEN] = {0};
    char fdTypeStr[FD_TYPE_STR_MAX_LEN] = {0};
    char *spacePos = strchr(params, ' ');
    if (spacePos == NULL) {
        return GetFdType(params, fdType);
    }
    if (GetPidStrAndFdTypeStrFromParams(params, pidStr, fdTypeStr) != 0) {
        return KNET_ERROR;
    }
    uint32_t inputPid = 0;
    if (KNET_TransStrToNum(pidStr, &inputPid) != 0 || inputPid != (uint32_t)getpid()) {
        KNET_ERR("K-NET telemetry input invalid pid or pid %u is not knet process pid", inputPid);
        return KNET_ERROR;
    }
    return GetFdType(fdTypeStr, fdType);
}
int KnetTelemetryGetFdCountCallback(const char *cmd, const char *params, struct rte_tel_data *data)
{
    if (data == NULL || params == NULL) {
        KNET_ERR("Rte telemetry data is null");
        return KNET_ERROR;
    }
    int fdType = -1;
    if (ValidateParamsAndGetFdType(params, &fdType) != 0) {
        KNET_ERR("K-NET telemetry get fd count failed, validate params failed");
        return KNET_ERROR;
    }
    if (g_dpTelemetryHooks.dpSocketCountGetHook == NULL) {
        KNET_ERR("K-NET telemetry get fd count failed, hookfunc is null");
        return KNET_ERROR;
    }
    int count = g_dpTelemetryHooks.dpSocketCountGetHook(fdType);
    char resStr[FD_COUNT_STR_MAX_LEN] = {0};
    (void)sprintf_s(resStr, FD_COUNT_STR_MAX_LEN - 1, "%d", count);
    if (rte_tel_data_string(data, resStr) != 0) {
        KNET_ERR("K-NET telemetry get fd count failed, rte_tel_data_string failed");
        return KNET_ERROR;
    }
    return KNET_OK;
}
KNET_STATIC int GetQueIdByInputPidStr(const char *pidStr, KNET_TelemetryInfo *telemetryInfo)
{
    uint32_t inputPid = 0;
    if (KNET_TransStrToNum(pidStr, &inputPid) != 0) {
        KNET_ERR("K-NET telemetry get fd count failed, input pid params error");
    }
    int queId = KnetGetQueIdByPid(inputPid, telemetryInfo);
    if (queId < 0) {
        KNET_ERR("K-NET telemetry get fd count failed, can't find pid %u from slave process", inputPid);
    }
    return queId;
}
KNET_STATIC int GetPidStrAndFdStrFromParams(const char *params, char pidStr[PID_STR_MAX_LEN],
                                            char fdStr[FD_STR_MAX_LEN])
{
    char *spacePos = strchr(params, ' ');
    if (spacePos == NULL || spacePos != strrchr(params, ' ') || (spacePos - params) >= PID_STR_MAX_LEN ||
        strlen(spacePos + 1) >= FD_STR_MAX_LEN) {
        KNET_ERR("K-NET telemetry get fd info validitate params failed");
        return KNET_ERROR;
    }
    if (strncpy_s(pidStr, PID_STR_MAX_LEN - 1, params, spacePos - params) != 0 ||
        strncpy_s(fdStr, FD_STR_MAX_LEN - 1, spacePos + 1, strlen(spacePos + 1))) {
        KNET_ERR("K-NET telemetry get socket info failed, invalid params");
        return KNET_ERROR;
    }
    return 0;
}
KNET_STATIC int ValidateParamsAndGetFdQueId(const char *params, int *fd, int *queId, KNET_TelemetryInfo *telemetryInfo)
{
    char pidStr[PID_STR_MAX_LEN] = {0};
    char fdStr[FD_TYPE_STR_MAX_LEN] = {0};

    if (GetPidStrAndFdStrFromParams(params, pidStr, fdStr) != 0) {
        return KNET_ERROR;
    }
    *queId = GetQueIdByInputPidStr(pidStr, telemetryInfo);
    if (*queId == -1) {
        return KNET_ERROR;
    }
    uint32_t inputFd = 0;
    if (KNET_TransStrToNum(fdStr, &inputFd) != 0) {
        KNET_ERR("K-NET telemetry validate params failed, input fd params error");
        return KNET_ERROR;
    }
    *fd = inputFd;
    return KNET_OK;
}

KNET_STATIC int ValidateParamsAndGetFdTypeQueId(const char *params, int *fdType, int *queId,
                                                KNET_TelemetryInfo *telemetryInfo)
{
    char pidStr[PID_STR_MAX_LEN] = {0};
    char fdTypeStr[FD_TYPE_STR_MAX_LEN] = {0};
    if (GetPidStrAndFdTypeStrFromParams(params, pidStr, fdTypeStr) != 0) {
        return KNET_ERROR;
    }
    *queId = GetQueIdByInputPidStr(pidStr, telemetryInfo);
    if (*queId == -1) {
        return KNET_ERROR;
    }
    return GetFdType(fdTypeStr, fdType);
}
int KnetTelemetryGetFdCountCallbackMp(const char *cmd, const char *params, struct rte_tel_data *data)
{
    if (data == NULL || params == NULL) {
        KNET_ERR("Rte telemetry data or params is null");
        return KNET_ERROR;
    }
    const struct rte_memzone *mz = rte_memzone_lookup(KNET_TELEMETRY_MZ_NAME);
    if (mz == NULL || mz->addr == NULL) {
        KNET_ERR("Subprocess couldn't allocate memory for stack debug info");
        return KNET_ERROR;
    }
    KNET_TelemetryInfo *telemetryInfo = (KNET_TelemetryInfo *)mz->addr;
    KnetUpdateSlaveProcessPidInfo(telemetryInfo);
    if (KnetWaitAllSlavePorcessHandle(telemetryInfo) != 0) {
        return KNET_ERROR;
    }
    int fdType = -1;
    int queId = -1;
    if (ValidateParamsAndGetFdTypeQueId(params, &fdType, &queId, telemetryInfo) != 0) {
        return KNET_ERROR;
    }
    telemetryInfo->statType = (uint32_t)fdType;
    telemetryInfo->telemetryType = KNET_TELEMETRY_GET_FD_COUNT;
    (void)memset_s(telemetryInfo->message[queId], MAX_OUTPUT_LEN, 0, MAX_OUTPUT_LEN);
    telemetryInfo->msgReady[queId] = 1;
    if (KnetHandleTimeout(telemetryInfo, queId) != 0) {
        KNET_ERR("K-NET telemetry get fd count failed, handle time out");
        return KNET_ERROR;
    }
    if (rte_tel_data_string(data, telemetryInfo->message[queId]) != 0) {
        KNET_ERR("K-NET telemetry get fd count failed, rte_tel_data_string failed");
        return KNET_ERROR;
    }

    return KNET_OK;
}
KNET_STATIC KNET_SocketState *GetNetStat()
{
    int fdMax = KNET_FdMaxGet();
    KNET_SocketState *sockets = (KNET_SocketState *)calloc(1, sizeof(KNET_SocketState) * (fdMax + 1));
    if (sockets == NULL) {
        KNET_ERR("K-NET telemetry get net stat failed, calloc failed");
        return NULL;
    }
    int index = 0;
    if (g_dpTelemetryHooks.dpGetSocketStateHook == NULL) {
        KNET_ERR("K-NET telemetry get net stat failed, dp hookfunc is null");
        goto abnormal;
    }
    for (int i = 0; i < fdMax; i++) {
        if (KNET_IsFdHijack(i) && KNET_GetFdType(i) == KNET_FD_TYPE_SOCKET) {
            int dpFd = KNET_OsFdToDpFd(i);
            DP_SocketState_t dpSocketState = {0};
            if (g_dpTelemetryHooks.dpGetSocketStateHook(dpFd, &dpSocketState) != 0) {
                KNET_ERR("K-NET telemetry get net stat failed, osFd %d, dpFd %d ", i, dpFd);
                goto abnormal;
            }
            uint32_t tid = 0;
            if (dpSocketState.workerId == UNCONNECTED_FLAG) {
                tid = 0;
            } else if (KnetGetTidByWorkerId(dpSocketState.workerId, &tid) != 0) {
                KNET_ERR("K-NET telemetry get net stat failed, osFd %d, dpFd %d ", i, dpFd);
                goto abnormal;
            }
            sockets[index].osFd = i;
            sockets[index].dpFd = dpFd;
            sockets[index].tid = tid;
            sockets[index].dpSocketState = dpSocketState;
            index++;
        }
    }
    sockets[index].isLast = true;
    return sockets;
abnormal:
    free(sockets);
    return NULL;
}
KNET_STATIC char *GetSocketPf(uint32_t pf)
{
    char *dpSocketPf[] = {[DP_PF_INET] = "AF_INET", [DP_PF_INET6] = "AF_INET6"};
    if (pf != DP_PF_INET && pf != DP_PF_INET6) {
        KNET_ERR("K-NET telemetry get net state failed, invalid pf %u", pf);
        return "INVALID";
    }
    return dpSocketPf[pf];
}

KNET_STATIC char *GetSocketProto(uint32_t proto)
{
    char *dpSocketProto[] = {[DP_IPPROTO_TCP] = "TCP", [DP_IPPROTO_UDP] = "UDP"};
    if (proto != DP_IPPROTO_TCP && proto != DP_IPPROTO_UDP) {
        KNET_ERR("K-NET telemetry get net state failed, invalid proto %u", proto);
        return "INVALID";
    }
    return dpSocketProto[proto];
}

KNET_STATIC char *GetSocketState(uint32_t state)
{
    char *dpSocketState[] = {[DP_SOCKET_STATE_CLOSED] = "CLOSED",           [DP_SOCKET_STATE_LISTEN] = "LISTEN",
                             [DP_SOCKET_STATE_SYN_SENT] = "SYN_SENT",       [DP_SOCKET_STATE_SYN_RECV] = "SYN_RECV",
                             [DP_SOCKET_STATE_ESTABLISHED] = "ESTABLISHED", [DP_SOCKET_STATE_CLOSE_WAIT] = "CLOSE_WAIT",
                             [DP_SOCKET_STATE_FIN_WAIT1] = "FIN_WAIT1",     [DP_SOCKET_STATE_CLOSING] = "CLOSING",
                             [DP_SOCKET_STATE_LAST_ACK] = "LAST_ACK",       [DP_SOCKET_STATE_FIN_WAIT2] = "FIN_WAIT2",
                             [DP_SOCKET_STATE_TIME_WAIT] = "TIME_WAIT",     [DP_SOCKET_STATE_INVALID] = "INVALID"};
    if (state < DP_SOCKET_STATE_CLOSED || state > DP_SOCKET_STATE_INVALID) {
        state = DP_SOCKET_STATE_INVALID;
    }
    return dpSocketState[state];
}
KNET_STATIC int GetIpStr(uint32_t ipAddrNum, char ipStr[INET_IP_STR_LEN])
{
    struct in_addr addr = {0};
    addr.s_addr = ipAddrNum;
    if (inet_ntop(AF_INET, &addr, ipStr, INET_ADDRSTRLEN) == NULL) {
        KNET_ERR("K-NET telemetry get net state failed, inet_ntop failed");
        return -1;
    }
    return 0;
}
KNET_STATIC int ProcessSocketState(KNET_SocketState socketState, struct rte_tel_data *socketStatDict)
{
    if (rte_tel_data_start_dict(socketStatDict) != 0) {
        KNET_ERR("K-NET telemetry get net state failed, start socketStatDict failed");
        return -1;
    }
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_string, socketStatDict, "pf",
                            GetSocketPf(socketState.dpSocketState.pf));
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_string, socketStatDict, "proto",
                            GetSocketProto(socketState.dpSocketState.proto));
    char lAddrStr[INET_IP_STR_LEN] = {0};
    char rAddrStr[INET_IP_STR_LEN] = {0};
    if (GetIpStr(socketState.dpSocketState.lAddr4, lAddrStr) == -1 ||
        GetIpStr(socketState.dpSocketState.rAddr4, rAddrStr) == -1) {
        return -1;
    }
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_string, socketStatDict, "lAddr", lAddrStr);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, socketStatDict, "lPort",
                            htons((uint16_t)socketState.dpSocketState.lPort));
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_string, socketStatDict, "rAddr", rAddrStr);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, socketStatDict, "rPort",
                            htons((uint16_t)socketState.dpSocketState.rPort));
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_string, socketStatDict, "state",
                            GetSocketState(socketState.dpSocketState.state));

    if (socketState.tid == 0) {
        CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_string, socketStatDict, "tid", INVALID_WORKER_TID);
    } else {
        char tidStr[MAX_TID_LEN] = {0};
        (void)snprintf_s(tidStr, MAX_TID_LEN, MAX_TID_LEN - 1, "%u", socketState.tid);
        CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_string, socketStatDict, "tid", tidStr);
    }
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_int, socketStatDict, "innerFd", socketState.dpFd);

    return 0;
}

KNET_STATIC int ProcessSocketStatesInfo(KNET_SocketState *sockets, struct rte_tel_data *data,
                                        uint32_t startFd, uint32_t fdCnt)
{
    int i = 0;
    uint32_t count = 0;
    fdCnt = (fdCnt == 0) ? MAX_FD_NUM_LIMIT : fdCnt;
    if (rte_tel_data_start_dict(data) != 0) {
        KNET_ERR("K-NET telemetry get net state failed, start first level dict failed");
        return KNET_ERROR;
    }
    while (!sockets[i].isLast && count < fdCnt) {
        if (sockets[i].osFd >= (int)startFd) {
            struct rte_tel_data *socketStat = rte_tel_data_alloc();
            if (socketStat == NULL) {
                KNET_ERR("K-NET telemetry get net state failed, rte_tel_data_alloc failed");
                return KNET_ERROR;
            }
            if (ProcessSocketState(sockets[i], socketStat) != 0) {
                rte_tel_data_free(socketStat);
                return KNET_ERROR;
            }
            char keyName[MAX_JSON_KEY_NAME_LEN] = "osFd";
            (void)snprintf_s(keyName + strlen(keyName), MAX_JSON_KEY_NAME_LEN - strlen(keyName),
                             MAX_JSON_KEY_NAME_LEN - strlen(keyName) - 1, " %d", sockets[i].osFd);
            if (CheckAddContainerToDict(data, keyName, socketStat) != 0) {
                return KNET_ERROR;
            }
            ++count;
        }
        ++i;
    }
    return KNET_OK;
}
KNET_STATIC int ParseNetStatParams(const char *params, uint32_t *pid, uint32_t *startFd, uint32_t *fdCnt)
{
    uint32_t paramsArr[NET_STAT_PARAMS_NUM] = {0};
    if (ParseTelemetryParams(params, paramsArr, NET_STAT_PARAMS_NUM) != NET_STAT_PARAMS_NUM) {
        KNET_ERR("K-NET telemetry get net state failed, invalid input params, expect <pid> <start_fd> <fd_cnt>");
        return KNET_ERROR;
    }
    *pid = paramsArr[NET_STAT_PARAM_PID];
    *startFd = paramsArr[NET_STAT_PARAM_START_FD];
    *fdCnt = paramsArr[NET_STAT_PARAM_FD_CNT];
    if (*fdCnt > MAX_FD_NUM_LIMIT) {
        KNET_ERR("K-NET telemetry get net state failed, fd_cnt must be less than 256");
        return KNET_ERROR;
    }
    return KNET_OK;
}

int KnetTelemetryGetNetStatCallback(const char *cmd, const char *params, struct rte_tel_data *data)
{
    if (data == NULL) {
        KNET_ERR("Rte telemetry data is null");
        return KNET_ERROR;
    }
    uint32_t pid = 0;
    uint32_t startFd = 0;
    uint32_t fdCnt = 0;
    if (ParseNetStatParams(params, &pid, &startFd, &fdCnt) != KNET_OK) {
        KNET_ERR("K-NET telemetry get net state failed, parse params failed");
        return KNET_ERROR;
    }
    if (pid != (uint32_t)getpid()) {
        KNET_ERR("K-NET telemetry get net state failed, pid %u is not knet process", pid);
        return KNET_ERROR;
    }
    KNET_SocketState *socketStates = GetNetStat();
    if (socketStates == NULL) {
        KNET_ERR("K-NET telemetry get net state failed");
        return KNET_ERROR;
    }

    if (ProcessSocketStatesInfo(socketStates, data, startFd, fdCnt) != 0) {
        free(socketStates);
        KNET_ERR("K-NET telemetry get net state failed, process socket states failed");
        return KNET_ERROR;
    }
    free(socketStates);
    return KNET_OK;
}

int KnetTelemetryGetNetStatCallbackMp(const char *cmd, const char *params, struct rte_tel_data *data)
{
    if (data == NULL || params == NULL) {
        KNET_ERR("Rte telemetry data is null or params is null");
        return KNET_ERROR;
    }
    KNET_TelemetryInfo *telemetryInfo = KnetMultiSetTelemetrySHM();
    if (telemetryInfo == NULL) {
        KNET_ERR("K-NET telemetry get sock info failed, get telemetry info failed");
        return KNET_ERROR;
    }
    uint32_t pid = 0;
    uint32_t startFd = 0;
    uint32_t fdCnt = 0;
    if (ParseNetStatParams(params, &pid, &startFd, &fdCnt) != KNET_OK) {
        KNET_ERR("K-NET telemetry get net state failed, parse params failed");
        return KNET_ERROR;
    }
    int queId = KnetGetQueIdByPid(pid, telemetryInfo);
    if (queId == -1) {
        KNET_ERR("K-NET telemetry get net state failed, can't find queId by pid %u", pid);
        return KNET_ERROR;
    }
    telemetryInfo->telemetryType = KNET_TELEMETRY_GET_NET_STAT;
    telemetryInfo->socketStates = NULL;
    telemetryInfo->msgReady[queId] = 1;
    if (KnetHandleTimeout(telemetryInfo, queId) != KNET_OK) {
        KNET_ERR("K-NET telemetry get net state failed, handle timeout");
        return KNET_ERROR;
    }

    if (telemetryInfo->socketStates == NULL) {
        KNET_ERR("K-NET telemetry get net state failed, telemetryInfo socketStates is null");
        return KNET_ERROR;
    }
    if (ProcessSocketStatesInfo(telemetryInfo->socketStates, data, startFd, fdCnt) != KNET_OK) {
        rte_free(telemetryInfo->socketStates);
        KNET_ERR("K-NET telemetry get net state failed, multi process socket states failed");
        return KNET_ERROR;
    }
    rte_free(telemetryInfo->socketStates);
    return KNET_OK;
}

KNET_STATIC int ValidateParamsAndGetFd(const char *params, uint32_t *inputFd)
{
    uint32_t inputPid = 0;
    char *spacePos = strchr(params, ' ');
    if (spacePos == NULL) {
        if (KNET_TransStrToNum(params, inputFd) != 0 || !KNET_IsFdHijack(*inputFd)) {
            KNET_ERR("K-NET telemetry get socket info failed, invalid params or fd %u is not hijacked", *inputFd);
            return KNET_ERROR;
        }
        return KNET_OK;
    }
    uint32_t paramsArr[SOCK_INFO_PARAM_NUM] = {0};
    if (ParseTelemetryParams(params, paramsArr, SOCK_INFO_PARAM_NUM) != SOCK_INFO_PARAM_NUM) {
        KNET_ERR("K-NET telemetry get socket info failed, invalid input params, expect <pid> <start_fd> <fd_cnt>");
        return KNET_ERROR;
    }
    inputPid = paramsArr[SOCK_INFO_PARAM_PID];
    *inputFd = paramsArr[SOCK_INFO_PARAM_FD];
    if (inputPid != (uint32_t)getpid()) {
        KNET_ERR("K-NET telemetry get socket info failed, pid %u is not knet process pid", inputPid);
        return KNET_ERROR;
    }
    if (!KNET_IsFdHijack(*inputFd)) {
        KNET_ERR("K-NET telemetry get socket info failed, fd %u is not hijacked", *inputFd);
        return KNET_ERROR;
    }
    return KNET_OK;
}
KNET_STATIC int ProcessSockInfo(DP_SockDetails_t *dpSockDetails, struct rte_tel_data *sockInfo)
{
    if (rte_tel_data_start_dict(sockInfo) != 0) {
        KNET_ERR("K-NET telemetry get sock info failed, start sockInfo dict failed");
        return KNET_ERROR;
    }
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_string, sockInfo, "protocol",
                            GetSocketProto(dpSockDetails->protocol));
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, sockInfo, "isLingerOnoff", dpSockDetails->lingerOnoff);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, sockInfo, "isNonblock", dpSockDetails->nonblock);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, sockInfo, "isReuseAddr", dpSockDetails->reuseAddr);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, sockInfo, "isReusePort", dpSockDetails->reusePort);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, sockInfo, "isBroadcast", dpSockDetails->broadcast);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, sockInfo, "isKeepAlive", dpSockDetails->keepalive);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, sockInfo, "isBindDev", dpSockDetails->bindDev);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, sockInfo, "isDontRoute", dpSockDetails->dontRoute);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, sockInfo, "options", dpSockDetails->options);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, sockInfo, "error", dpSockDetails->error);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_string, sockInfo, "pf", GetSocketPf(dpSockDetails->family));
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_int, sockInfo, "linger", dpSockDetails->linger);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, sockInfo, "flags", dpSockDetails->flags);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, sockInfo, "state", dpSockDetails->state);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, sockInfo, "rdSemCnt", dpSockDetails->rdSemCnt);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, sockInfo, "wrSemCnt", dpSockDetails->wrSemCnt);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_int, sockInfo, "rcvTimeout", dpSockDetails->rcvTimeout);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_int, sockInfo, "sndTimeout", dpSockDetails->sndTimeout);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, sockInfo, "sndDataLen", dpSockDetails->sndDataLen);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, sockInfo, "rcvDataLen", dpSockDetails->rcvDataLen);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, sockInfo, "sndLowat", dpSockDetails->sndLowat);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, sockInfo, "rcvLowat", dpSockDetails->rcvLowat);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, sockInfo, "sndHiwat", dpSockDetails->sndHiwat);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, sockInfo, "rcvHiwat", dpSockDetails->rcvHiwat);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, sockInfo, "bandWidth", dpSockDetails->bandWidth);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_int, sockInfo, "priority", dpSockDetails->priority);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_int, sockInfo, "associateFd", dpSockDetails->associateFd);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_int, sockInfo, "notifyType", dpSockDetails->notifyType);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_int, sockInfo, "wid", dpSockDetails->wid);
    return KNET_OK;
}

KNET_STATIC int ProcessInetSkInfo(DP_InetDetails_t *inetDetails, struct rte_tel_data *inetSkInfo)
{
    if (rte_tel_data_start_dict(inetSkInfo) != 0) {
        KNET_ERR("K-NET telemetry get sock info failed, start inetSkInfo dict failed");
        return KNET_ERROR;
    }
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, inetSkInfo, "ttl", inetDetails->ttl);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, inetSkInfo, "tos", inetDetails->tos);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, inetSkInfo, "mtu", inetDetails->mtu);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, inetSkInfo, "isIncHdr", inetDetails->options.incHdr);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, inetSkInfo, "isTos", inetDetails->options.tos);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, inetSkInfo, "isTtl", inetDetails->options.ttl);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, inetSkInfo, "isMtu", inetDetails->options.mtu);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, inetSkInfo, "isPktInfo", inetDetails->options.pktInfo);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, inetSkInfo, "isRcvTos", inetDetails->options.rcvTos);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, inetSkInfo, "isRcvTtl", inetDetails->options.rcvTtl);
    return KNET_OK;
}
KNET_STATIC char *GetTcpConnType(uint8_t connType)
{
    if (connType != 0 && connType != 1) {
        return "Invalid";
    }
    char *connTypeName[] = {"Active", "Passive"};
    return connTypeName[connType];
}
KNET_STATIC int ProcessTcpBaseInfo(DP_TcpBaseDetails_t *tcpBaseDetails, struct rte_tel_data *tcpBaseInfo)
{
    if (rte_tel_data_start_dict(tcpBaseInfo) != 0) {
        KNET_ERR("K-NET telemetry get sock info failed, start tcpBaseInfo dict failed");
        return KNET_ERROR;
    }

    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_string, tcpBaseInfo, "state", GetSocketState(tcpBaseDetails->state));
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_string, tcpBaseInfo, "connType",
                            GetTcpConnType(tcpBaseDetails->connType));
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "noVerifyCksum", tcpBaseDetails->noVerifyCksum);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "ackNow", tcpBaseDetails->ackNow);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "delayAckEnable", tcpBaseDetails->delayAckEnable);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "nodelay", tcpBaseDetails->nodelay);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "rttRecord", tcpBaseDetails->rttRecord);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "cork", tcpBaseDetails->cork);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "deferAccept", tcpBaseDetails->deferAccept);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "flags", tcpBaseDetails->flags);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_int, tcpBaseInfo, "wid", tcpBaseDetails->wid);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_int, tcpBaseInfo, "txQueid", tcpBaseDetails->txQueid);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_int, tcpBaseInfo, "childCnt", tcpBaseDetails->childCnt);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_int, tcpBaseInfo, "backlog", tcpBaseDetails->backlog);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "accDataCnt", tcpBaseDetails->accDataCnt);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "accDataMax", tcpBaseDetails->accDataMax);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_int, tcpBaseInfo, "caAlgId", tcpBaseDetails->caAlgId);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "caState", tcpBaseDetails->caState);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "dupAckCnt", tcpBaseDetails->dupAckCnt);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "cwnd", tcpBaseDetails->cwnd);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "ssthresh", tcpBaseDetails->ssthresh);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "seqRecover", tcpBaseDetails->seqRecover);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "reorderCnt", tcpBaseDetails->reorderCnt);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "rttStartSeq", tcpBaseDetails->rttStartSeq);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "srtt", tcpBaseDetails->srtt);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "rttval", tcpBaseDetails->rttval);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "tsVal", tcpBaseDetails->tsVal);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "tsEcho", tcpBaseDetails->tsEcho);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "lastChallengeAckTime",
                            tcpBaseDetails->lastChallengeAckTime);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "fastMode", tcpBaseDetails->fastMode);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "sndQueSize", tcpBaseDetails->sndQueSize);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "rcvQueSize", tcpBaseDetails->rcvQueSize);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "rexmitQueSize", tcpBaseDetails->rexmitQueSize);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpBaseInfo, "reassQueSize", tcpBaseDetails->reassQueSize);

    return KNET_OK;
}

KNET_STATIC int ProcessTcpTransInfo(DP_TcpTransDetails_t *tcpTransDetails, struct rte_tel_data *tcpTransInfo)
{
    if (rte_tel_data_start_dict(tcpTransInfo) != 0) {
        KNET_ERR("K-NET telemetry get sock info failed, start tcpTransInfo dict failed");
        return KNET_ERROR;
    }
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "lport", tcpTransDetails->lport);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "pport", tcpTransDetails->pport);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "synOpt", tcpTransDetails->synOpt);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "negOpt", tcpTransDetails->negOpt);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "rcvWs", tcpTransDetails->rcvWs);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "sndWs", tcpTransDetails->sndWs);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "rcvMss", tcpTransDetails->rcvMss);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "mss", tcpTransDetails->mss);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "iss", tcpTransDetails->iss);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "irs", tcpTransDetails->irs);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "sndUna", tcpTransDetails->sndUna);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "sndNxt", tcpTransDetails->sndNxt);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "sndMax", tcpTransDetails->sndMax);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "sndWnd", tcpTransDetails->sndWnd);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "sndUp", tcpTransDetails->sndUp);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "sndWl1", tcpTransDetails->sndWl1);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "sndSml", tcpTransDetails->sndSml);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "rcvNxt", tcpTransDetails->rcvNxt);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "rcvWnd", tcpTransDetails->rcvWnd);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "rcvMax", tcpTransDetails->rcvMax);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "rcvWup", tcpTransDetails->rcvWup);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "idleStart", tcpTransDetails->idleStart);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "keepIdle", tcpTransDetails->keepIdle);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "keepIntvl", tcpTransDetails->keepIntvl);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "keepProbes", tcpTransDetails->keepProbes);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "keepProbeCnt", tcpTransDetails->keepProbeCnt);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "keepIdleLimit", tcpTransDetails->keepIdleLimit);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "keepIdleCnt", tcpTransDetails->keepIdleCnt);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "backoff", tcpTransDetails->backoff);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "maxRexmit", tcpTransDetails->maxRexmit);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "rexmitCnt", tcpTransDetails->rexmitCnt);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "userTimeout", tcpTransDetails->userTimeout);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "userTimeStartFast",
                            tcpTransDetails->userTimeStartFast);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "userTimeStartSlow",
                            tcpTransDetails->userTimeStartSlow);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "fastTimeoutTick",
                            tcpTransDetails->fastTimeoutTick);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "slowTimeoutTick",
                            tcpTransDetails->slowTimeoutTick);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "delayAckTimoutTick",
                            tcpTransDetails->delayAckTimoutTick);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, tcpTransInfo, "synRetries", tcpTransDetails->synRetries);
    return KNET_OK;
}
KNET_STATIC int CreateSockInfoAndAddReply(DP_SockDetails_t *dpSockDetails, struct rte_tel_data *data)
{
    struct rte_tel_data *sockInfo = rte_tel_data_alloc();
    if (sockInfo == NULL || ProcessSockInfo(dpSockDetails, sockInfo) != 0) {
        rte_tel_data_free(sockInfo);
        KNET_ERR("K-NET telemetry get sock info failed, process sock info failed");
        return KNET_ERROR;
    }
    if (CheckAddContainerToDict(data, "SockInfo", sockInfo) != 0) {
        return KNET_ERROR;
    }
    return KNET_OK;
}

KNET_STATIC int CreateInetSkInfoAndAddReply(DP_SockDetails_t *dpSockDetails, struct rte_tel_data *data)
{
    struct rte_tel_data *inetSkInfo = rte_tel_data_alloc();
    if (inetSkInfo == NULL || ProcessInetSkInfo(&dpSockDetails->inetDetails, inetSkInfo) != 0) {
        rte_tel_data_free(inetSkInfo);
        KNET_ERR("K-NET telemetry get sock info failed, process inetSkInfo failed");
        return KNET_ERROR;
    }
    if (CheckAddContainerToDict(data, "InetSkInfo", inetSkInfo) != 0) {
        return KNET_ERROR;
    }
    return KNET_OK;
}
KNET_STATIC int CreateTcpBaseInfoAndAddReply(DP_SockDetails_t *dpSockDetails, struct rte_tel_data *data)
{
    struct rte_tel_data *tcpBaseInfo = rte_tel_data_alloc();
    if (tcpBaseInfo == NULL || ProcessTcpBaseInfo(&dpSockDetails->tcpDetails.baseDetails, tcpBaseInfo) != 0) {
        rte_tel_data_free(tcpBaseInfo);
        KNET_ERR("K-NET telemetry get sock info failed, process sock info failed");
        return KNET_ERROR;
    }
    if (CheckAddContainerToDict(data, "TcpBaseInfo", tcpBaseInfo) != 0) {
        return KNET_ERROR;
    }
    return KNET_OK;
}
KNET_STATIC int CreateTcpTransInfoAndAddReply(DP_SockDetails_t *dpSockDetails, struct rte_tel_data *data)
{
    struct rte_tel_data *tcpTransInfo = rte_tel_data_alloc();
    if (tcpTransInfo == NULL || ProcessTcpTransInfo(&dpSockDetails->tcpDetails.transDetails, tcpTransInfo) != 0) {
        rte_tel_data_free(tcpTransInfo);
        KNET_ERR("K-NET telemetry get sock info failed, process sock info failed");
        return KNET_ERROR;
    }
    if (CheckAddContainerToDict(data, "TcpTransInfo", tcpTransInfo) != 0) {
        return KNET_ERROR;
    }
    return KNET_OK;
}
KNET_STATIC int ProcessSocketInfo(DP_SockDetails_t *socketDetails, struct rte_tel_data *data)
{
    if (rte_tel_data_start_dict(data) != 0) {
        KNET_ERR("K-NET telemetry get sock info failed, start first level dict failed");
        return KNET_ERROR;
    }
    if (CreateSockInfoAndAddReply(socketDetails, data) != 0 ||
        CreateInetSkInfoAndAddReply(socketDetails, data) != 0 ||
        CreateTcpBaseInfoAndAddReply(socketDetails, data) != 0 ||
        CreateTcpTransInfoAndAddReply(socketDetails, data) != 0) {
        return KNET_ERROR;
    }
    return KNET_OK;
}
int KnetTelemetryGetSockInfoCallback(const char *cmd, const char *params, struct rte_tel_data *data)
{
    if (data == NULL || params == NULL) {
        KNET_ERR("Rte telemetry data is null or params is null");
        return KNET_ERROR;
    }
    uint32_t inputFd = 0;
    if (ValidateParamsAndGetFd(params, &inputFd) != KNET_OK) {
        return KNET_ERROR;
    }
    int dpFd = KNET_OsFdToDpFd(inputFd);
    DP_SockDetails_t sockDetails = {0};
    if (g_dpTelemetryHooks.dpGetSocketDetailsHook == NULL) {
        KNET_ERR("K-NET telemetry get sock info failed, dphookfunc is null");
        return KNET_ERROR;
    }
    g_dpTelemetryHooks.dpGetSocketDetailsHook(dpFd, &sockDetails);
    return ProcessSocketInfo(&sockDetails, data);
}
int KnetTelemetryGetSockInfoCallbackMp(const char *cmd, const char *params, struct rte_tel_data *data)
{
    if (data == NULL || params == NULL) {
        KNET_ERR("Rte telemetry data is null or params is null");
        return KNET_ERROR;
    }
    KNET_TelemetryInfo *telemetryInfo = KnetMultiSetTelemetrySHM();
    if (telemetryInfo == NULL) {
        KNET_ERR("K-NET telemetry get sock info failed, get telemetry info failed");
        return KNET_ERROR;
    }
    int fd = -1;
    int queId = -1;
    if (ValidateParamsAndGetFdQueId(params, &fd, &queId, telemetryInfo) == -1) {
        return KNET_ERROR;
    }
    telemetryInfo->socketDetails.osFd = fd;
    telemetryInfo->telemetryType = KNET_TELEMETRY_GET_SOCKET_INFO;
    telemetryInfo->socketDetails.isReady = false;
    telemetryInfo->msgReady[queId] = 1;
    if (KnetHandleTimeout(telemetryInfo, queId) != KNET_OK) {
        KNET_ERR("K-NET telemetry get socket info failed, handle timeout failed");
        return KNET_ERROR;
    }
    if (!telemetryInfo->socketDetails.isReady) {
        KNET_ERR("K-NET telemetry get socket info failed, socket details not ready");
        return KNET_ERROR;
    }

    return ProcessSocketInfo(&telemetryInfo->socketDetails.dpSockDetails, data);
}