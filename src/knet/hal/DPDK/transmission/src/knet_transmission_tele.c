/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: transmission telemetry维护流表的维测信息
 */
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "rte_eal.h"
#include "rte_hash.h"
#include "rte_errno.h"
#include "rte_memzone.h"

#include "knet_log.h"
#include "knet_types.h"
#include "knet_transmission.h"
#include "knet_transmission_hash.h"
#include "knet_config.h"
#include "knet_utils.h"
#include "knet_offload.h"

#define TELEMETRY_DEBUG_USLEEP 100000
#define MAX_KEY_NUM 128
#define MAX_KEY_LEN 32
#define TIMEOUT_TIMES 10
#define DECIMAL_NUM 10
#define PID_MAX_LEN 20
#define FLOW_TABLE_MAX_NUM 256
#define RIGHT_MOVE_16BITS 16
#define IP_32BITS_FULL_MASK 0xFFFF
#define INET_IP_STR_LEN 16
#define INET_PORT_STR_LEN 8
#define MASK_STR_LEN 16
#define INVALID_TID 0U
#define FLOW_PROTO_STR_MAX_LEN 32
#define FLOW_ACTION_STR_MAX_LEN 256 // 支持队列最大数量为32，action最大为RSS-0,1,2,...,31
#define FLOW_ARP_QUEUE_ID_STR_LEN 64
#define MAX_JSON_KEY_NAME_LEN 100

