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
#ifndef PBUF_H
#define PBUF_H

#include <stdio.h>

#include "dp_pbuf_api.h"

#include "utils_debug.h"
#include "dp_inet.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DP_PBUF_FLAGS_EXTERNAL   0x01
#define DP_PBUF_FLAGS_REFERENCED 0x02

typedef struct DP_Pbuf Pbuf_t;

/*******************************************************************************
* ------------------------------------------------------------------------------
* |                              结构类型定义                                   |
* ------------------------------------------------------------------------------
********************************************************************************/

/** pbuf链，可以用于报文缓存等 */
typedef struct PBUF_Chain_t {
    Pbuf_t* head;
    Pbuf_t* tail;
    size_t       bufLen;
    uint32_t     pktCnt;
} PBUF_Chain_t;

// 报文类型
enum {
    PBUF_PKTTYPE_UNKONW = 0,
    PBUF_PKTTYPE_HOST,
    PBUF_PKTTYPE_LOCAL,
    PBUF_PKTTYPE_BROADCAST,
    PBUF_PKTTYPE_MULTICAST,
    PBUF_PKTTYPE_FORWARD,
};

#ifndef PBUF_MAX_PAYLOAD_LEN
#define PBUF_MAX_PAYLOAD_LEN 0xFFFF
#endif

#ifndef PBUF_MIN_PAYLOAD_LEN
#define PBUF_MIN_PAYLOAD_LEN 64
#endif

#define PBUF_PKTFLAGS_BROADCAST 0x1
#define PBUF_PKTFLAGS_MULTICAST 0x4

#define PBUF_PKTFLAGS_IFINDEX 0x10 // rx属性，放入到缓冲区后，不能保存netdev指针
#define PBUF_PKTFLAGS_FRAGMENTED 0x20 // rx 属性，标记当前报文分片, tx 可以复用

#define PBUF_PKTFLAGS_NO_ROTUE         0x100 // tx属性，rx可以复用
#define PBUF_PKTFLAGS_IP_INCHDR        0x200 // tx属性，rx可以复用
#define PBUF_PKTFLAGS_ENABLE_BROADCAST 0x400 // tx属性，rx可以复用
#define PBUF_PKTFLAGS_RT_CACHED        0x800 // tx属性，rx可以复用
#define PBUF_PKTFLAGS_FLOW             0x1000 // tx属性，rx可以复用

/*******************************************************************************
* ------------------------------------------------------------------------------
* |                                 内存管理                                    |
* ------------------------------------------------------------------------------
********************************************************************************/

/**
 * @brief 申请pbuf
 *
 * @param headroom 预留的首部空间
 * @param dataroom 预留的数据空间
 * @return pbuf指针，为空时，为内存不足 \n
 此接口为模块内部调用，headroom + dataroom的有效性由调用者保证
 */
Pbuf_t* PBUF_Alloc(uint16_t headroom, uint16_t dataroom);

/**
 * @brief 释放pbuf，如果ref==0时，则释放
 *
 * @param pbuf pbuf指针
 */
void PBUF_Free(Pbuf_t* pbuf);

/** PBUF 引用计数 */
#define PBUF_REF(pbuf)   (++(pbuf)->ref)
#define PBUF_DEREF(pbuf) (--(pbuf)->ref)

/*******************************************************************************
* ------------------------------------------------------------------------------
* |                              报文管理                                       |
* ------------------------------------------------------------------------------
********************************************************************************/

/**
 * @brief 从 data 中构建一个 pbuf
 *
 * @param data 数据
 * @param dataLen 数据长度
 * @param headroom 头部空间
 * @return 构建的 pbuf
 */
Pbuf_t* PBUF_Build(const uint8_t* data, uint16_t dataLen, uint16_t headroom);


/**
 * @brief 使用 pbuf 的数据构建一个 新的 pbuf
 *
 * @param src 源 pbuf
 * @param dataLen 数据长度，内部调用保证 dataLen 小于 src 的 pbuf 长度
 * @param headroom 头部空间
 * @return 构建的 pbuf
 */
