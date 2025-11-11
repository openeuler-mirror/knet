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
#include "knet_dp_hijack_inner.h"

void KNET_DefaultExitHandler(int signum)
{
    switch (signum) {
        case SIGINT:
        case SIGQUIT:
        case SIGTERM:
            exit(0);
        default:
            break;
    }

    return;
}

void KNET_DefaultOtherHandler(int signum)
{
    (void)raise(signum);
    return;
}