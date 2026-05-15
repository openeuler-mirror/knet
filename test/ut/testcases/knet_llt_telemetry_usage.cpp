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
#include <unistd.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

#include "cJSON.h"
#include "rte_ethdev.h"
#include "dp_debug_api.h"
#include "knet_mock.h"
#include "knet_types.h"
#include "knet_log.h"
#include "knet_dpdk_init.h"
#include "securec.h"
#include "common.h"
#include "mock.h"

#define MAX_USAGE_OUTPUT_LEN_TEST 8192
#define MAX_STR_SIZE_TEST 64
extern "C" {
#include "rte_telemetry.h"
#include "knet_telemetry_call.h"
extern char g_usageDebugOutput[MAX_USAGE_OUTPUT_LEN_TEST];
extern int FormatBitrate(char *output, size_t outputSize, uint64_t bytes, double interval);
}

/**
 * @brief KnetTelemetryEthdevUsageCallback 函数，空入参测试
 *  测试步骤：
 *  1. 清空 g_usageDebugOutput；
 *  2. 入参 cmd 为空，有预期结果 1；
 *  3. 入参 cmd 非空，入参 params 为空，有预期结果 1；
 *  4. 入参 cmd 非空，入参 params 非空，入参 data 为空，有预期结果 1；
 *  预期结果：
 *  1. 返回失败，g_usageDebugOutput 仍然为空；
 */
DTEST_CASE_F(TELE_USAGE, TEST_KNET_TELEMETRY_USAGE_CALLBACK_NULL_IPUT, NULL, NULL)
{
    int ret;
    (void)memset_s(g_usageDebugOutput, sizeof(g_usageDebugOutput), 0, sizeof(g_usageDebugOutput));

    ret = KnetTelemetryEthdevUsageCallback(NULL, NULL, NULL);
    DT_ASSERT_NOT_EQUAL(ret, 0);
    DT_ASSERT_EQUAL(g_usageDebugOutput[0] == '\0', true);

    const char *cmd = "/knet/ethdev/usage";
    ret = KnetTelemetryEthdevUsageCallback(cmd, NULL, NULL);
    DT_ASSERT_NOT_EQUAL(ret, 0);
    DT_ASSERT_EQUAL(g_usageDebugOutput[0] == '\0', true);

    const char *params = "0 1";
    ret = KnetTelemetryEthdevUsageCallback(cmd, params, NULL);
    DT_ASSERT_NOT_EQUAL(ret, 0);
    DT_ASSERT_EQUAL(g_usageDebugOutput[0] == '\0', true);
}

/**
 * @brief KnetTelemetryEthdevUsageCallback 函数，非法 params 测试，params 的格式应为 "<port> <time>"，port 和 time 为正整数
 *  测试步骤：
 *  1. 清空 g_usageDebugOutput；
 *  2. 入参非空，params 为非法字符 "?"，有预期结果 1；
 *  3. 入参非空，params 为 "0 "，参数数量不足，有预期结果 1；
 *  4. 入参非空，params 为 "0 -1"，time 小于 0，有预期结果 1；
 *  5. 入参非空，params 为 "0 61"，time 大于最大值，有预期结果 1；
 *  预期结果：
 *  1. 返回失败，g_usageDebugOutput 仍然为空，入参 data 的内容没有发生变化；
 */
DTEST_CASE_F(TELE_USAGE, TEST_KNET_TELEMETRY_USAGE_CALLBACK_INVALID_PARAMS, NULL, NULL)
{
    int ret;
    (void)memset_s(g_usageDebugOutput, sizeof(g_usageDebugOutput), 0, sizeof(g_usageDebugOutput));

    const char *cmd = "/knet/ethdev/usage";
    struct rte_tel_data *data = rte_tel_data_alloc();
    DT_ASSERT_NOT_EQUAL(data, NULL);

    const char *params = "?";
    ret = KnetTelemetryEthdevUsageCallback(cmd, params, data);
    DT_ASSERT_NOT_EQUAL(ret, 0);
    DT_ASSERT_EQUAL(g_usageDebugOutput[0] == '\0', true);

    params = "0 ";
    ret = KnetTelemetryEthdevUsageCallback(cmd, params, data);
    DT_ASSERT_NOT_EQUAL(ret, 0);
    DT_ASSERT_EQUAL(g_usageDebugOutput[0] == '\0', true);

    params = "0 -1";
    ret = KnetTelemetryEthdevUsageCallback(cmd, params, data);
    DT_ASSERT_NOT_EQUAL(ret, 0);
    DT_ASSERT_EQUAL(g_usageDebugOutput[0] == '\0', true);

    params = "0 61";
    ret = KnetTelemetryEthdevUsageCallback(cmd, params, data);
    DT_ASSERT_NOT_EQUAL(ret, 0);
    DT_ASSERT_EQUAL(g_usageDebugOutput[0] == '\0', true);

    rte_tel_data_free(data);
}

static int g_successCnt = 0;
static int g_maxSuccessCnt = 0;

