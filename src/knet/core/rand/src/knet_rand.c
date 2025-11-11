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

#define FILE "/dev/random"

int64_t KNET_GetRandomNum(uint8_t *data, uint32_t len)
{
    int fd;

    fd = open(FILE, O_RDONLY);
    if (fd < 0) {
        KNET_ERR("Open rand file failed, errno: %d.", errno);
        return -1;
    }

    int64_t bytesRead = read(fd, data, len);
    if (bytesRead < 0) {
        close(fd);
        return -1;
    }

    close(fd);
    return bytesRead;
}