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
#include <rte_memcpy.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_log.h>
#include <rte_memzone.h>
#include <rte_errno.h>
#include <rte_string_fns.h>
#include <rte_pcapng.h>
#include <rte_mempool.h>
#include <rte_ring.h>
#include <rte_pdump.h>
#include <rte_ether.h>
#include <rte_config.h>
#include "knet_config.h"
#include "knet_log.h"
#include "knet_lock.h"
#include "knet_pdump.h"

#define RING_NAME "capture-ring"
#define MEMPOOL_NAME "capture_mbufs"
#define MBUF_POOL_CACHE_SIZE 32
#define DEFAULT_SNAPLEN 192
#define DEFAULT_RING_SIZE 2048
#define PCAPNG_ENHANCE_PACKET_BLOCK = 28
#define LOW8BIT_TO_HIGH8BIT 8
#define LOW4BIT1_IN_1BYTE 0x0f
#define HIGH4BIT1_IN_1BYTE 0xf0
#define DROP_FIRST_FOUR_BIT 4
#define BIT_TO_BYTE_MOVE_BIT 2
#define L2_ARP_HEADER_LEN 42
#define L2_IPV4 0x0800
#define L2_PROTOCOL_OFFSET 12
#define L2_ETH_HEADER_LEN 14
#define L3_MIN_IP_HEADER_LEN 20
#define L3_INDICATED_TCP_PROTOCOL 0x06
#define L3_INDICATED_UDP_PROTOCOL 0x11
#define L3_INDICATED_ICMP_PROTOCOL 0x01
#define L3_INDICATED_PROTOCOL_REVERSE_OFFSET 11
#define L4_TCP_HEADER_OFFSET 12
#define L4_UDP_HEADER_LEN 8

enum PdumpOperation { DISABLE = 1, ENABLE = 2 };

/* 写入数据包格式1代还是2代, 1代pcap, 2代pcapng, pcapng目前还没有规划 */
enum PdumpVersion {
    V1 = 1, /* no filtering or snap */
    V2 = 2,
};

struct PdumpRequest {
    uint16_t ver;                       /* 抓包版本，当前支持V1 */
    uint16_t op;                        /* 开关标志位 */
    uint32_t flags;                     /* RX/TX方向的报文 */
    char device[RTE_DEV_NAME_MAX_LEN];  /* 设备名称 */
    uint16_t queue;                     /* 队列号，若抓包从进程指定RTE_PDUMP_ALL_QUEUES，则抓取所有队列 */
    const struct rte_bpf_prm *prm;      /* 过滤条件 */
    KNET_SpinLock sharedLock;           /* 多进程抓包同步锁 */
};

struct PdumpRxTxCb {
    struct rte_ring *ring;
    struct rte_mempool *mp;
    const struct rte_eth_rxtx_callback *rxTxCb;
    const struct rte_bpf *filter;
    enum PdumpVersion ver;
    uint32_t snaplen;
};

struct PdumpCopyArgs {
    struct rte_mbuf **pkts;
    const uint16_t nbPkts;
    char padding[6];   // 填充字节，确保结构体8 字节对齐
    const struct PdumpRxTxCb *cb;
};

struct PdumpCbsRequiredArgs {
    struct rte_ring *ring;
    struct rte_mempool *mp;
    struct rte_bpf *filter;
    enum PdumpVersion ver;
    uint16_t end_q;
    uint16_t port;
    uint16_t queue;
    uint16_t operation;
    uint32_t snaplen;
    struct PdumpRxTxCb* (*getCb)(uint16_t, uint16_t);
    int (*removeCb)(uint16_t, uint16_t, const struct rte_eth_rxtx_callback*);
};

/* 以port 和 queue 为粒度存放抓包cb */
static struct PdumpRxTxCb g_rxCbs[RTE_MAX_ETHPORTS][RTE_MAX_QUEUES_PER_PORT] = {0},
                           g_txCbs[RTE_MAX_ETHPORTS][RTE_MAX_QUEUES_PER_PORT] = {0};

/* 让主进程的共享内存对应指针赋值，调用API的时候直接使用这块共享内存 */
static KNET_SpinLock *g_pdumpSpinlock = NULL;

static struct rte_mbuf* g_dupBufs[DEFAULT_RING_SIZE] = {0};

void SetPdumpLock(KNET_SpinLock * lock)
{
    g_pdumpSpinlock = lock;
}

static uint64_t g_retCbs[DEFAULT_RING_SIZE] = {0};

