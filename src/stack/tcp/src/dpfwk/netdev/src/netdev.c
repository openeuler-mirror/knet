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
#include <securec.h>

#include "dp_netdev_api.h"

#include "pbuf.h"
#include "shm.h"
#include "worker.h"
#include "utils_log.h"
#include "utils_statistic.h"

#include "dev.h"
#include "devtbl.h"

#define XMIT_CACHE_PBUF_NUM 16  /* 单次处理缓存报文数量 */

const DevOps_t* g_devOps[] = {
    [DP_NETDEV_TYPE_LO]      = &g_loOps,
    [DP_NETDEV_TYPE_ETH]     = &g_ethDevOps,
    [DP_NETDEV_TYPE_ETHVLAN] = &g_vlanDevOps,
    [DP_NETDEV_TYPE_BUTT]    = NULL,
};

static inline size_t GetDevQueSize(uint16_t cached)
{
    return sizeof(NetdevQue_t) + sizeof(void*) * cached;
}

static uint8_t* InitNetdevQue(NetdevQue_t* que, uint8_t* buf, uint16_t cached)
{
    uint8_t* ret = buf;

    if (RING_Init(&que->cached, (void**)ret, cached) == -1) {
        return NULL;
    }
    ret += sizeof(void*) * cached;

    (void)SPINLOCK_Init(&que->lock);

    return ret;
}

static uint8_t* InitNetdevQues(NetdevQue_t* ques, Netdev_t* dev, int queCnt, uint16_t cached, uint8_t* buf)
{
    uint8_t*     ret = buf;
    NetdevQue_t* que = ques;

    for (uint16_t i = 0; i < queCnt; i++, que++) {
        ret = InitNetdevQue(que, ret, cached);
        if (ret == NULL) {
            break;
        }
        que->queid = i;
        que->wid = -1;
        que->dev = dev;
    }

    return ret;
}

static uint8_t* InitNetdevQuesFlash(NetdevQue_t* ques, int queCnt, uint16_t flashSize, uint8_t* buf)
{
    uint8_t*     ret = buf;
    NetdevQue_t* que = ques;

    for (uint16_t i = 0; i < queCnt; i++, que++) {
        que->flash = (void**)ret;
        que->flashSize = flashSize;
        ret += sizeof(void*) * flashSize;
    }

    return ret;
}

static void FreeNetdev(Netdev_t* dev)
{
    if (dev == NULL) {
        return;
    }

    if (dev->in.ifAddr != NULL) {
        NETDEV_FreeIfAddr(dev->in.ifAddr);
    }

    SHM_FREE(dev, DP_MEM_FREE);
}

static Netdev_t* AllocNetdev(DP_NetdevCfg_t* cfg, size_t privateLen)
{
    size_t    allocSize = sizeof(Netdev_t);
    Netdev_t* dev = NULL;
    uint8_t*  buf;

    allocSize += privateLen;
    allocSize += (GetDevQueSize(cfg->rxCachedDeep) + cfg->rxFlashSize * sizeof(void*)) * cfg->rxQueCnt;
    allocSize += GetDevQueSize(cfg->txCachedDeep) * cfg->txQueCnt;

    dev = SHM_MALLOC(allocSize, MOD_NETDEV, DP_MEM_FREE);
    if (dev == NULL) {
        DP_LOG_ERR("Malloc memory failed for netDev. allocSize = %zu", allocSize);
        return NULL;
    }

    (void)memset_s(dev, allocSize, 0, allocSize);

    dev->rxQueCnt = cfg->rxQueCnt;
    dev->txQueCnt = cfg->txQueCnt;
    dev->rxQues   = (NetdevQue_t*)(dev + 1);
    dev->txQues   = (NetdevQue_t*)(dev->rxQues + cfg->rxQueCnt);
    dev->ctx  = (void*)(dev->txQues + cfg->txQueCnt);
    dev->ifindex  = -1;
    dev->devType  = cfg->devType;
    dev->tsoSize = cfg->tsoSize;
    dev->maxSegNum = cfg->maxSegNum;
    dev->offloads = cfg->offloads;
    dev->enabledOffloads = cfg->offloads;

    buf = (uint8_t*)((uint8_t*)(dev->ctx) + privateLen);

    buf = InitNetdevQues(dev->rxQues, dev, cfg->rxQueCnt, cfg->rxCachedDeep, buf);
    if (buf == NULL) {
        DP_LOG_ERR("Init netdev rxQues failed!");
        goto err;
    }
    buf = InitNetdevQuesFlash(dev->rxQues, cfg->rxQueCnt, cfg->rxFlashSize, buf);
    if (buf == NULL) {
        DP_LOG_ERR("Init netdev que flash failed!");
        goto err;
    }
    buf = InitNetdevQues(dev->txQues, dev, cfg->txQueCnt, cfg->txCachedDeep, buf);
    if (buf == NULL) {
        DP_LOG_ERR("Init netdev txQues failed!");
        goto err;
    }

    return dev;
err:
    SHM_FREE(dev, DP_MEM_FREE);
    return NULL;
}

