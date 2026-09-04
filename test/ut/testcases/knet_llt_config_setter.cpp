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
#include <limits.h>

#include "securec.h"
#include "cJSON.h"
#include "knet_lock.h"
#include "common.h"
#include "mock.h"
#include "knet_types.h"
#include "knet_log.h"
#include "knet_config.h"
extern "C" {
#include "knet_config_setter.h"
}
#include "knet_utils.h"

/* 测试 knet_config_setter.c 中的 setter 函数参数校验和错误路径.
 * 这些函数为纯函数(无外部依赖), 直接构造 cJSON/union 入参调用即可. */

/* ============ IntSetter ============ */

DTEST_CASE_F(SETTER, TEST_INT_SETTER_NULL_PARAM, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    param.intValue.min = 0;
    param.intValue.max = 100;
    cJSON json = {0};
    json.type = cJSON_Number;
    json.valuedouble = 5;
    json.valueint = 5;

    /* NULL jsonValue */
    DT_ASSERT_EQUAL(IntSetter(NULL, &val, &param), -1);
    /* NULL value */
    DT_ASSERT_EQUAL(IntSetter(&json, NULL, &param), -1);
    /* NULL param */
    DT_ASSERT_EQUAL(IntSetter(&json, &val, NULL), -1);
}

DTEST_CASE_F(SETTER, TEST_INT_SETTER_WRONG_TYPE, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    param.intValue.min = 0;
    param.intValue.max = 100;
    cJSON json = {0};
    json.type = cJSON_String; /* 期望Number, 传String */

    DT_ASSERT_EQUAL(IntSetter(&json, &val, &param), -1);
}

DTEST_CASE_F(SETTER, TEST_INT_SETTER_OUT_OF_RANGE, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    param.intValue.min = 0;
    param.intValue.max = 100;
    cJSON json = {0};
    json.type = cJSON_Number;
    json.valuedouble = (double)INT32_MAX + 1; /* 超出int32范围 */
    json.valueint = 0;

    DT_ASSERT_EQUAL(IntSetter(&json, &val, &param), -1);
}

DTEST_CASE_F(SETTER, TEST_INT_SETTER_NOT_INTEGER, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    param.intValue.min = 0;
    param.intValue.max = 100;
    cJSON json = {0};
    json.type = cJSON_Number;
    json.valuedouble = 5.5; /* 非整数 */
    json.valueint = 5;

    DT_ASSERT_EQUAL(IntSetter(&json, &val, &param), -1);
}

DTEST_CASE_F(SETTER, TEST_INT_SETTER_BELOW_MIN, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    param.intValue.min = 10;
    param.intValue.max = 100;
    cJSON json = {0};
    json.type = cJSON_Number;
    json.valuedouble = 5;
    json.valueint = 5;

    DT_ASSERT_EQUAL(IntSetter(&json, &val, &param), -1);
}

DTEST_CASE_F(SETTER, TEST_INT_SETTER_ABOVE_MAX, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    param.intValue.min = 0;
    param.intValue.max = 100;
    cJSON json = {0};
    json.type = cJSON_Number;
    json.valuedouble = 200;
    json.valueint = 200;

    DT_ASSERT_EQUAL(IntSetter(&json, &val, &param), -1);
}

DTEST_CASE_F(SETTER, TEST_INT_SETTER_OK, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    param.intValue.min = 0;
    param.intValue.max = 100;
    cJSON json = {0};
    json.type = cJSON_Number;
    json.valuedouble = 50;
    json.valueint = 50;

    DT_ASSERT_EQUAL(IntSetter(&json, &val, &param), 0);
    DT_ASSERT_EQUAL(val.intValue, 50);
}

/* ============ Uint64Setter ============ */

DTEST_CASE_F(SETTER, TEST_UINT64_SETTER_NULL_PARAM, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    cJSON json = {0};
    json.type = cJSON_String;
    json.valuestring = (char *)"123";

    DT_ASSERT_EQUAL(Uint64Setter(NULL, &val, &param), -1);
    DT_ASSERT_EQUAL(Uint64Setter(&json, NULL, &param), -1);
    DT_ASSERT_EQUAL(Uint64Setter(&json, &val, NULL), -1);
}

DTEST_CASE_F(SETTER, TEST_UINT64_SETTER_WRONG_TYPE, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    cJSON json = {0};
    json.type = cJSON_Number; /* 期望String */

    DT_ASSERT_EQUAL(Uint64Setter(&json, &val, &param), -1);
}

