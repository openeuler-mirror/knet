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

#ifndef __KNET_DTOE_EVENTS_H__
#define __KNET_DTOE_EVENTS_H__
#include <sys/queue.h>

#include "knet_lock.h"
#include "knet_dtoe_api.h"
#include "flexda_dtoe_interface.h"

struct KnetSendChannel {
    struct knet_send_channel channel;
};

TAILQ_HEAD(KnetLeakListHead, KNET_Fd);

struct KnetRecvChannel {
    struct knet_recv_channel channel;
    /* 以下是knet需要的结构 */
    struct knet_recv_events* events;
    uint32_t maxevents;
    uint32_t nextEventIdx;

    struct KnetLeakListHead leakList; // 当连接有泄漏数据时，KNET_Fd在leakedList中
    KNET_SpinLock leakLock;
};

typedef struct KnetReqNode {
    TAILQ_ENTRY(KnetReqNode) node;
    knet_tx_req_free_cb_t freeCb;
    uint64_t wr_id;     // 用户透传id
    int sockfd;
    uint16_t send_sn;   // dtoe_send时出参的curr_msn
} KnetReqNode;

TAILQ_HEAD(KnetReqListHead, KnetReqNode);

#endif