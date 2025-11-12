/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 提供poll事件管理接口
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
 *
 * @param fds [IN/OUT] 可读socket集合
 * @param nfds [IN] 备选最大socket描述符+1 <大于0>
 * @param timeout [IN] 时间参数，任务调用poll阻塞等待时间（毫秒）
 * @retval 0 成功
 * @retval -1 失败，可以通过errno获取具体错误码 \n
 * \n
 * 支持返回的errno: \n
 * EINTR: 系统调用被中断，信号量初始化或者处理过程中被中断 \n
 * EFAULT: 指向fds的指针无效 \n
 * EINVAL: 指定的nfds无效 \n
 * ENOMEM: 内存分配失败，创建poll上下文失败 \n

 */
int DP_PosixPoll(struct pollfd *fds, nfds_t nfds, int timeout);

#ifdef __cplusplus
}
#endif

#endif
