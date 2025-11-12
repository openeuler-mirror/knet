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

#include <unistd.h>
#include <sys/types.h>

#include "knet_hash_table.h"

#include "rte_hash.h"
#include "rte_malloc.h"
#include "rte_errno.h"
#include "rte_jhash.h"

#include "knet_atomic.h"
#include "knet_atomic.h"
#include "knet_lock.h"
#include "securec.h"
#include "knet_log.h"
#include "knet_rpc.h"
#include "knet_hash_rpc.h"

#define HASH_TABLE_NAME_LEN 2
#define HASH_TABLE_QUEUE_NAME_LEN_MAX 64

/**
 * @brief 默认hash table大小
 */
#define DEFAULT_HASH_TBL_NUM 128

typedef struct {
    struct rte_hash *handle;
    KNET_ATOMIC64_T usedNum; /* 已经使用的表项个数 */
    KNET_HASH_FUNC hashFunc;   /**< hash表hash函数 */
    uint32_t tableId;           /**< 表单ID         */
    uint32_t maxEntryNum;         /**< 最大表项数     */
    uint32_t keySize;             /**< 散列键值长度   */
    uint32_t entrySize;           /**< 表项数据长度   */
    KNET_RWLock rwLock;                /* 写互斥 */
    uint8_t initFlag;  /* 是否已经初始化标志。1:Yes; 0:No */
    char padding[7];   // 填充字节，确保结构体8 字节对齐
} KNET_HashTblCb;

typedef struct {
    uint32_t tableIdNum;                          /* ID规格，配置输入，如果没有内部定义 */
    char padding[4];                              /* 填充字节，确保结构体8 字节对齐 */
    KNET_HashTblCb infoCbs[DEFAULT_HASH_TBL_NUM]; /* 表信息结构体起始地址，代替以前的全局遍历方式 ，
                                                   * 内存大小为 sizeOf(KNET_HashTblCb)*tableIdNum
                                                   */
} KNET_HashTblMng;

static KNET_SpinLock g_hashTableLock = {
    .value = KNET_SPIN_UNLOCKED_VALUE,
};

KNET_HashTblMng g_tblMng = {0};

uint32_t DefaultHashFunc(uint8_t *key, uint32_t keyLen)
{
    return rte_jhash(key, keyLen, 0);
}

KNET_HASH_FUNC g_hashFunc = DefaultHashFunc;

int32_t GetHashTblId(uint32_t *tableId)
{
    /* 局部变量定义 */
    uint32_t index;
    KNET_HashTblCb *tblInfo = NULL;

    /* 获取TBM全局控制数据指针 */
    tblInfo = g_tblMng.infoCbs;

    KNET_SpinlockLock(&g_hashTableLock);

    /* table id从0开始分 */
    for (index = 0; index < g_tblMng.tableIdNum; index++, tblInfo++) {
        if (tblInfo->initFlag != 1) { /* 获取第一个无效的table id */
            *tableId = (uint32_t) (index);

            /* 置该表ID被使用状态 */
            tblInfo->initFlag = 1;
            KNET_RwlockInit(&tblInfo->rwLock);
            KNET_SpinlockUnlock(&g_hashTableLock);

            return 0;
        }
    }

    KNET_SpinlockUnlock(&g_hashTableLock);

    KNET_ERR("There is no free table id");
    return -1;
}

void ReleaseHashTblIdLocked(uint32_t tableId)
{
    KNET_HashTblCb *tblInfo = NULL;

    /* 获取TBM全局控制数据指针 */
    tblInfo = g_tblMng.infoCbs + tableId;

    (void) memset_s((void *) tblInfo, sizeof(KNET_HashTblCb), 0, sizeof(KNET_HashTblCb));
    tblInfo->initFlag = 0;
}

