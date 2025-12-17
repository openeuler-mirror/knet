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
#include "dp_in_api.h"
#include "dp_ether_api.h"

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

typedef struct DP_NdItem {
    union {
        DP_InAddr_t  ipv4;
        uint32_t     padding[4];
    } dst;
    DP_EthAddr_t mac;
    uint16_t     state;
    uint8_t      priv[10]; // 预留字段
    void*        cb;       // 预留字段
    uint32_t     ifIndex;
    uint32_t     time;     // 预留字段
} DP_NdItem_t;

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

typedef enum {
    DP_NEW_NL = 0,
    DP_DEL_NL,
} DP_NetlinkOp_t;

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
    DP_NDA_NS,
    DP_NDA_MAX,
};

int DP_NdCfg(DP_NdOp_t op, DP_NdMsg_t* msg, DP_TbmAttr_t* attrs[], int attrCnt);

typedef struct DP_TBM_Notify {
    int      pid;
    uint32_t groups; // 组播位，复用dp_netlink.h中定义的组播位

    /**
     * @brief
     * type: TBM_NOTIFY_TYPE_*
     * op: 各自对象的op
     * family: DP_AF_INET、DP_AF_INET6
     * item: 表项信息DP_TBM_Item_t，使用者自行保存
     */
    void (*cb)(void* tn, int type, int op, uint8_t family, void* item);
    void* ctx;
} DP_TBM_Notify_t;

/**
 * @brief     CP注册arp表项miss上报回调
 * @attention 在协议栈初始化过程中调用

 * @param  ns [IN] 网络空间id
 * @param  tn [IN] 回调函数，DP适配CP接口nbc_arp_miss_proc实现
 * @retval 返回0 成功
 * @retval 返回-1 失败
 */
int DP_TbmAddNotify(int ns, DP_TBM_Notify_t* tn);

#ifdef __cplusplus
}
#endif
#endif
