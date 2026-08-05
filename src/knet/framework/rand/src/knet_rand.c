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

#include "knet_rand.h"

#include <fcntl.h>
#include <unistd.h>

#include "knet_log.h"

#define RAND_FILE "/dev/urandom"

static int g_randFd = -1;

int KNET_RandInit(void)
{
    if (g_randFd >= 0) {
        KNET_WARN("Rand file already init");
        return 0;
    }

    g_randFd = open(RAND_FILE, O_RDONLY);
    if (g_randFd < 0) {
        KNET_ERR("Open rand file failed, errno %d", errno);
        return -1;
    }
    return 0;
}

void KNET_RandUninit(void)
{
    if (g_randFd >= 0) {
        close(g_randFd);
        g_randFd = -1;
    }
}

int64_t KNET_GetRandomNum(uint8_t *data, uint32_t len)
{
    if (g_randFd < 0) {
        KNET_ERR("Rand file not init");
        return -1;
    }

    int64_t bytesRead = read(g_randFd, data, len);
    if (bytesRead < 0) {
        KNET_ERR("Read rand file failed, ret %lld, errno %d", bytesRead, errno);
        return -1;
    }

    return bytesRead;
}