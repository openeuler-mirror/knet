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

#ifndef UTILS_CFG_H
#define UTILS_CFG_H

#include "dp_cfg_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TCP_DEFAULT_INIT_CWND     (10)
#define TCP_DEFAULT_INIT_CWND_MAX (100)
#define TCP_DEFAULT_MSS_MAX       (536)

#define TBM_NDTBL_MISS_CACHED_SIZE (4)
#define TBM_FAKE_NDITEM_ALIVE_TIME (30000)
#define TBM_FAKE_NDTBL_SIZE        (16)
#define TBM_ARP_MISS_INTERVAL      (5000)

typedef enum {
    CFG_RAWCB_IP_MAX = DP_CFG_MAX,
    CFG_RAWCB_IPV6_MAX,
    CFG_RAWCB_ETH_MAX,
    CFG_FD_OFFSET,
    CFG_NDTBL_MISS_CACHED_SIZE,
    CFG_FAKE_NDITEM_ALIVE_TIME,
    CFG_FAKE_NDTBL_SIZE,
    CFG_ARP_MISS_INTERVAL,

    CFG_INNER_MAX,
} CFG_Key_t;

typedef enum {
    CFG_TCP_NO_CKSUM_VERIFY = DP_CFG_TCP_MAX, // 关闭tcp校验和校验，默认值为开启
    CFG_TCP_NO_CKSUM_CALC, // 关闭tcp校验和填充计算，默认值为开启
    CFG_TCP_NO_TIMESTAMP, // 关闭tcp时间戳选项，默认值为开启
    CFG_TCP_CA_ALG, // TCP默认拥塞算法，默认值为new reno算法
    CFG_TCP_MSS_MAX, // TCP携带最大数据量设置，默认值为536

    CFG_INNER_TCP_MAX,
} CFG_TcpKey_t;

typedef enum {
    CFG_IP_NO_CKSUM_VERIFY = DP_CFG_IP_MAX, // 关闭ip校验和校验，默认值为开启
    CFG_IP_NO_CKSUM_CALC, // 关闭ip校验和计算，默认值为开启

    CFG_INNER_IP_MAX,
} CFG_IpKey_t;

typedef enum {
    CFG_ETH_PADDING = DP_CFG_ETH_MAX, // ETH报文填充(报文总长度小于64字节时，填充0到64字节)，默认关闭

    CFG_INNER_ETH_MAX,
} CFG_EthKey_t;

typedef struct {
    int currentVal;
    int minVal;
    int maxVal;
} PreCfgVal_t;

extern PreCfgVal_t g_cfgVal[CFG_INNER_MAX];
extern PreCfgVal_t g_cfgTcpVal[CFG_INNER_TCP_MAX];
extern PreCfgVal_t g_cfgIpVal[CFG_INNER_IP_MAX];
extern PreCfgVal_t g_cfgEthVal[CFG_INNER_ETH_MAX];

#define CFG_GET_VAL(key) g_cfgVal[(key)].currentVal

#define CFG_GET_IP_VAL(key) g_cfgIpVal[(key)].currentVal

#define CFG_GET_TCP_VAL(key) g_cfgTcpVal[(key)].currentVal

#define CFG_GET_ETH_VAL(key) g_cfgEthVal[(key)].currentVal

#define CFG_INITIAL 0
#define CFG_INITED 1
void UTILS_SetCfgInit(int init);
int UTILS_IsCfgInited(void);

int DP_Cfg_Inter(DP_CfgKv_t* kv, int cnt);

#ifdef __cplusplus
}
#endif
#endif
