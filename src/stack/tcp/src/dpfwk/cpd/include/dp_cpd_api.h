/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 控制面对接相关对外接口
 */


#ifndef DP_CPD_API_H
#define DP_CPD_API_H

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup cpd 控制面对接 */

/**
 * @ingroup cpd
 * @brief 控制面对接模块初始化
 *
 * @attention
 * 1.DP自身不处理控制报文，支持由CPD模块将控制报文转发给控制面(内核协议栈)
 * 2.必须在协议栈初始化后，且创建NetDev设备(DP_CreateNetdev)、设置NetDev属性并使能网卡设备(DP_ProcIfreq)后调用该接口。
 *   该接口会为每个已创建的NetDev设备创建转发链路，要求NetDev设备已设置IP、MAC地址和MTU(DP_ProcIfreq)
 *   此后创建的NetDev设备无法进行控制报文转发。
 * 3.必须在调用DP_CpdRunOnce之前调用。
 *
 * @param NA
 * @retval 0 成功
 * @retval -1 异常
 * @see DP_CpdRunOnce
 */
int DP_CpdInit(void);

/**
 * @ingroup cpd
 * @brief 控制面对接模块调度入口
 *
 * @attention
 * 1.必须在调用DP_CpdInit之后调用。
 * 2.该接口从内核同步ARP表项至DP，通过netlink读取内核的更新信息。
 * 3.该接口通过转发链路读取内核发送的控制报文(ARP ICMP),从对应的NetDev设备转发至外部。
 *
 * @param NA
 * @retval 0 成功
 * @retval -1 异常
 * @see DP_CpdInit
 */
int DP_CpdRunOnce(void);

#ifdef __cplusplus
}
#endif

#endif
