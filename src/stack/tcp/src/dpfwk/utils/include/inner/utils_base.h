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
 * @file utils_base.h
 * @brief
 */

#ifndef UTILS_BASE_H
#define UTILS_BASE_H

#include <stddef.h>
#include <time.h>

#include "dp_clock_api.h"
#include "dp_sem_api.h"

#include "utils_debug.h"

#include "dp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CACHE_LINE 64

typedef uint32_t (*DP_RandHook_t)(void);

/** 内存的基础方法，所有字段必填 */
typedef struct {
    void* (*mallocFunc)(size_t size);
    void (*freeFunc)(void* ptr);
} DP_MemHooks_t;

typedef void* DP_Mutex_t;

/** mutex锁方法，所有字段必填
init: 负责完成mutex lock的内存申请，lock为出参，成功返回0，其他值视为失败
deinit: mutex lock的去初始化，完成锁对象的释放，及内存释放，协议栈不关注返回值
lock: 获取锁，成功返回0，其他值视为失败
trylock: 尝试获取锁，成功返回0，其他值视为失败
unlock: 释放锁，协议栈不关注返回值
 */
typedef struct {
    uint16_t size;
    int (*init)(DP_Mutex_t lock, int shared);
    void (*deinit)(DP_Mutex_t lock);
    int (*lock)(DP_Mutex_t lock);
    int (*tryLock)(DP_Mutex_t lock);
    void (*unlock)(DP_Mutex_t lock);
} DP_MutexLockHooks_t;

typedef struct {
    DP_MemHooks_t*       memFns;
    DP_SemHooks_S*       semFns;
    DP_ClockGetTimeHook        timeFn;
    DP_RandHook_t        randFn;
} DP_Hooks_t;

DP_Hooks_t* UTILS_GetBaseFunc(void);

/** mutex初始化 */
#define MUTEX_INIT(mutex) UTILS_GetBaseFunc()->mutexFns->init((mutex), 0)
#define MUTEX_DEINIT(mutex) UTILS_GetBaseFunc()->mutexFns->deinit((mutex))
#define MUTEX_Size UTILS_GetBaseFunc()->mutexFns->size
#define MUTEX_LOCK(mutex) UTILS_GetBaseFunc()->mutexFns->lock((mutex))
#define MUTEX_UNLOCK(mutex) UTILS_GetBaseFunc()->mutexFns->unlock((mutex))

/** sem初始化 */
#define SEM_INIT(sem) UTILS_GetBaseFunc()->semFns->init((sem), 0, 0)
#define SEM_DEINIT(sem) UTILS_GetBaseFunc()->semFns->deinit((sem))
#define SEM_Size UTILS_GetBaseFunc()->semFns->size
#define SEM_WAIT(sem, timeout) UTILS_GetBaseFunc()->semFns->timeWait((sem), (timeout))
#define SEM_SIGNAL(sem) UTILS_GetBaseFunc()->semFns->post((sem))

/** 内存申请 */
#define MEM_MALLOC(size, mod, type)  DP_MemAlloc((size), (mod), (type))
/** 内存释放 */
#define MEM_FREE(ptr, type)          DP_MemFree((ptr), (type))
/** 按照指定字节对齐申请内存 */
#define MEM_MALLOC_ALIGN(size, align, mod, type) \
    MEM_MALLOC(((size) + (align) - 1) / (align) * (align), (mod), (type))

#define MEMPOOL_CREATE(cfg, attr, mempool) UTILS_GetMpFunc()->mpCreate((cfg), (attr), (mempool))
#define MEMPOOL_DESTROY(mempool) UTILS_GetMpFunc()->mpDestroy((mempool))
#define MEMPOOL_FREE(mempool, ptr) UTILS_GetMpFunc()->mpFree((mempool), (ptr))
#define MEMPOOL_ALLOC(mempool) UTILS_GetMpFunc()->mpAlloc((mempool))