int NETDEV_Init(int slave)
{
    if (InitDevTbl() != 0) {
        return -1;
    }

    if (InitDevTasks(slave) != 0) {
        return -1;
    }

    return 0;
}

void NETDEV_Deinit(int slave)
{
    DeinitDevTasks(slave);
}

static int PutDevSafe(NS_Net_t* net, Netdev_t* dev, int ifindex)
{
    int ret;

    SPINLOCK_Lock(&net->lock);

    ret = PutDev(NS_GET_DEV_TBL(net), dev, ifindex);

    SPINLOCK_Unlock(&net->lock);

    return ret;
}

static Netdev_t* PopDevSafe(NS_Net_t* net, int ifindex)
{
    Netdev_t* dev = NULL;

    SPINLOCK_Lock(&net->lock);

    dev = PopDev(NS_GET_DEV_TBL(net), ifindex);

    SPINLOCK_Unlock(&net->lock);

    return dev;
}

int NETDEV_SetNs(Netdev_t* dev, int id)
{
    NS_Net_t* oldNet = dev->net;
    int       ifindex = dev->ifindex;
    NS_Net_t* newNet  = NS_GetNet(id);
    if (newNet == NULL) {
        DP_LOG_ERR("NETDEV_SetNs failed for NS_GetNet(%d).", id);
        return -1;
    }

    if (id == NS_GetId(oldNet)) {
        return 0;
    }

    int ret = PutDevSafe(newNet, dev, ifindex);
    if (ret < 0) {
        DP_LOG_ERR("NETDEV_SetNs failed by cant find free dev left in devTbl.");
        return -1;
    }
    (void)PopDevSafe(oldNet, ifindex);

    dev->net = newNet;
    dev->ifindex = ifindex;

    return 0;
}

