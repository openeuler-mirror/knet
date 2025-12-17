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
#include "utils_log.h"

#include "tcp_inet.h"
#include "tcp_tsq.h"
#include "tcp_timer.h"
#include "tcp_cc.h"

int TCP_Init(int slave)
{
    if (TcpCaModuleInit() != 0) {
        DP_LOG_ERR("Tcp ca module init failed.");
        return -1;
    }

    if (TcpTsqInit(slave) != 0) {
        DP_LOG_ERR("Tcp tsq init failed.");
        return -1;
    }

    if (TcpInetInit() != 0) {
        DP_LOG_ERR("Tcp inet init failed.");
        return -1;
    }

    if (slave != 0) {
        return 0;
    }

    if (TcpInitTimer() != 0) {
        DP_LOG_ERR("Tcp timer init failed.");
        return -1;
    }

    return 0;
}

void TCP_Deinit(int slave)
{
    TcpInetDeinit();
    TcpDeinitTimer();
    TcpTsqDeinit(slave);
    TcpCaModuleDeinit();
}