void ReleaseHashTblId(uint32_t tableId)
{
    KNET_SpinlockLock(&g_hashTableLock);
    ReleaseHashTblIdLocked(tableId);
    KNET_SpinlockUnlock(&g_hashTableLock);
}

void HashTableReleaseAllEntry(KNET_HashTblCb *table)
{
    uint8_t *key = NULL;
    uint8_t *entry = NULL;
    uint32_t iter = 0;
    int32_t ret;
    int32_t delPos;
    hash_sig_t hash_value;

    KNET_RwlockWriteLock(&table->rwLock);

    while (KNET_HalAtomicRead64(&table->usedNum) > 0) {
        ret = rte_hash_iterate(table->handle, (const void **) &key, (void **) &entry, &iter);
        if (ret < 0) {
            if (ret == -ENOENT) {
                break;
            }
            KNET_ERR("Get entry failed. iterate ret(%d), errno:%d", ret, errno);
            KNET_RwlockWriteUnlock(&table->rwLock);
            return;
        }

        hash_value = g_hashFunc((uint8_t *)key, table->keySize);  // 重新计算hash值
        delPos = rte_hash_del_key_with_hash(table->handle, key, hash_value);
        if (delPos < 0) {
            KNET_ERR("Delete hash table entry failed. (TableId=%u), errno:%d", table->tableId, errno);
            KNET_RwlockWriteUnlock(&table->rwLock);
            return;
        }

        free(entry);
        KNET_HalAtomicSub64(&table->usedNum, 1);
    }
    KNET_RwlockWriteUnlock(&table->rwLock);
}

int KNET_HashTblInit(void)
{
    KNET_SpinlockLock(&g_hashTableLock);

    if (g_tblMng.tableIdNum != 0) {
        KNET_SpinlockUnlock(&g_hashTableLock);
        KNET_DEBUG("K-NET hash table already inited");
        return 0;
    }

    (void)memset_s(&g_tblMng, sizeof(g_tblMng), 0, sizeof(g_tblMng));
    /* 长度写死 */
    g_tblMng.tableIdNum = DEFAULT_HASH_TBL_NUM;

    KNET_SpinlockUnlock(&g_hashTableLock);
    return 0;
}

static int ParameterCheck(KNET_HashTblCfg *cfg, uint32_t *tableId)
{
    if (g_tblMng.tableIdNum == 0) {
        KNET_ERR("Hash table not init");
        return -1;
    }

    if ((cfg == NULL) || (tableId == NULL)) {
        KNET_ERR("Create hash table failed, because hash table parameter or table id is null"); // 入参校验
        return -1;
    }

    return 0;
}

static int GetHashTblName(char *queueName, char *hashTblName)
{
    int32_t ret;
    int8_t mode = KNET_GetCfg(CONF_COMMON_MODE).intValue;
    if (mode == KNET_RUN_MODE_SINGLE) {
        ret = sprintf_s(queueName, HASH_TABLE_QUEUE_NAME_LEN_MAX, "hash_table_%s", hashTblName);
        if (ret < 0) {
            KNET_ERR("Sinlge mode get hash table name err");
            return -1;
        }
    } else {
        int32_t queueId = KNET_GetCfg(CONF_INNER_QID).intValue;
        ret = sprintf_s(queueName, HASH_TABLE_QUEUE_NAME_LEN_MAX, "%d_%s", queueId, hashTblName);
        if (ret < 0) {
            KNET_ERR("Multiple mode get hash table name err");
            return -1;
        }
    }
    return 0;
}

struct rte_hash *CreateHashTblSingle(KNET_HashTblCfg *cfg, char *queueName, int queueNameSize)
{
    struct rte_hash_parameters params = {0};
    struct rte_hash *handle;

    params.name = queueName;
    params.entries = cfg->entryNum;
    params.key_len = cfg->keySize;
    params.hash_func = (rte_hash_function) cfg->hashFunc;
    params.hash_func_init_val = 0;
    params.socket_id = SOCKET_ID_ANY;

