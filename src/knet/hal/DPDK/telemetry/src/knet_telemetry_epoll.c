/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: telemetry epoll相关操作
 */

#include <unistd.h>

#include "rte_malloc.h"
#include "rte_memzone.h"

#include "knet_log.h"
#include "knet_telemetry.h"
#include "knet_telemetry_call.h"
#include "knet_telemetry_debug.h"
#include "tcp_fd.h"

#define RESERVED_EPOLL_EVENT_AMOUNT 100
#define MAX_FD_NUM_LIMIT 256
#define MAX_TID_LEN 20

typedef struct {
    uint32_t pid;
    uint32_t epollStartFd;
    uint32_t epollFdCnt;
    uint32_t startFd;
    uint32_t fdCnt;
} TelemetryEpollParams;

typedef enum {
    PARAM_PID = 0,
    PARAM_EPOLL_START_FD,
    PARAM_EPOLL_FD_CNT,
    PARAM_START_FD,
    PARAM_FD_CNT,
    EPOLL_DETAILS_PARAMS_NUM
} EpollIndex;

KNET_STATIC int ParseEpollDetailsParams(const char *params, TelemetryEpollParams *epollParams)
{
    uint32_t paramsArr[EPOLL_DETAILS_PARAMS_NUM] = {0};
    if (ParseTelemetryParams(params, paramsArr, EPOLL_DETAILS_PARAMS_NUM) != EPOLL_DETAILS_PARAMS_NUM) {
        KNET_ERR("K-NET telemetry epoll details callback failed, invalid input, except format <pid> <start_epoll_fd> "
                 "<epoll_fd_cnt> <start_fd> <fd_cnt>");
        return KNET_ERROR;
    }
    epollParams->pid = paramsArr[PARAM_PID];
    epollParams->epollStartFd = paramsArr[PARAM_EPOLL_START_FD];
    epollParams->epollFdCnt = paramsArr[PARAM_EPOLL_FD_CNT];
    epollParams->startFd = paramsArr[PARAM_START_FD];
    epollParams->fdCnt = paramsArr[PARAM_FD_CNT];
    if (epollParams->epollStartFd < 0 || epollParams->epollFdCnt < 0 || epollParams->epollFdCnt > MAX_FD_NUM_LIMIT ||
        epollParams->startFd < 0 || epollParams->fdCnt < 0 || epollParams->fdCnt > MAX_FD_NUM_LIMIT) {
        KNET_ERR("K-NET telemetry epoll details callback failed, all params must be greater than 0, and epoll_fd_cnt "
                 "and fd_cnt must be less than 256");
        return KNET_ERROR;
    }
    if (epollParams->pid != (uint32_t)getpid()) {
        KNET_ERR("K-NET telemetry epoll details callback failed, pid %d is not knet process pid", epollParams->pid);
        return KNET_ERROR;
    }
    return KNET_OK;
}

