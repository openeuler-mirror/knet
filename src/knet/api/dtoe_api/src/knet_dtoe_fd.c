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
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "securec.h"

#include "knet_log.h"
#include "knet_dtoe_tcp.h"
#include "knet_dtoe_epoll.h"
#include "knet_dtoe_fd.h"

#define KNET_INVALID_SN (0)
#define KNET_INITIAL_FREEREQ_SIZE (128)

struct KNET_Fd *g_knetDtoeFdMap = NULL;
int g_dtoeFdMax = 0;

int KNET_FdInit(void)
{
    if (g_dtoeFdMax > 0) {
        KNET_DEBUG("DTOE fd module reinit");
        return 0;
    }

    struct rlimit limit = {0};
    // 获取当前的资源 limit
    if (getrlimit(RLIMIT_NOFILE, &limit) != 0) {
        KNET_ERR("Get Linux fd limit failed");
        return -1;
    }

    g_knetDtoeFdMap = (struct KNET_Fd *)calloc((size_t)limit.rlim_cur, sizeof(struct KNET_Fd));
    if (g_knetDtoeFdMap == NULL) {
        KNET_ERR("Alloc DTOE fd map failed, fd cur limit %llu", (unsigned long long)limit.rlim_cur);
        return -1;
    }
    g_dtoeFdMax = (int)limit.rlim_cur;
    KNET_INFO("DTOE fd max %d", g_dtoeFdMax);

    return 0;
}

void KNET_FdDeinit(void)
{
    if (g_knetDtoeFdMap == NULL) {
        return;
    }

    free(g_knetDtoeFdMap);
    g_knetDtoeFdMap = NULL;
    g_dtoeFdMax = 0;
}
