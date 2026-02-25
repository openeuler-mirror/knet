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

#ifndef K_NET_TCP_OS_H
#define K_NET_TCP_OS_H

#include "knet_types.h"

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief 判断是否是父进程
 * @return bool 如果是父进程返回true,否则返回false
 */
bool KNET_DpIsForkedParent(void);

/**
 * @brief tcp fork函数
 * @return pid_t 如果是子进程返回0, 父进程返回子进程的pid, 失败返回-1
 */
pid_t KNET_DpFork(void);

/**
 * @brief tcp sigaction函数入口
 * @param [IN] int signum 信号编号
 * @param [IN] sigaction act 信号处理结构体
 * @param [OUT] sigaction oldact 原信号处理结构体
 * @return int 0成功，-1失败
 */
int KNET_DpSigaction(int signum, const struct sigaction *act, struct sigaction *oldact);

/**
 * @brief tcp signal函数
 * @param [IN] int signum 信号编号
 * @param [IN] sighandler_t handler 信号处理函数
 * @return sighandler_t 原信号处理函数
 */
sighandler_t KNET_DpSignal(int signum, sighandler_t handler);

#ifdef __cplusplus
}
#endif
#endif // K_NET_TCP_OS_H