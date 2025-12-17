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
#include "dp_posix_epoll_api.h" // 必须放到 dp_epoll.h 前面

#include "dp_epoll.h"

int DP_PosixEpollCreate(int size)
{
    return DP_EpollCreate(size);
}

int DP_PosixEpollCtl(int epfd, int op, int fd, struct epoll_event *event)
{
    return DP_EpollCtl(epfd, op, fd, (struct DP_EpollEvent*)event);
}

int DP_PosixEpollWait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    return DP_EpollWait(epfd, (struct DP_EpollEvent*)events, maxevents, timeout);
}
