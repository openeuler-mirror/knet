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

#ifndef __KNET_TCP_API_INIT_H__
#define __KNET_TCP_API_INIT_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 设置协议栈初始化完成
 */
void KNET_SetDpInited(void);

/**
 * @brief 关闭所有劫持fd
 */
void KNET_AllHijackFdsClose(void);

/**
 * @brief 因信号退出时主线程需要等待其他线程可能有的dp流程结束
 */
void KNET_DpExit(void);

extern bool g_tcpInited;

#ifdef __cplusplus
}
#endif
#endif // __KNET_TCP_API_INIT_H__
