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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <regex.h>
#include <sys/prctl.h>

#include "securec.h"
#include "knet_types.h"
#include "knet_log.h"
#include "knet_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REG_ERROR_MSG_LEN 100
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
        KNET_ERR("Access failed, core_id %d does not exist", lcoreld);
        return -1;
    }
    return 0;
}

bool KNET_RegMatch(const char *pattern, const char *string)
{
    if (pattern == NULL || string == NULL) {
        KNET_ERR("Invalid reg pattern and string");
        return false;
    }

    // 编译正则表达式
    regex_t regex;
    int ret = regcomp(&regex, pattern, REG_EXTENDED);
    if (ret != 0) {
        KNET_ERR("Could not compile regex, error code %d", ret);
        return false;
    }

    // 执行正则表达式匹配
    ret = regexec(&regex, string, 0, NULL, 0);
    bool matched = false;
    if (ret == 0) {
        matched = true;
    } else if (ret == REG_NOMATCH) {
        matched = false;
        KNET_ERR("Regex match failed, no match");
    } else {
        char msgbuf[REG_ERROR_MSG_LEN] = {0};
        regerror(ret, &regex, msgbuf, sizeof(msgbuf));
        matched = false;
        KNET_ERR("Regex match failed, msgbuf %s", msgbuf);
    }

    // 释放正则表达式
    regfree(&regex);

    return matched;
}

const char *KNET_GetSelfThreadName(char *name, size_t len)
{
    if (name == NULL || len < KNET_THREAD_NAME_LEN) {
        return "invalid parameter";
    }
    int ret = prctl(PR_GET_NAME, name);
    if (ret < 0) {
        return "invalid thread name";
    }

    return name;
}

#ifdef __cplusplus
}
#endif