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


#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "knet_log.h"
#include "knet_utils.h"
#include "knet_dtoe_config_setter.h"
#include "knet_dtoe_config.h"

#define MAX_CFG_SIZE 8000
#define KNET_CFG_FILE "/etc/knet/knet_comm.conf"

char *g_moduleName[CONF_MAX] = {"dtoe"};

int g_moduleConfMin[CONF_MAX] = {CONF_DTOE_MIN};
int g_moduleConfMax[CONF_MAX] = {CONF_DTOE_MAX};

struct ConfKeyHandle {
    enum KNET_ConfKey key;
    char *name;
    union KNET_CfgValue value;
    FuncSetter setter;
    union KnetCfgValidateParam validateParam;
};

struct ConfKeyHandle g_dtoeConfHandler[CONF_DTOE_MAX - CONF_DTOE_MIN] = {
    {CONF_DTOE_LOG_LEVEL, "log_level", {.strValue = "WARNING"}, LogLevelSetter, {}},
    {CONF_DTOE_CHANNEL_NUM, "channel_num", {1}, IntSetter, {.intValue = {.min = 1, .max = 128}}},
};

struct ConfKeyHandle *g_confHandleMap[CONF_MAX] = {
    g_dtoeConfHandler,
};

KNET_STATIC char *GetKnetCfgContent(const char *fileName)
{
    char *cfgContent = NULL;
    FILE *file = fopen(fileName, "r");
    if (file == NULL) {
        KNET_ERR("Open K-NET conf file failed, errno %d", errno);
        return cfgContent;
    }
    cfgContent = (char *)malloc(MAX_CFG_SIZE);
    if (cfgContent == NULL) {
        KNET_ERR("Malloc conf size %d failed, errno %d", MAX_CFG_SIZE, errno);
        goto END;
    }
    (void)memset_s(cfgContent, MAX_CFG_SIZE, 0, MAX_CFG_SIZE);
    size_t readSize = fread(cfgContent, 1, MAX_CFG_SIZE, file);
    if (readSize >= MAX_CFG_SIZE) {
        free(cfgContent);
        cfgContent = NULL;
        KNET_ERR("Fread failed, read larger than %d, errno %d", MAX_CFG_SIZE, errno);
        goto END;
    }

    if (ferror(file) != 0) {
        // 读取过程发生错误
        free(cfgContent);
        cfgContent = NULL;
        KNET_ERR("Error reading file, errno %d", errno);
        goto END;
    }

END:
    (void)fclose(file);

    return cfgContent;
}

/*
 * @brief 配置配置项的值，模块内接口，且性能敏感，不检查参数有效性。建议使用前确认参数有效性。
 */
void SetCfgValue(enum KNET_ConfKey key, const union KNET_CfgValue *value)
{
    g_confHandleMap[key >> MAX_CONF_NUM_PER_INDEX_BITS][key & CONF_INDEX_LOWER_MASK].value = *value;
}

KNET_STATIC void DelKnetCfgContent(char *cfgCtx)
{
    free(cfgCtx);
}
static int GetCfgValue(enum KNET_ConfModule module, cJSON *json)
{
    // 内部配置项，不需要从外部获取。
    if (g_moduleName[module] == NULL || g_moduleName[module][0] == '\0') {
        return 0;
    }

    cJSON *subJson = cJSON_GetObjectItem(json, g_moduleName[module]);
    if (subJson == NULL) {
        KNET_ERR("cJSON get sub json %s failed", g_moduleName[module]);
        return -1;
    }

    int ret = 0;
    struct ConfKeyHandle *confItem = NULL;
    for (int key = 0; key < g_moduleConfMax[module] - g_moduleConfMin[module]; ++key) {
        confItem = &g_confHandleMap[module][key];
        // 内部配置项，不需要从外部获取。
        if (confItem->setter == NULL) {
            continue;
        }

        cJSON *jsonValue = cJSON_GetObjectItem(subJson, confItem->name);
        ret = confItem->setter(jsonValue, &confItem->value, &confItem->validateParam);
        if (ret != 0) {
            KNET_ERR("cJSON get value %s:%s failed, err:%d", g_moduleName[module], confItem->name, ret);
            break;
        }
    }

    if (ret != 0) {
        return -1;
    }

    return 0;
}

static int LoadCfgFromFile(void)
{
    char *cfgCtx = GetKnetCfgContent(KNET_CFG_FILE);
    if (cfgCtx == NULL) {
        KNET_ERR("Get cfg content failed");
        return -1;
    }

    int ret = 0;
    cJSON *json = cJSON_Parse(cfgCtx);
    if (json == NULL) {
        KNET_ERR("K-NET config cJSON parse failed");
        ret = -1;
        goto END;
    }

    for (int i = 0; i < CONF_MAX; ++i) {
        ret = GetCfgValue(i, json);
        if (ret != 0) {
            KNET_ERR("Get cfg value failed");
            goto END;
        }
    }

END:
    DelKnetCfgContent(cfgCtx);
    if (json != NULL) {
        cJSON_Delete(json);
    }

    return ret;
}

int KNET_InitCfg(void)
{
    // 主进程和从进程都需要先读配置文件
    int ret = LoadCfgFromFile();
    if (ret != 0) {
        KNET_ERR("Load knet cfg failed, ret %d", ret);
        return -1;
    }

    return 0;
}

/*
 * @brief 获取配置项的值，模块内接口，且性能敏感，不检查参数有效性。建议使用前确认参数有效性。
 */
const union KNET_CfgValue *KNET_GetCfg(enum KNET_ConfKey key)
{
    return &(g_confHandleMap[key >> MAX_CONF_NUM_PER_INDEX_BITS][key & CONF_INDEX_LOWER_MASK].value);
}