DP_Netdev_t* DP_CreateNetdev(DP_NetdevCfg_t* cfg)
{
    DP_Netdev_t* dev = NULL;

    if (cfg == NULL || (uint32_t)cfg->devType >= DP_NETDEV_TYPE_BUTT) {
        DP_LOG_ERR("Creating netdev failed, invalid cfg!");
        return NULL;
    }

    if (cfg->rxQueCnt != cfg->txQueCnt || cfg->txQueCnt > QUE_SIZE_MAX || cfg->txQueCnt == 0 ||
        cfg->rxCachedDeep > CACHE_DEEP_SIZE_MAX || cfg->txCachedDeep > CACHE_DEEP_SIZE_MAX) {
        DP_LOG_ERR("Creating netdev failed, invalid cfgQueCnt!");
        return NULL;
    }

    if (cfg->maxSegNum == 0 || cfg->maxSegNum > MAX_SEG_NUM_MAX) {
        DP_LOG_ERR("Creating netdev failed, invalid maxSegNum!");
        return NULL;
    }

    if (cfg->rxFlashSize == 0) {
        cfg->rxFlashSize = DP_NETDEV_FLASH_SIZE_DEFAULT;
        DP_LOG_DBG("Creating netdev, set default rx flash size");
    }

    if (cfg->rxFlashSize > FLASH_SIZE_MAX) {
        DP_LOG_ERR("Creating netdev failed, invalid rxFlashSize!");
        return NULL;
    }

    if (g_devOps[cfg->devType] == NULL) {
        DP_LOG_ERR("Creating netdev failed, null g_devOps!");
        return NULL;
    }

    dev = DP_AllocNetdev(cfg);
    if (dev == NULL) {
        return NULL;
    }

    if (DP_InitNetdev(dev, cfg) != 0) {
        DP_FreeNetdev(dev);
        return NULL;
    }

    dev->ref = 1;
    return dev;
}

DP_Netdev_t* DP_AllocNetdev(DP_NetdevCfg_t* cfg)
{
    DP_Netdev_t* dev = NULL;
    NS_Net_t*    net = NS_GetDftNet();

    dev = AllocNetdev(cfg, g_devOps[cfg->devType]->privateLen);
    if (dev == NULL) {
        return NULL;
    }

    if (PutDevSafe(net, dev, cfg->ifindex) != 0) {
        DP_LOG_ERR("Can't find free dev left in devTbl.");
        FreeNetdev(dev);
        return NULL;
    }

    dev->net = net;

    return dev;
}

int DP_FreeNetdev(DP_Netdev_t* dev)
{
    if (dev == NULL) {
        return -1;
    }

    (void)PopDevSafe(dev->net, dev->ifindex);

    if (g_devOps[dev->devType]->deinit != NULL) {
        g_devOps[dev->devType]->deinit(dev);
    }

    FreeNetdev(dev);

    return 0;
}

int DP_InitNetdev(DP_Netdev_t* dev, DP_NetdevCfg_t* cfg)
{
    if (dev == NULL || cfg == NULL || dev->devType >= DP_NETDEV_TYPE_BUTT) {
        DP_LOG_ERR("Init netdev failed, invalid dev or cfg!");
        return -1;
    }

    if (g_devOps[dev->devType]->init != NULL) {
        if (g_devOps[dev->devType]->init(dev, cfg) != 0) {
            DP_LOG_ERR("Netdev ops init failed! devType = %u", dev->devType);
            return -1;
        }
    }

    return 0;
}

DP_Netdev_t* DP_GetNetdev(int ifindex)
{
    NS_Net_t*    net = NS_GetDftNet();
    DP_Netdev_t* dev;

    NS_Lock(net);

    dev = NETDEV_GetDev(net, ifindex);

    NS_Unlock(net);

    return dev;
}

DP_Netdev_t* DP_GetNetdevByIndex(int index)
{
    NS_Net_t*    net = NS_GetDftNet();
    DP_Netdev_t* dev;

    NS_Lock(net);

    dev = NETDEV_GetDevInArray(net, index);

    NS_Unlock(net);

    return dev;
}

DP_Netdev_t* DP_GetNetdevByIndexLockFree(int index)
{
    NS_Net_t*    net = NS_GetDftNet();
    DP_Netdev_t* dev;
    dev = NETDEV_GetDevInArray(net, index);
    return dev;
}

DP_Netdev_t* DP_GetNetdevByName(const char* name)
{
    NS_Net_t*    net = NS_GetDftNet();
    DP_Netdev_t* dev;

    dev = NETDEV_GetDevByName(net, name);

    return dev;
}