/** 模块ID信息 */
enum {
    MOD_INIT,    /**< 初始化 */
    MOD_CPD,     /**< 控制面对接 */
    MOD_DBG,     /**< 维测日志 */
    MOD_NETDEV,  /**< 网络设备 */
    MOD_NS,      /**< 命名空间 */
    MOD_PBUF,    /**< 包缓存 */
    MOD_PMGR,    /**< 协议管理 */
    MOD_SHM,     /**< 共享内存 */
    MOD_TBM,     /**< 表项管理 */
    MOD_UTILS,   /**< 辅助能力 */
    MOD_WORKER,  /**< 调度实例 */
    MOD_FD,      /**< FD句柄管理 */
    MOD_EPOLL,   /**< EPOLL事件 */
    MOD_POLL,    /**< POLL事件 */
    MOD_SELECT,  /**< SELECT事件 */
    MOD_SOCKET,  /**< SOCKET管理 */
    MOD_NETLINK, /**< ARP SK */
    MOD_ETH,     /**< 以太层 */
    MOD_IP,      /**< IP协议 */
    MOD_TCP,     /**< TCP协议 */
    MOD_UDP,     /**< UDP协议 */
    MOD_MAX,
};

/**
堆内存头部信息，保存模块ID及内存大小，便于统计和释放
+---------+----------+-------------------+
|   mod   |   size   |    malloc_size    |
+---------+----------+-------------------+
^                    ^                   ^
DP_MemInfo_t         MemAlloc
*/
typedef struct {
    uint32_t mod;
    size_t size;
} DP_MemInfo_t;

// 变长内存类型
typedef enum {
    DP_MEM_FIX,  // 初始化固定内存
    DP_MEM_FREE, // 过程释放堆内存
} DP_MemType_t;

/** 时间相关定义 */
#define SEC_PER_HOUR  (60 * 60)
#define MSEC_PER_SEC  (1000)
#define USEC_PER_SEC  (1000 * 1000)
#define USEC_PER_MSEC (1000)
#define NSEC_PER_SEC  (1000LL * 1000LL * 1000LL)
#define NSEC_PER_MSEC (1000 * 1000)

/** 获取时间 */
static inline uint32_t UTILS_TimeNow(void)
{
    if (UTILS_GetBaseFunc()->timeFn == NULL) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
        return (uint32_t)((ts.tv_sec * MSEC_PER_SEC) + (ts.tv_nsec / NSEC_PER_MSEC));
    }

    int64_t seconds = 0;
    int64_t nanoseconds = 0;
    UTILS_GetBaseFunc()->timeFn(DP_CLOCK_MONOTONIC_COARSE, &seconds, &nanoseconds);
    int64_t msNow = (nanoseconds / (int64_t)NSEC_PER_MSEC) + (seconds * (int64_t)MSEC_PER_SEC);

    return (uint32_t)msNow;
}

/** 获取高精度时间 */
#define TIME_GET_HP_TIME(ts)   UTILS_GetBaseFunc()->hpTimeFn((ts))
#define TIME_SUB_HP_TIME(r, l) (((r)->sec - (l)->sec) * NSEC_PER_SEC + (r)->nsec - (l)->nsec)

/** 时间比较, a > b时，返回值大于0；a==b时，返回值等于0；a < b时，返回值小于0 */
#define TIME_CMP(a, b) (int)((a) - (b))

/** 获取随机数 */
#define RAND_GEN() UTILS_GetBaseFunc()->randFn()

#ifndef INT_MAX
#define INT_MAX (0x7FFFFFFF)
#endif

/** 分支预测 */
#define UTILS_LIKELY(x)   (x)
#define UTILS_UNLIKELY(x) (x)

#define UTILS_SWAP16(x) ((((x) >> 8) & 0xFF) | (((x) & 0xFF) << 8))
#define UTILS_SWAP32(x) ((((x) >> 24) & 0xFF) | (((x) >> 8) & 0xFF00) | (((x) & 0xFF00) << 8) | (((x) & 0xFF) << 24))

#if (DP_BYTE_ORDER == DP_BIG_ENDIAN)
#define UTILS_HTONS(x) (x)
#define UTILS_HTONL(x) (x)
#define UTILS_NTOHS(x) (x)
#define UTILS_NTOHL(x) (x)
#else
#define UTILS_HTONS(x) UTILS_SWAP16(x) /**< htons */
#define UTILS_HTONL(x) UTILS_SWAP32(x) /**< htonl */
#define UTILS_NTOHS(x) UTILS_SWAP16(x) /**< ntohs */
#define UTILS_NTOHL(x) UTILS_SWAP32(x) /**< ntohl */
#endif

