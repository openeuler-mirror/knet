/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 提供PBUF操作，注意禁止直接使用PBUF结构字段
 */

#ifndef DP_PBUF_API_H
#define DP_PBUF_API_H

#include <stdint.h>
#include <stddef.h>

#include "dp_in_api.h"
#include "dp_ether_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup pbuf 报文管理 */

/**
 * @ingroup pbuf
 * pbuf信息
 *
 * PBUF为报文BUF管理块，管理报文长度信息，内存控制等，一个PBUF最多管理64k字节数据, \n
一个报文可以由多个PBUF组成的单向链表组成，目前TCPIP栈不支持报文首部分片
 */
typedef struct DP_Pbuf {
    /* cache line 1 */
    uint8_t* payload; //!< 负载起始地址

    uint32_t totLen; //!< 报文总长度
    uint16_t offset; //!< 报文相对于负载起始地址的偏移量
    uint16_t segLen; //!< 当前PBUF的长度
    uint16_t payloadLen; //!< 负载空间长度

    uint8_t nsegs; //!< 本报文包含的pbuf个数，仅第一片pbuf配置，其余链上的pbuf此字段为1
    uint8_t ref; //!< pbuf被引用的个数
    uint8_t flags; //!< pbuf自身的flags
    uint8_t wid; //!< 实例id

    uint8_t  pentry;
    uint8_t  queid;
    uint16_t pktFlags; //!< 报文属性：host、broadcast、multicast

    union {
        void* dev; //!< netdev，tx/rx均需要使用
        int   ifindex; //!< 用户侧时，需要设置为ifindex
    };
    union {
        DP_InAddr_t   in;
        DP_In6Addr_t* in6; //!< 为了节省内存，这里用指针
        DP_EthAddr_t* mac;
    } paddr;

    void* flow;
    void* nd;

    uint16_t l3type; // dp_ethernet.h中定义的类型
    uint8_t  l3Off;
    uint8_t  l4type; // dp_in.h中定义的类型
    uint8_t  ptype;
    uint8_t  pktType;
    uint16_t l4Off;

    union {
        uint32_t hash;
        struct {
            uint16_t tsoFragSize;
            uint8_t  l4Len;
            uint8_t  reserve;
        };
    };
    uint16_t olFlags;

    struct DP_Pbuf* next; //!< 报文单向链表

    void* mp; //!< 这个字段放到第二个cache，通常来说不应该用到

    struct DP_Pbuf* end; //!< 为了提升处理性能，记录报文链尾部

    //!< 提供PBUF链表能力，与报文的单向链表独立
    struct DP_Pbuf* chainNext;
    struct DP_Pbuf* chainPrev;

    uint8_t cb[24];
} DP_Pbuf_t;


/**
 * @ingroup pbuf
 * @brief 通过已有的数据构建pbuf
 *
 * @param data 数据指针
 * @param dataLen 数据长度
 * @param headroom 预留首部空间，可以为0
 * @retval 返回pbuf指针 成功
 * @retval NULL 失败，可以通过errno获取到错误码 \n
 有两种情况会返回为空： \n
 1. 参数异常：dataLen + headroom超出pbuf能够管理的范围，data或者dataLen为0，错误码为EINVAL \n
 2. 内存不足，错误码为ENOMEM
 */
DP_Pbuf_t* DP_PbufBuild(const uint8_t* data, uint16_t dataLen, uint16_t headroom);

/**
 * @ingroup pbuf
 * @brief PBUF数据读取，会将数据拷贝到data，读空的pbuf不会释放，由调用者释放
 *
 * @param pbuf pbuf指针
 * @param data 数据
 * @param len 长度
 * @retval 返回值 实际读取的长度
 */
uint16_t DP_PbufCopy(DP_Pbuf_t* pbuf, uint8_t* data, size_t len);

/**
 * @ingroup pbuf
 * @brief 释放pbuf
 *
 * @param pbuf PBUF指针，可以为空
 * @retval NA
 */
void DP_PbufFree(DP_Pbuf_t* pbuf);

/**
 * @ingroup pbuf
 * TX 硬件计算 IP 校验和
 */
#define DP_PBUF_OLFLAGS_TX_IP_CKSUM 0x1
#define DP_PBUF_OLFLAGS_TX_TCP_CKSUM 0x2
#define DP_PBUF_OLFLAGS_TX_UDP_CKSUM 0x4

#define DP_PBUF_OLFLAGS_TX_TSO 0x8

/**
 * @ingroup pbuf
 * RX 硬件计算校验和结果正确
 */
#define DP_PBUF_OLFLAGS_RX_IP_CKSUM_GOOD 0x1
#define DP_PBUF_OLFLAGS_RX_IP_CKSUM_BAD 0x2

#define DP_PBUF_OLFLAGS_RX_L4_CKSUM_GOOD 0x4
#define DP_PBUF_OLFLAGS_RX_L4_CKSUM_BAD 0x8