int rte_eth_stats_get_mock(uint16_t port_id, struct rte_eth_stats *stats)
{
    if (g_successCnt == g_maxSuccessCnt) {
        return -1;
    }

    ++g_successCnt;
    
    stats->ipackets = 0;
    stats->opackets = 0;
    stats->ibytes = 0;
    stats->obytes = 0;
    return 0;
}

/**
 * @brief KnetTelemetryEthdevUsageCallback 函数，params 中的 port 不存在，或采样过程中 port 消失；
 *  测试步骤：
 *  1. 清空 g_usageDebugOutput；
 *  2. 打桩 rte_eth_stats_get 函数，令其返回失败，有预期结果 1；
 *  3. 入参非空，params 为 "-1 1"，有预期结果 2；
 *  4. 打桩 rte_eth_stats_get 函数，当成功 3 次执行后返回失败，有预期结果 1；
 *  5. 入参非空，params 为 "0 10"，有预期结果 2；
 *  预期结果：
 *  1. 打桩成功；
 *  2. 返回失败，g_usageDebugOutput 仍然为空，入参 data 的内容没有发生变化；
 */
DTEST_CASE_F(TELE_USAGE, TEST_KNET_TELEMETRY_USAGE_CALLBACK_PORT_NOT_EXSIT, NULL, NULL)
{
    int ret;
    (void)memset_s(g_usageDebugOutput, sizeof(g_usageDebugOutput), 0, sizeof(g_usageDebugOutput));

    const char *cmd = "/knet/ethdev/usage";
    struct rte_tel_data *data = rte_tel_data_alloc();
    DT_ASSERT_NOT_EQUAL(data, NULL);

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(sleep, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_eth_stats_get, TEST_GetFuncRetPositive(1));
    const char *params = "-1 1";
    g_successCnt = 0;
    g_maxSuccessCnt = 3;    // 令 rte_eth_stats_get 成功 3 次
    ret = KnetTelemetryEthdevUsageCallback(cmd, params, data);
    DT_ASSERT_NOT_EQUAL(ret, 0);
    DT_ASSERT_EQUAL(g_usageDebugOutput[0] == '\0', true);
    Mock->Delete(rte_eth_stats_get);

    Mock->Create(rte_eth_stats_get, rte_eth_stats_get_mock);
    params = "1 10";
    ret = KnetTelemetryEthdevUsageCallback(cmd, params, data);
    DT_ASSERT_NOT_EQUAL(ret, 0);
    DT_ASSERT_EQUAL(g_usageDebugOutput[0] == '\0', true);
    Mock->Delete(rte_eth_stats_get);
    Mock->Delete(sleep);
    
    rte_tel_data_free(data);
    DeleteMock(Mock);
}

/**
 * @brief FormatBitrate 函数，snprintf_s 失败
 *  测试步骤：
 *  1. 打桩 snprintf_s 函数，令其返回失败，有预期结果 1；
 *  2. 入参合法，有预期结果 2；
 *  预期结果：
 *  1. 打桩成功；
 *  2. 返回失败，output 字符串没有发生变化；
 */
DTEST_CASE_F(TELE_USAGE, TEST_FORMAT_BITRATE_SNPRINTF_FAILED, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    int ret;
    Mock->Create(snprintf_s, TEST_GetFuncRetNegative(1));

    uint64_t bytes = 1000;
    char output[MAX_STR_SIZE_TEST] = {0};
    double interval = 1.0;
    ret = FormatBitrate(output, MAX_STR_SIZE_TEST, bytes, interval);
    DT_ASSERT_NOT_EQUAL(ret, 0);
    DT_ASSERT_EQUAL(output[0] == '\0', true);

    Mock->Delete(snprintf_s);

    DeleteMock(Mock);
}

/**
 * @brief KnetTelemetryEthdevUsageCallback，json 解析失败
 *  测试步骤：
 *  1. 清空 g_usageDebugOutput；
 *  2. 打桩 cJSON_Parse 函数，令其返回空，有预期结果 1；
 *  3. 入参合法，有预期结果 2；
 *  预期结果：
 *  1. 打桩成功；
 *  2. 返回失败，g_usageDebugOutput 中的内容符合预期，入参 data 的内容没有发生变化；
 */
DTEST_CASE_F(TELE_USAGE, TEST_KNET_TELEMETRY_USAGE_CALLBACK_JSON_PARSE_FAILED, NULL, NULL)
{
    int ret;
    (void)memset_s(g_usageDebugOutput, sizeof(g_usageDebugOutput), 0, sizeof(g_usageDebugOutput));

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(cJSON_Parse, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_eth_stats_get, rte_eth_stats_get_mock);
    Mock->Create(sleep, TEST_GetFuncRetPositive(0));

    const char *cmd = "/knet/ethdev/usage";
    struct rte_tel_data *data = rte_tel_data_alloc();
    DT_ASSERT_NOT_EQUAL(data, NULL);

    const char *params = "0 1";
    g_successCnt = 0;
    g_maxSuccessCnt = 10;   // 令 rte_eth_stats_get 成功 10 次
    ret = KnetTelemetryEthdevUsageCallback(cmd, params, data);
    DT_ASSERT_NOT_EQUAL(ret, 0);
    const char *outputStr = "[{\"t\":\"0_1s\",\"tx\":\"0.00 bit/s, 0 p/s\",\"rx\":\"0.00 bit/s, 0 p/s\"}]";
    DT_ASSERT_EQUAL(strcmp(g_usageDebugOutput, outputStr), 0);
    
    Mock->Delete(cJSON_Parse);
    Mock->Delete(rte_eth_stats_get);
    Mock->Delete(sleep);
    
    rte_tel_data_free(data);
    DeleteMock(Mock);
}