#define UTILS_MAX(a, b)                \
    ({                                 \
        typeof(a) aTemp = (a);         \
        typeof(b) bTemp = (b);         \
        aTemp > bTemp ? aTemp : bTemp; \
    })

#define UTILS_MIN(a, b)                \
    ({                                 \
        typeof(a) aTemp = (a);         \
        typeof(b) bTemp = (b);         \
        aTemp < bTemp ? aTemp : bTemp; \
    })

/**
 * @brief 由ptr转换为uint32_t, dst = *(*uint32_t)src
 */
#define UTILS_BYTE2LONG(src)                           \
    ({                                                 \
        uint32_t dst_;                                 \
        ((uint8_t*)(&dst_))[0] = ((uint8_t*)(src))[0]; \
        ((uint8_t*)(&dst_))[1] = ((uint8_t*)(src))[1]; \
        ((uint8_t*)(&dst_))[2] = ((uint8_t*)(src))[2]; \
        ((uint8_t*)(&dst_))[3] = ((uint8_t*)(src))[3]; \
        dst_;                                          \
    })

/**
 * @brief 由uint32_t类型赋值给地址， *(uint32_t*)dst = src
 */
#define UTILS_LONG2BYTE(dst, src)                     \
    do {                                              \
        ((uint8_t*)(dst))[0] = ((uint8_t*)&(src))[0]; \
        ((uint8_t*)(dst))[1] = ((uint8_t*)&(src))[1]; \
        ((uint8_t*)(dst))[2] = ((uint8_t*)&(src))[2]; \
        ((uint8_t*)(dst))[3] = ((uint8_t*)&(src))[3]; \
    } while (0)

#define LIST_ENTRY(type)   \
    struct {               \
        struct type* next; \
        struct type* prev; \
    }

#define LIST_HEAD(name, type) \
    struct name {             \
        struct type* first;   \
        struct type* last;    \
    }

#define LIST_INIT_HEAD(head)   \
    do {                      \
        (head)->first = NULL; \
        (head)->last  = NULL; \
    } while (0)

#define LIST_COPY_HEAD(dst, src)      \
    do {                             \
        (dst)->first = (src)->first; \
        (dst)->last  = (src)->last;  \
        (src)->first = NULL;         \
        (src)->last  = NULL;         \
    } while (0)

#define LIST_FOREACH(head, elm, field) for ((elm) = (head)->first; (elm) != NULL; (elm) = (elm)->field.next)

#define LIST_INSERT_CHECK(head, elm, field)             \
    ({                                                 \
        int         ret_ = 0;                          \
        typeof(elm) temp;                              \
        LIST_FOREACH((head), temp, field)                \
        {                                              \
            if (&((temp)->field) == &((elm)->field)) { \
                ret_ = -1;                             \
                break;                                 \
            }                                          \
        }                                              \
        ret_;                                          \
    })

#define LIST_REMOVE_CHECK(head, elm, field)             \
    ({                                                 \
        int         ret_ = -1;                         \
        typeof(elm) temp;                              \
        LIST_FOREACH((head), temp, field)                \
        {                                              \
            if (&((temp)->field) == &((elm)->field)) { \
                ret_ = 0;                              \
                break;                                 \
            }                                          \
        }                                              \
        ret_;                                          \
    })

#define LIST_INSERT_HEAD(head, elm, field)                \
    do {                                                 \
        ASSERT(LIST_INSERT_CHECK((head), (elm), field) == 0); \
        (elm)->field.prev = NULL;                        \
        (elm)->field.next = (head)->first;               \
        if ((head)->first == NULL) {                     \
            (head)->last = (elm);                          \
        } else {                                         \
            (head)->first->field.prev = (elm);             \
        }                                                \
        (head)->first = elm;                             \
    } while (0)

#define LIST_INSERT_TAIL(head, elm, field)                \
    do {                                                 \
        ASSERT(LIST_INSERT_CHECK((head), (elm), field) == 0); \
        (elm)->field.prev = (head)->last;                \
        (elm)->field.next = NULL;                        \
        if ((head)->first == NULL) {                     \
            (head)->first = (elm);                         \
        } else {                                         \
            (head)->last->field.next = elm;              \
        }                                                \
        (head)->last = elm;                              \
    } while (0)