#define DP_PBUF_GET_SEG_LEN(pbuf) (pbuf)->segLen
#define DP_PBUF_SET_SEG_LEN(pbuf, len) ((pbuf)->segLen = (len))

#define DP_PBUF_GET_TOTAL_LEN(pbuf) (pbuf)->totLen
#define DP_PBUF_SET_TOTAL_LEN(pbuf, len) ((pbuf)->totLen = (len))

#define DP_PBUF_GET_SEG_NUM(pbuf) (pbuf)->nsegs
#define DP_PBUF_SET_SEG_NUM(pbuf, num) ((pbuf)->nsegs = (num))

#define DP_PBUF_GET_NEXT(pbuf) (pbuf)->next
#define DP_PBUF_SET_NEXT(pbuf, n) ((pbuf)->next = (n))

#define DP_PBUF_GET_END(pbuf) (pbuf)->end
#define DP_PBUF_SET_END(pbuf, e) ((pbuf)->end = (e))

#define DP_PBUF_GET_REF(pbuf) (pbuf)->ref
#define DP_PBUF_SET_REF(pbuf, r) ((pbuf)->ref = (r))

#define DP_PBUF_GET_PAYLOAD(pbuf) (pbuf)->payload
#define DP_PBUF_SET_PAYLOAD(pbuf, addr) ((pbuf)->payload = (addr))

#define DP_PBUF_GET_PAYLOAD_LEN(pbuf) (pbuf)->payloadLen
#define DP_PBUF_SET_PAYLOAD_LEN(pbuf, len) ((pbuf)->payloadLen = (len))

#define DP_PBUF_GET_OFFSET(pbuf) (pbuf)->offset
#define DP_PBUF_SET_OFFSET(pbuf, o) ((pbuf)->offset = (o))

#define DP_PBUF_GET_MP(pbuf) (pbuf)->mp
#define DP_PBUF_SET_MP(pbuf, m) ((pbuf)->mp =(m))

#define DP_PBUF_GET_OLFLAGS(pbuf) (pbuf)->olFlags
#define DP_PBUF_SET_OLFLAGS(pbuf, flags) ((pbuf)->olFlags = (flags))
#define DP_PBUF_SET_OLFLAGS_BIT(pbuf, b) ((pbuf)->olFlags |= (b))

#define DP_PBUF_GET_TSO_FRAG_SIZE(pbuf) (pbuf)->tsoFragSize
#define DP_PBUF_SET_TSO_FRAG_SIZE(pbuf, size) ((pbuf)->tsoFragSize = (size))

#define DP_PBUF_GET_L2_LEN(pbuf) ((pbuf)->l3Off - (pbuf)->offset)
#define DP_PBUF_GET_L3_LEN(pbuf) ((pbuf)->l4Off - (pbuf)->l3Off)
#define DP_PBUF_GET_L4_LEN(pbuf) ((pbuf)->l4Len)

#define DP_PBUF_MTOD(pbuf, type) (type)((pbuf)->payload + (pbuf)->offset)

/**
 * @ingroup pbuf
 * @brief pbuf内存申请钩子
 *
 * @param mp 内存池句柄，全局一个内存池
 * @param payload 内存分配大小
 * @retval 内存指针 成功
 * @retval NULL 失败
 */
typedef DP_Pbuf_t* (*DP_PbufAllocHook_t)(void* mp, uint32_t payload);

/**
 * @ingroup pbuf
 * @brief pbuf内存释放钩子
 *
 * @param mp 内存池句柄，全局一个内存池
 * @param pbuf 需要释放的pbuf内存指针
 * @retval NA

 */
typedef void (*DP_PbufFreeHook_t)(void* mp, DP_Pbuf_t* pbuf);

/**
 * @ingroup pbuf
 * pbuf内存管理钩子信息
 */
typedef struct {
    void* mp; // 分配 pbuf 的内存池句柄，全局一个内存池
    DP_PbufAllocHook_t pbufAlloc;
    DP_PbufFreeHook_t pbufFree;
} DP_PbufMemHooks_t;

/**
 * @ingroup pbuf
 * @brief pbuf内存管理接口注册
 *
 * @param pbufMemHooks 外部注册的pbuf内存管理函数
 * @retval 0 成功
 * @retval -1 失败

 */
int DP_PbufMemHooksReg(DP_PbufMemHooks_t* pbufMemHooks);

static inline void DP_PbufRawReset(DP_Pbuf_t* pbuf, uint8_t* payload, uint16_t plen)
{
    pbuf->payload    = payload;
    pbuf->payloadLen = plen;
    pbuf->totLen     = 0;
    pbuf->offset     = 0;
    pbuf->segLen     = 0;
    pbuf->nsegs      = 1;
    pbuf->ref        = 1;
    pbuf->flags      = 0;
    pbuf->end        = pbuf;
    pbuf->next       = NULL;
}

#ifdef __cplusplus
}
#endif
#endif
