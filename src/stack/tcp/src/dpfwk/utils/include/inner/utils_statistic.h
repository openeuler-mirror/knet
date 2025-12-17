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
#ifndef UTILS_STATISTIC_H
#define UTILS_STATISTIC_H

#include <stdint.h>

#include "dp_show_api.h"
#include "dp_debug_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 维测信息show全局结构体变量 */
extern DP_DebugShowHook g_debugShow;

#define DEBUG_SHOW g_debugShow

// 保证产品注册的g_debugShow能够处理以下两种长度
#define LEN_STAT 16384          // 产品(计算knet)约束最长处理字符串16k，用于传出打点信息得字符串
#define LEN_INFO 2048           // DP_ShowSocketInfoByFd使用，单次能写入最长长度

#define MOD_LEN 16

/* 协议栈统计项描述字段结构 */
typedef struct {
    uint32_t fieldId;    /* 协议栈统计字段项目编号 */
    char *fieldName;     /* 协议栈统计字段项目名称 */
} DP_MibField_t;

/* 协议栈统计类别信息结构 */
typedef struct {
    uint32_t mibType;                              /* MIB类型 */
    uint32_t fieldNum;                             /* MIB统计项目数量 */
    int8_t mibDescript[DP_MIB_FIELD_NAME_LEN_MAX]; /* MIB类型含义 */
} DP_MibDetail_t;

/* TCP统计结构 */
typedef struct {
    uint64_t fieldStat[DP_TCP_STAT_MAX];
} DP_TcpMibStat_t;

/* 协议栈报文统计结构 */
typedef struct {
    uint64_t fieldStat[DP_PKT_STAT_MAX];
} DP_PktMibStat_t;

/* 协议栈异常打点统计结构 */
typedef struct {
    uint64_t fieldStat[DP_ABN_STAT_MAX];
} DP_AbnMibStat_t;

/* TCP连接统计结构 */
typedef struct {
    uint64_t fieldStat[DP_TCP_CONN_STAT_MAX];
} DP_TcpConnMibStat_t;

/* 协议栈MIB汇总统计结构 */
typedef struct {
    DP_TcpMibStat_t *tcpStat;
    DP_PktMibStat_t *pktStat;
    DP_AbnMibStat_t *abnStat;
    DP_TcpConnMibStat_t *tcpConnStat;
} DP_MibStat_t;

extern DP_MibStat_t g_statMibs;
typedef struct {
    uint64_t ipFragPktNum;    /**< ipv4重组缓存pbuf数目 */
    uint64_t tcpReassPktNum;  /**< ipv4重组缓存pbuf数目 */
    uint64_t sendBufPktNum;   /**< ipv4重组缓存pbuf数目 */
    uint64_t recvBufPktNum;   /**< ipv4重组缓存pbuf数目 */
} DP_PbufStat_t;

/* 全局异常打点统计，不支持workerId统计，通过原子操作增减 */
#define DP_GET_ABN_STAT(field) g_statMibs.abnStat->fieldStat[(field)]
#define DP_ADD_ABN_STAT(field) ATOMIC64_Inc(&(DP_GET_ABN_STAT(field)))
#define DP_SUB_ABN_STAT(field) ATOMIC64_Dec(&(DP_GET_ABN_STAT(field)))

/* 基于workerId统计，tcp打点数据获取及增减 */
#define DP_GET_TCP_STAT(wid, field)        g_statMibs.tcpStat[(wid)].fieldStat[(field)]
#define DP_ADD_TCP_STAT(wid, field, value) DP_GET_TCP_STAT((wid), (field)) += (uint32_t)(value)
#define DP_SUB_TCP_STAT(wid, field, value) DP_GET_TCP_STAT((wid), (field)) -= (uint32_t)(value)
#define DP_INC_TCP_STAT(wid, field)        DP_ADD_TCP_STAT((wid), (field), 1)
#define DP_DEC_TCP_STAT(wid, field)        DP_SUB_TCP_STAT((wid), (field), 1)
#define DP_CLEAR_TCP_STATE(wid, field)     DP_GET_TCP_STAT((wid), (field)) = 0

/* 基于workerId统计，tcp conn打点数据获取及增减 */
#define DP_GET_TCP_CONN_STAT(wid, field)        g_statMibs.tcpConnStat[(wid)].fieldStat[(field)]
#define DP_ADD_TCP_CONN_STAT(wid, field, value) DP_GET_TCP_CONN_STAT((wid), (field)) += (uint32_t)(value)
#define DP_SUB_TCP_CONN_STAT(wid, field, value) DP_GET_TCP_CONN_STAT((wid), (field)) -= (uint32_t)(value)
#define DP_INC_TCP_CONN_STAT(wid, field)        DP_ADD_TCP_CONN_STAT((wid), (field), 1)
#define DP_DEC_TCP_CONN_STAT(wid, field)        DP_SUB_TCP_CONN_STAT((wid), (field), 1)

/* 基于workerId统计，package打点数据获取及增减 */
#define DP_GET_PKT_STAT(wid, field)        g_statMibs.pktStat[(wid)].fieldStat[(field)]
#define DP_ADD_PKT_STAT(wid, field, value) DP_GET_PKT_STAT((wid), (field)) += (uint32_t)(value)
#define DP_SUB_PKT_STAT(wid, field, value) DP_GET_PKT_STAT((wid), (field)) -= (uint32_t)(value)
#define DP_INC_PKT_STAT(wid, field)        DP_ADD_PKT_STAT((wid), (field), 1)
#define DP_DEC_PKT_STAT(wid, field)        DP_SUB_PKT_STAT((wid), (field), 1)

typedef void (*DP_ShowStatByType)(DP_StatType_t type, int workerId, uint32_t flag);

typedef struct {
    uint32_t type;
    DP_ShowStatByType showStat;
} DP_TYPE_STATIS_S;

uint32_t GetMemModName(uint32_t modId, uint8_t *modName);
uint32_t GetFieldName(uint32_t fieldId, DP_StatType_t type, DP_MibStatistic_t *showStat);
uint64_t GetFieldValue(int workerId, uint32_t fieldId, DP_StatType_t type);

uint64_t GetSendBufPktNum(int workerId);
uint64_t GetRecvBufPktNum(int workerId);

uint32_t UTILS_StatInit(void);
void UTILS_StatDeinit(void);

uint32_t UTILS_IsStatInited(void);

#ifdef __cplusplus
}
#endif
#endif