#define CHECK_ADD_VALUE_TO_DICT(func, ...)                                                                             \
    do {                                                                                                               \
        int __ret = -1;                                                                                                \
        if ((__ret = func(__VA_ARGS__)) != 0) {                                                                        \
            KNET_ERR("KNET telemetry failed to add %s ret %d", #func, __ret);                                          \
            return -1;                                                                                                 \
        }                                                                                                              \
    } while (0)

KNET_STATIC int GetIpStrAndPort(uint64_t ipPort, char ipStr[INET_IP_STR_LEN], char portStr[INET_PORT_STR_LEN])
{
    uint16_t port = ipPort & IP_32BITS_FULL_MASK;
    struct in_addr addr = {0};
    addr.s_addr = htonl(ipPort >> RIGHT_MOVE_16BITS);
    if (inet_ntop(AF_INET, &addr, ipStr, INET_ADDRSTRLEN) == NULL) {
        KNET_ERR("K-NET telemetry get flow failed, inet_ntop failed");
        return -1;
    }
    (void)snprintf_s(portStr, INET_PORT_STR_LEN, INET_PORT_STR_LEN - 1, "%u", port);
    return 0;
}
KNET_STATIC char *GetFlowPatternTypeStr(enum rte_flow_item_type type)
{
    // 目前只支持这四种协议
    if (type != RTE_FLOW_ITEM_TYPE_ETH && type != RTE_FLOW_ITEM_TYPE_IPV4 && type != RTE_FLOW_ITEM_TYPE_TCP &&
        type != RTE_FLOW_ITEM_TYPE_UDP) {
        return "No support";
    }
    char *patternStr[] = {[RTE_FLOW_ITEM_TYPE_ETH] = "ETH",
                          [RTE_FLOW_ITEM_TYPE_IPV4] = "IPV4",
                          [RTE_FLOW_ITEM_TYPE_TCP] = "TCP",
                          [RTE_FLOW_ITEM_TYPE_UDP] = "UDP"};
    return patternStr[type];
}
KNET_STATIC int GetFlowPatternStr(struct rte_flow_item *pattern, size_t patternLen, char *patternStr,
                                  size_t patternStrLen)
{
    size_t remaining = patternStrLen;
    size_t offset = 0;
    int ret = snprintf_s(patternStr, remaining, remaining - 1, "%s", GetFlowPatternTypeStr(pattern[0].type));
    if (ret < 0 || (size_t)ret >= remaining) {
        goto ERR;
    }
    offset += (size_t)ret;
    remaining -= (size_t)ret;
    for (size_t i = 1; i < patternLen && pattern[i].type != RTE_FLOW_ITEM_TYPE_END; i++) {
        if (remaining <= 1) {
            goto ERR;
        }
        ret = snprintf_s(patternStr + offset, remaining, remaining - 1, " %s", GetFlowPatternTypeStr(pattern[i].type));
        if (ret < 0 || (size_t)ret >= remaining) {
            goto ERR;
        }
        offset += (size_t)ret;
        remaining -= (size_t)ret;
    }
    return KNET_OK;
ERR:
    KNET_ERR("K-NET telemetry flow table proto pattern string truncated");
    return KNET_ERROR;
}

KNET_STATIC char *GetActionTypeStr(enum rte_flow_action_type type)
{
    // 目前只支持这两种action,且action互相独立
    if (type != RTE_FLOW_ACTION_TYPE_QUEUE && type != RTE_FLOW_ACTION_TYPE_RSS) {
        return "NoSupport";
    }
    char *actionStr[] = {[RTE_FLOW_ACTION_TYPE_QUEUE] = "QUEUE-", [RTE_FLOW_ACTION_TYPE_RSS] = "RSS-"};
    return actionStr[type];
}

KNET_STATIC int GetFlowActionStr(struct rte_flow_action *action, size_t actionLen, struct Entry *nextEntry,
                                 char *actionStr, size_t actionStrLen)
{
    // 只有QUEUE和RSS两种action才有后续的queId列表
    (void)strncpy_s(actionStr, actionStrLen, GetActionTypeStr(action[0].type), actionStrLen - 1);
    size_t remaining = actionStrLen;
    size_t offset = strlen(actionStr);
    int ret = snprintf_s(actionStr + offset, remaining, remaining - 1, "%u", nextEntry->map.queueId[0]);
    if (ret < 0 || (size_t)ret >= remaining) {
        goto ERR;
    }
    offset += (size_t)ret;
    remaining -= (size_t)ret;
    for (int i = 1; i < nextEntry->map.queueIdSize; i++) {
        if (remaining <= 1) {
            goto ERR;
        }
        ret = snprintf_s(actionStr + offset, remaining, remaining - 1, ",%u", nextEntry->map.queueId[i]);
        if (ret < 0 || (size_t)ret >= remaining) {
            goto ERR;
        } else {
            offset += (size_t)ret;
            remaining -= (size_t)ret;
        }
    }
    return KNET_OK;
ERR:
    KNET_ERR("K-NET telemetry flow table proto action string truncated");
    return KNET_ERROR;
}

KNET_STATIC int ProcessFlowInfo(struct Entry *nextEntry, struct rte_tel_data *flowDict)
{
    if (rte_tel_data_start_dict(flowDict) != 0) {
        KNET_ERR("K-NET telemetry flow callback failed, flowDict start dict failed");
        return KNET_ERROR;
    }
    char dstIpStr[INET_IP_STR_LEN] = {0};
    char dstPortStr[INET_PORT_STR_LEN] = {0};
    if (GetIpStrAndPort(nextEntry->ip_port, dstIpStr, dstPortStr) != 0) {
        return KNET_ERROR;
    }
    char dstPortMaskStr[MASK_STR_LEN] = {0};
    (void)snprintf_s(dstPortMaskStr, MASK_STR_LEN, MASK_STR_LEN - 1, "%#x", nextEntry->map.dPortMask);

    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_string, flowDict, "dip", dstIpStr);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_string, flowDict, "dipMask", "0xffffffff");
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_string, flowDict, "dport", dstPortStr);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_string, flowDict, "dportMask", dstPortMaskStr);
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_string, flowDict, "sip", "0.0.0.0");
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_string, flowDict, "sipMask", "0x0");
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_string, flowDict, "sport", "0");
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_string, flowDict, "sportMask", "0");

    struct rte_flow_item *pattern = nextEntry->map.pattern;
    struct rte_flow_action *action = nextEntry->map.action;
    if (pattern == NULL || action == NULL) {
        KNET_ERR("K-NET telemetry flow callback failed, pattern or action is null");
        return KNET_ERROR;
    }
    char protoStr[FLOW_PROTO_STR_MAX_LEN] = {0};
    if (GetFlowPatternStr(pattern, MAX_TRANS_PATTERN_NUM, protoStr, sizeof(protoStr)) != KNET_OK) {
        return KNET_ERROR;
    }
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_string, flowDict, "protocol", protoStr);
    char actionStr[FLOW_ACTION_STR_MAX_LEN] = {0};
    if (GetFlowActionStr(action, MAX_ACTION_NUM, nextEntry, actionStr, sizeof(actionStr)) != KNET_OK) {
        return KNET_ERROR;
    }
    CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_string, flowDict, "action", actionStr);
    // 只有多进程会下发arpflow
    if (nextEntry->map.arpFlow != NULL) {
        char arpQueueIdStr[FLOW_ARP_QUEUE_ID_STR_LEN] = {0};
        (void)snprintf_s(arpQueueIdStr, FLOW_ARP_QUEUE_ID_STR_LEN, FLOW_ARP_QUEUE_ID_STR_LEN - 1, "%u",
                         nextEntry->map.queueId[0]);
        CHECK_ADD_VALUE_TO_DICT(rte_tel_data_add_dict_string, flowDict, "arpQueueId", arpQueueIdStr);
    }
    return KNET_OK;
}
KNET_STATIC int ProcessPerFlow(struct Entry *nextEntry, uint32_t id, struct rte_tel_data *data)
{
    struct rte_tel_data *flowDict = rte_tel_data_alloc();
    if (flowDict == NULL) {
        KNET_ERR("K-NET telemetry flow callback failed, flowDict alloc failed");
        return KNET_ERROR;
    }
    if (ProcessFlowInfo(nextEntry, flowDict) != KNET_OK) {
        rte_tel_data_free(flowDict);
        return KNET_ERROR;
    }
    char keyName[MAX_JSON_KEY_NAME_LEN] = "flow";
    (void)snprintf_s(keyName + strlen(keyName), MAX_JSON_KEY_NAME_LEN - strlen(keyName),
                     MAX_JSON_KEY_NAME_LEN - strlen(keyName) - 1, "%u", id);
    int ret = rte_tel_data_add_dict_container(data, keyName, flowDict, 0);
    if (ret != 0) {
        rte_tel_data_free(flowDict);
        KNET_ERR("K-NET telemetry add_dict_container fail to add key %s, ret %d", keyName, ret);
        return KNET_ERROR;
    }
    return KNET_OK;
}

