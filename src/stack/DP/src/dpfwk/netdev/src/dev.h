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

#ifndef DEV_H
#define DEV_H

#include "dp_netdev_api.h"
#include "netdev.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    DEV_CTL_GET_PRIVATE,
};

typedef struct {
    size_t privateLen;
    int (*init)(Netdev_t* dev, DP_NetdevCfg_t* cfg);
    void (*deinit)(Netdev_t* dev);
    int (*ctrl)(Netdev_t* dev, int cmd, void* val);
    void (*doRcv)(NetdevQue_t* que);
    void (*doXmit)(NetdevQue_t* que, DP_Pbuf_t** pbuf, uint16_t cnt);
} DevOps_t;

typedef int (*DoIfreqFn_t)(Netdev_t* dev, struct DP_Ifreq* ifreq);

typedef struct {
    int         reqeust;
    DoIfreqFn_t doIfreq;
} IfReqOps_t;

extern const DevOps_t* g_devOps[];

void DoRcvPkts(NetdevQue_t* rxQue);

void DevStart(Netdev_t* dev);

void DevStop(Netdev_t* dev);

int InitDevTasks(int slave);
void DeinitDevTasks(int slave);

void XmitCached(NetdevQue_t* que);

extern DevOps_t g_ethDevOps;
extern DevOps_t g_vlanDevOps;
extern DevOps_t g_loOps;

#ifdef __cplusplus
}
#endif
#endif
