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
#ifndef K_NET_KNET_THREAD_H
#define K_NET_KNET_THREAD_H

#include "knet_types.h"

int32_t KNET_CreateThread(uint64_t *thread, void *(* func) (void *), void *arg);

int32_t KNET_SetThreadAffinity(uint64_t threadId, const uint16_t *cpus, uint32_t len);

int32_t KNET_GetThreadAffinity(uint64_t threadId, uint16_t *cpus, uint32_t *len);

int32_t KNET_ThreadNameSet(uint64_t threadId, const char *name);

int32_t KNET_JoinThread(uint64_t threadId, void **ret);

uint64_t KNET_ThreadId(void);

#endif // K_NET_KNET_THREAD_H