static void* GetPrivate(NS_Net_t* net, const char* name)
{
    void*     priv = NULL;
    Netdev_t* dev;

    NS_Lock(net);

    dev = GetDevByname(NS_GET_DEV_TBL(net), name);
    if (dev != NULL) {
        if (g_devOps[dev->devType]->ctrl != NULL) {
            g_devOps[dev->devType]->ctrl(dev, DEV_CTL_GET_PRIVATE, &priv);
        }
    }

    NS_Unlock(net);

    return priv;
}

Netdev_t* NETDEV_GetDevByName(NS_Net_t* net, const char* name)
{
    Netdev_t* dev;

    NS_Lock(net);

    dev = GetDevByname(NS_GET_DEV_TBL(net), name);

    NS_Unlock(net);

    return dev;
}

void* DP_GetDevPrivate(const char* name)
{
    NS_Net_t* net = NS_GetDftNet();

    return GetPrivate(net, name);
}

void DoRcvPkts(NetdevQue_t* rxQue)
{
    if (g_devOps[rxQue->dev->devType]->doRcv != NULL) {
        g_devOps[rxQue->dev->devType]->doRcv(rxQue);
    }
}

NETDEV_IfAddr_t* NETDEV_AllocIfAddr(void)
{
    NETDEV_IfAddr_t* ret;

    ret = SHM_MALLOC(sizeof(NETDEV_IfAddr_t), MOD_NETDEV, DP_MEM_FREE);
    if (ret == NULL) {
        DP_LOG_ERR("Malloc memory failed for netdev ifAddr.");
        return NULL;
    }

    (void)memset_s(ret, sizeof(NETDEV_IfAddr_t), 0, sizeof(NETDEV_IfAddr_t));

    return ret;
}

NETDEV_IfAddr_t* NETDEV_CopyIfAddr(NETDEV_IfAddr_t* addr)
{
    NETDEV_IfAddr_t* ret;

    /* 在该函数中全字段赋值，无需初始化 */
    ret = SHM_MALLOC(sizeof(NETDEV_IfAddr_t), MOD_NETDEV, DP_MEM_FREE);
    if (ret == NULL) {
        DP_LOG_ERR("Malloc memory failed for netdev ifAddr.");
        return NULL;
    }

    // 当前不拷贝NEXT，仅拷贝head
    ret->next = NULL;
    ret->local = addr->local;
    ret->broadcast = addr->broadcast;
    ret->mask = addr->mask;
    ret->dev = addr->dev;

    return ret;
}

void NETDEV_FreeIfAddr(NETDEV_IfAddr_t* ifAddr)
{
    SHM_FREE(ifAddr, DP_MEM_FREE);
}

Netdev_t* NETDEV_GetDev(NS_Net_t* net, int ifindex)
{
    return GetDev(NS_GET_DEV_TBL(net), ifindex);
}

Netdev_t* NETDEV_GetDevInArray(NS_Net_t* net, int index)
{
    return GetDevInArray(NS_GET_DEV_TBL(net), index);
}

Netdev_t* NETDEV_RefDevByIfindex(NS_Net_t* net, int ifindex)
{
    Netdev_t* ret;

    NS_Lock(net);

    ret = GetDev(NS_GET_DEV_TBL(net), ifindex);
    if (ret != NULL) {
        ATOMIC32_Inc(&ret->ref);
    }

    NS_Unlock(net);

    return ret;
}

Netdev_t* NETDEV_RefDevByName(NS_Net_t* net, const char* name)
{
    Netdev_t* ret;

    NS_Lock(net);

    ret = GetDevByname(NS_GET_DEV_TBL(net), name);
    if (ret != NULL) {
        ATOMIC32_Inc(&ret->ref);
    }

    NS_Unlock(net);

    return ret;
}

