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

#include "securec.h"

#include <stdio.h>
#include <stdlib.h>
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
#include <fcntl.h>

#include "rte_timer.h"
#include "rte_ethdev.h"

#include "knet_log.h"
#include "knet_config_hw_scan.h"
#include "knet_config_core_queue.h"
#include "knet_config_setter.h"
#include "knet_utils.h"
#include "knet_rpc.h"
#include "knet_config_rpc.h"
#include "knet_config.h"

#include "common.h"
#include "mock.h"
#include "cJSON.h"

extern "C" {
static cJSON *g_json = NULL;

cJSON *CreateConfigJson()
{
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        return NULL;
    }

    cJSON_AddStringToObject(json, "version", "2.0.0");

    cJSON *common = cJSON_CreateObject();
    cJSON_AddNumberToObject(common, "mode", 0);
    cJSON_AddStringToObject(common, "log_level", "WARNING");
    cJSON_AddNumberToObject(common, "ctrl_vcpu_nums", 1);
    cJSON_AddNumberToObject(common, "ctrl_ring_per_vcpu", 1);
    
    int vcpu_array[] = {0};
    cJSON *ctrl_vcpu_ids = cJSON_CreateIntArray(vcpu_array, 1);
    cJSON_AddItemToObject(common, "ctrl_vcpu_ids", ctrl_vcpu_ids);
    cJSON_AddNumberToObject(common, "zcopy_enable", 0);
    cJSON_AddNumberToObject(common, "cothread_enable", 0);
    cJSON_AddItemToObject(json, "common", common);

    cJSON *interface = cJSON_CreateObject();
    cJSON_AddNumberToObject(interface, "user_bond_enable", 0);
    // 4表示bond模式
    cJSON_AddNumberToObject(interface, "user_bond_mode", 4);

    char *bdf_num[] = {"0000:06:00.0"};
    cJSON *bdf_nums = cJSON_CreateStringArray(bdf_num, 1);
    cJSON_AddItemToObject(interface, "bdf_nums", bdf_nums);
    cJSON_AddStringToObject(interface, "mac", "52:54:00:2e:1b:a0");
    cJSON_AddStringToObject(interface, "ip", "192.168.1.6");
    cJSON_AddStringToObject(interface, "netmask", "255.255.255.0");
    cJSON_AddStringToObject(interface, "gateway", "0.0.0.0");
    // 1500表示最大传输单元
    cJSON_AddNumberToObject(interface, "mtu", 1500);
    cJSON_AddItemToObject(json, "interface", interface);

    cJSON *hw_offload = cJSON_CreateObject();
    cJSON_AddNumberToObject(hw_offload, "tso", 0);
    cJSON_AddNumberToObject(hw_offload, "lro", 0);
    cJSON_AddNumberToObject(hw_offload, "tcp_checksum", 0);
    cJSON_AddNumberToObject(hw_offload, "bifur_enable", 0);
    cJSON_AddItemToObject(json, "hw_offload", hw_offload);

    cJSON *proto_stack = cJSON_CreateObject();
    // 20480表示max_mbuf个数
    cJSON_AddNumberToObject(proto_stack, "max_mbuf", 20480);
    cJSON_AddNumberToObject(proto_stack, "max_worker_num", 1);
    // 1024表示max_route个数
    cJSON_AddNumberToObject(proto_stack, "max_route", 1024);
    // 1024表示max_arp个数
    cJSON_AddNumberToObject(proto_stack, "max_arp", 1024);
    // 4096表示max_tcpcb个数
    cJSON_AddNumberToObject(proto_stack, "max_tcpcb", 4096);
    // 4096表示max_udpcb个数
    cJSON_AddNumberToObject(proto_stack, "max_udpcb", 4096);
    cJSON_AddNumberToObject(proto_stack, "tcp_sack", 1);
    cJSON_AddNumberToObject(proto_stack, "tcp_dack", 1);
    // 30表述msl时间
    cJSON_AddNumberToObject(proto_stack, "msl_time", 30);
    // 600表示fin超时时间
    cJSON_AddNumberToObject(proto_stack, "fin_timeout", 600);
    // 49152表示min_port个数
    cJSON_AddNumberToObject(proto_stack, "min_port", 49152);
    // 65535表示max_port个数
    cJSON_AddNumberToObject(proto_stack, "max_port", 65535);
    // 10485760表示max_sendbuf大小
    cJSON_AddNumberToObject(proto_stack, "max_sendbuf", 10485760);
    // 8192表示def_sendbuf大小
    cJSON_AddNumberToObject(proto_stack, "def_sendbuf", 8192);
    // 10485760表示max_recvbuf大小
    cJSON_AddNumberToObject(proto_stack, "max_recvbuf", 10485760);
    // 8192表示def_recvbuf大小
    cJSON_AddNumberToObject(proto_stack, "def_recvbuf", 8192);
    cJSON_AddNumberToObject(proto_stack, "tcp_cookie", 0);
    // 1000表示reass_max次数
    cJSON_AddNumberToObject(proto_stack, "reass_max", 1000);
    // 30表示reass_timeout
    cJSON_AddNumberToObject(proto_stack, "reass_timeout", 30);
    // 5表示synack_retries次数
    cJSON_AddNumberToObject(proto_stack, "synack_retries", 5);
    // 65535表示zcopy_sge_len长度
    cJSON_AddNumberToObject(proto_stack, "zcopy_sge_len", 65535);
    // 8192表示zcopy_sge_num数量
    cJSON_AddNumberToObject(proto_stack, "zcopy_sge_num", 8192);
    cJSON_AddStringToObject(proto_stack, "epoll_data", "0");
    cJSON_AddItemToObject(json, "proto_stack", proto_stack);

    cJSON *dpdk = cJSON_CreateObject();
    cJSON_AddStringToObject(dpdk, "core_list_global", "1");
    cJSON_AddNumberToObject(dpdk, "queue_num", 1);
    // 256表示tx_cache_size大小
    cJSON_AddNumberToObject(dpdk, "tx_cache_size", 256);
    // 256表示rx_cache_size大小
    cJSON_AddNumberToObject(dpdk, "rx_cache_size", 256);
    cJSON_AddStringToObject(dpdk, "socket_mem", "--socket-mem=1024");
    cJSON_AddStringToObject(dpdk, "socket_limit", "--socket-limit=1024");
    cJSON_AddStringToObject(dpdk, "external_driver", "-dlibrte_net_sp600.so");
    cJSON_AddNumberToObject(dpdk, "telemetry", 1);
    cJSON_AddStringToObject(dpdk, "huge_dir", "");
    cJSON_AddStringToObject(dpdk, "base-virtaddr", "");
    cJSON_AddItemToObject(json, "dpdk", dpdk);

    return json;
}