KNET_STATIC int ParseEpollDetailsParamsAndGetQueId(const char *params, TelemetryEpollParams *epollParams, int *queId,
                                                   KNET_TelemetryInfo *telemetryInfo)
{
    uint32_t paramsArr[EPOLL_DETAILS_PARAMS_NUM] = {0};
    if (ParseTelemetryParams(params, paramsArr, EPOLL_DETAILS_PARAMS_NUM) != EPOLL_DETAILS_PARAMS_NUM) {
        KNET_ERR("K-NET telemetry epoll details callback failed, invalid input, except format <pid> <start_epoll_fd> "
                 "<epoll_fd_cnt> <start_fd> <fd_cnt>");
        return KNET_ERROR;
    }
    epollParams->pid = paramsArr[PARAM_PID];
    epollParams->epollStartFd = paramsArr[PARAM_EPOLL_START_FD];
    epollParams->epollFdCnt = paramsArr[PARAM_EPOLL_FD_CNT];
    epollParams->startFd = paramsArr[PARAM_START_FD];
    epollParams->fdCnt = paramsArr[PARAM_FD_CNT];
    if (epollParams->epollStartFd < 0 || epollParams->epollFdCnt < 0 || epollParams->epollFdCnt > MAX_FD_NUM_LIMIT ||
        epollParams->startFd < 0 || epollParams->fdCnt < 0 || epollParams->fdCnt > MAX_FD_NUM_LIMIT) {
        KNET_ERR("K-NET telemetry epoll details callback failed, all params must be greater than 0");
        return KNET_ERROR;
    }
    *queId = KnetGetQueIdByPid(epollParams->pid, telemetryInfo);
    if (*queId == -1) {
        KNET_ERR("K-NET telemetry get epoll details failed, can't find queId by pid %u", epollParams->pid);
        return KNET_ERROR;
    }
    return KNET_OK;
}
KNET_STATIC int GetEpollDetailsHookHandler(int dpFd, DP_EpollDetails_t *details, int allocFdAmount, int *workerId)
{
    if (g_dpTelemetryHooks.dpGetEpollDetailsHook == NULL) {
        KNET_ERR("K-NET telemetry epoll details callback failed, dpGetEpollDetailsHook is null");
        return KNET_ERROR;
    }
    int sockFdCount = g_dpTelemetryHooks.dpGetEpollDetailsHook(dpFd, details, allocFdAmount, workerId);
    if (sockFdCount < 0) {
        KNET_ERR("K-NET telemetry epoll details callback failed for dpFd %d, err %d", dpFd, sockFdCount);
    } else if (sockFdCount == 0) {
        KNET_WARN("Called dpFd %d has no event, but the corresponding osFd is type epoll", dpFd);
    }
    return sockFdCount;
}

KNET_STATIC DP_EpollDetails_t *SortSockDetailsByOsFd(DP_EpollDetails_t *sockDetails, int len, int *maxSockFd,
                                                     bool isSecondary)
{
    int fdMax = KNET_FdMaxGet();
    DP_EpollDetails_t *sockDetailsSorted = NULL;

    if (isSecondary) {
        sockDetailsSorted = (DP_EpollDetails_t *)rte_malloc(NULL, (fdMax + 1) * sizeof(DP_EpollDetails_t),
            RTE_CACHE_LINE_SIZE);
    } else {
        sockDetailsSorted = (DP_EpollDetails_t *)calloc(1, (fdMax + 1) * sizeof(DP_EpollDetails_t));
    }
    if (sockDetailsSorted == NULL) {
        KNET_ERR("K-NET telemetry epoll details callback failed, alloc sorted details failed");
        return NULL;
    }
    *maxSockFd = 0;
    for (int i = 0; i < len; i++) {
        sockDetailsSorted[sockDetails[i].fd] = sockDetails[i];
        if (sockDetails[i].fd > *maxSockFd) {
            *maxSockFd = sockDetails[i].fd;
        }
    }
    return sockDetailsSorted;
}