Pbuf_t* PBUF_BuildFromPbuf(const Pbuf_t* src, uint16_t dataLen, uint16_t headroom);

/**
 * @brief 添加数据，如果当前数据空间不足，则会申请新的pbuf
 *
 * @param pbuf pbuf指针
 * @param data 数据
 * @param len 长度
 * @return 写入长度
 */
uint16_t PBUF_Append(Pbuf_t* pbuf, const uint8_t* data, uint16_t len);

/**
 * @brief 读取数据
 *
 * @param pbuf pbuf指针
 * @param data 数据
 * @param len 长度
 * @param peek 0：会调整pbuf的offset和segLen字段，1，则只读取数据
 * @return 读数据长度
 */
uint16_t PBUF_Read(Pbuf_t* pbuf, uint8_t* data, size_t len, int peek);

/**
 * @brief 裁剪前部数据
 *
 */
#define PBUF_CUT_DATA(pbuf, len) PBUF_Read((pbuf), NULL, (len), 0)

/**
 * @brief 裁剪尾部数据
 */
uint16_t PBUF_CutTailData(Pbuf_t* pbuf, uint16_t len);

/**
 * @brief 计算pbuf数据的cksum
 *
 * @param pbuf
 * @return
 */
uint32_t PBUF_CalcCksumAcc(Pbuf_t* pbuf);

/** 获取pbuf中数据开始地址 */
#define PBUF_MTOD(pbuf, type) (type)((pbuf)->payload + (pbuf)->offset)

/** 获取首部预留空间 */
#define PBUF_GET_HEADROOM(pbuf) ((pbuf)->offset)

/** 获取报文长度 */
#define PBUF_GET_PKT_LEN(pbuf) ((pbuf)->totLen)

/** 获取PBUF单片的长度 */
#define PBUF_GET_SEG_LEN(pbuf) ((pbuf)->segLen)

/** 获取下一个分片 */
#define PBUF_NEXT_SEG(pbuf) ((pbuf)->next)

/** 获取PBUF分片数 */
#define PBUF_GET_SEGS(pbuf) ((pbuf)->nsegs)

/** 移除首部长度 */
#define PBUF_CUT_HEAD(pbuf, hlen)  \
    do {                          \
        (pbuf)->offset += (hlen); \
        (pbuf)->totLen -= (hlen); \
        (pbuf)->segLen -= (hlen); \
    } while (0)

/** 添加首部长度 */
#define PBUF_PUT_HEAD(pbuf, hlen)  \
    do {                          \
        (pbuf)->offset -= (hlen); \
        (pbuf)->totLen += (hlen); \
        (pbuf)->segLen += (hlen); \
    } while (0)

/** 设置首部长度 */
#define PBUF_SET_HEAD(pbuf, off)                 \
    do {                                         \
        uint16_t hlen_ = (pbuf)->offset - (off); \
        if ((pbuf)->offset > (off)) {            \
            PBUF_PUT_HEAD((pbuf), hlen_);          \
        } else {                                 \
            hlen_ = (off) - (pbuf)->offset;      \
            PBUF_CUT_HEAD((pbuf), hlen_);          \
        }                                        \
    } while (0)

/**
 * @brief pbuf分割
 *
 * @param pbuf pbuf指针
 * @param spliceLen 分割长度
 * @param headroom 分割出来
 * @return 新的pbuf指针
 异常场景：
 1. 空，内存不足
 2. 空，参数错误
 */
Pbuf_t* PBUF_Splice(Pbuf_t* pbuf, uint16_t spliceLen, uint16_t headroom);

Pbuf_t* PBUF_Clone(Pbuf_t* src);

/**
 * @brief PBUF合并，dst和src连接
 */
static inline void PBUF_Concat(Pbuf_t *dst, Pbuf_t *src)
{
    dst->end->next = src;
    dst->end       = src->end;
    dst->nsegs    += src->nsegs;
    dst->totLen   += src->totLen;
}