    handle = rte_hash_create(&params);
    return handle;
}

int KNET_CreateHashTbl(KNET_HashTblCfg *cfg, uint32_t *tableId)
{
    struct rte_hash *handle;
    uint32_t localTableId = 0;
    int32_t ret;
    KNET_HashTblCb *tblInfo = NULL;
    int8_t mode = KNET_GetCfg(CONF_COMMON_MODE).intValue;

    ret = ParameterCheck(cfg, tableId);
    if (ret != 0) {
        KNET_ERR("Parameter check failed");
        return -1;
    }

    ret = GetHashTblId(&localTableId);
    if (ret != 0) {
        KNET_ERR("Create hash table failed: %d", ret);
        return -1;
    }

    tblInfo = g_tblMng.infoCbs + localTableId;
    tblInfo->hashFunc = cfg->hashFunc;
    tblInfo->tableId = localTableId;
    tblInfo->maxEntryNum = cfg->entryNum;
    tblInfo->keySize = cfg->keySize;
    tblInfo->entrySize = cfg->entrySize;

    static char hashTblName[HASH_TABLE_NAME_LEN] = "0";
    char queueName[HASH_TABLE_QUEUE_NAME_LEN_MAX] = "0";
    ret = GetHashTblName(queueName, hashTblName);
    if (ret != 0) {
        KNET_ERR("Get hash table name failed: %d", ret);
        return -1;
    }

    if (mode == KNET_RUN_MODE_SINGLE) {
        handle = CreateHashTblSingle(cfg, queueName, HASH_TABLE_QUEUE_NAME_LEN_MAX);
    } else {
        handle = KnetCreateHashTblMultiple(cfg->entryNum, cfg->keySize, queueName, HASH_TABLE_QUEUE_NAME_LEN_MAX);
    }

    if (handle == NULL) {
        KNET_ERR("Create hash table failed, %s, name %s, entries %u, key_len %u",
            rte_strerror(rte_errno), queueName, cfg->entryNum, cfg->keySize);
        ReleaseHashTblId(localTableId);
        return -1;
    }

    if (tblInfo->hashFunc != NULL) {
        g_hashFunc = tblInfo->hashFunc;
    }

    *tableId = localTableId;
    tblInfo->handle = handle;

    KNET_INFO("Hash tableId %u tableName %s create, entries %u, key_len %u",
        *tableId, queueName, cfg->entryNum, cfg->keySize);

    ++hashTblName[0]; // rte_hash_create要求name不能重复，所以每次成功名称+1
    return 0;
}

int DestroyHashTblLocked(uint32_t tableId)
{
    int mode = KNET_GetCfg(CONF_COMMON_MODE).intValue;
    KNET_HashTblCb *tblInfo = NULL;

    if (g_tblMng.tableIdNum == 0) {
        KNET_ERR("Hash table not init");
        return -1;
    }
    if (tableId >= g_tblMng.tableIdNum) {
        KNET_ERR("Table id exceeds the maximum value. (TableId=%u, MaxTableId=%u)", tableId,
                 g_tblMng.tableIdNum - 1);
        return -1;
    }
    tblInfo = g_tblMng.infoCbs + tableId;

    if (tblInfo->initFlag == 0) {
        return 0;
    }

    if (mode == KNET_RUN_MODE_MULTIPLE) {
        return KnetDestroyHashTblMultiple();
    }

    /* 释放资源 */
    HashTableReleaseAllEntry(tblInfo);
    rte_hash_free(tblInfo->handle);
    ReleaseHashTblIdLocked(tableId);
    return 0;
}

int KNET_DestroyHashTbl(uint32_t tableId)
{
    int ret = 0;
    KNET_SpinlockLock(&g_hashTableLock);
    ret = DestroyHashTblLocked(tableId);
    KNET_SpinlockUnlock(&g_hashTableLock);
    return ret;
}