DP_EpollDetails_t *KNET_GetEpollSockDetails(int epFd, int *workerId, int *maxSockFd, bool isSecondary)
{
    int exceptedSockFdCount = GetEpollDetailsHookHandler(epFd, NULL, 0, workerId);
    if (exceptedSockFdCount < 0) {
        return NULL;
    }
    int allocFdAmount = exceptedSockFdCount + RESERVED_EPOLL_EVENT_AMOUNT;
    int detailsFdCount = 0;
    DP_EpollDetails_t *details = (DP_EpollDetails_t *)calloc(1, allocFdAmount * sizeof(DP_EpollDetails_t));
    if (details == NULL) {
        KNET_ERR("K-NET telemetry epoll details callback failed, alloc details failed");
        return NULL;
    }
    detailsFdCount = GetEpollDetailsHookHandler(epFd, details, allocFdAmount, workerId);
    if (detailsFdCount < 0) {
        free(details);
        return NULL;
    }
    DP_EpollDetails_t *sortedDetails = SortSockDetailsByOsFd(details, detailsFdCount, maxSockFd, isSecondary);
    if (sortedDetails == NULL) {
        free(details);
        return NULL;
    }
    free(details);
    return sortedDetails;
}
KNET_STATIC int ProcessPerSockDetail(DP_EpollDetails_t sockDetails, struct rte_tel_data *sockDict)
{
    // 处理每个套接字的详细信息
    if (rte_tel_data_start_dict(sockDict) != 0) {
        return KNET_ERROR;
    }
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, sockDict, "fd", sockDetails.fd);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, sockDict, "expectEvents", sockDetails.expectEvents);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, sockDict, "readyEvents", sockDetails.readyEvents);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, sockDict, "notifiedEvents", sockDetails.notifiedEvents);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, sockDict, "shoted", sockDetails.shoted);
    return KNET_OK;
}
KNET_STATIC int ProcessSockDetails(DP_EpollDetails_t *sockDetails, struct rte_tel_data *sockDetailDict,
                                   int startFd, int fdCnt, int maxSockFd)
{
    if (rte_tel_data_start_dict(sockDetailDict) != 0) {
        KNET_ERR("K-NET telemetry epoll details callback failed, sockDetailDict alloc failed");
        return KNET_ERROR;
    }
    int count = 0;
    int maxCount = (fdCnt == 0) ? maxSockFd + 1 : fdCnt;
    for (int i = 0; i < maxSockFd + 1 && count < maxCount; i++) {
        if ((sockDetails[i].fd != 0 || sockDetails[i].expectEvents != 0) && sockDetails[i].fd >= (uint32_t)startFd) {
            struct rte_tel_data *sockDict = rte_tel_data_alloc();
            if (sockDict == NULL) {
                KNET_ERR("K-NET telemetry epoll details callback failed, sockDict alloc failed");
                return KNET_ERROR;
            }
            if (ProcessPerSockDetail(sockDetails[i], sockDict) != 0) {
                rte_tel_data_free(sockDict);
                KNET_ERR("K-NET telemetry epoll details callback failed, process per sock detail failed");
                return KNET_ERROR;
            }
            char keyName[MAX_JSON_KEY_NAME_LEN] = "socket_";
            (void)snprintf_s(keyName + strlen(keyName), MAX_JSON_KEY_NAME_LEN - strlen(keyName),
                             MAX_JSON_KEY_NAME_LEN - strlen(keyName) - 1, "%u", sockDetails[i].fd);
            if (CheckAddContainerToDict(sockDetailDict, keyName, sockDict) != 0) {
                return KNET_ERROR;
            }
            ++count;
        }
    }
    return KNET_OK;
}
KNET_STATIC int ProcessPerEpollInfo(TelemetryEpollParams *epollParams, EpollTelemetryContext *epollctx,
                                    struct rte_tel_data *epollDict)
{
    if (rte_tel_data_start_dict(epollDict) != 0) {
        KNET_ERR("K-NET telemetry epoll details callback failed, epollDict start dict failed");
        return KNET_ERROR;
    }
    struct rte_tel_data *sockDetailDict = rte_tel_data_alloc();
    if (sockDetailDict == NULL) {
        KNET_ERR("K-NET telemetry epoll details callback failed, epollDict alloc failed");
        return KNET_ERROR;
    }
    if (ProcessSockDetails(epollctx->details, sockDetailDict, epollParams->startFd, epollParams->fdCnt,
                           epollctx->maxSockFd) != 0) {
        rte_tel_data_free(sockDetailDict);
        KNET_ERR("K-NET telemetry epoll details callback failed, process sock details failed");
        return KNET_ERROR;
    }
    if (epollctx->tid == 0) {
        CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_string, epollDict, "tid", INVALID_WORKER_TID);
    } else {
        char tidStr[MAX_TID_LEN] = {0};
        (void)snprintf_s(tidStr, MAX_TID_LEN, MAX_TID_LEN - 1, "%u", epollctx->tid);
        CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_string, epollDict, "tid", tidStr);
    }
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_u64, epollDict, "pid", epollctx->pid);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_int, epollDict, "osFd", epollctx->osFd);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_int, epollDict, "innerFd", epollctx->dpFd);
    if (CheckAddContainerToDict(epollDict, "details", sockDetailDict) != 0) {
        return KNET_ERROR;
    }

    return KNET_OK;
}
KNET_STATIC int ProcessEpollInfo(TelemetryEpollParams *epollParams, EpollTelemetryContext *epollctx,
                                 struct rte_tel_data *data)
{
    struct rte_tel_data *epollDict = rte_tel_data_alloc();
    if (epollDict == NULL) {
        KNET_ERR("K-NET telemetry epoll details callback failed, epollDict alloc failed");
        return KNET_ERROR;
    }
    if (ProcessPerEpollInfo(epollParams, epollctx, epollDict) != 0) {
        rte_tel_data_free(epollDict);
        KNET_ERR("K-NET telemetry epoll details callback failed, process per epoll info failed");
        return KNET_ERROR;
    }
    char keyName[MAX_JSON_KEY_NAME_LEN] = "epoll_";
    (void)snprintf_s(keyName + strlen(keyName), MAX_JSON_KEY_NAME_LEN - strlen(keyName),
                     MAX_JSON_KEY_NAME_LEN - strlen(keyName) - 1, "%d", epollctx->osFd);
    if (CheckAddContainerToDict(data, keyName, epollDict) != 0) {
        return KNET_ERROR;
    }

