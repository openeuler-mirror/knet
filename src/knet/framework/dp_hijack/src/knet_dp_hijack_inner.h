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
#ifndef __KNET_DP_HIJACK_INNER_H__
#define __KNET_DP_HIJACK_INNER_H__

#include <signal.h>

#include "knet_types.h"

#define KNET_API __attribute__((visibility("default")))
#define KNET_EPOLL_MAX_NUM (((uint32_t)0xFFFFFFFF) / sizeof(struct epoll_event))

#define FAST_POLL_TIMES 5
#define POLL_INTERVAL 10

#define DEFAULT_EVENT_NUM 512

#define ADDRLEN_NULL_VALUE 0xAAAA // 用作addrlen为NULL时的打印值

#define DP_EXIT_WAIT_SLEEP_TIME (50000) // 50ms
#define DP_EXIT_WAIT_TRY_TIMES (10)     // 尝试等待次数

/* Linux下未定义的信号 */
#define SIGUNKNOWN1 32
#define SIGUNKNOWN2 33

#define KNET_SIGDFL   1
#define KNET_SIGIGN   2
#define KNET_SIGOTHER 3

#define KNET_SIGQUIT_WAIT (10 * 1000)

struct SelectFdInfo {
    fd_set *readfds;
    fd_set *writefds;
    fd_set *exceptfds;
    struct pollfd *dpPollFds;
    int selectNfds;
    int dpPollNfds;
    int osPollRet; // os poll返回的event个数
    int dpPollRet; // dp轮询得到的event个数
};

struct SignalFlags {
    bool sigDelay;          // 判断是否在dp锁流程内
    bool sigExitTriggered;  // 判断是否触发了退出信号
    bool inExitUserHandler; // 收到退出信号处理时判断是否在用户回调函数内
    bool inSigHandler;      // 判断是否在信号处理函数流程内
    int curExitSig;         // 当前退出信号
    int curSig;             // 当前收到的信号,针对除退出信号外的处理
};

void KNET_DefaultExitHandler(int signum);
void KNET_DefaultOtherHandler(int signum);
#endif