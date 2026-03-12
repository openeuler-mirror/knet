/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: telemetry初始化
 */
#ifndef __KNET_TELEMETRY_H__
#define __KNET_TELEMETRY_H__

#include "rte_telemetry.h"
#include "dp_debug_api.h"
#include "knet_types.h"


#ifdef __cplusplus
extern "C" {
#endif

#define MAX_PROCESS_NUM 32
#define MAX_OUTPUT_LEN 16384 /* 适配DP_ShowStatistics接口维测信息长度 */
#define KNET_TELEMETRY_MZ_NAME "knet_telemetry_debug_info_mz"
#define KNET_TELEMETRY_PERSIST_MZ_NAME "knet_telemetry_persist_mz"
#define UNCONNECTED_FLAG 0xFFFFFFFF

/* 统计信息输出打印方式 */
typedef enum {
    KNET_STAT_OUTPUT_TO_LOG = 0,   /* 打印到日志 */
    KNET_STAT_OUTPUT_TO_SCREEN,    /* 打印到屏幕 */
    KNET_STAT_OUTPUT_TO_TELEMETRY, /* 打印到telemetry */
    KNET_STAT_OUTPUT_TO_FILE,      /* 打印到文件 */
    KNET_STAT_OUTPUT_MAX
} KNET_StatOutputType;

/* telemetry回调处理类型 */
typedef enum {
    KNET_TELEMETRY_STATISTIC = 0,
    KNET_TELEMETRY_UPDATE_QUE_INFO,
    KNET_TELEMETRY_GET_FD_COUNT,
    KNET_TELEMETRY_GET_NET_STAT,
    KNET_TELEMETRY_GET_SOCKET_INFO,
    KNET_TELEMETRY_GET_EPOLL_STAT,
    KNET_TELEMETRY_MAX
} KNET_TelemetryType;

/* 存储单个fd的socket详细信息 */
typedef struct {
    bool isLast;
    uint32_t tid;
    int osFd;
    int dpFd;
    DP_SocketState_t dpSocketState;
} KNET_SocketState;

typedef struct {
    int osFd;
    bool isReady;
    DP_SockDetails_t dpSockDetails;
} KNET_SocketDetails;

typedef struct {
    bool isLast;
    bool isSecondary;
    uint32_t pid;
    uint32_t tid;
    int osFd;
    int dpFd;
    DP_EpollDetails_t *details;
    int maxSockFd;
    int sockCount;
} EpollTelemetryContext;

typedef struct {
    int msgReady[MAX_QUEUE_NUM];  // 从进程判断消息是否需要发送，1表示需要
    uint32_t statType;
    KNET_TelemetryType telemetryType;  // 回调类型
    char message[MAX_QUEUE_NUM][MAX_OUTPUT_LEN];
    uint32_t pid[MAX_QUEUE_NUM];  // 共享内存内记录pid
    uint32_t tid[MAX_QUEUE_NUM];  // 共享内存内记录tid
    uint32_t lcoreId[MAX_QUEUE_NUM];  // 共享内存内记录lcoreId
    KNET_SocketState *socketStates; // 单个进程内所有连接信息
    KNET_SocketDetails socketDetails; // 单个fd详细信息
    EpollTelemetryContext *epollDetailCtx;
} KNET_TelemetryInfo;

typedef enum {
    KNET_TELE_PERSIST_INTI,       /* 初始化状态 */
    KNET_TELE_PERSIST_WAITSECOND, /* 主进程等待从进城刷新数据 */
    KNET_TELE_PERSIST_MSGREADY,   /* 从进程消息准备好 */
    KNET_TELE_PERSIST_ERROR,      /* 从进程消息准备出错 */
    KNET_TELE_PERSIST_MAX
} KNET_TELE_PERSIST_STATE;

typedef struct {
    KNET_TELE_PERSIST_STATE state;
    pid_t curPid;
    DP_StatType_t msgType;
    char message[DP_STAT_MAX][MAX_OUTPUT_LEN];
} KNET_TelemetryPersistInfo;

/**
 * @brief 进行支持dpdk-telemetry工具必要的初始化操作，包括初始化运行时目录、查找遥感套接字、创建硬链接和注册遥感命令
 * @return int32_t 0：成功-1：失败
 */
int32_t KNET_InitDpdkTelemetry(void);

/**
 * @brief 进行资源释放，删除创建的用于支持dpdk-telemetry工具的套接字文件
 * @return int32_t 0：成功-1：失败
 */
int32_t KNET_UninitDpdkTelemetry(void);

/**
 * @brief 将tcp接口获取到的报文打点统计信息写入共享内存
 * @param output 信息的字符缓冲区
 * @param len 缓冲区的长度
 * @return int 0：成功-1：失败
 * @attention 只能被 KNET_ACC_Debug 调用，调用前 output 已做判空
 */
int KNET_DebugOutputToTelemetry(const char *output, uint32_t len);

typedef void (*KNET_DpShowStatisticsHook)(DP_StatType_t type, int workerId, uint32_t flag);
typedef int (*KNET_DpSocketCountGetHook)(int type);
typedef int (*KNET_DpGetSocketStateHook)(int fd, DP_SocketState_t *state);
typedef int (*KNET_DpGetSocketDetailsHook)(int fd, DP_SockDetails_t *details);
typedef int (*KNET_DpGetEpollDetailsHook)(int fd, DP_EpollDetails_t *details, int len, int *wid);

typedef struct {
    KNET_DpShowStatisticsHook dpShowStatisticsHook;
    KNET_DpSocketCountGetHook dpSocketCountGetHook;
    KNET_DpGetSocketStateHook dpGetSocketStateHook;
    KNET_DpGetSocketDetailsHook dpGetSocketDetailsHook;
    KNET_DpGetEpollDetailsHook dpGetEpollDetailsHook;
} KNET_DpTelemetryHooks;
/**
 * @brief 注册协议栈遥测功能钩子
 * @param dpTelemetryHooks 协议栈遥测钩子
 * @return int 0：成功 -1：失败
 */
int KNET_DpTelemetryHookReg(KNET_DpTelemetryHooks dpTelemetryHooks);

/**
 * @brief 注册协议栈遥测功能钩子
 * @param hook 协议栈遥测钩子
 * @return int 0：成功 -1：失败
 */
int KNET_DpShowStatisticsHookRegPersist(KNET_DpShowStatisticsHook hook);

/**
 * @brief 将调试信息输出到文件
 * @param output 信息的字符缓冲区
 * @param len 缓冲区的长度
 * @return int 0：成功 -1：失败
 * @attention 只能被 KNET_ACC_Debug 调用，调用前 output 已做判空
 */
int KNET_DebugOutputToFile(const char *output, uint32_t len);

/**
 * @brief 更新queueId到pid和tid的映射关系
 * @param queId 队列号
 * @param pid 进程号
 * @param tid 线程号
 * return int 0, 成功; -1, 失败;
 */
int KNET_MaintainQueue2TidPidMp(uint32_t queId);

/**
 * @brief 获取epoll的socket详细信息
 * @param epFd epoll的fd
 * @param workerId 工作进程id
 * @param maxSockFd 最大socket fd
 * @param sockCount epoll监听socket数量
 * @param isSecondary 是否是从进程
 * @return DP_EpollDetails_t* 详细信息
 * @note: 返回的内存需要调用者释放
 */
DP_EpollDetails_t *KNET_GetEpollSockDetails(int epFd, int *workerId, int *maxSockFd, int *sockCount, bool isSecondary);

/**
 * @brief 启动数据持久化线程
 * return uint64_t tid, 成功; 0, 失败;
 */
uint64_t KNET_TelemetryStartPersistThread();

/**
 * @brief 退出持久化线程
 */
void KNET_TelemetrySetPersistThreadExit(void);

#ifdef __cplusplus
}
#endif
#endif  // __KNET_TELEMETRY_H__
