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

#ifndef KNET_CAPBILITY
#define KNET_CAPBILITY

#include <fcntl.h>
#include <sys/stat.h>
#include "knet_log.h"

#define KNET_CAP_MAX_BITMAP     0x7f
#define KNET_CAP_MAX_NUM        7

#define KNET_CAP_SYS_RAWIO        1
#define KNET_CAP_NET_ADMIN        (1 << 1)
#define KNET_CAP_DAC_READ_SEARCH  (1 << 2)
#define KNET_CAP_IPC_LOCK         (1 << 3)
#define KNET_CAP_SYS_ADMIN        (1 << 4)
#define KNET_CAP_NET_RAW          (1 << 5)
#define KNET_CAP_DAC_OVERRIDE     (1 << 6)

void KNET_GetCap(uint8_t getCapBitmap);

void KNET_ClearCap(uint8_t clearCapBitmap);

#endif /* KNET_CAPBILITY */