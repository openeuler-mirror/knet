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
#ifndef __KNET_DLOPEN_H__
#define __KNET_DLOPEN_H__

#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>

#include "dp_debug_api.h"
#include "dp_socket_api.h"
#include "dp_addr_ext_api.h"
#include "dp_mem_api.h"
#include "dp_init_api.h"
#include "dp_netdev_api.h"
#include "dp_worker_api.h"
#include "dp_cfg_api.h"
#include "dp_cpd_api.h"
#include "dp_tbm_api.h"
#include "dp_posix_socket_api.h"
#include "dp_posix_poll_api.h"
#include "dp_posix_epoll_api.h"
#include "dp_log_api.h"
#include "dp_show_api.h"
#include "dp_mp_api.h"
#include "dp_rand_api.h"
#include "dp_hashtbl_api.h"
#include "dp_fib4tbl_api.h"
#include "dp_clock_api.h"
#include "dp_sem_api.h"

#define ARRAY_SIZE(ARRAY) (sizeof(ARRAY) / sizeof((ARRAY)[0]))

#define KNET_ADD_SYMBOL(symbol, member) {#symbol, (void**)&g_##member}

struct KnetSymbolsInfo {
    const char *symName;
    void **symRef;
};

struct KnetSymbolsCfg {
    void *handle;
    const char* soName;
    struct KnetSymbolsInfo *symbols;
    int symbolCnt;
};

/**
 * @brief 初始化Dp Symbols
 *
 * @return int 0：成功；-1：失败
 */
int KnetInitDpSymbols(void);

/**
 * @brief 去初始化Dp Symbols
 *
 */
void KnetDeinitDpSymbols(void);

#endif