int DP_PutPkts(DP_Netdev_t* dev, uint16_t queId, void** bufs, int cnt)
{
    if (dev == NULL || bufs == NULL || cnt <= 0) {
        return -1;
    }

    NetdevQue_t* rxQue = (NetdevQue_t*)&dev->rxQues[queId];

    if ((dev->ifflags & DP_IFF_UP) == 0) {
        return -1;
    }

    for (int i = 0; i < cnt; i++) {
        SPINLOCK_Lock(&rxQue->lock);

        if (RING_Push(&rxQue->cached, bufs[i]) != 0) {
            DP_PbufFree(bufs[i]);
        }

        SPINLOCK_Unlock(&rxQue->lock);
    }

    DP_WakeupWorker(rxQue->wid);

    return 0;
}

void NETDEV_XmitPbuf(Pbuf_t* pbuf)
{
    Netdev_t *dev = PBUF_GET_DEV(pbuf);
    if (dev == NULL || (dev->ifflags & DP_IFF_UP) == 0) {
        PBUF_Free(pbuf);
        return;
    }
    Pbuf_t *dst = NULL;
    NetdevQue_t* que;
    que = NETDEV_GetTxQue(dev, PBUF_GET_QUE_ID(pbuf));
    if (que == NULL) {
        que = NETDEV_GetTxQue(dev, 0);
    }

    if (que->wid != DP_PBUF_GET_WID(pbuf)) {
        int ret;
        if (DP_PBUF_GET_REF(pbuf) > 1) {
            dst = PBUF_Clone(pbuf);
            PBUF_Free(pbuf);
            if (dst == NULL) {
                return;
            }
        } else {
            dst = pbuf;
        }
        SPINLOCK_Lock(&que->lock);
        ret = RING_Push(&que->cached, dst);
        SPINLOCK_Unlock(&que->lock);

        if (ret != 0) {
            DP_INC_PKT_STAT(que->wid, DP_PKT_KERNEL_FDIR_CACHE_MISS);
            PBUF_Free(dst);
        } else {
            DP_WakeupWorker(que->wid);
        }

        return;
    }

    if (g_devOps[dev->devType]->doXmit != NULL) {
        g_devOps[dev->devType]->doXmit(que, &pbuf, 1);
    }
}

void XmitCached(NetdevQue_t* que)
{
    Pbuf_t* pbufs[XMIT_CACHE_PBUF_NUM];
    uint32_t cnt;

    SPINLOCK_Lock(&que->lock);

    cnt = RING_PopBurst(&que->cached, (void**)pbufs, sizeof(pbufs) / sizeof(pbufs[0]));

    SPINLOCK_Unlock(&que->lock);

    if (g_devOps[que->dev->devType]->doXmit != NULL) {
        g_devOps[que->dev->devType]->doXmit(que, pbufs, (int)cnt);
    }
}

int16_t NETDEV_RxHash(Netdev_t* dev, const struct DP_Sockaddr* rAddr, DP_Socklen_t rAddrLen,
    const struct DP_Sockaddr *lAddr, DP_Socklen_t lAddrLen)
{
    if (g_devOps[dev->devType]->rxHash == NULL) {
        return 0;
    }
    int queId = g_devOps[dev->devType]->rxHash(dev, rAddr, rAddrLen, lAddr, lAddrLen);
    if (queId < 0 || queId >= dev->rxQueCnt) {
        DP_LOG_ERR("rxHash get queId err, ifindex = %d, queId = %d", dev->ifindex, queId);
        DP_ADD_ABN_STAT(DP_NETDEV_RXHASH_FAILED);
        return -1;
    }
    return (int16_t)queId;
}

int32_t DP_GetNetdevQueMap(int32_t wid, int32_t ifIndex, uint32_t* queMap, int32_t mapCnt)
{
    if ((wid < 0) || (wid >= CFG_GET_VAL(DP_CFG_WORKER_MAX))) {
        DP_LOG_ERR("Invalid worker id %d", wid);
        return -1;
    }
    // 32代表uint32_t类型的bit位数，数组queMap每个成员可以表示32个队列。必须提供足够长的queMap数组，避免访问越界
    int32_t minMapLen = QUE_SIZE_MAX / 32;
    if (queMap == NULL || mapCnt < minMapLen) {
        DP_LOG_ERR("Invalid queMap or mapCnt, queMap should not be NULL, mapCnt should larger than %d", minMapLen);
        return -1;
    }
    return (int32_t)NETDEV_TaskQueMapGet(wid, ifIndex, queMap, (uint32_t)mapCnt);
}

