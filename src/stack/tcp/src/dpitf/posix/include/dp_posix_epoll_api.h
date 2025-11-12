/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 提供epoll相关对外接口
 */

#ifndef DP_POSIX_EPOLL_API_H
#define DP_POSIX_EPOLL_API_H

#include <sys/epoll.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup epoll 事件管理
 * @ingroup socket
 */

/**
 * @ingroup epoll
 * @brief epoll事件回调钩子
 *
 * @param data
 * @retval NA

 */
typedef void (*DP_EpollNotifyFn_t)(uint8_t *);

/**
 * @ingroup epoll
 * @brief epoll事件回调结构
 */
typedef struct DP_EpollNotify {
    DP_EpollNotifyFn_t fn;
    uint8_t*           data; /* 回调函数私有数据协议栈不关心 */
} DP_EpollNotify_t;

/**
 * @ingroup epoll
 * @brief 创建epoll句柄
 *
 * @par 描述:创建epoll句柄
 * @attention
 * @li 通过DP_EpollCtl管理监听的socket，通过DP_Close释放epoll资源。
 * @li 释放epoll资源之前需要保证其监听的socket已经关闭，或者从epoll中移除。
 *
 * @param size[IN]监控的数量大小（这个值当前没有使用，仅做预留）
 *
 * @retval epoll句柄 成功,返回epoll句柄(该值>=0才表示成功)
 * @retval -1 具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 * EINVAL: 入参size小于等于0 \n
 * ENOMEM: 内存申请失败 \n
 * EMFILE: 文件描述符资源已耗尽

 */
int DP_PosixEpollCreate(int size);

/**
 * @ingroup epoll
 * @brief 创建epoll句柄
 *
 * @par 描述:创建epoll句柄
 * @attention
 * @li 适用于本协议栈与其他协议栈的epoll事件同时监听场景，如不涉及可以直接使用DP_EpollCreate
 * @li 调用者需要管理notify的生命周期，保证notify及其成员内存空间在epollfd使用期间有效，并在epollfd销毁后自行回收notify的空间。
 * @li 为了避免重复调用回调影响性能，需要在回调内实现仅首次调用会去唤醒调用者，后续则不做处理；在epoll_wait调用结束之后再重置
 *
 * @param size[IN]监控的数量大小（这个值当前没有使用，仅做预留）
 * @param callback[IN]epoll回调函数，用于epoll监听的fd上有事件可获取时通知调用者。适用于本协议栈与其他协议栈的epoll事件同时监听场景。
 *
 * @retval epoll句柄 成功,返回epoll句柄(该值>=0才表示成功)
 * @retval -1 具体错误通过errno呈现

 */
int DP_EpollCreateNotify(int size, DP_EpollNotify_t* callback);

/**
 * @ingroup epoll
 * @brief 将fd需监控的事件加入epoll管理
 *
 * @par 描述:将fd需监控的事件加入epoll管理
 * @attention
 * 业务多线程并发操作相同epoll/socket对象时，需要考虑对象一致性编程（比如不同的线程同时进行读/写/删操作epoll或fd）
 *
 * @param epfd[IN]epoll句柄<大于0>
 * @param op[IN]操作类型,加入、删除、修改<EPOLL_CTL_ADD, EPOLL_CTL_DEL, EPOLL_CTL_MOD>
 * @param fd[IN]待加入的fd<大于0>
 * @param event[IN]待加入的fd及对应监控事件<非空指针>
 *
 * @retval 0 成功
 * @retval -1 具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 * EBADF: 参数epfd或者fd无效 \n
 * EINVAL: 入参op不在支持范围内或epfd对应的套接字不是epoll类型 \n
 * EPERM: fd对应的套接字不是socket类型 \n
 * ENOMEM: 内存申请失败 \n
 * EMFILE: 文件描述符资源已耗尽 \n
 * ENOENT: 待操作的fd不在epoll监听列表中 \n
 * EFAULT: event为空 \n
 * EEXIST: op为EPOLL_CTL_ADD，且fd已经在epoll监听列表中

 */
int DP_PosixEpollCtl(int epfd, int op, int fd, struct epoll_event *event);

/**
 * @ingroup epoll
 * @brief 等待事件产生接口
 *
 * @par 描述:等待事件产生接口
 * @attention
 * @li maxevents必须小于events元素的个数，否则可能出现踩内存异常
 * @li 业务多线程并发操作相同epoll/socket对象时，需要考虑对象一致性编程 \n
 * （比如不同的线程同时进行读/写/删操作epoll或fd）
 *
 * @param epfd[IN]epoll句柄<大于0>
 * @param events[OUT]记录产生事件的fd及对应事件信息，该值作为出参，会覆盖数组的原数据内容，如后续需使用原数据，必须先保存到其他地方。
 * @param maxevents[IN]监控的最大事件数,0<maxevents<=DP_EPOLL_MAX_NUM，且比events数组个数要小，用户保证
 * @param timeout[IN]超时值<非阻塞模式:0，获取不到事件也立即返回; \n
 * 阻塞模式:超时等待，[1,0x7FFFFFFF]],单位为毫秒，指定等待时间， \n
 * 如果指定时间内获取不到事件，也立即返回; \n
 * 无超时等待，-1，不限制等待时间，只有获取到事件才返回>
 *
 * @retval 产生事件的fd个数 成功,产生事件的fd个数不大于maxevents参数值
 * @retval -1 具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 * EBADF: 参数epfd无效 \n
 * EINVAL: maxevents小于0或者大于EPOLL监控的最大事件数 \n
 * EPERM: fd对应的套接字不是socket类型 \n
 * EFAULT: event为空

 */
int DP_PosixEpollWait(int epfd, struct epoll_event *events, int maxevents, int timeout);

#ifdef __cplusplus
}
#endif
#endif