/**
 * @brief KnetTelemetryEthdevUsageCallback，rte_tel_data_alloc 失败
 *  测试步骤：
 *  1. 清空 g_usageDebugOutput；
 *  2. 打桩 rte_tel_data_alloc 函数，令其返回空，有预期结果 1；
 *  3. 入参合法，有预期结果 2；
 *  预期结果：
 *  1. 打桩成功；
 *  2. 返回失败，g_usageDebugOutput 中的内容符合预期，入参 data 的内容没有发生变化；
 */
DTEST_CASE_F(TELE_USAGE, TEST_KNET_TELEMETRY_USAGE_CALLBACK_RTE_TEL_DATA_ALLOC_FAILED, NULL, NULL)
{
    int ret;
    (void)memset_s(g_usageDebugOutput, sizeof(g_usageDebugOutput), 0, sizeof(g_usageDebugOutput));

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    Mock->Create(rte_eth_stats_get, rte_eth_stats_get_mock);
    Mock->Create(sleep, TEST_GetFuncRetPositive(0));

    const char *cmd = "/knet/ethdev/usage";
    struct rte_tel_data *data = rte_tel_data_alloc();
    DT_ASSERT_NOT_EQUAL(data, NULL);

    Mock->Create(rte_tel_data_alloc, TEST_GetFuncRetPositive(0));

    const char *params = "0 1";
    g_successCnt = 0;
    g_maxSuccessCnt = 10;   // 令 rte_eth_stats_get 成功 10 次
    ret = KnetTelemetryEthdevUsageCallback(cmd, params, data);
    DT_ASSERT_NOT_EQUAL(ret, 0);
    const char *outputStr = "[{\"t\":\"0_1s\",\"tx\":\"0.00 bit/s, 0 p/s\",\"rx\":\"0.00 bit/s, 0 p/s\"}]";
    DT_ASSERT_EQUAL(strcmp(g_usageDebugOutput, outputStr), 0);
    
    Mock->Delete(rte_eth_stats_get);
    Mock->Delete(sleep);
    Mock->Delete(rte_tel_data_alloc);
    
    rte_tel_data_free(data);
    DeleteMock(Mock);
}

/**
 * @brief KnetTelemetryEthdevUsageCallback，正常运行测试，由于 telemetry data 结构体未对外暴露，且没有提供查询其内容以及递归释放
 *        的接口，所以对 telemetry data 相关接口进行打桩处理。
 *  测试步骤：
 *  1. 清空 g_usageDebugOutput；
 *  2. 入参合法，params 为 "0 1", 有预期结果 1；
 *  预期结果：
 *  1. 返回失败，g_usageDebugOutput 中的内容符合预期，入参 data 的内容符合预期；
 */
DTEST_CASE_F(TELE_USAGE, TEST_KNET_TELEMETRY_USAGE_CALLBACK_NORMAL, NULL, NULL)
{
    int ret;
    (void)memset_s(g_usageDebugOutput, sizeof(g_usageDebugOutput), 0, sizeof(g_usageDebugOutput));

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    struct rte_tel_data *data = rte_tel_data_alloc();

    Mock->Create(rte_eth_stats_get, rte_eth_stats_get_mock);
    Mock->Create(sleep, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_tel_data_alloc, TEST_GetFuncRetPositive(1));
    Mock->Create(rte_tel_data_start_dict, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_tel_data_add_dict_string, TEST_GetFuncRetPositive(0));
    Mock->Create(rte_tel_data_add_dict_container, TEST_GetFuncRetPositive(0));

    const char *cmd = "/knet/ethdev/usage";
    DT_ASSERT_NOT_EQUAL(data, NULL);

    const char *params = "0 1";
    g_successCnt = 0;
    g_maxSuccessCnt = 10;    // 令 rte_eth_stats_get 成功 10 次
    ret = KnetTelemetryEthdevUsageCallback(cmd, params, data);
    DT_ASSERT_EQUAL(ret, 0);
    const char *outputStr = "[{\"t\":\"0_1s\",\"tx\":\"0.00 bit/s, 0 p/s\",\"rx\":\"0.00 bit/s, 0 p/s\"}]";
    DT_ASSERT_EQUAL(strcmp(g_usageDebugOutput, outputStr), 0);
    
    Mock->Delete(rte_eth_stats_get);
    Mock->Delete(sleep);
    Mock->Delete(rte_tel_data_alloc);
    Mock->Delete(rte_tel_data_start_dict);
    Mock->Delete(rte_tel_data_add_dict_string);
    Mock->Delete(rte_tel_data_add_dict_container);
    
    rte_tel_data_free(data);
    DeleteMock(Mock);
}