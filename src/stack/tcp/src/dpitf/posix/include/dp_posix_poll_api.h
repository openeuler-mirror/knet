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
/**
 * @file dp_posix_poll_api.h
 * @brief 提供poll事件管理接口
 */

#ifndef DP_POSIX_POLL_API_H
#define DP_POSIX_POLL_API_H

#include <sys/poll.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup poll poll事件管理
 * @ingroup socket
 */

/**
 * @ingroup poll
 * @brief 标准poll接口，获取指定事件的socket集合
 *
 * @attention
 * @li 此接口调用要在协议栈启动初始化完成之后才能正常使用
 *
 * @param fds [IN/OUT] 可读socket集合
 * @param nfds [IN] 备选最大socket描述符+1 <大于0>
 * @param timeout [IN] 时间参数，任务调用poll阻塞等待时间（毫秒）
 * @retval 0 成功
 * @retval -1 失败，可以通过errno获取具体错误码 \n
 * \n
 * 支持返回的errno: \n
 * EFAULT: 1.入参fds为空指针 \n
 * 	       2.入参fds中fd无效，与linux不一致 \n
 *         3.入参fds中fd不是套接字类型fd，与linux不一致 \n
 * EINTR: 被信号中断 \n
 * EINVAL: 入参nfds超过最大fd数量 \n
 * ENOMEM: 内存申请失败

 */
int DP_PosixPoll(struct pollfd *fds, nfds_t nfds, int timeout);

#ifdef __cplusplus
}
#endif

#endif
