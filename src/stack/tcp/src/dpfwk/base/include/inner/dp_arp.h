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
 * @file dp_arp.h
 * @brief ARP协议首部及相关类型定义
 */

#ifndef DP_ARP_H
#define DP_ARP_H

#include "dp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DP_ARPHDR_HRD_ETHER    1 //!< 以太协议类型
#define DP_ARPHDR_HRD_LOOPBACK 772 //!< LO设备
#define DP_ARPHDR_HRD_NONE     0xFFFF //! none类型

#define DP_ARPHDR_OP_REQUEST  1 //!< ARP request.
#define DP_ARPHDR_OP_REPLY    2 //!< ARP reply.
#define DP_ARPHDR_OP_RREQUEST 3 //!< RARP request.
#define DP_ARPHDR_OP_RREPLY   4 //!< RARP reply.

/** ARP报文首部 */
typedef struct DP_ArpHdr {
    uint16_t hrd; //!< 链路层协议类型
    uint16_t pro; //!< 网络层协议类型
    uint8_t  hln; //!< 链路层地址长度
    uint8_t  pln; //!< 网络层地址长度
    uint16_t op; //!< ARP操作
} DP_PACKED DP_ArpHdr_t;

#ifdef __cplusplus
}
#endif
#endif
