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


#ifndef __KNET_CONFIG_CORE_QUEUED_H__
#define __KNET_CONFIG_CORE_QUEUED_H__

#include "knet_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KNET_QUEUE_ID_NULL (-1)
#define KNET_QUEUE_ID_INVALID (-2)
#define MAX_CORE_NUM 128

// config
int KnetGetCoreNum(void);
int KnetGetCoreByQueueId(int queueId);

// config setter
int KnetCoreListAppend(int num);
bool KnetCheckDuplicateCore(void);
bool KnetCheckAvailableCore(void);
void KnetQueueInit(void);

// cothread queue
void KnetSetQueueNum(int num);

// config rpc
int KnetSetProcessLocalQid(int clientId, int queueId, pid_t pid);
int KnetGetProcessLocalQid(int clientId);
int KnetGetQueueIdFromPool(int requeseQueueId);
int KnetFreeQueueIdInPool(int index);
int KnetDelProcessLocalQid(int clientId);

#ifdef __cplusplus
}
#endif
#endif