DTEST_CASE_F(SETTER, TEST_UINT64_SETTER_NULL_STRING, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    cJSON json = {0};
    json.type = cJSON_String;
    json.valuestring = NULL;

    DT_ASSERT_EQUAL(Uint64Setter(&json, &val, &param), -1);
}

DTEST_CASE_F(SETTER, TEST_UINT64_SETTER_INVALID_STR, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    cJSON json = {0};
    json.type = cJSON_String;
    json.valuestring = (char *)"abc"; /* 非数字 */

    DT_ASSERT_EQUAL(Uint64Setter(&json, &val, &param), -1);
}

DTEST_CASE_F(SETTER, TEST_UINT64_SETTER_NEGATIVE, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    cJSON json = {0};
    json.type = cJSON_String;
    json.valuestring = (char *)"-1"; /* 负号不允许 */

    DT_ASSERT_EQUAL(Uint64Setter(&json, &val, &param), -1);
}

DTEST_CASE_F(SETTER, TEST_UINT64_SETTER_OK, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    cJSON json = {0};
    json.type = cJSON_String;
    json.valuestring = (char *)"12345";

    DT_ASSERT_EQUAL(Uint64Setter(&json, &val, &param), 0);
    DT_ASSERT_EQUAL(val.uint64Value, 12345);
}

/* ============ IpSetter ============ */

DTEST_CASE_F(SETTER, TEST_IP_SETTER_NULL_PARAM, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    cJSON json = {0};
    json.type = cJSON_String;
    json.valuestring = (char *)"192.168.1.1";

    DT_ASSERT_EQUAL(IpSetter(NULL, &val, &param), -1);
    DT_ASSERT_EQUAL(IpSetter(&json, NULL, &param), -1);
    DT_ASSERT_EQUAL(IpSetter(&json, &val, NULL), -1);
}

DTEST_CASE_F(SETTER, TEST_IP_SETTER_WRONG_TYPE, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    cJSON json = {0};
    json.type = cJSON_Number;

    DT_ASSERT_EQUAL(IpSetter(&json, &val, &param), -1);
}

DTEST_CASE_F(SETTER, TEST_IP_SETTER_INVALID, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    cJSON json = {0};
    json.type = cJSON_String;
    json.valuestring = (char *)"999.999.999.999";

    DT_ASSERT_EQUAL(IpSetter(&json, &val, &param), -1);
}

DTEST_CASE_F(SETTER, TEST_IP_SETTER_OK, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    cJSON json = {0};
    json.type = cJSON_String;
    json.valuestring = (char *)"10.0.0.1";

    DT_ASSERT_EQUAL(IpSetter(&json, &val, &param), 0);
}

/* ============ NetMaskSetter ============ */

DTEST_CASE_F(SETTER, TEST_NETMASK_SETTER_NULL_PARAM, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    cJSON json = {0};
    json.type = cJSON_String;
    json.valuestring = (char *)"255.255.255.0";

    DT_ASSERT_EQUAL(NetMaskSetter(NULL, &val, &param), -1);
    DT_ASSERT_EQUAL(NetMaskSetter(&json, NULL, &param), -1);
    DT_ASSERT_EQUAL(NetMaskSetter(&json, &val, NULL), -1);
}

DTEST_CASE_F(SETTER, TEST_NETMASK_SETTER_INVALID_ADDR, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    cJSON json = {0};
    json.type = cJSON_String;
    json.valuestring = (char *)"invalid";

    DT_ASSERT_EQUAL(NetMaskSetter(&json, &val, &param), -1);
}

DTEST_CASE_F(SETTER, TEST_NETMASK_SETTER_INVALID_MASK, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    cJSON json = {0};
    json.type = cJSON_String;
    json.valuestring = (char *)"255.0.255.0"; /* 非连续掩码 */

    DT_ASSERT_EQUAL(NetMaskSetter(&json, &val, &param), -1);
}

DTEST_CASE_F(SETTER, TEST_NETMASK_SETTER_OK, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    cJSON json = {0};
    json.type = cJSON_String;
    json.valuestring = (char *)"255.255.0.0";

    DT_ASSERT_EQUAL(NetMaskSetter(&json, &val, &param), 0);
}

/* ============ MacSetter ============ */

DTEST_CASE_F(SETTER, TEST_MAC_SETTER_NULL_PARAM, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    cJSON json = {0};
    json.type = cJSON_String;
    json.valuestring = (char *)"aa:bb:cc:dd:ee:ff";

    DT_ASSERT_EQUAL(MacSetter(NULL, &val, &param), -1);
    DT_ASSERT_EQUAL(MacSetter(&json, NULL, &param), -1);
    DT_ASSERT_EQUAL(MacSetter(&json, &val, NULL), -1);
}

