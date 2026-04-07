/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 定长内存管理算法
 */
#ifndef __KNET_FMM_H__
#define __KNET_FMM_H__

#include "knet_types.h"
#include "knet_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KNET_FMM_POOL_MAX_NUM          256
#define KNET_FMM_POOL_NAME_LEN         32   // DPDK中的限制
#define KNET_FMM_ERROR                 1
#define KNET_FMM_CACHE_SIZE_MULTIPLERT 1.5    // DPDK中的限制

typedef void (*KnetFmmObjInitCb)(void *obj);

typedef struct {
    char name[KNET_FMM_POOL_NAME_LEN]; /* pool的名字，必须带字符串结束符 */
    uint32_t eltNum;         /* pool的内存单元个数 */
    uint32_t eltSize;          /* 每个内存单元的大小 */
    uint32_t cacheSize;     /* 每个cache容纳的内存单元数目 */
    uint32_t socketId;       /* numa id */
    KnetFmmObjInitCb objInit; /* 内存单元初始化钩子函数 */
} KNET_FmmPoolCfg;

typedef void (*KNET_ExtBufFreeCb_t)(void *addr, void *opaque);

struct KNET_MbufExtSharedInfo {
    KNET_ExtBufFreeCb_t freeCb;
    void *opaque;
    uint16_t refcnt;
};

struct KNET_ExtBufFreeInfo {
    KNET_ExtBufFreeCb_t freeCb;     // 用户自定义的释放回调函数
    void *addr;                     // extern buffer 的起始地址
    void *opaque;                   // 用户自定义的释放回调函数所需的输入参数
};

/* extern buffer 首部结构体 */
struct KNET_ExtBuf {
    struct KNET_MbufExtSharedInfo shinfo;
    struct KNET_ExtBufFreeInfo freeInfo;
};

/**
 * @brief 创建定长内存池
 *
 * @param cfg [IN] 参数类型KNET_FmmPoolCfg*。创建内存池的结构体信息
 * @param poolId [OUT] 参数类型uint32_t*。内存池的ID号
 * @return uint32_t 定长内存池创建结果。成功返回0，失败返回0xFFFFFFFF
 */
uint32_t KNET_FmmCreatePool(KNET_FmmPoolCfg *cfg, uint32_t *poolId);

/**
 * @brief 销毁定长内存池
 *
 * @param poolId [IN] 参数类型uint32_t。内存池的ID号
 * @return uint32_t 定长内存池销毁结果。成功返回0，失败返回0xFFFFFFFF
 */
uint32_t KNET_FmmDestroyPool(uint32_t poolId);

/**
 * @brief 申请定长内存
 *
 * @param poolId [IN] 参数类型uint32_t。内存池的ID号
 * @param obj [OUT] 参数类型void**。申请到的内存对象
 * @return uint32_t 定长内存申请结果。成功返回0，失败返回0xFFFFFFFF
 */
uint32_t KNET_FmmAlloc(uint32_t poolId, void **obj);

/**
 * @brief 释放定长内存
 *
 * @param poolId [IN] 参数类型uint32_t。内存池的ID号
 * @param obj [IN] 参数类型void*。需要释放的内存对象
 * @return uint32_t 定长内存释放结果。成功返回0，失败返回0xFFFFFFFF
 */
uint32_t KNET_FmmFree(uint32_t poolId, void *obj);

/**
 * @brief 定长内存池模块的RPC服务初始化
 *
 * @param procType [IN] 参数类型enum KNET_ProcType。进程类型
 * @return int 定长内存池模块的RPC服务初始化结果。成功返回0，失败返回-1
 */
int KNET_InitFmm(enum KNET_ProcType procType);

/**
 * @brief 定长内存池模块的RPC服务注销
 *
 * @param procType [IN] 参数类型enum KNET_ProcType。进程类型
 * @return int 定长内存池模块的RPC服务注销结果。成功返回0，失败返回-1
 */
int KNET_UnInitFmm(enum KNET_ProcType procType);

/**
 * @brief 规范化定长内存池创建cfg中的cache size的值；dpdk限制cacheSize*1.5不得超过eltNum
 *
 * @param eltNum 总内存单元数量
 * @param cacheSize 每个cache容纳的内存单元数目
 * @return uint32_t 规范化后的cacheSize
 */
static inline uint32_t KNET_FmmNormalizeCacheSize(uint32_t eltNum, uint32_t cacheSize)
{
    uint32_t maxCacheSize = (uint32_t)(eltNum / KNET_FMM_CACHE_SIZE_MULTIPLERT);
    if (cacheSize > maxCacheSize) {
        return maxCacheSize;
    }
 
    return cacheSize;
}

#ifdef __cplusplus
}
#endif

#endif