#define LIST_IS_EMPTY(head) ((head)->first == NULL)

#define LIST_CONCAT(dst, src, field)                    \
    do {                                                \
        if (!LIST_IS_EMPTY(src)) {                       \
            if (LIST_IS_EMPTY(dst)) {                    \
                (dst)->first = (src)->first;            \
                (dst)->last  = (src)->last;             \
            } else {                                    \
                (dst)->last->field.next = (src)->first; \
                (dst)->last             = (src)->last;  \
            }                                           \
            LIST_INIT_HEAD(src);                         \
        }                                               \
    } while (0)

#define LIST_REMOVE_HEAD(head, elm, field)                \
    do {                                                 \
        ASSERT(LIST_REMOVE_CHECK((head), (elm), field) == 0); \
        (elm) = (head)->first;                             \
        if ((elm) != NULL) {                             \
            (head)->first = (elm)->field.next;           \
            if ((elm)->field.next == NULL) {             \
                (head)->last = NULL;                     \
            } else {                                     \
                (elm)->field.next->field.prev = NULL;    \
            }                                            \
            (elm)->field.next = NULL;                    \
            (elm)->field.prev = NULL;                    \
        }                                                \
    } while (0)

#define LIST_REMOVE(head, elm, field)                          \
    do {                                                       \
        ASSERT(LIST_REMOVE_CHECK((head), (elm), field) == 0);       \
        if ((elm)->field.prev != NULL) {                       \
            (elm)->field.prev->field.next = (elm)->field.next; \
        } else {                                               \
            (head)->first = (elm)->field.next;                 \
        }                                                      \
        if ((elm)->field.next != NULL) {                       \
            (elm)->field.next->field.prev = (elm)->field.prev; \
        } else {                                               \
            (head)->last = (elm)->field.prev;                  \
        }                                                      \
        (elm)->field.prev = NULL;                              \
        (elm)->field.next = NULL;                              \
    } while (0)

#define LIST_FIRST(head)      (head)->first
#define LIST_TAIL(head)       (head)->last
#define LIST_NEXT(elm, field) (elm)->field.next
#define LIST_PREV(elm, field) (elm)->field.prev

#define LIST_REMOVE_BEFORE(head, elm) (head)->first = (elm)

#define F_OFFSET(type, filed) (uintptr_t)(&((((type)*)(0))->filed))

#define SIZE_ALIGNED(x, n) (((x) + (n)-1) / (n) * (n))

#define PTR_ALIGNED(ptr, n)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define ALIGNED(x, n)      (((x) + (n) - 1) / (n) * (n))
#define PTR_NEXT(ptr, size) ((uint8_t*)(ptr) + (size))
#define PTR_PREV(ptr, size) ((uint8_t*)(ptr) - (size))

typedef struct HASH_Node {
    LIST_ENTRY(HASH_Node) node;
} HASH_Node_t;

typedef LIST_HEAD(, HASH_Node) HASH_NodeHead_t;

typedef struct {
    uint32_t         mask;
    HASH_NodeHead_t* nhs;
} Hash_t;

#define HASH_GET_SIZE(masklen) (sizeof(HASH_NodeHead_t) * (1 << (masklen)))

#define HASH_INIT(tbl, nodeHeads, masklen)            \
    do {                                              \
        (tbl)->mask = (1 << (masklen)) - 1;           \
        (tbl)->nhs  = (nodeHeads);                    \
        for (uint32_t i = 0; i <= (tbl)->mask; i++) { \
            LIST_INIT_HEAD(&(tbl)->nhs[i]);            \
        }                                             \
    } while (0)

#define HASH_GET_NODE_HEAD(tbl, hashVal) (&(tbl)->nhs[(tbl)->mask & (hashVal)])
#define HASH_IS_EMPTY(tbl, hashVal)     LIST_IS_EMPTY(HASH_GET_NODE_HEAD((tbl), (hashVal)))