void NETDEV_PutDev(DP_Netdev_t *dev)
{
    NS_Net_t *net = NS_GetDftNet();
    // 此时 NS 已经去初始化，所有表项已经移除
    if (net == NULL || net->isUsed == 0) {
        return;
    }
    if (NETDEV_DerefDev(dev) == 0) {
        if (NETDEV_IsDevUsed(dev)) {
            DP_LOG_DBG("dev is used, but ref is 0, ifindex = %d", dev->ifindex);
            (void)NETDEV_RefDev(dev);
            return;
        }
        (void)PopDev(NS_GET_DEV_TBL(net), dev->ifindex);
        if (g_devOps[dev->devType]->deinit != NULL) {
            g_devOps[dev->devType]->deinit(dev);
        }
        FreeNetdev(dev);
    }
}

int32_t DP_DestroyNetdev(int32_t ifIndex)
{
    NS_Net_t *net = NS_GetDftNet();
    DP_Netdev_t *dev = NULL;

    NS_Lock(net);

    dev = NETDEV_GetDev(net, ifIndex);
    if (dev == NULL) {
        NS_Unlock(net);
        return -1;
    }

    if (!NETDEV_IsDevUsed(dev)) {
        NS_Unlock(net);
        return -1;
    }

    if ((dev->ifflags & DP_IFF_UP) != 0) {
        DevStop(dev);
    }

    dev->freed = 1;
    NETDEV_PutDev(dev);

    NS_Unlock(net);

    return 0;
}

static void NETDEV_StatsAccumulate(DP_DevStats_t* dst, DP_DevStats_t* src)
{
    dst->txStats.bytes   += src->txStats.bytes;
    dst->txStats.packets += src->txStats.packets;
    dst->txStats.errs    += src->txStats.errs;
    dst->txStats.drop    += src->txStats.drop;

    dst->rxStats.bytes     += src->rxStats.bytes;
    dst->rxStats.packets   += src->rxStats.packets;
    dst->rxStats.errs      += src->rxStats.errs;
    dst->rxStats.drop      += src->rxStats.drop;
    dst->rxStats.multicast += src->rxStats.multicast;
}

int DP_NetdevGetStats(int ifIndex, DP_DevStats_t* stats)
{
    DP_DevStats_t devStats = {0};
    DP_Netdev_t* dev = DP_GetNetdev(ifIndex);
    if (dev == NULL || stats == NULL) {
        return -1;
    }

    for (int i = 0; i < dev->rxQueCnt; i++) {
        SPINLOCK_Lock(&dev->rxQues[i].lock);
        NETDEV_StatsAccumulate(&devStats, &dev->rxQues[i].info);
        NETDEV_StatsAccumulate(&devStats, &dev->txQues[i].info);
        SPINLOCK_Unlock(&dev->rxQues[i].lock);
    }

    *stats = devStats;
    return 0;
}

int DP_NetdevCleanStats(int ifIndex)
{
    DP_Netdev_t* dev = DP_GetNetdev(ifIndex);
    if (dev == NULL) {
        return -1;
    }
    for (int i = 0; i < dev->rxQueCnt; i++) {
        SPINLOCK_Lock(&dev->rxQues[i].lock);
        (void)memset_s(&dev->rxQues[i].info, sizeof(DP_DevStats_t), 0, sizeof(DP_DevStats_t));
        (void)memset_s(&dev->txQues[i].info, sizeof(DP_DevStats_t), 0, sizeof(DP_DevStats_t));
        SPINLOCK_Unlock(&dev->rxQues[i].lock);
    }
    return 0;
}