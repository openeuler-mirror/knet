/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef K_NET_TCP_EVENT_INNER_H
#define K_NET_TCP_EVENT_INNER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POLL_INTERVAL 10
#define FAST_POLL_TIMES 5

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

int SelectPollingLoops(struct pollfd *osPollFds, nfds_t osPollNfds, int64_t timeoutMs, struct SelectFdInfo *fdInfo);

#ifdef __cplusplus
}
#endif
#endif // K_NET_TCP_EVENT_INNER_H
