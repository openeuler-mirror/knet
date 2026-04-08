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

#ifndef __KNET_DPDK_INIT_DEV_H__
#define __KNET_DPDK_INIT_DEV_H__

#include "rte_ethdev.h"
#include "knet_log.h"
#include "knet_config.h"
#include "knet_bond.h"
#include "knet_offload.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t KnetGetPktPoolId(void);

int32_t KnetInitDpdkPort(uint16_t portId, int procType, int dpdkPortType);
int32_t KnetGetDpdkPortIdAndInit(const char *devName, uint16_t *portId, int procType);
int32_t KnetUninitUnbondDpdkPort(uint16_t portId, int procType);

#ifdef __cplusplus
}
#endif
#endif