DTEST_CASE_F(SETTER, TEST_MAC_SETTER_WRONG_TYPE, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    cJSON json = {0};
    json.type = cJSON_Number;

    DT_ASSERT_EQUAL(MacSetter(&json, &val, &param), -1);
}

DTEST_CASE_F(SETTER, TEST_MAC_SETTER_INVALID, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    cJSON json = {0};
    json.type = cJSON_String;
    json.valuestring = (char *)"invalid_mac";

    DT_ASSERT_EQUAL(MacSetter(&json, &val, &param), -1);
}

DTEST_CASE_F(SETTER, TEST_MAC_SETTER_OK, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    cJSON json = {0};
    json.type = cJSON_String;
    json.valuestring = (char *)"aa:bb:cc:dd:ee:ff";

    DT_ASSERT_EQUAL(MacSetter(&json, &val, &param), 0);
}

/* ============ LogLevelSetter ============ */

DTEST_CASE_F(SETTER, TEST_LOGLEVEL_SETTER_NULL_PARAM, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    cJSON json = {0};
    json.type = cJSON_String;
    json.valuestring = (char *)"INFO";

    DT_ASSERT_EQUAL(LogLevelSetter(NULL, &val, &param), -1);
    DT_ASSERT_EQUAL(LogLevelSetter(&json, NULL, &param), -1);
    DT_ASSERT_EQUAL(LogLevelSetter(&json, &val, NULL), -1);
}

DTEST_CASE_F(SETTER, TEST_LOGLEVEL_SETTER_WRONG_TYPE, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    cJSON json = {0};
    json.type = cJSON_Number;

    DT_ASSERT_EQUAL(LogLevelSetter(&json, &val, &param), -1);
}

DTEST_CASE_F(SETTER, TEST_LOGLEVEL_SETTER_INVALID, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    cJSON json = {0};
    json.type = cJSON_String;
    json.valuestring = (char *)"VERBOSE"; /* 不在ERROR|WARNING|DEBUG|INFO中 */

    DT_ASSERT_EQUAL(LogLevelSetter(&json, &val, &param), -1);
}

DTEST_CASE_F(SETTER, TEST_LOGLEVEL_SETTER_OK, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    cJSON json = {0};
    json.type = cJSON_String;
    json.valuestring = (char *)"debug";

    DT_ASSERT_EQUAL(LogLevelSetter(&json, &val, &param), 0);
}

/* ============ StringSetter ============ */

DTEST_CASE_F(SETTER, TEST_STRING_SETTER_NULL_PARAM, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    (void)snprintf_truncated_s(param.pattern, sizeof(param.pattern), "^[a-z]+$");
    cJSON json = {0};
    json.type = cJSON_String;
    json.valuestring = (char *)"abc";

    DT_ASSERT_EQUAL(StringSetter(NULL, &val, &param), -1);
    DT_ASSERT_EQUAL(StringSetter(&json, NULL, &param), -1);
    DT_ASSERT_EQUAL(StringSetter(&json, &val, NULL), -1);
}

DTEST_CASE_F(SETTER, TEST_STRING_SETTER_WRONG_TYPE, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    (void)snprintf_truncated_s(param.pattern, sizeof(param.pattern), "^[a-z]+$");
    cJSON json = {0};
    json.type = cJSON_Number;

    DT_ASSERT_EQUAL(StringSetter(&json, &val, &param), -1);
}

DTEST_CASE_F(SETTER, TEST_STRING_SETTER_REGEX_MISMATCH, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    (void)snprintf_truncated_s(param.pattern, sizeof(param.pattern), "^[a-z]+$");
    cJSON json = {0};
    json.type = cJSON_String;
    json.valuestring = (char *)"123"; /* 不匹配^[a-z]+$ */

    DT_ASSERT_EQUAL(StringSetter(&json, &val, &param), -1);
}

DTEST_CASE_F(SETTER, TEST_STRING_SETTER_OK, NULL, NULL)
{
    union KNET_CfgValue val = {0};
    union KnetCfgValidateParam param = {0};
    (void)snprintf_truncated_s(param.pattern, sizeof(param.pattern), "^[a-z]+$");
    cJSON json = {0};
    json.type = cJSON_String;
    json.valuestring = (char *)"hello";

    DT_ASSERT_EQUAL(StringSetter(&json, &val, &param), 0);
}