int KNET_HashTblAddEntry(uint32_t tableId, const uint8_t *key, const uint8_t *data)
{
    int32_t ret;
    uint8_t *entry = NULL;
    KNET_HashTblCb *tblInfo = NULL;
    hash_sig_t hash_value;

    if (g_tblMng.tableIdNum == 0) {
        KNET_ERR("Hash table not init");
        return -1;
    }

    tblInfo = g_tblMng.infoCbs + tableId;
    if (tableId >= g_tblMng.tableIdNum || tblInfo->initFlag == 0 || key == NULL || data == NULL) {
        KNET_ERR("Add entry invalid params. (TableId=%u, MaxTableId=%u)",
                 tableId, g_tblMng.tableIdNum - 1);
        return -1;
    }

    KNET_RwlockWriteLock(&tblInfo->rwLock);

    hash_value = g_hashFunc((uint8_t *)key, tblInfo->keySize);  // 重新计算hash值
    /* 算法附带修改功能，先查找 */
    ret = rte_hash_lookup_with_hash(tblInfo->handle, key, hash_value);
    if (ret > 0) {
        KNET_ERR("Add entry key already exist");
        KNET_RwlockWriteUnlock(&tblInfo->rwLock);
        return -1;
    }

    entry = (uint8_t *)malloc(tblInfo->entrySize);
    if (entry == NULL) {
        KNET_ERR("Add entry alloc entry failed");
        KNET_RwlockWriteUnlock(&tblInfo->rwLock);
        return -1;
    }

    (void)memcpy_s(entry, tblInfo->entrySize, data, tblInfo->entrySize);

    ret = rte_hash_add_key_with_hash_data(tblInfo->handle, key, hash_value, (uint8_t *)entry);
    if (ret != 0) {
        KNET_ERR("Add entry add entry failed");
        free(entry);
        KNET_RwlockWriteUnlock(&tblInfo->rwLock);
        return -1;
    }
    KNET_HalAtomicAdd64(&tblInfo->usedNum, 1);
    KNET_RwlockWriteUnlock(&tblInfo->rwLock);
    return 0;
}

int KNET_HashTblDelEntry(uint32_t tableId, const uint8_t *key)
{
    KNET_HashTblCb *tblInfo = NULL;
    int32_t ret;
    uint8_t *entry = NULL;
    int32_t delPos;
    hash_sig_t hash_value;

    if (g_tblMng.tableIdNum == 0) {
        KNET_ERR("Hash table not init");
        return -1;
    }

    tblInfo = g_tblMng.infoCbs + tableId;
    if (tableId >= g_tblMng.tableIdNum || tblInfo->initFlag == 0) {
        KNET_ERR("Del entry invalid params. (TableId=%u, MaxTableId=%u)",
                 tableId, g_tblMng.tableIdNum - 1);
        return -1;
    }

    KNET_RwlockWriteLock(&tblInfo->rwLock);

    hash_value = g_hashFunc((uint8_t *)key, tblInfo->keySize);  // 重新计算hash值
    ret = rte_hash_lookup_with_hash_data(tblInfo->handle, key, hash_value, (void **) &entry);
    if (ret < 0) {
        KNET_ERR("Del entry key not exist");
        KNET_RwlockWriteUnlock(&tblInfo->rwLock);
        return -1;
    }

    delPos = rte_hash_del_key_with_hash(tblInfo->handle, key, hash_value);
    if (delPos < 0) {
        KNET_ERR("Delete hash table entry failed. (TableId=%u), errno:%d", tableId, errno);
        KNET_RwlockWriteUnlock(&tblInfo->rwLock);
        return -1;
    }

    free(entry);

    KNET_HalAtomicSub64(&tblInfo->usedNum, 1);
    KNET_RwlockWriteUnlock(&tblInfo->rwLock);
    return 0;
}