/**
 * @brief 合并chain上的at及其下一个pbuf
 *
 * @param chain buf chain
 * @param at 待合并的pbuf
 * @return NA
 */
static inline void PBUF_ChainMergeNext(PBUF_Chain_t* chain, Pbuf_t* at)
{
    Pbuf_t *next = at->chainNext;
    ASSERT(next != NULL);  // 当前仅在IP重组场景使用，调用点保证next不为空
    at->chainNext = next->chainNext;
    PBUF_Concat(at, next);
    if (at->chainNext != NULL) {
        at->chainNext->chainPrev = at;
    } else {
        chain->tail = at;
    }

    chain->pktCnt--;
}

/*******************************************************************************
* ------------------------------------------------------------------------------
* |                              协议上下文管理                                 |
* ------------------------------------------------------------------------------
********************************************************************************/

/** 设置/获取协议类型 */
#define PBUF_SET_PTYPE(pbuf, type) ((pbuf)->ptype = (type))
#define PBUF_GET_PTYPE(pbuf)       ((pbuf)->ptype)

#define PBUF_SET_PKT_FLAGS(pbuf, flags)    (pbuf)->pktFlags = (flags)
#define PBUF_GET_PKT_FLAGS(pbuf)           ((pbuf)->pktFlags)
#define PBUF_SET_PKT_FLAGS_BIT(pbuf, flags) (pbuf)->pktFlags |= (flags)

#define PBUF_GET_DEV(pbuf)         ((pbuf)->dev)
#define PBUF_SET_DEV(pbuf, netdev) (pbuf)->dev = (void*)(netdev)

#define PBUF_GET_DST_ADDR4(pbuf)       (pbuf)->paddr.in
#define PBUF_GET_DST_ADDR6(pbuf)       (pbuf)->paddr.in6
// 返回为 void*类型指针
#define PBUF_GET_DST_ADDR(pbuf)                                                                     \
    ({                                                                                              \
        ASSERT(PBUF_GET_L3_TYPE(pbuf) == DP_ETH_P_IP || PBUF_GET_L3_TYPE(pbuf) == DP_ETH_P_IPV6); \
        PBUF_GET_L3_TYPE(pbuf) == DP_ETH_P_IP ? &((pbuf)->paddr.in) : (void*)((pbuf)->paddr.in6);  \
    })

#define PBUF_SET_DST_ADDR4(pbuf, addr) (pbuf)->paddr.in = (addr)
#define PBUF_SET_DST_ADDR6(pbuf, addr) (pbuf)->paddr.in6 = (addr)

#define PBUF_SET_DST_MAC(pbuf, addr) (pbuf)->paddr.mac = (addr)
#define PBUF_GET_DST_MAC(pbuf)       (pbuf)->paddr.mac

#define PBUF_GET_FLOW(pbuf) ((((pbuf)->pktFlags & PBUF_PKTFLAGS_FLOW) != 0) ? (pbuf)->flow : NULL)
#define PBUF_SET_FLOW(pbuf, f)                   \
    do {                                        \
        (pbuf)->flow = (void*)(f);              \
        (pbuf)->pktFlags |= PBUF_PKTFLAGS_FLOW; \
    } while (0)

#define PBUF_SET_ND(pbuf, _nd) (pbuf)->nd = (void*)(_nd)
#define PBUF_GET_ND(pbuf)     ((pbuf)->nd)

#define PBUF_GET_L3_TYPE(pbuf)       (pbuf)->l3type
#define PBUF_SET_L3_TYPE(pbuf, type) (pbuf)->l3type = (type)

#define PBUF_SET_L3_OFF(pbuf) (pbuf)->l3Off = (pbuf)->offset
#define PBUF_GET_L3_OFF(pbuf) (pbuf)->l3Off
#define PBUF_GET_L3_HDR(pbuf) ((pbuf)->l3Off + (pbuf)->payload)

