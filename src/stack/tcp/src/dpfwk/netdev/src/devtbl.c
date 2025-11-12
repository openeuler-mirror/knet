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

#include <securec.h>

#include "ns.h"
#include "shm.h"
#include "utils_log.h"

#include "devtbl.h"

static void* AllocDevTbl()
{
    DevTbl_t* tbl;

    tbl = SHM_MALLOC(sizeof(DevTbl_t), MOD_NETDEV, DP_MEM_FREE);
    if (tbl == NULL) {
        DP_LOG_ERR("Malloc memory failed for devTbl.");
        return NULL;
    }

    (void)memset_s(tbl, sizeof(*tbl), 0, sizeof(*tbl));

    return tbl;
}

static void FreeDevTbl(void* tbl)
{
    SHM_FREE(tbl, DP_MEM_FREE);
}

int InitDevTbl()
{
    NS_SetNetOps(NS_NET_DEVTBL, AllocDevTbl, FreeDevTbl);

    return 0;
}

Netdev_t* GetDev(DevTbl_t* tbl, int ifindex)
{
    ASSERT(tbl != NULL);
    for (int i = 0; i < DEV_TBL_SIZE; ++i) {
        if (tbl->devs[i] != NULL && tbl->devs[i]->ifindex == ifindex) {
            return tbl->devs[i];
        }
    }
    return NULL;
}


Netdev_t* GetDevInArray(DevTbl_t* tbl, int index)
{
    Netdev_t* dev = NULL;

    ASSERT(tbl != NULL);

    if (index >= 0 && index < DEV_TBL_SIZE) {
        dev = tbl->devs[index];
    }

    return dev;
}

Netdev_t* GetDevByname(DevTbl_t* tbl, const char* name)
{
    Netdev_t* dev = NULL;

    ASSERT(tbl != NULL);

    if (name == NULL) {
        return NULL;
    }

    for (int i = 0; i < DEV_TBL_SIZE; i++) {
        if (tbl->devs[i] != NULL && strcmp(tbl->devs[i]->name, name) == 0) {
            dev = tbl->devs[i];
            break;
        }
    }

    return dev;
}

static int AllocDevTblIndex(DevTbl_t* tbl)
{
    for (int i = 0; i < DEV_TBL_SIZE; i++) {
        if (tbl->devs[i] == NULL) {
            return i;
        }
    }
    return -1;
}

int PutDev(DevTbl_t* tbl, Netdev_t* dev, int ifindex)
{
    ASSERT(tbl != NULL);

    int index = -1;

    index = AllocDevTblIndex(tbl);
    if (index < 0) {
        return -1;
    }

    tbl->devs[index] = dev;
    dev->ifindex     = ifindex;

    return 0;
}

Netdev_t* PopDev(DevTbl_t* tbl, int ifindex)
{
    Netdev_t* dev = NULL;

    ASSERT(tbl != NULL);

    for (int i = 0; i < DEV_TBL_SIZE; ++i) {
        if (tbl->devs[i] != NULL && tbl->devs[i]->ifindex == ifindex) {
            dev          = tbl->devs[i];
            dev->ifindex = -1;
            tbl->devs[i] = NULL;
        }
    }
    return dev;
}

void WalkAllDevs(DevTbl_t* tbl, int (*hook)(Netdev_t* dev, void* p), void* p)
{
    for (int i = 0; i < DEV_TBL_SIZE; i++) {
        if (tbl->devs[i] == NULL) {
            continue;
        }

        if (hook(tbl->devs[i], p) != 0) {
            break;
        }
    }
}
