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

#include <unistd.h>

#include "knet_types.h"
#include "knet_log.h"
#include "knet_dpdk_init.h"
#include "rte_hash.h"
#include "rte_errno.h"
#include "knet_rpc.h"
#include "knet_lock.h"
#include "knet_transmission.h"
#include "knet_transmission_hash.h"

struct rte_hash *g_fdirHandle = NULL;

/**
 * @brief 默认Fdirhash table的entry大小
 */
#define MAX_ENTRIES 2048
#define DEFAULT_RING_SIZE 1024

struct rte_hash *KnetGetFdirHandle(void)
{
    return g_fdirHandle;
}

// 创建FdirHashTbl
int KnetCreateFdirHashTbl(void)
{
    if (g_fdirHandle != NULL) {
        KNET_ERR("Create Fdir hash table failed, because table already exist");
        return -1;
    }

    struct rte_hash_parameters hashParams = {0};
    hashParams.name = "Flow_table";
    hashParams.entries = MAX_ENTRIES;
    hashParams.key_len = (uint32_t)sizeof(uint64_t);
    hashParams.hash_func_init_val = 0;
    hashParams.socket_id = SOCKET_ID_ANY;

    g_fdirHandle = rte_hash_create(&hashParams);
    if (g_fdirHandle == NULL) {
        KNET_ERR("Create Fdir hash table failed, errno %d", rte_errno);
        return -1;
    }
    return 0;
}

KNET_STATIC struct rte_ring *RteRingCreateAndStartQueue(uint16_t portId, uint16_t queueId)
{
    /* 构造每个从进程的ring名字，以queueId标识 */
    char name[MAX_CPD_NAME_LEN] = {0};
    int ret = snprintf_s(name, MAX_CPD_NAME_LEN, MAX_CPD_NAME_LEN - 1, "cpdtaprx%hu", queueId);
    if (ret < 0) {
        KNET_ERR("Cpdtap name get error, ret %d", ret);
        return NULL;
    }
    
    // 如果已经存在ring，不需要创建，否则创建ring，再去start queue
    struct rte_ring *cpdTapRing = rte_ring_lookup(name);
    if (cpdTapRing == NULL) {
        cpdTapRing = rte_ring_create(name, DEFAULT_RING_SIZE, rte_socket_id(), 0);
        if (cpdTapRing == NULL) {
            KNET_ERR("Cpd in ring creation failed, port %hu, queue %hu, err %s", \
                portId, queueId, rte_strerror(rte_errno));
            return NULL;
        }
    }

    ret = rte_eth_dev_rx_queue_start(portId, queueId);
    if (ret != 0) {
        KNET_ERR("Failed to start port %hu RX queue %hu, ret %d", portId, queueId, ret);
        rte_ring_free(cpdTapRing);
        return NULL;
    }

    return cpdTapRing;
}

// 插入Fdir规则到hash表
int KnetFdirHashTblAdd(struct Entry *newEntry)
{
    if (g_fdirHandle == NULL) {
        KNET_ERR("Fdirhandle does not exist");
        return -1;
    }

    int ret = 0;
    KNET_DpdkNetdevCtx *netdevCtx = KNET_GetNetDevCtx();
    struct rte_ring *cpdTapRing = NULL;

    // 如果需要在内核转发时rte_eth_dev_rx_queue_stop。这里需要在下流表时start queue
    /* TM280限制：TM280不支持rte_eth_dev_rx_queue_stop操作，所以必须所有队列都要有数据面轮询，tap转发才能保证正常工作；
     * 否则会出现报文RSS散列到没有数据面轮询的队列，无法转发到tap口，导致丢包 */
    if (KNET_GetCfg(CONF_INNER_NEED_STOP_QUEUE)->intValue == KNET_STOP_QUEUE) {
        cpdTapRing = RteRingCreateAndStartQueue(netdevCtx->portId, newEntry->map.queueId[0]);
    }

    ret = rte_hash_lookup(g_fdirHandle, &(newEntry->ip_port));
    if (ret >= 0) {
        KNET_ERR("FdirHashTblAdd key %lu already exist, ret %d", newEntry->ip_port, ret);
        /* 由于Fdir已插入Hash表，之前已经开启队列并创建ring，因此不能去释放之前创建的，直接返回异常码 */
        return -1;
    }
    
    ret = rte_hash_add_key_data(g_fdirHandle, &(newEntry->ip_port), newEntry);
    if (ret != 0) {
        KNET_ERR("FdirHashTblAdd entry failed, ret %d", ret);
        /* Fdir已存在的情况已经判断，下面是Fdir不存在，插入错误，ENOSPC无空间插入或EINVAL参数无效，关掉队列并清理ring
           TM280停队列驱动打桩，无法真正停止队列，因此要确保所有队列都有进程工作才能稳定转发内核流量 */
        if (KNET_GetCfg(CONF_INNER_NEED_STOP_QUEUE)->intValue == KNET_STOP_QUEUE) {
            rte_eth_dev_rx_queue_stop(netdevCtx->portId, newEntry->map.queueId[0]);
            rte_ring_free(cpdTapRing);
        }
        return -1;
    }
    return 0;
}

struct Entry *KnetFdirHashTblFind(uint64_t *key)
{
    if (g_fdirHandle == NULL) {
        KNET_ERR("Fdirhandle does not exist");
        return NULL;
    }

    struct Entry *oldEntry = NULL;
    int32_t ret = rte_hash_lookup_data(g_fdirHandle, key, (void **) &oldEntry);
    if (ret < 0) {
        KNET_DEBUG("FdirHashTblFind key not exist, ret %d", ret);
        return NULL;
    }

    return oldEntry;
}

// 删除hash表的Fdir规则
int KnetFdirHashTblDel(uint64_t *key)
{
    struct Entry *oldEntry = NULL;
    int32_t ret = rte_hash_lookup_data(g_fdirHandle, key, (void **) &oldEntry);
    if (ret < 0) {
        KNET_ERR("Delete Fdirhash table key not exist. ret %d", ret);
        return -1;
    }

    int32_t delPos = rte_hash_del_key(g_fdirHandle, key);
    if (delPos < 0) {
        KNET_ERR("Delete Fdirhash table entry failed. delPos %d.", delPos);
        return -1;
    }

    free(oldEntry);
    return 0;
}

// 销毁FdirHash表
int KnetDestroyFdirHashTbl(void)
{
    if (g_fdirHandle == NULL) {
        KNET_WARN("Fdirhandle does not exist in destory hash table");
        return 0;
    }
    uint32_t iter = 0;
    uint64_t *key = NULL;
    struct Entry *nextEntry = NULL;

    while (rte_hash_iterate(g_fdirHandle, (const void **) &key, (void **) &nextEntry, &iter) >= 0) {
        int32_t delPos = rte_hash_del_key(g_fdirHandle, key);
        if (delPos < 0) {
            KNET_ERR("Delete Fdirhash table entry failed. delPos %d", delPos);
            return -1;
        }
        free(nextEntry);
    }

    rte_hash_free(g_fdirHandle);
    g_fdirHandle = NULL;
    return 0;
}