static struct PdumpRxTxCb* GetRxCbs(uint16_t port, uint16_t queue)
{
    if (port >= RTE_MAX_ETHPORTS) {
        KNET_ERR("Port exceeds ports limit");
        return NULL;
    }
    if (queue >= RTE_MAX_QUEUES_PER_PORT) {
        KNET_ERR("Queue exceeds queue limit");
        return NULL;
    }
    return &g_rxCbs[port][queue];
}

static struct PdumpRxTxCb* GetTxCbs(uint16_t port, uint16_t queue)
{
    if (port >= RTE_MAX_ETHPORTS) {
        KNET_ERR("Port exceeds ports limit");
        return NULL;
    }
    if (queue >= RTE_MAX_QUEUES_PER_PORT) {
        KNET_ERR("Queue exceeds queue limit");
        return NULL;
    }
    return &g_txCbs[port][queue];
}

int GetHeaderLen(struct rte_mbuf *buf)
{
    uint8_t *dataPointer = rte_pktmbuf_mtod(buf, uint8_t *);
    if (buf->pkt_len < L2_ETH_HEADER_LEN) {
        return L2_ETH_HEADER_LEN;
    }
    uint16_t l2IndicatedProtocol = dataPointer[L2_PROTOCOL_OFFSET];
    l2IndicatedProtocol <<= LOW8BIT_TO_HIGH8BIT;
    l2IndicatedProtocol |= dataPointer[L2_PROTOCOL_OFFSET + 1];

    /* 当前只对IPV4精准控制，IPV6会直接截断, 默认返回ARP头 */
    if (l2IndicatedProtocol != L2_IPV4) {
        return L2_ARP_HEADER_LEN;
    }

    if (buf->pkt_len < L2_ETH_HEADER_LEN + L3_MIN_IP_HEADER_LEN) {
        return L2_ETH_HEADER_LEN + L3_MIN_IP_HEADER_LEN;
    }
    uint16_t l3Len = 0;
    l3Len = (dataPointer[L2_ETH_HEADER_LEN] & LOW4BIT1_IN_1BYTE) << BIT_TO_BYTE_MOVE_BIT;

    uint16_t totalL3Len = L2_ETH_HEADER_LEN + l3Len;
    if (buf->pkt_len < totalL3Len) {
        return buf->pkt_len;
    }
    uint16_t l3IndicatedProtocol = dataPointer[totalL3Len - L3_INDICATED_PROTOCOL_REVERSE_OFFSET];
    if (l3IndicatedProtocol == L3_INDICATED_ICMP_PROTOCOL) {
        /* ARP 和 ICMP 一样长 */
        return L2_ARP_HEADER_LEN;
    }
    if (l3IndicatedProtocol == L3_INDICATED_UDP_PROTOCOL) {
        return totalL3Len + L4_UDP_HEADER_LEN;
    }

    if (l3IndicatedProtocol == L3_INDICATED_TCP_PROTOCOL) {
        if (buf -> pkt_len <= totalL3Len + L4_TCP_HEADER_OFFSET) {
            return totalL3Len;
        }
        uint16_t tcpLen = (dataPointer[totalL3Len + L4_TCP_HEADER_OFFSET] & HIGH4BIT1_IN_1BYTE) >>
                          DROP_FIRST_FOUR_BIT << BIT_TO_BYTE_MOVE_BIT;
        return totalL3Len + tcpLen;
    }

    /* 其他默认只返回 L3 层头 */
    return totalL3Len;
}

/* 创建一份拷贝的mbuf放入ring */
void PdumpCopy(struct PdumpCopyArgs* args)
{
    unsigned int idx;
    unsigned int ringEnq;
    uint16_t dPkts = 0;
    const uint16_t nbPkts = (args -> nbPkts > DEFAULT_RING_SIZE ? DEFAULT_RING_SIZE : args -> nbPkts);
    const struct PdumpRxTxCb *cb = args -> cb;
    struct rte_ring *ring = NULL;
    struct rte_mempool *mp = NULL;
    struct rte_mbuf *mbuf = NULL;

    if (cb->filter) {
        rte_bpf_exec_burst(cb->filter, (void **)(args -> pkts), g_retCbs, nbPkts);
    }
    ring = cb->ring;
    mp = cb->mp;
    KNET_SpinlockLock(g_pdumpSpinlock);
    for (idx = 0; idx < nbPkts; idx++) {
        /* 使用BPF返回值作为pcap过滤条件 */
        if (cb->filter && g_retCbs[idx] == 0) {
            continue;
        }
        /* pcap就简单复制 */
        mbuf = rte_pktmbuf_copy((args->pkts)[idx], mp, 0, GetHeaderLen((args->pkts)[idx]));
        if (unlikely(mbuf == NULL)) {
            continue;
        }
        g_dupBufs[dPkts++] = mbuf;
    }
    ringEnq = rte_ring_enqueue_burst(ring, (void *)&g_dupBufs[0], dPkts, NULL);
    if (unlikely(ringEnq < dPkts)) {
        unsigned int drops = dPkts - ringEnq;
        rte_pktmbuf_free_bulk(&g_dupBufs[ringEnq], drops);
    }
    KNET_SpinlockUnlock(g_pdumpSpinlock);
}