char *CreateConfigJsonString()
{
    if (g_json == NULL) {
        return NULL;
    }
    char *jsonStr = cJSON_PrintUnformatted(g_json);
    if (jsonStr == NULL) {
        return NULL;
    }
    return jsonStr;
}

void ModifyConfigJson(cJSON *json, const char *objectName, const char *fieldName, cJSON *newValue)
{
    cJSON *obj = cJSON_GetObjectItem(json, objectName);
    if (obj != NULL) {
        cJSON *field = cJSON_GetObjectItem(obj, fieldName);
        if (field != NULL) {
            cJSON_DeleteItemFromObject(obj, fieldName);
            cJSON_AddItemToObject(obj, fieldName, newValue);
        }
    }
}

extern char *GetKnetCfgContent(const char *fileName);
extern int GetnicNeedId(void *hv, const char *interfaceName, int type);
extern int g_coreListIndex;
}
DTEST_CASE_F(COMM_CONF, TEST_COMM_CONF_DEFAULT, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    g_coreListIndex = 0;

    cJSON *json = CreateConfigJson();
    if (json == NULL) {
        printf("create config json failed\n");
    }
    g_json = json;

    Mock->Create(GetKnetCfgContent, CreateConfigJsonString);
    Mock->Create(GetnicNeedId, TEST_GetFuncRetPositive(0));
    int ret = KNET_InitCfg(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, 0);
    Mock->Delete(GetnicNeedId);
    Mock->Delete(GetKnetCfgContent);

    g_coreListIndex = 0;
    cJSON_Delete(json);
    g_json = NULL;
    DeleteMock(Mock);
}

