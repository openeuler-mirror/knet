/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: knet utils相关操作
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "securec.h"
#include "knet_types.h"
#include "knet_log.h"
#include "knet_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KNET_FILE_LEN 1024
#define SYS_CPU_FILE "/sys/devices/system/cpu/cpu%d/topology/core_id"

/* 获取CPU个数 不受taskset 等亲和性影响 */
int KNET_CpuDetected(int lcoreld)
{
    char path[KNET_FILE_LEN] = {0};
    int len = snprintf_s(path, sizeof(path), sizeof(path) - 1, SYS_CPU_FILE, lcoreld);
    if (len <= 0) {
        KNET_ERR("Snprintf failed, the path does not exist");
        return -1;
    }

    if (access(path, F_OK) != 0) {
        KNET_ERR("Access failed, core_id %d no exist", lcoreld);
        return -1;
    }
    return 0;
}

#ifdef __cplusplus
}
#endif