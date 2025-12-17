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
#ifndef __KNET_TRANSMISSION_HASH_H__
#define __KNET_TRANSMISSION_HASH_H__

#include "knet_offload.h"
#include "knet_atomic.h"

struct Entry {
    uint64_t ip_port;
    struct Map {
        int clientId;
        uint16_t queueIdSize;
        uint16_t dPortMask;
        KNET_ATOMIC64_T count;
        uint16_t queueId[KNET_MAX_QUEUES_PER_PORT];
        struct rte_flow *flow;
        struct rte_flow *arpFlow;
    } map;
};

struct rte_hash *KnetGetFdirHandle(void);
int KnetCreateFdirHashTbl(void);
int KnetFdirHashTblAdd(struct Entry *newEntry);
struct Entry *KnetFdirHashTblFind(uint64_t *key);
int KnetFdirHashTblDel(uint64_t *key);
int KnetDestroyFdirHashTbl(void);

#endif // __KNET_TRANSMISSION_HASH_H__