uint16_t PdumpRx(uint16_t port, uint16_t queue, struct rte_mbuf **pkts, uint16_t nbPkts,
    uint16_t max_pkts __rte_unused, void *userParams)
{
    const struct PdumpRxTxCb *userPassedCb = userParams;
    struct PdumpCopyArgs args = {
        .pkts = pkts,
        .nbPkts = nbPkts,
        .cb = userPassedCb,
    };

    PdumpCopy(&args);
    return nbPkts;
}

uint16_t PdumpTx(uint16_t port, uint16_t queue, struct rte_mbuf **pkts, uint16_t nbPkts, void *userParams)
{
    const struct PdumpRxTxCb *userPassedCb = userParams;
    struct PdumpCopyArgs args = {
        .pkts = pkts,
        .nbPkts = nbPkts,
        .cb = userPassedCb,
    };

    PdumpCopy(&args);
    return nbPkts;
}

KNET_STATIC int PdumpRegisterRxTxCallbacks(struct PdumpCbsRequiredArgs *args, uint8_t flag)
{
    uint16_t end_q = args->end_q;
    uint16_t port = args->port;
    uint16_t queue = args->queue;
    uint16_t operation = args->operation;
    uint16_t qid;
    qid = (queue == RTE_PDUMP_ALL_QUEUES) ? 0 : queue;

    for (; qid < end_q; qid++) {
        struct PdumpRxTxCb *cb = args->getCb(port, qid);
        if (cb == NULL) {
            KNET_ERR("Failed to get rx/tx callback for port %hu and queue %hu. Caused by Null cb", port, qid);
            return -1;
        }

        if (operation == ENABLE) {
            if (cb->rxTxCb != NULL) {
                KNET_ERR("The rx/tx callback for port and queue, already exists");
                return -EEXIST;
            }
            cb->ver = args -> ver;
            cb->ring = args -> ring;
            cb->mp = args -> mp;
            cb->snaplen = args -> snaplen;
            cb->filter = args -> filter;
            /* 每次加一个rx cb 或 一个 tx cb */
            if (flag == RTE_PDUMP_FLAG_RX) {
                cb->rxTxCb = rte_eth_add_first_rx_callback(port, qid, PdumpRx, cb);
            } else if (flag == RTE_PDUMP_FLAG_TX) {
                cb-> rxTxCb = rte_eth_add_tx_callback(port, qid, PdumpTx, cb);
            }
            if (cb->rxTxCb == NULL) {
                KNET_ERR("Failed to add rx/tx callback, errno %d, Caused by %s", rte_errno, rte_strerror(rte_errno));
                return -rte_errno;
            }
        } else if (operation == DISABLE) {
            int ret;

            if (cb->rxTxCb == NULL) {
                KNET_ERR("No existing rx/tx callback for port %hu and queue %hu", port, qid);
                return -EINVAL;
            }
            ret = args->removeCb(port, qid, cb->rxTxCb);
            if (ret < 0) {
                KNET_ERR("Failed to remove rx/tx callback, errno %d", -ret);
                return ret;
            }
            cb->rxTxCb = NULL;
        }
    }

    return 0;
}

/*
  查找校验并构造抓包需要的完整参数，构造完成后注册回调
 */