int KNET_HashTblModifyEntry(uint32_t tableId, const uint8_t *key, const uint8_t *data)
{
    int32_t ret;
    uint8_t *entry = NULL;
    KNET_HashTblCb *tblInfo = NULL;
    hash_sig_t hash_value;

    if (g_tblMng.tableIdNum == 0) {
        KNET_ERR("Hash table not init");
        return -1;
    }
    tblInfo = g_tblMng.infoCbs + tableId;
    if (tableId >= g_tblMng.tableIdNum || tblInfo->initFlag == 0 || key == NULL || data == NULL) {
        KNET_ERR("Modify entry invalid params. (TableId=%u, MaxTableId=%u)",
                 tableId, g_tblMng.tableIdNum - 1);
        return -1;
    }
    KNET_RwlockWriteLock(&tblInfo->rwLock);

    hash_value = g_hashFunc((uint8_t *)key, tblInfo->keySize);  // 重新计算hash值
    ret = rte_hash_lookup_with_hash_data(tblInfo->handle, key, hash_value, (void **) &entry);
    if (ret < 0) {
        KNET_ERR("Modify entry key not exist");
        KNET_RwlockWriteUnlock(&tblInfo->rwLock);
        return -1;
    }

    (void) memcpy_s(entry, tblInfo->entrySize, data, tblInfo->entrySize);

    KNET_RwlockWriteUnlock(&tblInfo->rwLock);
    return 0;
}

int KNET_HashTblLookupEntry(uint32_t tableId, const uint8_t *key, uint8_t *data)
{
    int32_t ret;
    uint8_t *entry = NULL;
    KNET_HashTblCb *tblInfo = NULL;
    hash_sig_t hash_value;

    if (g_tblMng.tableIdNum == 0) {
        KNET_ERR("Hash table not init");
        return -1;
    }
    tblInfo = g_tblMng.infoCbs + tableId;
    if (tableId >= g_tblMng.tableIdNum || tblInfo->initFlag == 0 || key == NULL || data == NULL) {
        KNET_ERR("Look up entry invalid params. (TableId=%u, MaxTableId=%u)",
                 tableId, g_tblMng.tableIdNum - 1);
        return -1;
    }

    hash_value = g_hashFunc((uint8_t *)key, tblInfo->keySize);  // 重新计算hash值
    ret = rte_hash_lookup_with_hash_data(tblInfo->handle, key, hash_value, (void **) &entry);
    if (ret < 0) {
        /* 上层存在查找确定是否已有表项的情况，采用INFO级别即可 */
        KNET_INFO("Look up entry key not exist");
        return -1;
    }

    (void) memcpy_s(data, tblInfo->entrySize, entry, tblInfo->entrySize);

    return 0;
}

int KNET_GetHashTblInfo(uint32_t tableId, KNET_HashTblInfo *info)
{
    KNET_HashTblCb *tblInfo = NULL;

    if (g_tblMng.tableIdNum == 0) {
        KNET_ERR("Hash table not init");
        return -1;
    }
    tblInfo = g_tblMng.infoCbs + tableId;
    if (tableId >= g_tblMng.tableIdNum || tblInfo->initFlag == 0 || info == NULL) {
        KNET_ERR("Get hashTbl info invalid params. (TableId=%u, MaxTableId=%u)",
                 tableId, g_tblMng.tableIdNum - 1);
        return -1;
    }

    info->tableId = tableId;
    info->pfHashFunc = tblInfo->hashFunc;
    info->keySize = tblInfo->keySize;
    info->currEntryNum = (uint32_t)KNET_HalAtomicRead64(&tblInfo->usedNum);
    info->maxEntryNum = tblInfo->maxEntryNum;
    info->entrySize = tblInfo->entrySize;

    return 0;
}

