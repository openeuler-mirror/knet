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

#include <unistd.h>
#include <sys/capability.h>
#include "securec.h"
#include "knet_log.h"
#include "knet_capability.h"

struct KnetCapInfo {
    uint8_t capEffMap;
    char padding[7];   // 填充字节，确保结构体8 字节对齐
};
#ifdef KNET_TEST
struct KnetCapInfo g_knetCapInfo = { 0 };
#else
static struct KnetCapInfo g_knetCapInfo = { 0 };
#endif

cap_value_t g_capPemittedList[] = { CAP_SYS_RAWIO, CAP_NET_ADMIN, CAP_DAC_READ_SEARCH, CAP_IPC_LOCK,
    CAP_SYS_ADMIN, CAP_NET_RAW, CAP_DAC_OVERRIDE};

static void KNET_SetCap(void)
{
    int32_t capIndex = 0;
    int32_t capEffIndex = 0;
    cap_t caps = cap_init();
    if (caps == NULL) {
        KNET_ERR("Cap init failed");
        return;
    }

    cap_value_t capEffList[KNET_CAP_MAX_NUM] = {0};

    for (capIndex = 0; capIndex < KNET_CAP_MAX_NUM; capIndex++) {
        if ((g_knetCapInfo.capEffMap & (1 << capIndex)) != 0) {
            capEffList[capEffIndex] = g_capPemittedList[capIndex];
            capEffIndex++;
        }
    }

    cap_set_flag(caps, CAP_EFFECTIVE, capEffIndex, capEffList, CAP_SET);
    cap_set_flag(caps, CAP_INHERITABLE, capEffIndex, capEffList, CAP_SET);
    cap_set_flag(caps, CAP_PERMITTED, KNET_CAP_MAX_NUM, g_capPemittedList, CAP_SET);
    if (cap_set_proc(caps) != 0) {
        KNET_ERR("Set capbility failed");
    }

    cap_free(caps);
    return;
}

static bool KNET_UpdataCapInfo(uint8_t bitmap, bool isClear)
{
    int i;

    if (bitmap > KNET_CAP_MAX_BITMAP) {
        KNET_ERR("Capability bitmap is not valid");
        return false;
    }

    if (bitmap == KNET_CAP_MAX_BITMAP && isClear == true) {
        (void)memset_s(&g_knetCapInfo, sizeof(struct KnetCapInfo), 0, sizeof(struct KnetCapInfo));
        return true;
    }

    for (i = 0; i < KNET_CAP_MAX_NUM; i++) {
        if ((bitmap & (1 << i)) == 0) {
            continue;
        }
        if (isClear) {
            g_knetCapInfo.capEffMap &= (~(1 << i));
        } else {
            g_knetCapInfo.capEffMap |= (1 << i);
        }
    }

    return true;
}

static bool KNET_IsNormalUser(void)
{
    uid_t euid = geteuid();
    if (euid == 0) {
        return false;
    }

    return true;
}

void KNET_GetCap(uint8_t getCapBitmap)
{
    if (!KNET_IsNormalUser()) {
        return;
    }
    
    if (getCapBitmap > KNET_CAP_MAX_BITMAP) {
        KNET_ERR("Capability bitmap is not valid");
        return;
    }

    if (KNET_UpdataCapInfo(getCapBitmap, false) == false) {
        KNET_ERR("Updata capability bitmap failed");
        return;
    }
    KNET_SetCap();

    return;
}

void KNET_ClearCap(uint8_t clearCapBitmap)
{
    if (!KNET_IsNormalUser()) {
        return;
    }

    if (clearCapBitmap > KNET_CAP_MAX_BITMAP) {
        KNET_ERR("Capability bitmap is not valid");
        return;
    }

    if (KNET_UpdataCapInfo(clearCapBitmap, true) == false) {
        KNET_ERR("Updata capability bitmap failed");
        return;
    }
    KNET_SetCap();

    return;
}