KNET_STATIC int ValidAndRegister(const struct PdumpRequest *pr, uint16_t port, struct rte_bpf *filter)
{
    struct PdumpCbsRequiredArgs args = {0};
    uint32_t flags = pr->flags;
    uint16_t nbRxQueue = 0, nbTxQueue = 0;
    if (KNET_GetCfg(CONF_COMMON_MODE)->intValue == KNET_RUN_MODE_SINGLE) {
        nbRxQueue = (uint16_t)KNET_GetCfg(CONF_TCP_MAX_WORKER_NUM)->intValue;
        nbTxQueue = (uint16_t)KNET_GetCfg(CONF_TCP_MAX_WORKER_NUM)->intValue;
        args.queue = pr->queue;
    } else if (KNET_GetCfg(CONF_COMMON_MODE)->intValue == KNET_RUN_MODE_MULTIPLE) {
        args.queue = (uint16_t)KNET_GetCfg(CONF_INNER_QID)->intValue;
    }
    args.operation = pr->op;

    args.ring = rte_ring_lookup(RING_NAME);
    if (args.ring == NULL) {
        KNET_ERR("Not Found ring for packet capture, ring name %s", RING_NAME);
        return -1;
    }

    args.mp = rte_mempool_lookup(MEMPOOL_NAME);
    if (args.mp == NULL) {
        KNET_ERR("Not Found mempool for packet capture, mempool name %s", MEMPOOL_NAME);
        return -1;
    }
    
    args.ver = pr->ver;
    args.port = port;
    args.filter = filter;
    args.snaplen = DEFAULT_SNAPLEN;
    int ret = 0;

    /* 注册Rx回调 */
    if (flags & RTE_PDUMP_FLAG_RX) {
        args.end_q = (args.queue == RTE_PDUMP_ALL_QUEUES) ? nbRxQueue : args.queue + 1;
        args.getCb = GetRxCbs, args.removeCb = rte_eth_remove_rx_callback;
        ret = PdumpRegisterRxTxCallbacks(&args, RTE_PDUMP_FLAG_RX);
        if (ret < 0) {
            /* 异常已打印 */
            return ret;
        }
    }

    /* 注册Tx回调 */
    if (flags & RTE_PDUMP_FLAG_TX) {
        args.end_q = (args.queue == RTE_PDUMP_ALL_QUEUES) ? nbTxQueue : args.queue + 1;
        args.getCb = GetTxCbs, args.removeCb = rte_eth_remove_tx_callback;
        ret = PdumpRegisterRxTxCallbacks(&args, RTE_PDUMP_FLAG_TX);
        if (ret < 0) {
            /* 异常已打印 */
            return ret;
        }
    }
    return ret;
}

KNET_STATIC int SetPdumpRxTxCbs(const struct rte_memzone *pdumpRequestMz)
{
    if (pdumpRequestMz == NULL || pdumpRequestMz->addr == NULL) {
        KNET_ERR("Set pdump failed, not support NULL memzone");
        return -1;
    }
    struct PdumpRequest *pr = pdumpRequestMz->addr;
    static uint16_t oldOp = DISABLE;
    uint16_t newOp = pr->op;
    if (newOp == oldOp) {   /* 没有开启开关 */
        return 0;
    }

    /* 检查版本是否匹配 */
    if (!(pr->ver == V1 || pr->ver == V2)) {
        KNET_ERR("Incorrect client version, current request version %hu", pr->ver);
        return -EINVAL;
    }

    struct rte_bpf *filter = NULL;
    if (pr->prm != NULL) {
        if (pr->prm->prog_arg.type != RTE_BPF_ARG_PTR_MBUF) {
            KNET_ERR("Invalid BPF program type");
            return -EINVAL;
        }

        filter = rte_bpf_load(pr->prm);
        if (filter == NULL) {
            KNET_ERR("Cannot load BPF filter %s", rte_strerror(rte_errno));
            return -rte_errno;
        }
    }

    uint16_t port;
    if (rte_eth_dev_get_port_by_name(pr->device, &port) < 0) {
        KNET_ERR("Failed to get port id for non-existent device name");
        return -EINVAL;
    }

    static int spinlockInited = 0;
    if (spinlockInited == 0) {
        SetPdumpLock((KNET_SpinLock*)&pr->sharedLock);
        spinlockInited = 1;
    }
    
    int ret = ValidAndRegister(pr, port, filter);
    oldOp = newOp;
    return ret;
}

int KNET_SetPdumpRxTxCbs(const struct rte_memzone *pdumpRequestMz)
{
    return SetPdumpRxTxCbs(pdumpRequestMz);
}

// 查找大于等于 size 的下一个 2 的幂
KNET_STATIC unsigned int NextPowerOf2(unsigned int size)
{
    if (size == 0) {
        return size + 1; // 特殊情况：0 的下一个 2 的幂为 1
    }
    unsigned realSize = size;
    realSize--; // 如果 size 恰好是 2 的幂，这一步防止重复计算
    unsigned int halfUnsignedIntBit = 16;
    unsigned int doubleToCover = 2;
    for (unsigned int rightMoveBit = 1; rightMoveBit <= halfUnsignedIntBit; rightMoveBit *= doubleToCover) {
        realSize |= realSize >> rightMoveBit;
    }
    return realSize + 1;
}