#define PBUF_GET_L4_TYPE(pbuf)       (pbuf)->l4type
#define PBUF_SET_L4_TYPE(pbuf, type) (pbuf)->l4type = (type)

#define PBUF_SET_L4_OFF(pbuf) (pbuf)->l4Off = (pbuf)->offset
#define PBUF_GET_L4_OFF(pbuf) (pbuf)->l4Off
#define PBUF_GET_L4_HDR(pbuf) ((pbuf)->l4Off + (pbuf)->payload)

#define PBUF_SET_L4_LEN(pbuf, len) ((pbuf)->l4Len = (len))

#define PBUF_SET_PKT_TYPE(pbuf, type) ((pbuf)->pktType) = (type)
#define PBUF_GET_PKT_TYPE(pbuf)       ((pbuf)->pktType)

#define PBUF_GET_ENTRY(pbuf)        (pbuf)->pentry
#define PBUF_SET_ENTRY(pbuf, entry) (pbuf)->pentry = (entry)

#define PBUF_GET_QUE_ID(pbuf)      (pbuf)->queid
#define PBUF_SET_QUE_ID(pbuf, qid) (pbuf)->queid = (qid)

#define PBUF_SET_IFINDEX(pbuf, ifi)                 \
    do {                                           \
        (pbuf)->ifindex = (ifi);                     \
        (pbuf)->pktFlags |= PBUF_PKTFLAGS_IFINDEX; \
    } while (0)
#define PBUF_GET_IFINDEX(pbuf)                                  \
    ({                                                         \
        int ifindex = -1;                                      \
        if (((pbuf)->pktFlags & PBUF_PKTFLAGS_IFINDEX) != 0) { \
            ifindex = (pbuf)->ifindex;                         \
        }                                                      \
        ifindex;                                               \
    })

#define PBUF_SET_USER_DATA(pbuf, data) (pbuf)->userData = (data)
#define PBUF_GET_USER_DATA(pbuf) (pbuf)->userData

/*******************************************************************************
* ------------------------------------------------------------------------------
* |                              协议间信息传递管理                              |
* ------------------------------------------------------------------------------
********************************************************************************/

/** 提供协议间信息传递，由协议自行约定，上层协议调用者设置cb字段 */
#define PBUF_GET_CB_SIZE() sizeof(((Pbuf_t*)0)->cb)
#define PBUF_GET_CB(pbuf, type) ((type)((pbuf)->cb))

/*******************************************************************************
* ------------------------------------------------------------------------------
* |                              报文链管理                                     |
* ------------------------------------------------------------------------------
********************************************************************************/

/**
 * @brief buf chain初始化
 *
 * @param chain
 */
static inline void PBUF_ChainInit(PBUF_Chain_t* chain)
{
    chain->head   = NULL;
    chain->tail   = NULL;
    chain->bufLen = 0;
    chain->pktCnt = 0;
}

/**
 * @brief buf chain写入数据，按照指定的首部长度和分片长度生成pbuf，并添加到buf chain中
 *
 * @param chain buf chain
 * @param data 数据
 * @param len 数据长度
 * @param headroom 预留的首部空间
 * @param fragSize 分片长度
 * @return > 0 写入长度 \n
 异常场景： \n
 1. 返回长度 != len，部分写入，内存不足 \n
 2. 0，内存分配失败 \n
 3. -1，参数异常
 */
ssize_t PBUF_ChainWrite(PBUF_Chain_t* chain, uint8_t* data, size_t len, uint16_t fragSize, uint16_t headroom);

/**
 * @brief 将src的数据写入到chain
 *
 * @param chain
 * @param src
 * @param fragSize
 * @param headroom
 * @return
 */
ssize_t PBUF_ChainBuildFromPbuf(PBUF_Chain_t* chain, Pbuf_t* src, uint16_t fragSize, uint16_t headroom);

