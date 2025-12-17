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

#include "knet_types.h"
#include "knet_log.h"

#define DECIMAL 10

static inline int CharIsValidHex(const char input)
{
    if ((input >= '0' && input <= '9') ||
        (input >= 'a' && input <= 'f') ||
        (input >= 'A' && input <= 'F')) {
        return 0;
    }
    return -EINVAL;
}

static int CheckValidMac(const char *mac)
{
    bool canColon = false;
    int colonCount = 0;
    int hexCountBetweenColon = 0;
    const char *p = mac;
    const int colonLen = 5;
    const int colonBetweenLen = 2;

    while (*p != '\0') {
        if (canColon && (*p == ':')) {
            canColon = false;
            hexCountBetweenColon = 0;
            colonCount++;
            if (colonCount > colonLen) {
                return -1;
            }
            p++;
            continue;
        }

        hexCountBetweenColon++;
        if (hexCountBetweenColon > colonBetweenLen) {
            return -1;
        }

        if (CharIsValidHex(*p)) {
            return -1;
        }
        canColon = true;
        p++;
    }
    return 0;
}

int KNET_ParseMac(const char *mac, uint8_t *outputMac)
{
    if (mac == NULL || outputMac == NULL) {
        KNET_ERR("Mac is null");
        return -1;
    }

    if (CheckValidMac(mac)) {
        KNET_ERR("Mac is not valid");
        return -1;
    }

    int len = sscanf_s(mac, MAC_SCAN_FMT, MAC_SCAN_ARGS(outputMac));
    if (len != MAC_SIXTH + 1) {
        KNET_ERR("Scanf mac failed, errno %d", errno);
        return -1;
    }
    return 0;
}

int KNET_TransStrToNum(const char* str, uint32_t* num)
{
    if (num == NULL || str == NULL) {
        KNET_ERR("Trans str to num failed, num or str is null");
        return -1;
    }
    char *endptr = NULL;
    errno = 0;
    long res = strtol(str, &endptr, DECIMAL);
    if (errno != 0 || *endptr != '\0') {
        KNET_ERR("Trans str to num failed, errno %d ", errno);
        return -1;
    }

    if (res < 0 || res > UINT32_MAX) {
        KNET_ERR("Trans str to num failed, num is out of range [0,  UINT32_MAX]");
        return -1;
    }

    *num = (uint32_t)res;
    return 0;
}