    return KNET_OK;
}
KNET_STATIC uint32_t GetEpollWorkerTid(int workerId)
{
    uint32_t tid = 0;
    // 非共线程场景workerId为-1，没有意义
    if (workerId != -1) {
        if (KnetGetTidByWorkerId(workerId, &tid) != 0) {
            KNET_ERR("K-NET telemetry epoll details failed, get tid by workerId %d failed", workerId);
            return 0;
        }
    }
    return tid;
}
KNET_STATIC int ProcessEpollDetailsInfo(TelemetryEpollParams *epollParams, struct rte_tel_data *data)
{
    int fdMax = KNET_FdMaxGet();
    bool isSecondary = false;
    int count = 0;

    if (rte_tel_data_start_dict(data) != 0) {
        KNET_ERR("K-NET telemetry epoll details callback failed, data start dict failed");
        return KNET_ERROR;
    }
    epollParams->epollFdCnt = epollParams->epollFdCnt == 0 ? (uint32_t)fdMax : epollParams->epollFdCnt;
    for (int osFd = (int)epollParams->epollStartFd; osFd < fdMax && count < (int)epollParams->epollFdCnt; ++osFd) {
        if (KNET_IsFdHijack(osFd) && KNET_GetFdType(osFd) == KNET_FD_TYPE_EPOLL) {
            int epollDpFd = KNET_OsFdToDpFd(osFd);
            int workerId = 0;
            int maxSockFd = 0;
            DP_EpollDetails_t *sockDetails = KNET_GetEpollSockDetails(epollDpFd, &workerId, &maxSockFd, isSecondary);
            if (sockDetails == NULL) {
                KNET_ERR("K-NET telemetry epoll details callback failed, get epoll sock details failed");
                return KNET_ERROR;
            }
            EpollTelemetryContext epollctx = {.isSecondary = isSecondary,
                                              .pid = epollParams->pid,
                                              .tid = GetEpollWorkerTid(workerId),
                                              .osFd = osFd,
                                              .dpFd = epollDpFd,
                                              .details = sockDetails,
                                              .maxSockFd = maxSockFd};
            if (ProcessEpollInfo(epollParams, &epollctx, data) != KNET_OK) {
                free(sockDetails);
                KNET_ERR("K-NET telemetry epoll details callback failed, process epoll info failed");
                return KNET_ERROR;
            }
            count++;
            free(sockDetails);
        }
    }
    return KNET_OK;
}
int KnetTelemetryEpollDetailsCallback(const char *cmd, const char *params, struct rte_tel_data *data)
{
    if (data == NULL || params == NULL) {
        KNET_ERR("K-NET telemetry epoll details callback failed, data or params is null");
        return KNET_ERROR;
    }
    TelemetryEpollParams epollParams = {0};
    if (ParseEpollDetailsParams(params, &epollParams) != KNET_OK) {
        KNET_ERR("K-NET telemetry epoll details callback failed, parse params failed");
        return KNET_ERROR;
    }
    if (ProcessEpollDetailsInfo(&epollParams, data) != KNET_OK) {
        KNET_ERR("K-NET telemetry epoll details callback failed, get epoll details info failed");
        return KNET_ERROR;
    }
    return KNET_OK;
}

