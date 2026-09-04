/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
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
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>

#include "securec.h"
#include "knet_lock.h"
#include "common.h"
#include "mock.h"
#include "knet_types.h"
#include "knet_log.h"
#include "knet_config.h"
#include "knet_init.h"
#include "knet_tcp_api_init.h"
#include "tcp_event_inner.h"
#include "tcp_event.h"
#include "tcp_fd.h"

/* g_tcpInited: false时KNET_DpPoll直接走OS poll(跳过nfds校验, 有越界风险);
 * true时走nfds校验+DpPollHelper. UT默认false, 这里按用例需要切换并恢复. */

/**
 * @brief KNET_DpPoll: tcp未初始化, 走OS poll早返回路径
 */
DTEST_CASE_F(POLL, TEST_DP_POLL_TCP_NOT_INITED, NULL, NULL)
{
    g_tcpInited = false;
    struct pollfd fds[1] = {0};
    fds[0].fd = -1;
    fds[0].events = POLLIN;
    int ret = KNET_DpPoll(fds, 1, 0); /* timeout=0, OS poll立即返回0 */
    (void)ret;
    g_tcpInited = false;
}

/**
 * @brief KNET_DpPoll: tcp已初始化, nfds=0 (非法参数) 返回-1
 */
DTEST_CASE_F(POLL, TEST_DP_POLL_NFDS_ZERO, NULL, NULL)
{
    g_tcpInited = true;
    struct pollfd fds[1] = {0};
    int ret = KNET_DpPoll(fds, 0, 0);
    DT_ASSERT_EQUAL(ret, -1);
    g_tcpInited = false;
}

/**
 * @brief KNET_DpPoll: tcp已初始化, nfds过大超过KNET_POLL_MAX_NUM 返回-1
 */
DTEST_CASE_F(POLL, TEST_DP_POLL_NFDS_TOO_LARGE, NULL, NULL)
{
    g_tcpInited = true;
    struct pollfd fds[1] = {0};
    int ret = KNET_DpPoll(fds, 99999, 0); /* nfds校验在前, 不会越界访问fds */
    DT_ASSERT_EQUAL(ret, -1);
    g_tcpInited = false;
}

/**
 * @brief KNET_DpPoll: tcp已初始化, fds=NULL 返回-1 (errno=EFAULT)
 */
DTEST_CASE_F(POLL, TEST_DP_POLL_FDS_NULL, NULL, NULL)
{
    g_tcpInited = true;
    int ret = KNET_DpPoll(NULL, 1, 0);
    DT_ASSERT_EQUAL(ret, -1);
    g_tcpInited = false;
}

/**
 * @brief KNET_DpPoll: tcp已初始化, fd无效(-1) timeout=0
 *        覆盖DpPollHelper中dpPollNfds=0走OS poll的路径
 */
DTEST_CASE_F(POLL, TEST_DP_POLL_NORMAL, NULL, NULL)
{
    g_tcpInited = true;
    struct pollfd fds[2] = {0};
    fds[0].fd = -1;
    fds[1].fd = -2;
    fds[0].events = POLLIN;
    fds[1].events = POLLOUT;
    int ret = KNET_DpPoll(fds, 2, 0); /* 所有fd无效->dpPollNfds=0->走OS poll(timeout=0) */
    (void)ret;
    g_tcpInited = false;
}

/**
 * @brief KNET_DpPPoll: tcp未初始化, 走OS ppoll早返回路径
 */
DTEST_CASE_F(POLL, TEST_DP_PPOLL_TCP_NOT_INITED, NULL, NULL)
{
    g_tcpInited = false;
    struct pollfd fds[1] = {0};
    fds[0].fd = -1;
    struct timespec ts = {0};
    ts.tv_sec = 0;
    ts.tv_nsec = 0; /* timeout=0, 不阻塞 */
    int ret = KNET_DpPPoll(fds, 1, &ts, NULL);
    (void)ret;
    g_tcpInited = false;
}

/**
 * @brief KNET_DpPPoll: tcp已初始化, timeoutTs负值 (tv_sec<0) 返回-1 (errno=EINVAL)
 */
DTEST_CASE_F(POLL, TEST_DP_PPOLL_TIMEOUT_NEGATIVE, NULL, NULL)
{
    g_tcpInited = true;
    struct pollfd fds[1] = {0};
    struct timespec ts = {0};
    ts.tv_sec = -1;
    int ret = KNET_DpPPoll(fds, 1, &ts, NULL);
    DT_ASSERT_EQUAL(ret, -1);
    g_tcpInited = false;
}

/**
 * @brief KNET_DpPPoll: tcp已初始化, 正常timeout=0, fd无效
 *        覆盖SigDpPoll + KNET_DpPoll + DpPollHelper(dpPollNfds=0走OS poll)
 */
DTEST_CASE_F(POLL, TEST_DP_PPOLL_NORMAL_TIMEOUT, NULL, NULL)
{
    g_tcpInited = true;
    struct pollfd fds[1] = {0};
    fds[0].fd = -1;
    struct timespec ts = {0};
    ts.tv_sec = 0;
    ts.tv_nsec = 0; /* timeout=0, 不阻塞 */
    int ret = KNET_DpPPoll(fds, 1, &ts, NULL);
    (void)ret;
    g_tcpInited = false;
}

/**
 * @brief KNET_DpPPoll: tcp已初始化, sigmask非空, timeout=0
 *        覆盖SigDpPoll中pthread_sigmask设置/恢复路径
 */
DTEST_CASE_F(POLL, TEST_DP_PPOLL_SIGMASK_NONNULL, NULL, NULL)
{
    g_tcpInited = true;
    struct pollfd fds[1] = {0};
    fds[0].fd = -1;
    sigset_t mask = {0};
    sigemptyset(&mask);
    struct timespec ts = {0};
    ts.tv_sec = 0;
    ts.tv_nsec = 0; /* timeout=0, 不阻塞 */
    int ret = KNET_DpPPoll(fds, 1, &ts, &mask);
    (void)ret;
    g_tcpInited = false;
}