/**
 * @brief buf chain读取数据
 *
 * @param chain buf chain
 * @param buf 数据缓冲区，如果置空，则不写入
 * @param len 待读取数据长度
 * @param peek 正常情况下，读空PBUF数据后，释放PBUF，此字段非0时，只读取数据，不释放
 * @param resetData 是否需要恢复数据报文
 * @return >0 读取长度
 异常场景： \n
 1. 0 没有数据 \n
 2. -1 参数异常
 */
size_t PBUF_ChainRead(PBUF_Chain_t* chain, uint8_t* buf, size_t len, int peek, int resetData);

/**
 * @brief buf chain从前向后裁剪
 *
 * @param chain buf chain
 * @param len 裁剪长度
 * @return >0 长度
 */
#define PBUF_CHAIN_CUT(chain, len) PBUF_ChainRead((chain), NULL, (len), 0, 1)

#define PBUF_CHAIN_FIRST(chain) (chain)->head
#define PBUF_CHAIN_TAIL(chain)  (chain)->tail
#define PBUF_CHAIN_IS_EMPTY(chain) ((chain)->head == NULL)

#define PBUF_CHAIN_NEXT(pbuf) ((pbuf)->chainNext)
#define PBUF_CHAIN_PREV(pbuf) ((pbuf)->chainPrev)

/**
 * @brief buf chain中的pbuf释放
 *
 * @param chain
 */
void PBUF_ChainClean(PBUF_Chain_t* chain);

/**
 * @brief 向PBUF chain链表尾部插入pbuf
 *
 * @param chain pbuf链
 * @param pbuf 待插入链表的pbuf
 */
static inline void PBUF_ChainPush(PBUF_Chain_t *chain, Pbuf_t *pbuf)
{
    do {
        if ((chain)->tail == NULL) {
            (chain)->head     = (pbuf);
            (pbuf)->chainPrev = NULL;
        } else {
            (chain)->tail->chainNext = (pbuf);
            (pbuf)->chainPrev        = (chain)->tail;
        }
        (chain)->tail     = (pbuf);
        (pbuf)->chainNext = NULL;
        (chain)->pktCnt++;
        (chain)->bufLen += (pbuf)->totLen;
    } while (0);
}

/**
 * @brief 从PBUF chain链表中弹出链表尾部pbuf
 *
 * @param chain pbuf链
 * @param pbuf 弹出的pbuf指针
 */
#define PBUF_CHAIN_POP_TAIL(chain)            \
    ({                                        \
        Pbuf_t* _pbuf = NULL;                 \
        do {                                  \
            if ((chain)->tail == NULL) {      \
                _pbuf = NULL;                 \
                break;                        \
            }                                 \
            _pbuf         = (chain)->tail;    \
            (chain)->tail = _pbuf->chainPrev; \
            if ((chain)->tail == NULL) {      \
                (chain)->head = NULL;         \
            } else {                          \
                (chain)->tail->chainNext = NULL; \
            }                                 \
            (chain)->pktCnt--;                \
            (chain)->bufLen -= _pbuf->totLen; \
        } while (0);                          \
        _pbuf;                                \
    })

/**
 * @brief 从PBUF chain链表中弹出链表头部pbuf
 *
 * @param chain pbuf链
 * @param pbuf 弹出的pbuf指针
 */
#define PBUF_CHAIN_POP(chain)                  \
    ({                                        \
        Pbuf_t* inPbuf = NULL;                 \
        do {                                  \
            if ((chain)->head == NULL) {      \
                inPbuf = NULL;                 \
                break;                        \
            }                                 \
            inPbuf         = (chain)->head;    \
            (chain)->head = inPbuf->chainNext; \
            if ((chain)->head == NULL) {      \
                (chain)->tail = NULL;         \
            } else {                          \
                (chain)->head->chainPrev = NULL; \
            }                                 \
            (chain)->pktCnt--;                \
            (chain)->bufLen -= inPbuf->totLen; \
        } while (0);                          \
        inPbuf;                                \
    })