KNET_STATIC uint32_t PcapMbufSize(uint32_t length)
{
    /* The VLAN and EPB header 必须适配mbuf头空间 */
    RTE_ASSERT(PCAPNG_ENHANCE_PACKET_BLOCK + sizeof(struct rte_vlan_hdr) <= RTE_PKTMBUF_HEADROOM);
    return sizeof(struct rte_mbuf)
            + RTE_ALIGN(length, sizeof(uint32_t))
            + RTE_ALIGN(sizeof(uint32_t) + sizeof(uint32_t), sizeof(uint32_t)) /* flag option */
            + RTE_ALIGN(sizeof(uint32_t) + sizeof(uint32_t), sizeof(uint32_t)) /* queue option */
            + sizeof(uint32_t);               /* length */
}

KNET_STATIC struct rte_mempool *CreateMempool(void)
{
    const int doubleSize = 2;
    size_t numMbufs = doubleSize * DEFAULT_RING_SIZE;
    struct rte_mempool *mp = rte_mempool_lookup(MEMPOOL_NAME);
    if (mp != NULL) {
        return mp;
    }

    mp = rte_pktmbuf_pool_create_by_ops(MEMPOOL_NAME,
        numMbufs,
        MBUF_POOL_CACHE_SIZE,
        0,
        PcapMbufSize(DEFAULT_SNAPLEN),
        rte_socket_id(),
        "ring_mp_sc");
    if (mp == NULL) {
        KNET_ERR("Mempool (%s) creation failed. Caused by %s", MEMPOOL_NAME, rte_strerror(rte_errno));
    }
    return mp;
}

KNET_STATIC struct rte_ring *CreateRing(void)
{
    struct rte_ring *ring = NULL;
    size_t size;
    unsigned int ringSize = 2048;
    /* 找到下一个大于等于size的2的幂 */
    size = NextPowerOf2(ringSize);
    if (size != ringSize) {
        KNET_WARN("Ring size %u rounded up to %zu", ringSize, size);
        ringSize = size;
    }

    ring = rte_ring_lookup(RING_NAME);
    if (ring == NULL) {
        ring = rte_ring_create(RING_NAME, ringSize, rte_socket_id(), 0);
    }
    /* 没找到并且还创建失败 */
    if (ring == NULL) {
        KNET_ERR("Could not create ring %s. Caused by %s", RING_NAME, rte_strerror(rte_errno));
    }
    return ring;
}

const struct rte_memzone* KNET_MultiPdumpInit(void)
{
    const struct rte_memzone *pdumpRequestMz =
    rte_memzone_reserve(KNET_MULTI_PDUMP_MZ, sizeof(struct PdumpRequest), SOCKET_ID_ANY, 0);
    if (pdumpRequestMz == NULL) {
        KNET_ERR("Cannot allocate multi process dump descriptor");
        return NULL;
    }

    (void)memset_s(pdumpRequestMz->addr, sizeof(struct PdumpRequest), 0, sizeof(struct PdumpRequest));
    struct PdumpRequest *pdumpRequest = pdumpRequestMz->addr;
    pdumpRequest->op = DISABLE;

    struct rte_mempool* mp = CreateMempool();
    if (mp == NULL) {
        KNET_ERR("Cannot allocate mempool for multi process dump");
        goto err;
    }

    struct rte_ring* ring = CreateRing();
    if (ring == NULL) {
        rte_mempool_free(mp);
        mp = NULL;
        KNET_ERR("Cannot allocate ring for multi process dump");
        goto err;
    }
    return pdumpRequestMz;
    err:
        rte_memzone_free(pdumpRequestMz);
        pdumpRequestMz = NULL;
        return NULL;
}


KNET_STATIC int32_t FreeMempool()
{
    struct rte_mempool* mp = rte_mempool_lookup(MEMPOOL_NAME);
    if (mp == NULL) {
        KNET_ERR("No such mp resource for multi proc dump to uninit, mp name %s", MEMPOOL_NAME);
        return -1;
    }
    rte_mempool_free(mp);
    return 0;
}

KNET_STATIC int32_t FreeRing()
{
    struct rte_ring* ring = rte_ring_lookup(RING_NAME);
    if (ring == NULL) {
        KNET_ERR("No such ring resource for multi proc dump to uninit, ring name %s", RING_NAME);
        return -1;
    }
    rte_ring_free(ring);
    return 0;
}

KNET_STATIC int32_t FreeMemzone(const struct rte_memzone* memzone)
{
    if (rte_memzone_free(memzone) != 0) {
        KNET_ERR("No such memzone for multi proc dump to uninit");
        return -1;
    }
    return 0;
}


// 主进程释放mz
int32_t KNET_MultiPdumpUninit(const struct rte_memzone *pdumpRequestMz)
{
    int32_t withException = 0;
    withException += FreeMempool();
    withException += FreeRing();
    withException += FreeMemzone(pdumpRequestMz);
    return withException;
}