int KNET_ProcessFlowTable(uint32_t startIndex, uint32_t flowCount, struct rte_tel_data *data)
{
    // data已经初始化
    struct rte_hash *fdirHandle = KnetGetFdirHandle();
    if (fdirHandle == NULL) {
        return KNET_ERROR;
    }

    uint32_t iter = 0;
    uint64_t *key = NULL;
    struct Entry *nextEntry = NULL;
    uint32_t cnt = 0;
    uint32_t maxEntryId = KNET_GetMaxEntryId(); // 流表目前支持2K条

    typedef struct {
        uint32_t entryId;
        struct Entry *nextEntry;
    } IdMapEntry;
    IdMapEntry *idMapPtr = (IdMapEntry *)calloc(1, (maxEntryId + 1) * sizeof(IdMapEntry));
    if (idMapPtr == NULL) {
        KNET_ERR("K-NET telemetry flow callback failed, idMapPtr alloc failed");
        return KNET_ERROR;
    }
    while (rte_hash_iterate(fdirHandle, (const void **)&key, (void **)&nextEntry, &iter) >= 0) {
        if (nextEntry->map.entryId <= maxEntryId) {
            idMapPtr[nextEntry->map.entryId].entryId = nextEntry->map.entryId;
            idMapPtr[nextEntry->map.entryId].nextEntry = nextEntry;
        }
    }
    // 处理idMap和cnt
    flowCount = (flowCount == 0)? FLOW_TABLE_MAX_NUM : flowCount;
    for (uint32_t i = 1; i <= maxEntryId && cnt < flowCount; i++) {
        if (idMapPtr[i].nextEntry != NULL && idMapPtr[i].entryId >= startIndex) {
            if (ProcessPerFlow(idMapPtr[i].nextEntry, idMapPtr[i].entryId, data) != KNET_OK) {
                free(idMapPtr);
                return KNET_ERROR;
            }
            cnt++;
        }
    }
    free(idMapPtr);
    return KNET_OK;
}