#define HASH_INSERT(tbl, hashVal, hashNode)                       \
    do {                                                          \
        HASH_NodeHead_t* hashNh = HASH_GET_NODE_HEAD((tbl), (hashVal)); \
        LIST_INSERT_TAIL(hashNh, (hashNode), node);                  \
    } while (0)

#define HASH_REMOVE(tbl, hashVal, hashNode)                       \
    do {                                                          \
        HASH_NodeHead_t* hashNh = HASH_GET_NODE_HEAD((tbl), (hashVal)); \
        LIST_REMOVE(hashNh, (hashNode), node);                      \
    } while (0)

#define HASH_FOREACH(tbl, hashVal, hashNode) LIST_FOREACH(HASH_GET_NODE_HEAD((tbl), (hashVal)), (hashNode), node)

#define CONTAINER_OF(node, type, field) ((type*)((uint8_t*)(node) - (uintptr_t)&(((type*)0)->field)))

// time wheel
typedef struct TW_Node {
    LIST_ENTRY(TW_Node) node;
} TW_Node_t;

typedef LIST_HEAD(, TW_Node) TW_NodeHead_t;
typedef struct TW_Wheel TW_Wheel_t;

typedef int (*TW_Cb_t)(TW_Wheel_t* tw, TW_Node_t* tn, uint32_t twTick);
// 时间轮主要在计时过程中，触发对应tick的
struct TW_Wheel {
    uint32_t      mask;
    TW_Cb_t       cb;
    TW_NodeHead_t nodeLists[0];
};

// TW_Timer_t主要为计时使用
typedef struct {
    uint32_t twTick;
    uint32_t lastTick;
    uint32_t expiredTick;
    uint32_t intervalTick;
} TW_Timer_t;

static inline size_t TW_GetWheelSize(int bits)
{
    return sizeof(TW_Wheel_t) + sizeof(TW_NodeHead_t) * (1 << bits);
}

static inline void TW_InitWheel(TW_Wheel_t* tw, int bits, TW_Cb_t cb)
{
    tw->mask = (1 << bits) - 1;
    tw->cb   = cb;

    for (uint32_t i = 0; i <= tw->mask; i++) {
        LIST_INIT_HEAD(&tw->nodeLists[i]);
    }
}

static inline void TW_WalkNode(TW_Wheel_t* tw, uint32_t twTick)
{
    TW_NodeHead_t  tmpNh;
    TW_NodeHead_t* nh = &tmpNh;
    TW_Node_t*     node;
    TW_Node_t*     next;
    TW_NodeHead_t* twNh = &tw->nodeLists[twTick & tw->mask];

    LIST_INIT_HEAD(nh);
    LIST_CONCAT(nh, twNh, node);
    node = LIST_FIRST(nh);

    while (node != NULL) {
        next = LIST_NEXT(node, node);
        if (tw->cb(tw, node, twTick) != 0) {
            LIST_INSERT_TAIL(twNh, node, node);
        }
        node = next;
    }
}

static inline void TW_Timeout(TW_Timer_t* timer, uint32_t tickNow, TW_Wheel_t** tws, int twCnt)
{
    while (TIME_CMP(timer->expiredTick, tickNow) < 0) {
        timer->expiredTick += timer->intervalTick;
        timer->twTick++;

        for (int i = 0; i < twCnt; i++) {
            TW_WalkNode(tws[i], timer->twTick);
        }
    }

    timer->lastTick = tickNow;
}

static inline void TW_AddNode(TW_Wheel_t* tw, TW_Node_t* tn, uint32_t twTickExpired)
{
    TW_NodeHead_t* nh;

    nh = &tw->nodeLists[twTickExpired & tw->mask];

    LIST_INSERT_TAIL(nh, tn, node);
}

static inline void TW_DelNode(TW_Wheel_t* tw, TW_Node_t* tn, uint32_t twTickExpired)
{
    TW_NodeHead_t* nh;

    nh = &tw->nodeLists[twTickExpired & tw->mask];

    LIST_REMOVE(nh, tn, node);
}

void* DP_MemAlloc(size_t size, uint32_t mod, DP_MemType_t type);
void DP_MemFree(void* addr, DP_MemType_t type);
uint64_t DP_MemCntGet(uint32_t mod, DP_MemType_t type);

#ifdef __cplusplus
}
#endif
#endif