KNET_STATIC void FreeSockDetailsResource(EpollTelemetryContext *epollctx)
{
    // 统一释放所有 details
    int j = 0;
    while (!epollctx[j].isLast) {
        if (epollctx[j].details != NULL) {
            rte_free(epollctx[j].details);
            epollctx[j].details = NULL;
        }
        ++j;
    }
}

KNET_STATIC int ProcessMultiEpollDetails(TelemetryEpollParams *epollParams, EpollTelemetryContext *epollctx,
                                         struct rte_tel_data *data)
{
    int i = 0;
    uint32_t count = 0;
    int ret = KNET_OK;

    if (rte_tel_data_start_dict(data) != 0) {
        KNET_ERR("K-NET telemetry epoll details callback failed, data start dict failed");
        return KNET_ERROR;
    }
    epollParams->epollFdCnt = epollParams->epollFdCnt == 0 ? MAX_FD_NUM_LIMIT : epollParams->epollFdCnt;
    while (!epollctx[i].isLast && count < epollParams->epollFdCnt) {
        if (epollctx[i].osFd >= (int)epollParams->epollStartFd) {
            if (ProcessEpollInfo(epollParams, &epollctx[i], data) != KNET_OK) {
                KNET_ERR("K-NET telemetry get epoll details failed, process socket states failed");
                ret = KNET_ERROR;
                break; // 失败时退出循环
            }
            ++count;
        }
        ++i;
    }
    FreeSockDetailsResource(epollctx);
    return ret;
}

int KnetTelemetryEpollDetailsCallbackMp(const char *cmd, const char *params, struct rte_tel_data *data)
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
    TelemetryEpollParams epollParams = {0};
    int queId = -1;
    if (ParseEpollDetailsParamsAndGetQueId(params, &epollParams, &queId, telemetryInfo) != KNET_OK) {
        return KNET_ERROR;
    }
    telemetryInfo->telemetryType = KNET_TELEMETRY_GET_EPOLL_STAT;
    telemetryInfo->epollDetailCtx = NULL;
    telemetryInfo->msgReady[queId] = 1;
    if (KnetHandleTimeout(telemetryInfo, queId) != KNET_OK) {
        KNET_ERR("K-NET telemetry get net state failed, handle timeout");
        return KNET_ERROR;
    }

    if (telemetryInfo->epollDetailCtx == NULL) {
        KNET_ERR("K-NET telemetry get net state failed, telemetryInfo socketStates is null");
        return KNET_ERROR;
    }

    if (ProcessMultiEpollDetails(&epollParams, telemetryInfo->epollDetailCtx, data) != KNET_OK) {
        KNET_ERR("K-NET telemetry get epoll details failed, process multi epoll details failed");
        return KNET_ERROR;
    }

    rte_free(telemetryInfo->epollDetailCtx);
    return KNET_OK;
}