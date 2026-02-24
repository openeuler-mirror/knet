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

#include "knet_lock.h"

#ifdef __cplusplus
extern "C" {
#endif

static __thread KNET_WaitPolicy g_waitPolicy = KNET_SPIN_WAIT; /* 每个线程的调度等待属性，为线程变量 */

KNET_WaitPolicy KNET_HalGetWaitPolicy(void)
{
    return g_waitPolicy;
}

#ifdef __cplusplus
}
#endif /* __cpluscplus */