static inline void PBUF_ChainConcat(PBUF_Chain_t* dst, PBUF_Chain_t* src)
{
    if (dst->head == NULL) {
        *dst = *src;
        PBUF_ChainInit(src);
        return;
    }

    if (src->head == NULL) {
        return;
    }

    dst->tail->chainNext = src->head;
    src->head->chainPrev = dst->tail;

    dst->tail = src->tail;

    dst->bufLen += src->bufLen;
    dst->pktCnt += src->pktCnt;
    PBUF_ChainInit(src);
}

/**
 * @brief 向PBUF链表头部插入pbuf
 *
 */
static inline void PBUF_ChainPushHead(PBUF_Chain_t *chain, Pbuf_t *pbuf)
{
    do {
        if ((chain)->head == NULL) {
            (chain)->tail = (pbuf);
        } else {
            (chain)->head->chainPrev = (pbuf);
        }
        (pbuf)->chainNext = (chain)->head;
        (pbuf)->chainPrev = NULL;
        (chain)->head     = (pbuf);
        (chain)->pktCnt++;
        (chain)->bufLen += (pbuf)->totLen;
    } while (0);
}

/**
 * @brief 在at之前插入节点，如果at== NULL，则在尾部插入节点
 */
static inline void PBUF_ChainInsertBefore(PBUF_Chain_t *chain, Pbuf_t *at, Pbuf_t *pbuf)
{
    do {
        Pbuf_t* chainPrev_;
        if ((at) == NULL) {
            chainPrev_ = (chain)->tail;
        } else {
            chainPrev_ = PBUF_CHAIN_PREV(at);
        }
        (pbuf)->chainNext = (at);
        (pbuf)->chainPrev = chainPrev_;
        if ((at) == NULL) {
            (chain)->tail = (pbuf);
        } else {
            (at)->chainPrev = (pbuf);
        }
        if (chainPrev_ == NULL) {
            (chain)->head = (pbuf);
        } else {
            chainPrev_->chainNext = (pbuf);
        }
        (chain)->pktCnt++;
        (chain)->bufLen += (pbuf)->totLen;
    } while (0);
}

/**
 * @brief 从链表中移除pbuf节点
 */
static inline void PBUF_ChainRemove(PBUF_Chain_t *chain, Pbuf_t *pbuf)
{
    do {
        Pbuf_t* chainPrev_ = PBUF_CHAIN_PREV(pbuf);
        Pbuf_t* chainNext_ = PBUF_CHAIN_NEXT(pbuf);
        if (chainPrev_ == NULL) {
            (chain)->head = chainNext_;
        } else {
            chainPrev_->chainNext = chainNext_;
        }
        if (chainNext_ == NULL) {
            (chain)->tail = chainPrev_;
        } else {
            chainNext_->chainPrev = chainPrev_;
        }
        (chain)->pktCnt--;
        (chain)->bufLen -= (pbuf)->totLen;
        (pbuf)->chainNext = NULL;
        (pbuf)->chainPrev = NULL;
    } while (0);
}

/**
 * @brief 释放掉pbuf及之后的所有报文
 */
static inline uint16_t PBUF_ChainRemoveAfter(PBUF_Chain_t *chain, Pbuf_t *pbuf)
{
    Pbuf_t* curPbuf  = pbuf;
    Pbuf_t* prevPbuf = PBUF_CHAIN_PREV(pbuf);
    Pbuf_t* nextPbuf = NULL;
    uint16_t pktCnt = 0;

    while (curPbuf != NULL) {
        chain->pktCnt--;
        chain->bufLen -= curPbuf->totLen;
        nextPbuf = PBUF_CHAIN_NEXT(curPbuf);
        pktCnt += curPbuf->nsegs;
        PBUF_Free(curPbuf);
        curPbuf = nextPbuf;
    }

    chain->tail = prevPbuf;

    if (prevPbuf == NULL) {
        chain->head = NULL;
    } else {
        prevPbuf->chainNext = NULL;
    }

    return pktCnt;
}

/**
 * @brief extern buffer 申请
 */
void* PBUF_ExtBufAlloc(void);

