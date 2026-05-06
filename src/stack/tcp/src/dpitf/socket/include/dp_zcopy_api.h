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
 * @file dp_zcopy_api.h
 * @brief 零拷贝相关接口
 */

#ifndef DP_ZCOPY_API_H
#define DP_ZCOPY_API_H

#include <sys/types.h>

#include "dp_socket_types_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @ingroup zcopy
 * @brief 免拷贝socket发送数据接口，支持多缓冲区
 * @attention 只能在配置使能免拷贝功能后调用, 此接口调用要在协议栈启动初始化完成之后才能正常使用。
 * @param sockfd [IN] 读取的socket描述符
 * @param iov [IN/OUT] 免拷贝结构体指针, 免拷贝数据成功发送后用户不允许再使用，发送失败时这次调用的所有数据都不会发送，
 *                     用户通过入参 iov 中的 cb，和 freeCb 字段来控制协议栈不再使用 iov 后的行为，因异常而放失败的场景，
 *                     需要用户手动调用释放回调来进行释放，入参 iov 中的 freeCb 字段不得为空
 * @param iovcnt [IN] iov的数量, 必须保证iovcnt与iov的长度匹配
 * @retval 0 成功
 * @retval -1 失败

 */
ssize_t DP_ZWritev(int sockfd, const struct DP_ZIovec* iov, int iovcnt, ssize_t totalLen);

/**
 * @ingroup zcopy
 * @brief 免拷贝socket接收数据接口，支持多缓冲区
 * @attention 只能在配置使能免拷贝功能后调用，此接口调用要在协议栈启动初始化完成之后才能正常使用。
 * @param sockfd [IN] 读取的socket描述符
 * @param iov [IN/OUT] 免拷贝结构体指针, 成功读取的免拷贝数据后，用户需要通过回调释放控制块，不允许并发读取和释放
 *                     同一个socket的免拷贝数据，不允许篡改控制块内容，否则会产生未定义行为
 * @param iovcnt [IN] iov的数量
 * @retval 0 成功
 * @retval -1 失败

 */
ssize_t DP_ZReadv(int sockfd, struct DP_ZIovec* iov, int iovcnt);

/**
 * @ingroup zcopy
 * @brief 申请免拷贝发送缓冲区
 * @attention 只能在配置使能免拷贝功能后调用，此接口调用要在协议栈启动初始化完成之后才能正常使用。
 * @param size [IN] 申请的缓冲区长度，缓冲区最大长度通过预配置项 DP_CFG_ZBUF_LEN_MAX 配置，该配置必须与底层同步,
 *                  该接口底层实际上是分配了长度为 DP_CFG_ZBUF_LEN_MAX 的定长内存单元
 * @retval 非空 成功
 * @retval 空 失败

 */
void* DP_ZcopyAlloc(size_t size);

/**
 * @ingroup zcopy
 * @brief 释放免拷贝发送缓冲区
 * @attention 只能在配置使能免拷贝功能后调用，此接口调用要在协议栈启动初始化完成之后才能正常使用。
 * @param addr [IN] 待释放的免拷贝发送缓冲区的起始地址
 * @retval void

 */
void DP_ZcopyFree(void* addr);

#ifdef __cplusplus
}
#endif
#endif