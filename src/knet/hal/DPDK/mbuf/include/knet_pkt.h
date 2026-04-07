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

#ifdef __cplusplus
extern "C" {
#endif

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

#define KNET_PKT_DBG_SIZE  128 // TM280网卡要求mbuf data起始地址128字节对齐，所以该值需要为128；sp670网卡该值可为64

/**
 * @ingroup knet_pkt
 * @brief 报文池创建算法
 */
typedef enum {
    KNET_PKTPOOL_ALG_RING_MP_MC,
    KNET_PKTPOOL_ALG_BUTT
} KNET_PktPoolAlg;

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

/**
 * @brief 指针偏移，将 mbuf 转换为 pkt
 *
 * @param mbuf 参数类型 struct rte_mbuf *，待转换的 mbuf 指针
 * @return void * 转换后得到的 pkt 指针
 */
static KNET_ALWAYS_INLINE void *KNET_Mbuf2Pkt(struct rte_mbuf *mbuf)
{
    return KnetPtrAdd(mbuf, sizeof(struct rte_mbuf) + KNET_PKT_DBG_SIZE);
}

/**
 * @brief 指针偏移，将 pkt 转换为 mbuf
 *
 * @param pkt 参数类型 void *，待转换的 pkt 指针
 * @return rte_mbuf *，转换后得到的 mbuf 指针
 */
static KNET_ALWAYS_INLINE struct rte_mbuf *KNET_Pkt2Mbuf(void *pkt)
{
    return KnetPtrSub(pkt, sizeof(struct rte_mbuf) + KNET_PKT_DBG_SIZE);
}

/**< 创建报文池，初始化报文池中私有数据区的钩子函数 */
typedef void (*KNET_PktInit)(void *pkt);

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
    KNET_PktPoolAlg createAlg;         /**< 报文池创建算法 */
    int32_t numaId;                  /**< numaId */
    KNET_PktInit init;                 /**< 私有数据区初始化钩子，可选 */
} KNET_PktPoolCfg;

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
 * <ul><li> knet_pkt：该接口所属的开发包。</li>
 * <li> knet_pkt.h：该接口声明所在的头文件。</li></ul>
 * @since CE1800V V100R021C00
 * @see
*/
uint32_t KNET_PktModInit(void);

/**
 * @ingroup knet_pkt
 * @brief 创建报文池
 *
 * @par 描述: 根据指定的报文池配置信息，创建报文池。
 *
 * @param cfg    [IN] 参数类型 #const KNET_PktPoolCfg。pktPool配置信息
 * @param poolId [OUT] 参数类型 #uint32_t *。报文池创建成功后返回的报文池ID
 *
 * @retval #KNET_OK 成功
 * @retval #KNET_ERROR 失败
 *
 * @par 依赖
 * <ul><li> knet_pkt：该接口所属的开发包。</li>
 * <li> knet_pkt.h：该接口声明所在的头文件。</li></ul>
 * @since CE1800V V100R021C00
 * @see KNET_PktPoolDestroy
*/
uint32_t KNET_PktPoolCreate(const KNET_PktPoolCfg *cfg, uint32_t *poolId);

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
 * <ul><li> knet_pkt：该接口所属的开发包。</li>
 * <li> knet_pkt.h：该接口声明所在的头文件。</li></ul>
 * @since CE1800V V100R021C00
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
 * @retval #struct rte_mbuf * 报文私有数据空间起始地址。失败时，返回空指针
 * @par 依赖
 * <ul><li> knet_pkt：该接口所属的开发包。</li>
 * <li> knet_pkt.h：该接口声明所在的头文件。</li></ul>
 * @since CE1800V V100R021C00
 * @see KNET_PktNewPkt
*/
struct rte_mbuf *KNET_PktAlloc(uint32_t poolId);

/**
 * @ingroup knet_pkt
 * @brief 释放报文空间
 *
 * @param    mbuf  [IN] 参数类型 #struct rte_mbuf *。报文私有数据空间起始地址
 *
 * @retval #void
 * @par 依赖
 * <ul><li> knet_pkt：该接口所属的开发包。</li>
 * <li> knet_pkt.h：该接口声明所在的头文件。</li></ul>
 * @since CE1800V V100R021C00
 * @see KNET_PktAlloc
*/
void KNET_PktFree(struct rte_mbuf *mbuf);

/**
 * @ingroup knet_pkt
 * @brief 释放批处理缓存的mbuf
 *
 * @retval #void
 * @par 依赖
 * <ul><li> knet_pkt：该接口所属的开发包。</li>
 * <li> knet_pkt.h：该接口声明所在的头文件。</li></ul>
 * @see KNET_PktAlloc
 * @see KNET_PktFree
*/
void KNET_PktBatchFree(void);

/**
 * @brief 将外部缓冲区附加到pkt上
 *
 * @param pkt 包地址
 * @param addr 外部缓冲区物理地址
 * @param iova 外部缓冲区虚拟地址
 * @param len 外部缓冲区长度
 * @param shinfo attach所需的共享信息
 */
void KNET_MbufAttachExtBuf(struct rte_mbuf *pkt, void *addr, uint64_t iova, uint16_t len, void *shinfo);

#ifdef __cplusplus
}
#endif
#endif // __KNET_PKT_H__
