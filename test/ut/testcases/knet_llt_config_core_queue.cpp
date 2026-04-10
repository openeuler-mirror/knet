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

#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>

#include "securec.h"
#include "knet_lock.h"
#include "common.h"
#include "mock.h"

#include "rte_config.h"
#include "rte_errno.h"
#include "rte_mempool.h"
#include "securec.h"
#include "knet_log.h"
#include "knet_mock.h"
#include "knet_rpc.h"
#include "knet_config.h"
#include "knet_config_core_queue.h"

extern "C" {
int SetEnvQueueId(int requeseQueueId);
}

DTEST_CASE_F(CONFIG_CORE_QUEUE, TEST_FIND_CORE_IN_LIST_NORMAL, NULL, NULL)
{
    int ret = 0;

    ret = KNET_FindCoreInList(-1);
    DT_ASSERT_EQUAL(ret, -1);

    ret = KNET_FindCoreInList(1);
    DT_ASSERT_NOT_EQUAL(ret, -1);
}

DTEST_CASE_F(CONFIG_CORE_QUEUE, TEST_KNET_GET_CORE_NUM, NULL, NULL)
{
    int ret = 0;
    ret = KnetGetCoreNum();
    DT_ASSERT_EQUAL(ret, 1);
}

DTEST_CASE_F(CONFIG_CORE_QUEUE, TEST_KNET_CORE_LIST_APPEND, NULL, NULL)
{
    int ret = 0;
    ret = KnetCoreListAppend(-1);
    DT_ASSERT_EQUAL(ret, -1);
}

DTEST_CASE_F(CONFIG_CORE_QUEUE, TEST_KNET_CHECK_DUPLICATE_CORE, NULL, NULL)
{
    bool ret = KnetCheckDuplicateCore();
    DT_ASSERT_EQUAL(ret, false);
}

DTEST_CASE_F(CONFIG_CORE_QUEUE, TEST_KNET_QUEUE_INIT, NULL, NULL)
{
    KnetQueueInit();
}

DTEST_CASE_F(CONFIG_CORE_QUEUE, TEST_KNET_SET_PROCESS_LOCAL_QID, NULL, NULL)
{
    int ret = KnetSetProcessLocalQid(-1, 0, 0);
    DT_ASSERT_EQUAL(ret, -1);

    int clientId = 2;
    ret = KnetSetProcessLocalQid(clientId, clientId, 0);
    DT_ASSERT_EQUAL(ret, 0);
    ret = KnetDelProcessLocalQid(clientId);
    DT_ASSERT_EQUAL(ret, 0);
}

DTEST_CASE_F(CONFIG_CORE_QUEUE, TEST_KNET_GET_PROCESS_LOCAL_QID, NULL, NULL)
{
    int ret = KnetGetProcessLocalQid(-1);
    DT_ASSERT_EQUAL(ret, -1);

    int clientId = 2;
    ret = KnetSetProcessLocalQid(clientId, clientId, 0);
    DT_ASSERT_EQUAL(ret, 0);
    ret = KnetGetProcessLocalQid(clientId);
    DT_ASSERT_EQUAL(ret, clientId);
    ret = KnetDelProcessLocalQid(clientId);
    DT_ASSERT_EQUAL(ret, 0);
}

DTEST_CASE_F(CONFIG_CORE_QUEUE, TEST_KNET_GET_QUEUEID_FROM_POOL, NULL, NULL)
{
    int ret = KnetGetQueueIdFromPool(0);
    DT_ASSERT_NOT_EQUAL(ret, -1);
    ret = KnetFreeQueueIdInPool(ret);
    DT_ASSERT_EQUAL(ret, 0);
}

DTEST_CASE_F(CONFIG_CORE_QUEUE, TEST_KNET_FREE_QUEUEID_IN_POOL, NULL, NULL)
{
    int ret = KnetFreeQueueIdInPool(-1);
    DT_ASSERT_EQUAL(ret, -1);

    ret = KnetGetQueueIdFromPool(0);
    DT_ASSERT_NOT_EQUAL(ret, -1);
    ret = KnetFreeQueueIdInPool(ret);
    DT_ASSERT_EQUAL(ret, 0);
}

DTEST_CASE_F(CONFIG_CORE_QUEUE, TEST_KNET_DEL_PROCESS_LOCAL_QID, NULL, NULL)
{
    int ret = KnetDelProcessLocalQid(-1);
    DT_ASSERT_EQUAL(ret, -1);

    int clientId = 2;
    ret = KnetSetProcessLocalQid(clientId, clientId, 0);
    DT_ASSERT_EQUAL(ret, 0);
    ret = KnetDelProcessLocalQid(clientId);
    DT_ASSERT_EQUAL(ret, 0);
}

DTEST_CASE_F(CONFIG_CORE_QUEUE, TEST_GET_CORE_NORMAL, NULL, NULL)
{
    int ret = 0;

    ret = KnetGetCoreByQueueId(-1);
    DT_ASSERT_EQUAL(ret, -1);

    ret = KnetGetCoreByQueueId(KnetGetCoreNum());
    DT_ASSERT_EQUAL(ret, -1);

    ret = KnetGetCoreByQueueId(0);
    DT_ASSERT_NOT_EQUAL(ret, -1);
}

DTEST_CASE_F(CONFIG_CORE_QUEUE, TEST_SetEnvQueueId, NULL, NULL)
{
    int ret = SetEnvQueueId(KNET_QUEUE_ID_INVALID);
    DT_ASSERT_EQUAL(ret, -1);
}