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
#ifndef DEVTBL_H
#define DEVTBL_H

#include "utils_atomic.h"

#include "netdev.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Netdev_t* devs[DEV_TBL_SIZE];
    atomic32_t ref;
} DevTbl_t;

int InitDevTbl(void);

Netdev_t* GetDev(DevTbl_t* tbl, int ifindex);

Netdev_t* GetDevInArray(DevTbl_t* tbl, int index);

Netdev_t* GetDevByname(DevTbl_t* tbl, const char* name);

int PutDev(DevTbl_t* tbl, Netdev_t* dev, int ifindex);

Netdev_t* PopDev(DevTbl_t* tbl, int ifindex);

void WalkAllDevs(DevTbl_t* tbl, int (*hook)(Netdev_t* dev, void* p), void* p);

#ifdef __cplusplus
}
#endif
#endif
