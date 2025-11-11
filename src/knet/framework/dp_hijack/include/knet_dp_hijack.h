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
#ifndef KNET_DP_HIJACK_H
#define KNET_DP_HIJACK_H

#include "knet_lock.h"

struct SignalTriggerTimes {
    uint32_t knetSignalEnterCnt;
    uint32_t knetSignalExitCnt;
    KNET_SpinLock lock;
};

void SetDpInited(void);
void KNET_AllHijackFdsClose(void);
bool KNET_IsForkedParent(void);
void KNET_SigactionReg(void);
void KNET_DpExit(void);
bool KNET_IsDpWaitingExit(void);
bool KNET_IsSignalTriggered(void);
void KNET_CleanSignalTriggered(void);
bool KNET_IsInSignal(void);
struct SignalTriggerTimes* KNET_SignalTriggerTimesGet(void);

#endif // KNET_DP_HIJACK_H
