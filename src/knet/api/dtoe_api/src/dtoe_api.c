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

#include <stdarg.h>

#include "dtoe_interface.h"

#include "knet_log.h"

KNET_API int knet_init(const char * local_ip)
{
    KNET_LogInit();

    int ret = dtoe_init();
    if (ret != 0) {
        KNET_ERR("Dtoe init failed, ret %d", ret);
    }
    return ret;
}

KNET_API void knet_uninit()
{
    dtoe_uninit();
    KNET_LogUninit();
}