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

#ifndef __KNET_PKT_H__
#define __KNET_PKT_H__

#include "rte_mbuf.h"
#include "knet_types.h"

/**
 * @ingroup knet_pkt
 * @brief 非法报文池ID
 */
#define KNET_PKTPOOL_INVALID_ID 0xFFFFFFFF

/**
 * @ingroup knet_pkt
 * @brief 报文池名字长度
 */
#define KNET_PKTPOOL_NAME_LEN  24

/**
 * @brief 报文维测空间大小
 */
#define KNET_PKT_DBG_SIZE  64

/**
 * @ingroup knet_pkt
 * @brief 报文池创建算法
 */
typedef enum {
    KNET_PKTPOOL_ALG_RING_MP_MC,
    KNET_PKTPOOL_ALG_BUTT
} KnetPktPoolAlg;

/**
 * @brief rte_mbuf的影子结构，性能原因部分API需要内联，为不暴露rte_mbuf
 * @attention 外部用户禁止直接使用该结构体，只能通过该头文件提供的宏、函数间接使用
 */
struct MbufShadow {
    void *bufAddr;
    uint8_t resv1[8]; // 8用于保留区域
    uint16_t dataOff;
    uint8_t resv2[2]; // 2用于保留区域
    uint16_t nbSegs;
    uint8_t resv10[2]; // 2用于保留区域
    uint64_t olFlags;
    uint8_t resv3[4]; // 4用于保留区域
    uint32_t pktLen;
    uint16_t dataLen;
    uint8_t resv4[22]; // 22用于保留区域
    struct MbufShadow *next;
    union {
        uint64_t txOffload;
        struct {
            uint64_t resv5 : 24;      // 24用于占位
            uint64_t tsoSegsz : 16;   // 16是该字段所占的位数
            uint64_t resv6 : 24;      // 24用于占位
        };
    };
    uint8_t resv7[8];  // 8用于保留区域
    uint16_t privSize;
    uint8_t resv8[38];  // 38用于保留区域
} __attribute__((__aligned__((KNET_MBUF_CACHE_LINE_SIZE))));

/**
 * @brief pkt的结构说明图
 * @attention
 * @li rte_mbuf的长度为sizeof(rte_mbuf)，其他根据创建时的配置来决定
 *                   |<---privateSize--->|<-headroomSize->|<-dataroomSize->|
 * +-----------+-----+-------------------+----------------+----------------+
 * | rte_mbuf  | DBG |  pkt privateData  |    headroom    |    dataroom    |
 * +-----------+-----+-------------------+----------------+----------------+
 * ^                 ^
 * mbuf              pkt
 */
static KNET_ALWAYS_INLINE void *KnetMbuf2Pkt(struct rte_mbuf *mbuf)
{
    return KnetPtrAdd(mbuf, sizeof(struct MbufShadow) + KNET_PKT_DBG_SIZE);
}

static KNET_ALWAYS_INLINE void *KnetPkt2Mbuf(void *pkt)
{
    return KnetPtrSub(pkt, sizeof(struct MbufShadow) + KNET_PKT_DBG_SIZE);
}

/**< 创建报文池，初始化报文池中私有数据区的钩子函数 */
typedef void (*KnetPktInit)(void *pkt);

/**
 * @ingroup knet_pkt
 * @brief pkt pool配置信息
 */
typedef struct {
    char name[KNET_PKTPOOL_NAME_LEN]; /**< pkt pool的名字 */
    uint32_t bufNum;                 /**< pkt pool的buf个数 */
    uint32_t cacheNum;               /**< cache数目 */
    uint32_t cacheSize;              /**< 每个cache容纳的buf数目 */
    uint16_t privDataSize;           /**< 私有数据区大小，私有数据区需8字节对齐 */
    uint16_t headroomSize;           /**< 报文头区大小，报文头区需8字节对齐 */
    uint16_t dataroomSize;           /**< 报文数据区大小 */
    uint8_t rsvd[6];                 /**< 保留字段，6为对齐 */
    KnetPktPoolAlg createAlg;         /**< 报文池创建算法 */
    int32_t numaId;                  /**< numaId */
    KnetPktInit init;                 /**< 私有数据区初始化钩子，可选 */
} KnetPktPoolCfg;

/**
 * @ingroup knet_pkt
 * @brief pkt模块初始化接口
 *
 * @par 描述: 模块初始化接口。模块其他功能使用前需调用该初始化接口。
 *
 * @param void 无
 *
 * @retval #KNET_OK 成功
 * @retval #KNET_ERROR 失败
 *
 * @par 依赖
 *  knet_pkt：该接口所属的开发包。
 *  knet_pkt.h：该接口声明所在的头文件。
 * @see
*/
uint32_t KNET_PktModInit(void);

/**
 * @ingroup knet_pkt
 * @brief 创建报文池
 *
 * @par 描述: 根据指定的报文池配置信息，创建报文池。
 *
 * @param cfg    [IN] 参数类型 #const KnetPktPoolCfg。pktPool配置信息
 * @param poolId [OUT] 参数类型 #uint32_t *。报文池创建成功后返回的报文池ID
 *
 * @retval #KNET_OK 成功
 * @retval #KNET_ERROR 失败
 *
 * @par 依赖
 *  knet_pkt：该接口所属的开发包。
 *  knet_pkt.h：该接口声明所在的头文件。
 * @see KNET_PktPoolDestroy
*/
uint32_t KNET_PktPoolCreate(const KnetPktPoolCfg *cfg, uint32_t *poolId);

/**
 * @ingroup knet_pkt
 * @brief 释放报文池。
 *
 * @param    poolId   [IN] 参数类型 #uint32_t。报文池ID
 *
 * @attention
 * @li 报文池中已申请报文时不支持释放报文池
 *
 * @retval #void
 * @par 依赖
 *  knet_pkt：该接口所属的开发包。
 *  knet_pkt.h：该接口声明所在的头文件。
 * @see KNET_PktPoolCreate
*/
void KNET_PktPoolDestroy(uint32_t poolId);

/**
 * @ingroup knet_pkt
 * @brief 申请报文空间
 *
 * @par 描述: 在指定的报文池中申请指定长度的报文空间。报文长度最长为65535，
 * 但是最终能否申请成功，也受限于当前pool池中剩余buf的数量。
 *
 * @param    poolId   [IN] 参数类型 #uint32_t。报文池ID
 * @param    len      [IN] 参数类型 #uint32_t。报文长度，支持范围（0~65535B]
 *
 * @retval #void * 报文私有数据空间起始地址。失败时，返回空指针
 * @par 依赖
 *  knet_pkt：该接口所属的开发包。
 *  knet_pkt.h：该接口声明所在的头文件。
 * @see KNET_PktNewPkt
*/
struct rte_mbuf *KNET_PktAlloc(uint32_t poolId);

/**
 * @ingroup knet_pkt
 * @brief 释放报文空间
 *
 * @param    pkt  [IN] 参数类型 #void *。报文私有数据空间起始地址
 *
 * @retval #void
 * @par 依赖
 *  knet_pkt：该接口所属的开发包。
 *  knet_pkt.h：该接口声明所在的头文件。
 * @see KNET_PktAlloc
*/
void KNET_PktFree(void *pkt);

/**
 * @ingroup knet_pkt
 * @brief 释放批处理缓存的mbuf
 *
 * @retval #void
 * @par 依赖
 *  knet_pkt：该接口所属的开发包。
 *  knet_pkt.h：该接口声明所在的头文件。
 * @see KNET_PktAlloc
 * @see KNET_PktFree
*/
void KNET_PktBatchFree(void);

#endif // __KNET_PKT_H__
