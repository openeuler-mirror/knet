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
#include <sys/select.h> /* FD_SETSIZE, fd_set, FD_ZERO, FD_SET */
#include <sys/time.h>

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

/* g_tcpInited: false时KNET_DpSelect直接走OS select; true时走参数校验+SelectPollFdsGet.
 * 注意: g_fdMap未初始化时为NULL, SelectPollFdsGet中KNET_GetFdType会SEGV,
 * 所以只覆盖nfds=0或fd_set全空(循环全部continue)的安全路径. */

/**
 * @brief KNET_DpSelect: tcp未初始化, 走OS select早返回路径
 */
DTEST_CASE_F(SELECT, TEST_DP_SELECT_TCP_NOT_INITED, NULL, NULL)
{
    g_tcpInited = false;
    struct timeval tv = {0};
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    int ret = KNET_DpSelect(0, NULL, NULL, NULL, &tv);
    (void)ret;
    g_tcpInited = false;
}

/**
 * @brief KNET_DpSelect: tcp已初始化, nfds<0 返回-1 (EINVAL)
 */
DTEST_CASE_F(SELECT, TEST_DP_SELECT_NFDS_NEGATIVE, NULL, NULL)
{
    g_tcpInited = true;
    int ret = KNET_DpSelect(-1, NULL, NULL, NULL, NULL);
    DT_ASSERT_EQUAL(ret, -1);
    g_tcpInited = false;
}

/**
 * @brief KNET_DpSelect: tcp已初始化, nfds>FD_SETSIZE 返回-1 (EINVAL)
 */
DTEST_CASE_F(SELECT, TEST_DP_SELECT_NFDS_TOO_LARGE, NULL, NULL)
{
    g_tcpInited = true;
    int ret = KNET_DpSelect(FD_SETSIZE + 1, NULL, NULL, NULL, NULL);
    DT_ASSERT_EQUAL(ret, -1);
    g_tcpInited = false;
}

/**
 * @brief KNET_DpSelect: tcp已初始化, timeout负值 返回-1 (EINVAL)
 */
DTEST_CASE_F(SELECT, TEST_DP_SELECT_TIMEOUT_NEGATIVE, NULL, NULL)
{
    g_tcpInited = true;
    struct timeval tv = {0};
    tv.tv_sec = -1;
    tv.tv_usec = 0;
    int ret = KNET_DpSelect(0, NULL, NULL, NULL, &tv);
    DT_ASSERT_EQUAL(ret, -1);
    g_tcpInited = false;
}

/**
 * @brief KNET_DpSelect: tcp已初始化, nfds=0 timeout={0,0}
 *        覆盖SelectPollFdsGet(空循环)+dpPollNfds=0走OS select路径
 */
DTEST_CASE_F(SELECT, TEST_DP_SELECT_NFDS_ZERO, NULL, NULL)
{
    g_tcpInited = true;
    struct timeval tv = {0};
    tv.tv_sec = 0;
    tv.tv_usec = 0; /* timeout=0, 不阻塞 */
    int ret = KNET_DpSelect(0, NULL, NULL, NULL, &tv);
    (void)ret;
    g_tcpInited = false;
}

/**
 * @brief KNET_DpSelect: tcp已初始化, nfds=2 fd_set全空, timeout={0,0}
 *        覆盖SelectPollFdsGet循环(所有fd continue, 不调KNET_GetFdType)+OS select
 */
DTEST_CASE_F(SELECT, TEST_DP_SELECT_EMPTY_FDS, NULL, NULL)
{
    g_tcpInited = true;
    fd_set rset, wset, eset;
    FD_ZERO(&rset);
    FD_ZERO(&wset);
    FD_ZERO(&eset);
    struct timeval tv = {0};
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    int ret = KNET_DpSelect(2, &rset, &wset, &eset, &tv);
    (void)ret;
    g_tcpInited = false;
}

/**
 * @brief KNET_DpPSelect: tcp未初始化, 走OS pselect早返回路径
 */
DTEST_CASE_F(SELECT, TEST_DP_PSELECT_TCP_NOT_INITED, NULL, NULL)
{
    g_tcpInited = false;
    struct timespec ts = {0};
    ts.tv_sec = 0;
    ts.tv_nsec = 0;
    int ret = KNET_DpPSelect(0, NULL, NULL, NULL, &ts, NULL);
    (void)ret;
    g_tcpInited = false;
}

/**
 * @brief KNET_DpPSelect: tcp已初始化, timeout非空(timespec转timeval), sigmask非空
 *        覆盖pthread_sigmask设置/恢复 + KNET_DpSelect调用路径
 */
DTEST_CASE_F(SELECT, TEST_DP_PSELECT_TIMEOUT_AND_SIGMASK, NULL, NULL)
{
    g_tcpInited = true;
    struct timespec ts = {0};
    ts.tv_sec = 0;
    ts.tv_nsec = 0; /* timeout=0, 不阻塞 */
    sigset_t mask = {0};
    sigemptyset(&mask);
    int ret = KNET_DpPSelect(0, NULL, NULL, NULL, &ts, &mask);
    (void)ret;
    g_tcpInited = false;
}

/**
 * @brief KNET_DpPSelect: tcp已初始化, timeout=NULL, sigmask=NULL
 *        KNET_DpSelect内部timeout=NULL->timeoutMs=-1, nfds=0通过校验
 *        SelectPollFdsGet空循环->dpPollNfds=0->OS select(timeout=NULL)会阻塞!
 *        所以用nfds=0+timeout非NULL避免阻塞. 此用例timeout=NULL但有nfds<0早返回.
 */
DTEST_CASE_F(SELECT, TEST_DP_PSELECT_NFDS_NEGATIVE, NULL, NULL)
{
    g_tcpInited = true;
    int ret = KNET_DpPSelect(-1, NULL, NULL, NULL, NULL, NULL);
    DT_ASSERT_EQUAL(ret, -1);
    g_tcpInited = false;
}