/**
 * @brief extern buffer 释放
 */
void PBUF_ExtBufFree(void* ebuf);

/**
 * @brief 根据提供的 extern buffer 的内存片段构造 pbuf
 */
Pbuf_t* PBUF_Construct(void* ebuf, uint64_t offset, uint16_t len);

/**
 * @brief zero copy 版本的 pbuf chain read，限制每次读取一个 pbuf 中的数据
 */
size_t PBUF_ChainReadZcopy(PBUF_Chain_t* chain, struct DP_ZIovec* iov);

Pbuf_t* PBUF_BuildZcopy(Pbuf_t* pbuf, uint32_t dataLen, uint32_t segNum, uint16_t fragSize);

int Pbuf_Zcopy_Alloc(Pbuf_t** pbuf);

int PBUF_MpInit(void);
void PBUF_MpDeinit(void);

/**
 * @ingroup pbuf
 * @brief pbuf内存申请钩子
 *
 * @param mp 内存池句柄，全局一个内存池
 * @param payload 内存分配大小
 * @retval 内存指针 成功
 * @retval NULL 失败
 */
typedef Pbuf_t* (*PBUF_PbufAllocHook_t)(void* mp, uint32_t payload);

/**
 * @ingroup pbuf
 * @brief pbuf内存释放钩子
 *
 * @param mp 内存池句柄，全局一个内存池
 * @param pbuf 需要释放的pbuf内存指针
 * @retval NA
 */
typedef void (*PBUF_PbufFreeHook_t)(void* mp, Pbuf_t* pbuf);

/**
 * @ingroup pbuf
 * @brief pbuf内存单元构造钩子
 * 根据 extern buffer 上的某一个内存片段，通过 attach 的方式构造一个 pbuf， \n
 从而避免拷贝的发生，以实现发送方向的零拷贝
 *
 * @param mp 内存池句柄，全局一个内存池
 * @param ebuf 用于构造 pbuf 的 extern buffer 的内存指针
 * @param offset 内存片段的起始地址相较于 extern buffer 起始地址的偏移量
 * @param len 内存片段的长度
 * @retval 内存指针 成功
 * @retval NULL 失败
 * @since
 */
typedef Pbuf_t* (*PBUF_PbufConstruct_t)(void* mp, void* ebuf, uint64_t offset, uint16_t len);

/**
 * @ingroup pbuf
 * pbuf内存管理钩子信息
 */
typedef struct {
    void* mp; // 分配 pbuf 的内存池句柄，全局一个内存池
    uint16_t payloadLen; // 每个pbuf的数据空间大小
    uint16_t res[3];
    PBUF_PbufAllocHook_t pbufAlloc;
    PBUF_PbufFreeHook_t pbufFree;
    PBUF_PbufConstruct_t pbufConstruct;
} PBUF_PbufMemHooks_t;

/**
 * @ingroup pbuf
 * @brief pbuf内存管理接口注册
 *
 * @param pbufMemHooks 外部注册的pbuf内存管理函数
 * @retval 0 成功
 * @retval -1 失败
 */
int PBUF_PbufMemHooksReg(PBUF_PbufMemHooks_t* pbufMemHooks);

/**
 * @ingroup pbuf
 * @brief extern buffer 内存申请钩子
 *
 * @param ep extern buffer pool 句柄，全局存在一个定长内存池
 * @retval 内存指针 成功
 * @retval NULL 失败
 * @since
 */
typedef void* (*PBUF_EbufAllocHook_t)(void* ep);

/**
 * @ingroup pbuf
 * @brief extern buffer 内存释放钩子
 *
 * @param ep extern buffer pool 句柄，全局存在一个定长内存池
 * @param ebuf 需要释放的 extern buffer 内存指针
 * @retval NA
 * @since
 */
typedef void (*PBUF_EbufFreeHook_t)(void* ep, void* ebuf);

/**
 * @ingroup pbuf
 * extern buffer 内存管理钩子信息
 */
