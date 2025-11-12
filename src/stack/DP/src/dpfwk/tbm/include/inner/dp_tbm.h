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

#ifndef DP_TBM_H
#define DP_TBM_H

#include <stdint.h>

#include "dp_tbm_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DP_TBM_ATTR_GET_VAL(attr, type) (type*)((DP_TbmAttr_t*)(attr) + 1)

#define DP_TBM_ATTR_ALIGN(len) (((len) + 4 - 1) & ~(4 - 1))

#define DP_TBM_ATTR_OK(attr, attrlen) \
    ((attrlen) >= (int)sizeof(DP_TbmAttr_t) && (attr)->len >= sizeof(DP_TbmAttr_t) && (attr)->len <= (attrlen))

#define DP_TBM_ATTR_LENGTH(len) (DP_TBM_ATTR_ALIGN(sizeof(DP_TbmAttr_t)) + (len))

#define DP_TBM_ATTR_SPACE(len) DP_TBM_ATTR_ALIGN(DP_TBM_ATTR_LENGTH(len))

#define DP_TBM_ATTR_DATA(attr) ((void*)(((char*)(attr)) + DP_TBM_ATTR_LENGTH(0)))

#define DP_TBM_ATTR_PAYLOAD(attr) ((int)((attr)->len) - DP_TBM_ATTR_LENGTH(0))

#define DP_TBM_ATTR_TYPE(attr) (attr)->type

#define DP_TBM_ATTR_LEN(attr) (attr)->len

#define DP_TBM_ATTR_SET_TYPE(attr, attrtype) (attr)->type = (attrtype)

#define DP_TBM_ATTR_SET_LEN(attr, attrlen) (attr)->len = DP_TBM_ATTR_LENGTH(attrlen)

#define DP_TBM_ATTR_SET_DATA(attr, attrdata, datatype) (*((datatype*)(DP_TBM_ATTR_DATA((attr))))) = (attrdata)

#define DP_TBM_ATTR_NEXT(attr) (DP_TbmAttr_t*)(((char*)(attr)) + sizeof(*(attr)) + DP_TBM_ATTR_ALIGN((attr)->len))

typedef enum {
    DP_NEW_LINK = 0,
    DP_DEL_LINK,
    DP_GET_LINK,
} DP_LinkOpt_t;

typedef struct DP_LinkMsg {
    uint8_t  family; // 预留，AF_UNSPEC
    uint16_t type; // dev type
    int      ifindex; // 指定ifindex
    uint32_t ifflags; // ifflags预设标记
    uint32_t ifchange; // 预留字段，必须填为0
} DP_LinkMsg_t;

typedef enum {
    DP_NEWADDR = 0,
    DP_DELADDR,
    DP_GETADDR,
} DP_IfAddrOp_t;

enum {
    DP_IFA_UNSPEC,
    DP_IFA_BROADCAST,
    DP_IFA_LOCAL,
    DP_IFA_MAX,
};

typedef struct DP_IfaddrMsg {
    uint8_t family;
    uint8_t prefix;
    uint8_t flags; // 预留，必须设置为0
    uint8_t scope; // 预留，必须设置为0
    int     ifindex;
} DP_IfaddrMsg_t;

int DP_IfaCfg(DP_IfAddrOp_t op, DP_IfaddrMsg_t* msg, DP_TbmAttr_t* attrs[], int attrCnt);

typedef enum {
    DP_NEW_ND = 0,
    DP_DEL_ND,
    DP_GET_ND,
} DP_NdOp_t;

//!< nd state: 目前仅支持
#define DP_ND_STATE_INCOMPLETE 0x01
#define DP_ND_STATE_REACHABLE  0x02
#define DP_ND_STATE_STALE      0x04
#define DP_ND_STATE_DELAY      0x08
#define DP_ND_STATE_PROBE      0x10
#define DP_ND_STATE_FAILED     0x20
#define DP_ND_STATE_NOARP      0x40
#define DP_ND_STATE_PERMANENT  0x80 // 静态arp

typedef struct DP_NdMsg {
    uint8_t family; // 预留字段
    int     ifindex;
    // ND状态，支持设置DP_ND_STATE_INCOMPLETE、DP_ND_STATE_REACHABLE、DP_ND_STATE_PERMANENT三种状态，其他状态被忽略
    uint16_t state;
    uint8_t  flags; // 预留字段，必须填为0
    uint8_t  type; // 预留字段，必须填为0
} DP_NdMsg_t;

//!< tbm attr
enum {
    DP_NDA_UNSPEC = 0,
    DP_NDA_DST,
    DP_NDA_LLADDR,
    DP_NDA_MAX,
};

int DP_NdCfg(DP_NdOp_t op, DP_NdMsg_t* msg, DP_TbmAttr_t* attrs[], int attrCnt);

#ifdef __cplusplus
}
#endif
#endif