int KNET_GetHashTblFirstEntry(uint32_t tableId, uint8_t *key, uint8_t *data)
{
    uint8_t *entry = NULL;
    uint32_t iter = 0;
    int32_t ret;
    KNET_HashTblCb *tblInfo = NULL;

    if (g_tblMng.tableIdNum == 0) {
        KNET_ERR("Hash table not init");
        return -1;
    }
    tblInfo = g_tblMng.infoCbs + tableId;
    if (tableId >= g_tblMng.tableIdNum || tblInfo->initFlag == 0 || key == NULL || data == NULL) {
        KNET_ERR("GetHashTblFirstEntry invalid params. (TableId=%u, MaxTableId=%u)",
                 tableId, g_tblMng.tableIdNum - 1);
        return -1;
    }
    KNET_RwlockReadLock(&tblInfo->rwLock);

    ret = rte_hash_iterate(tblInfo->handle, (const void **)&key, (void **)&entry, &iter);
    if (ret < 0) {
        KNET_ERR("Get first entry failed. iterate ret(%d)", ret);
        KNET_RwlockReadUnlock(&tblInfo->rwLock);
        return -1;
    }
    (void) memcpy_s(data, tblInfo->entrySize, entry, tblInfo->entrySize);

    KNET_RwlockReadUnlock(&tblInfo->rwLock);
    return ret;
}

int KNET_GetHashTblNextEntry(uint32_t tableId, const uint8_t *curKey, uint8_t *key, uint8_t *data)
{
    uint8_t *entry = NULL;
    uint32_t iter;
    int32_t ret;
    int32_t pos;
    KNET_HashTblCb *tblInfo = NULL;
    hash_sig_t hash_value;

    if (g_tblMng.tableIdNum == 0) {
        KNET_ERR("Hash table not init");
        return -1;
    }
    tblInfo = g_tblMng.infoCbs + tableId;
    if (tableId >= g_tblMng.tableIdNum || tblInfo->initFlag == 0 || key == NULL || data == NULL || curKey == NULL) {
        KNET_ERR("Get next entry invalid params. (TableId=%u, MaxTableId=%u)",
                 tableId, g_tblMng.tableIdNum - 1);
        return -1;
    }
    KNET_RwlockReadLock(&tblInfo->rwLock);

    hash_value = g_hashFunc((uint8_t *)curKey, tblInfo->keySize);  // 重新计算hash值
    pos = rte_hash_lookup_with_hash(tblInfo->handle, curKey, hash_value);
    if (pos < 0) {
        KNET_ERR("Get hashTbl next entry key not exist");
        KNET_RwlockReadUnlock(&tblInfo->rwLock);
        return -1;
    }

    iter = (uint32_t)pos;
    ret = rte_hash_iterate(tblInfo->handle, (const void **)&key, (void **)&entry, &iter);
    if (ret < 0) {
        KNET_ERR("Get next entry failed. iterate ret(%d)", ret);
        KNET_RwlockReadUnlock(&tblInfo->rwLock);
        return -1;
    }
    (void) memcpy_s(data, tblInfo->entrySize, entry, tblInfo->entrySize);

    KNET_RwlockReadUnlock(&tblInfo->rwLock);
    return ret;
}

void KnetReleaseAllHashTable(void)
{
    uint32_t index;
    KNET_HashTblCb *tblInfo = NULL;

    /* 获取TBM全局控制数据指针 */
    tblInfo = g_tblMng.infoCbs;

    for (index = 0; index < g_tblMng.tableIdNum; index++, tblInfo++) {
        DestroyHashTblLocked(index);
    }
}

void KNET_HashTblDeinit(void)
{
    KNET_SpinlockLock(&g_hashTableLock);

    if (g_tblMng.tableIdNum == 0) {
        KNET_ERR("Tbm deinitialize but not inited");
        KNET_SpinlockUnlock(&g_hashTableLock);
        return;
    }
    KnetReleaseAllHashTable();
    g_tblMng.tableIdNum = 0;

    KNET_SpinlockUnlock(&g_hashTableLock);
}