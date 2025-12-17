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

#include "dp_posix_socket_api.h"
#include "dp_debug_api.h"

#include "knet_log.h"
#include "knet_osapi.h"
#include "knet_signal_tcp.h"
#include "tcp_fd.h"

#define DP_EXIT_WAIT_SLEEP_TIME (50000) // 50ms
#define DP_EXIT_WAIT_TRY_TIMES (10)     // 尝试等待次数

bool g_tcpInited = false; // true: tcp协议栈初始化完成

void KNET_SetDpInited(void)
{
    KNET_INFO("Dp init success");
    g_tcpInited = true;
    KNET_FdInit();
}

static int TcpSoLingerSet(int osFd)
{
    if (g_origOsApi.getsockopt == NULL) {
        return -1;
    }

    int type = -1;
    size_t optlen = sizeof(type);
    socklen_t optLen = (socklen_t)optlen;
    int ret = g_origOsApi.getsockopt(osFd, SOL_SOCKET, SO_TYPE, &type, &optLen);
    if (ret == 0 && type == SOCK_STREAM) {
        struct linger soLinger = {
            .l_onoff = true,
            .l_linger = 0,
        };
        BEFORE_DPFUNC();
        ret = DP_PosixSetsockopt(KNET_OsFdToDpFd(osFd), SOL_SOCKET, SO_LINGER, &soLinger, sizeof(soLinger));
        AFTER_DPFUNC();
        if (ret != 0) {
            KNET_ERR("Set OSFd %d dpFd %d sock opt SO_LINGER failed, ret %d, errno %d, %s",
                osFd, KNET_OsFdToDpFd(osFd), ret, errno, strerror(errno));
        }
    }

    return 0;
}

void KNET_AllHijackFdsClose(void)
{
    int fdMax = KNET_FdMaxGet();
    for (int osFd = 0; osFd < fdMax; ++osFd) {
        if (KNET_IsFdHijack(osFd)) {
            if (TcpSoLingerSet(osFd) != 0) {
                KNET_ERR("osFd %d solinger set failed", osFd);
            }
            close(osFd);
        }
    }
}

/**
 * @brief 因信号退出时主线程需要等待其他线程可能有的dp流程结束
 */
void KNET_DpExit(void)
{
    if (!g_tcpInited) {
        return;
    }

    KNET_DpSignalSetWaitExit(); // 设置主线程等待标记
    usleep(DP_EXIT_WAIT_SLEEP_TIME);   // 先等待50ms让其他线程都退出来
    KNET_AllHijackFdsClose();   // 关闭所有tcp协议栈的fd
    int tryTimes = 0;

    while (1) {
        /* 等待所有tcpfd关闭再退出 */
        int tcpSockCnt = DP_SocketCountGet(0);
        if (tcpSockCnt == 0) {
            break;
        }
        usleep(DP_EXIT_WAIT_SLEEP_TIME); //  每50ms判断一次

        ++tryTimes;
        if (tryTimes > DP_EXIT_WAIT_TRY_TIMES) {
            /* 出现如下打印的两种情况
             * 1.如果业务在此函数前自行关闭fd，就会发送FIN报文，需要等到msl time结束tcp才会关闭
             * 2.有遗漏的业务fd没有关闭 */
            KNET_WARN("%d tcp socket still alive, waiting for msl time or others", tcpSockCnt);
            break;
        }
    }
    return;
}