typedef struct {
    void* ep; //！用于分配 extern buffer 的内存池，采用定长内存池实现
    PBUF_EbufAllocHook_t ebufAlloc;
    PBUF_EbufFreeHook_t ebufFree;
} PBUF_EbufMemHooks_t;

/**
 * @ingroup pbuf
 * @brief extern buffer 内存管理接口注册
 *
 * @param ebufMemHooks 外部注册的 extern buffer 内存管理函数
 * @retval 0 成功
 * @retval -1 失败
 * @since
 */
int PBUF_EbufMemHooksReg(PBUF_EbufMemHooks_t* ebufMemHooks);

void PBUF_MemHooksUnreg(void);

/**
 * @brief 在零拷贝发送流程中，用于引用类型的pbuf来传递iov所属的extern buffer的信息，
 * 该cb中的内容在将ref pbuf放入sendBuf中时填入，在ref pbuf被从sendQue中取出时使用，
 * ref pbuf从sendQue上取下后将被销毁。
 */
typedef struct {
    void* ebuf;
} RefPbufCb_t;

static inline RefPbufCb_t* PBUF_GetRefPbufCb(Pbuf_t* pbuf)
{
    ASSERT(PBUF_GET_CB_SIZE() >= sizeof(RefPbufCb_t));

    return PBUF_GET_CB(pbuf, RefPbufCb_t*);
}

uint16_t PBUF_GetSegLen(void);

/**
 * @ingroup pbuf
 * @brief ref pbuf 内存申请钩子
 *
 * @param mp ref pbuf pool 句柄
 * @retval 内存指针 成功
 * @retval NULL 失败
 * @since
 */
typedef Pbuf_t* (*PBUF_RefPbufAllocHook_t)(void* mp);

/**
 * @ingroup pbuf
 * @brief extern buffer 内存释放钩子
 *
 * @param mp ref pbuf pool 句柄
 * @param pbuf 需要释放的 extern buffer 内存指针
 * @retval NA
 * @since
 */
typedef void (*PBUF_RefPbufFreeHook_t)(void* mp, Pbuf_t* pbuf);

/**
 * @ingroup pbuf
 * @brief ref pbuf内存单元构造钩子
 * 根据 extern buffer 上的某一个内存片段，通过 attach 的方式构造一个 ref pbuf
 *
 * @param mp 内存池句柄
 * @param ebuf 用于构造 pbuf 的 extern buffer 的内存指针
 * @param offset 内存片段的起始地址相较于 extern buffer 起始地址的偏移量
 * @param len 内存片段的长度
 * @retval 内存指针 成功
 * @retval NULL 失败
 * @since
 */
typedef Pbuf_t* (*PBUF_RefPbufConstruct_t)(void* mp, void* ebuf, uint64_t offset, uint16_t len);

/**
 * @ingroup pbuf
 * ref pbuf 内存管理钩子信息
 */
typedef struct {
    void* mp;
    PBUF_RefPbufAllocHook_t refPbufAlloc;
    PBUF_RefPbufFreeHook_t refPbufFree;
    PBUF_RefPbufConstruct_t refPbufConstruct;
} PBUF_RefPbufMemHooks_t;

/**
 * @ingroup pbuf
 * @brief ref pbuf 内存管理接口注册
 *
 * @param refPbufMemHooks 外部注册的 ref pbuf 内存管理函数
 * @retval 0 成功
 * @retval -1 失败
 * @since
 */
int PBUF_RefPbufMemHooksReg(PBUF_RefPbufMemHooks_t* refPbufMemHooks);

/**
 * @brief ref pbuf 申请
 */
Pbuf_t* PBUF_RefPbufAlloc(void);

/**
 * @brief ref pbuf 释放
 */
void PBUF_RefPbufFree(Pbuf_t* pbuf);

/**
 * @brief ref pbuf 构造
 */
Pbuf_t* PBUF_RefPbufConstruct(void* ebuf, uint64_t offset, uint16_t len);

#ifdef __cplusplus
}
#endif
#endif
