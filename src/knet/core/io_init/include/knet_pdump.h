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

#ifndef __MULTI_PROC_PDUMP_H__
#define __MULTI_PROC_PDUMP_H__

#include <rte_memcpy.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_log.h>
#include <rte_memzone.h>
#include <rte_errno.h>
#include <rte_string_fns.h>
#include <rte_pcapng.h>
#include "rte_pdump.h"
#include "knet_lock.h"

#define MZ_KNET_MULTI_PDUMP "knet_multi_pdump"

struct PdumpRequest {
    uint16_t ver;
    uint16_t op;
    uint32_t flags;
    char device[RTE_DEV_NAME_MAX_LEN];
    uint16_t queue;
    const struct rte_bpf_prm *prm;
    KNET_SpinLock sharedLock;
};

int KNET_SetPdumpRxTxCbs(struct PdumpRequest *p);

int32_t KNET_MultiPdumpInit(const struct rte_memzone **pdumpRequestMz);

int32_t KNET_MultiPdumpUninit(const struct rte_memzone *pdumpRequestMz);

#endif