DTEST_CASE_F(COMM_CONF, TEST_COMM_CONF_ALT, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    cJSON *json = CreateConfigJson();
    if (json == NULL) {
        printf("create config json failed\n");
    }
    g_json = json;
    
    Mock->Create(GetKnetCfgContent, CreateConfigJsonString);

    // 取值范围或类型错误的测试用例
    // 测试mode值为2的场景
    ModifyConfigJson(json, "common", "mode", cJSON_CreateNumber(2));
    int ret = KNET_InitCfg(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);
    // 测试mode值为0.5的场景
    ModifyConfigJson(json, "common", "mode", cJSON_CreateNumber(0.5));
    ret = KNET_InitCfg(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);
    ModifyConfigJson(json, "common", "mode", cJSON_CreateNumber(0));
    // 测试ctrl_vcpu_nums值为9的场景
    ModifyConfigJson(json, "common", "ctrl_vcpu_nums", cJSON_CreateNumber(9));
    ret = KNET_InitCfg(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);
    // 测试ctrl_vcpu_nums值为1.5的场景
    ModifyConfigJson(json, "common", "ctrl_vcpu_nums", cJSON_CreateNumber(1.5));
    ret = KNET_InitCfg(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);

    ModifyConfigJson(json, "common", "ctrl_vcpu_nums", cJSON_CreateNumber(-1));
    ret = KNET_InitCfg(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);
    ModifyConfigJson(json, "common", "ctrl_vcpu_nums", cJSON_CreateNumber(1));
    // 测试mtu值为255的场景
    ModifyConfigJson(json, "interface", "mtu", cJSON_CreateNumber(255));
    ret = KNET_InitCfg(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);
    // 1500表示mtu的默认值
    ModifyConfigJson(json, "interface", "mtu", cJSON_CreateNumber(1500));
    // 测试max_mbuf值为8191的场景
    ModifyConfigJson(json, "proto_stack", "max_mbuf", cJSON_CreateNumber(8191));
    ret = KNET_InitCfg(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);
    // 20480表示max_mbuf默认值
    ModifyConfigJson(json, "proto_stack", "max_mbuf", cJSON_CreateNumber(20480));
    // 测试queue_num值为65的场景
    ModifyConfigJson(json, "dpdk", "queue_num", cJSON_CreateNumber(65));
    ret = KNET_InitCfg(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);
    ModifyConfigJson(json, "dpdk", "queue_num", cJSON_CreateNumber(1));

    ModifyConfigJson(json, "interface", "mac", cJSON_CreateString("52:54:00:2e:1b:z0"));
    ret = KNET_InitCfg(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);
    ModifyConfigJson(json, "interface", "mac", cJSON_CreateString("52:54:00:2e:1b:a0"));

    ModifyConfigJson(json, "interface", "ip", cJSON_CreateString("192.168.1.256"));
    ret = KNET_InitCfg(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);
    ModifyConfigJson(json, "interface", "ip", cJSON_CreateString("192.168.1.6"));

    // ctrl_vcpu_nums与ctrl_vcpu_ids的大小不符
    // 测试ctrl_vcpu_nums为2但是ctrl_vcpu_ids只有一个的场景
    ModifyConfigJson(json, "common", "ctrl_vcpu_nums", cJSON_CreateNumber(2));
    ret = KNET_InitCfg(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);
    ModifyConfigJson(json, "common", "ctrl_vcpu_nums", cJSON_CreateNumber(1));

    // 多进程模式不支持多控制线程
    ModifyConfigJson(json, "common", "mode", cJSON_CreateNumber(1));
    // 测试多进程下存在4个ctrl_vcpu_ids的场景
    int a[]={0, 1, 2, 3};
    ModifyConfigJson(json, "common", "ctrl_vcpu_ids", cJSON_CreateIntArray(a, 4));
    ret = KNET_InitCfg(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);
    ModifyConfigJson(json, "common", "mode", cJSON_CreateNumber(0));
    int b[]={0};
    ModifyConfigJson(json, "common", "ctrl_vcpu_ids", cJSON_CreateIntArray(b, 1));

    // 控制面和数据面id相同
    ModifyConfigJson(json, "dpdk", "core_list_global", cJSON_CreateString("0"));
    ret = KNET_InitCfg(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);
    ModifyConfigJson(json, "dpdk", "core_list_global", cJSON_CreateString("1"));

    // 多进程模式开启bond
    ModifyConfigJson(json, "common", "mode", cJSON_CreateNumber(1));
    ModifyConfigJson(json, "interface", "user_bond_enable", cJSON_CreateNumber(1));
    ret = KNET_InitCfg(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);
    ModifyConfigJson(json, "common", "mode", cJSON_CreateNumber(0));
    ModifyConfigJson(json, "interface", "user_bond_enable", cJSON_CreateNumber(0));

    // bond与bdf数量不符
    ModifyConfigJson(json, "interface", "user_bond_enable", cJSON_CreateNumber(1));
    ret = KNET_InitCfg(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);
    char *c[] = {"0000:06:00.0", "0000:06:00.1", "0000:06:00.2" };
    ModifyConfigJson(json, "interface", "bdf_nums",
        cJSON_CreateStringArray(c, 3));
    ret = KNET_InitCfg(KNET_PROC_TYPE_PRIMARY);
    DT_ASSERT_EQUAL(ret, -1);
    ModifyConfigJson(json, "interface", "user_bond_enable", cJSON_CreateNumber(0));
    char *d[]={"0000:06:00.0"};
    ModifyConfigJson(json, "interface", "bdf_nums", cJSON_CreateStringArray(d, 1));

    Mock->Delete(GetKnetCfgContent);

    g_coreListIndex = 0;
    cJSON_Delete(json);
    g_json = NULL;
    DeleteMock(Mock);
}