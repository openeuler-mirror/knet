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
/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * @file dp_tbm_api.h
 * @brief 定义表项管理相关对外接口
 */

#ifndef DP_TBM_API_H
#define DP_TBM_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup tbm 表项管理
 * @ingroup tbm
 */

/**
 * @ingroup tbm
 * 表项attr类型定义
 */
typedef struct DP_TbmAttr {
    uint16_t len;
    uint16_t type;
} DP_TbmAttr_t;

/**
 * @ingroup tbm
 * rt option类型
 */
typedef enum {
    DP_NEW_ROUTE,
    DP_DEL_ROUTE,
    DP_GET_ROUTE,
} DP_RtOpt_t;

/**
 * @ingroup tbm
 * rt info信息
 */
typedef struct DP_RtInfo {
    uint8_t family;    /**< AF_INET/AF_INET6 */
    uint8_t dstLen;    /**< Length of destination */
    uint8_t srcLen;    /**< Length of source */
    uint8_t tos;       /**< 预留字段，必须填0 */

    uint8_t table;     /**< 预留字段，必须填RT_TABLE_DEFAULT */
    uint8_t protocol;  /**< 预留字段，必须填RTPROT_STATIC */
    uint8_t scope;     /**< 预留字段，必须填RT_SCOPE_LINK */
    uint8_t type;      /**< RTN_LOCAL/RTN_UNICAST */

    uint8_t rtm_flags; /**< 预留字段，必须填0 */
} DP_RtInfo_t;

/**
 * @ingroup tbm
 * rta 属性类型
 */
enum {
    DP_RTA_UNSPEC,
    DP_RTA_DST,
    DP_RTA_SRC,
    DP_RTA_IIF,
    DP_RTA_OIF,
    DP_RTA_GATEWAY,
    DP_RTA_PRIORITY,
    DP_RTA_PREFSRC,
    DP_RTA_METRICS,
    DP_RTA_MULTIPATH,
    DP_RTA_PROTOINFO,
    DP_RTA_FLOW,
    DP_RTA_CACHEINFO,
    DP_RTA_NS,
    DP_RTA_MAX
};

/**
 * @ingroup tbm
 * rtInfo type取值
 */
enum {
    DP_RTN_UNSPEC,      /**< unknown route */
    DP_RTN_UNICAST,     /**< a gateway or direct route */
    DP_RTN_LOCAL,       /**< a local interface route */
    DP_RTN_BROADCAST,   /**< a local broadcast route (sent as a broadcast) */
    DP_RTN_ANYCAST,     /**< a local broadcast route (sent as a unicast) */
    DP_RTN_MULTICAST,   /**< a multicast route */
    DP_RTN_BLACKHOLE,   /**< a packet dropping route */
    DP_RTN_UNREACHABLE, /**< an unreachable destination */
    DP_RTN_PROHIBIT,    /**< a packet rejection route */
    DP_RTN_THROW,       /**< continue routing lookup in another table */
    DP_RTN_NAT,         /**< a network address translation rule */
    DP_RTN_XRESOLVE,    /**< refer to an external resolver (not implemented) */
};

/**
 * @ingroup tbm
 * @brief
 *
 * @param op
 * @param msg
 * @param attrs
 * @param attrCnt
 *
 * @retval 0 成功
 * @retval #错误码 失败

 */
int DP_RtCfg(DP_RtOpt_t op, DP_RtInfo_t* msg, DP_TbmAttr_t* attrs[], int attrCnt);

#ifdef __cplusplus
}
#endif
#endif
