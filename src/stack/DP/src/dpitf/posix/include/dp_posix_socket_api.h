/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 定义了基于POSIX的socket对外接口
 */

#ifndef DP_POSIX_SOCKET_API_H
#define DP_POSIX_SOCKET_API_H

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup socket 标准socket接口
 * @ingroup socket
 */

/**
 * @ingroup socket
 * @brief 创建一个新的socket。
 *
 * @par 描述: 创建一个新的socket。
 * @attention
 * @li 此接口调用要在协议栈启动初始化完成之后才能正常使用。
 * @li 默认创建非阻塞的socket
 *
 * @param domain [IN] 协议族 <AF_INET>
 * @param type [IN] 插口类型 <SOCK_STREAM或SOCK_DGRAM>
 * @param protocol [IN] 协议类型 <IPPROTO_TCP或IPPROTO_UDP>
 *
 * @retval socketfd 成功, 新建的socket描述符, 即socket_id
 * @retval -1 具体错误通过errno呈现 \n
 * 支持返回的errno: \n
 * ENOMEM ：可用空间不足，无法创建新的socket \n
 * EMFILE : 没有可用文件描述符 \n
 * EAFNOSUPPORT : 不支持的协议类型 \n
 * @par 依赖:
 *     <ul><li>dp_posix_socket_api.h：该接口声明所在的头文件。</li></ul>

 */
int DP_PosixSocket(int domain, int type, int protocol);

/**
 * @ingroup socket
 * @brief socket连接
 *
 * @par 描述: 调用该接口后，本端向对端发送连接请求。
 * @attention
 * @li addr 参数中地址和端口赋值，需要使用网络序
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param addr [IN] 对于Socket4，addr按照struct sockaddr_in的定义来赋值，调用本接口时再转换 \n
 *                  成struct sockaddr *类型<非空指针>
 * @param addrlen [IN] 对于Socket4，i32AddrLen是struct sockaddr_in的长度 <大于0>
 *
 * @retval 0 成功
 * @retval -1 具体错误通过errno呈现 \n
 * 支持返回的errno: \n
 * EINVAL : addrlen长度异常 \n
 * ENOTSOCK : 传入的fd找不到对应的socket \n
 * EBADF : 传入的文件描述符异常 \n
 * EFAULT : 传入地址指针为空. (注:此部分与linux存在差异) \n
 * ECONNREFUSED : 对端拒绝了连接. \n
 * EINPROGRESS : 开启了非阻塞模式，建链尚未完成 \n
 * EINTR : 信号量被中断 \n
 * ETIMEDOUT : 建链连接超过等待时间 \n
 * @par 依赖:
 *     <ul><li>dp_posix_socket_api.h：该接口声明所在的头文件。</li></ul>

 */
int DP_PosixConnect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

/**
 * @ingroup socket
 * @brief 标准socket发送数据接口
 *
 * @par 描述: 标准socket发送数据接口。
 * @attention
 * @li 入参buf，由产品保证内存的有效性和独占性，且申请的数据空间大于等于入参len;
 * @li 发送成功后，buf由产品自行维护(释放或缓存).
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param buf [IN] 产品发送报文的内存地址<非空>
 * @param len [IN] 需要发送的报文长度<大于0>
 * @param flags [IN] 当前只支持 MSG_DONTWAIT标志，其他类型不支持
 *
 * @retval 大于0 实际能发送的数据长度
 * @retval -1 具体错误通过errno呈现 \n
 * 支持返回的errno: \n
 * EBADF : 文件描述符错误 \n
 * ENOTSOCK : 文件描述符找不到协议的socket \n
 * EFAULT : buf指针为空. (注:此部分与linux存在差异) \n
 * ECONNREFUSED : 与对端的连接断开. (注:此部分与linux存在差异) \n
 * EMSGSIZE : 要发送的数据长度过大 \n
 * EINVAL : 文件描述符异常 \n
 * EAGAIN : 非阻塞模式下发送 报文被阻塞 \n
 * EINTR : 发送报文过程中信号量被中断 \n
 * ECONNRESET : 对端强制关闭了连接 \n
 * ENOTCONN : 尚未建立连接 \n
 * EPIPE : 写关闭或者对端断开了连接 \n
 * ENOMEM : socket缓冲区已满，无法继续添加数据. (注：此部分与linux存在差异) \n
 * EOPNOTSUPP : 不支持对应的flag类型 \n
 * EDESTADDRREQ : 未设置目的地址 \n
 * ENETUNREACH : 找不到对应路由 \n
 * @par 依赖:
 *     <ul><li>dp_posix_socket_api.h：该接口声明所在的头文件。</li></ul>

 */
ssize_t DP_PosixSend(int sockfd, const void* buf, size_t len, int flags);

/**
 * @ingroup socket
 * @brief 指定目的地址，发送报文
 *
 * @par 描述: socket sendto发送API，执行报文的发送
 * @attention
 * @li 目的端口和地址不能为0
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param buf [IN] 产品发送报文的内存地址<非空>
 * @param len [IN] 需要发送的报文长度<大于0>
 * @param flags [IN] 当前只支持 MSG_DONTWAIT 标志，其他类型不支持
 * @param destAddr [IN] 数据发往的目的地址。对于Socket4，pstSockAddr按照struct sockaddr_in \n
 *                      的定义来赋值，调用本接口时再转换成struct sockaddr *类型；<非空指针>
 * @param addrlen [IN] 目的地址长度。对于Socket4，i32AddrLen是struct sockaddr的长度； <非0>
 *
 * @retval 大于0 成功,产品实际能发送的数据长度
 * @retval -1 失败，具体错误通过errno呈现 \n
 * 支持返回的errno \n
 * EBADF : 文件描述符错误 \n
 * ENOTSOCK : 文件描述符找不到协议的socket \n
 * EFAULT : buf指针为空. (注:此部分与linux存在差异) \n
 * ECONNREFUSED : 与对端的连接断开. (注:此部分与linux存在差异) \n
 * EMSGSIZE : 要发送的数据长度过大 \n
 * EINVAL : 文件描述符异常 \n
 * EAGAIN : 非阻塞模式下发送报文被阻塞 \n
 * EINTR : 发送报文过程中信号量被中断 \n
 * ECONNRESET : 对端强制关闭了连接 \n
 * ENOTCONN : 尚未建立连接 \n
 * EPIPE : 写关闭或者对端断开了连接 \n
 * ENOMEM : socket缓冲区已满，无法继续添加数据. (注：此部分与linux存在差异) \n
 * EOPNOTSUPP : 不支持对应的flag类型 \n
 * ENETUNREACH : 找不到对应路由 \n
 * @par 依赖:
 *     <ul><li>dp_posix_socket_api.h：该接口声明所在的头文件。</li></ul>

 */
ssize_t DP_PosixSendto(int sockfd, const void *buf, size_t len, int flags,
                       const struct sockaddr *destAddr, socklen_t addrlen);

/**
 * @ingroup socket
 * @brief socket 通过消息信息执行数据发送
 *
 * @par 描述: socket 通过消息信息执行数据发送
 * @attention
 * @li 当前不支持flags标志
 * @li 目的端口和地址不能为0
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param msg [IN] 消息地址<非空指针> \n
 *              会忽略struct msghdr中的msg_flags参数 \n
 *              msg_iovlen不为0或者超过1024； \n
 *              待发送数据总长度不为0或者超过7FFFFFFF \n
 *              UDP协议下msg_controllen不超过128； \n
 *              TCP下忽略msg_control，msg_controllen参数
 *
 * @param flags [IN] 当前不支持该参数
 *
 * @retval 大于0 成功,产品实际能发送的数据长度
 * @retval -1 失败，具体错误通过errno呈现 \n
 * 支持返回的errno \n
 * EBADF : 文件描述符错误 \n
 * ENOTSOCK : 文件描述符找不到协议的socket \n
 * EFAULT : msg指针为空. (注:此部分与linux存在差异) \n
 * ECONNREFUSED : 与对端的连接断开. (注:此部分与linux存在差异) \n
 * EMSGSIZE : 入参msg->msg_iovLen小于0 或 超过1024 \n
 * EINVAL : 文件描述符异常 \n
 * EAGAIN : 非阻塞模式下发送报文被阻塞 \n
 * EINTR : 发送报文过程中信号量被中断. \n
 * ECONNRESET : 对端强制关闭了连接. \n
 * ENOTCONN : 尚未建立连接 \n
 * EPIPE : 写关闭或者对端断开了连接 \n
 * ENOMEM : socket缓冲区已满，无法继续添加数据. (注：此部分与linux存在差异) \n
 * EOPNOTSUPP : 不支持对应的flag类型 \n
 * EDESTADDRREQ : 未设置对端地址 \n
 * ENETUNREACH : 找不到对应路由 \n
 * @par 依赖:
 *     <ul><li>dp_posix_socket_api.h：该接口声明所在的头文件。</li></ul>

 */
ssize_t DP_PosixSendmsg(int sockfd, const struct msghdr *msg, int flags);

/**
 * @ingroup socket
 * @brief 标准socket发送数据接口
 *
 * @par 描述: 标准socket发送数据接口
 * @attention
 * @li 入参*buf，由产品保证内存的有效性和独占性，且申请的数据空间大于等于入参count;
 * @li 发送成功后，buf由产品自行维护(释放或缓存)。
 *
 * @param fd [IN] Socket描述符 <大于0>
 * @param buf [IN] 产品发送报文的内存地址<非空>
 * @param count [IN] 需要发送的字节数<大于0>
 *
 * @retval 大于0 发送的数据字节数
 * @retval -1 具体错误通过errno呈现 \n
 * 支持返回的errno \n
 * EBADF : 文件描述符错误 \n
 * ENOTSOCK : 文件描述符找不到协议的socket \n
 * EFAULT : buf指针为空. (注:此部分与linux存在差异) \n
 * ECONNREFUSED : 与对端的连接断开. (注:此部分与linux存在差异) \n
 * EMSGSIZE : 入参msg->msg_iovLen小于0 或 超过1024 \n
 * EINVAL : 文件描述符异常 \n
 * EAGAIN : 非阻塞模式下发送报文被阻塞 \n
 * EINTR : 发送报文过程中信号量被中断. \n
 * ECONNRESET : 对端强制关闭了连接. \n
 * ENOTCONN : 尚未建立连接 \n
 * EPIPE : 写关闭或者对端断开了连接 \n
 * ENOMEM : socket缓冲区已满，无法继续添加数据. (注：此部分与linux存在差异) \n
 * EOPNOTSUPP : 不支持对应的flag类型 \n
 * EDESTADDRREQ : 未设置对端地址 \n
 * ENETUNREACH : 找不到对应路由 \n
 * @par 依赖:
 *     <ul><li>dp_posix_socket_api.h：该接口声明所在的头文件。</li></ul>

 */
ssize_t DP_PosixWrite(int sockfd, const void *buf, size_t count);

/**
 * @ingroup socket
 * @brief 标准socket发送数据接口
 *
 * @par 描述: 标准socket发送数据接口
 * @attention
 * @li 入参*pstIov，由产品保证内存的有效性和独占性，iove中iov_base申请的数据空间 \n
 *     大于等于结构中的iov_len，且iov_base不为空;iovec个数大于等于 \n
 *     入参iovcnt, i32IovCnt属于[1, 1024]
 * @li 待发送数据必须小于0x80000000，同时受发送缓冲区大小限制。
 * @li 发送成功后，pbuf由产品自行维护(释放或缓存)
 *
 * @param fd [IN] Socket描述符 <大于0>
 * @param iov [IN] Iov数组<元素个数大于iovcnt,由产品释放内存>
 * @param iovcnt [IN] Iov数组元素个数<[1, 1024]>
 *
 * @retval 大于0 成功,产品实际能发送的数据长度。
 * @retval -1 失败，具体错误通过errno呈现 \n
 * 支持返回的errno \n
 * EBADF : 文件描述符错误 \n
 * ENOTSOCK : 文件描述符找不到协议的socket \n
 * EFAULT : iov指针为空. (注:此部分与linux存在差异) \n
 * ECONNREFUSED : 与对端的连接断开. (注:此部分与linux存在差异) \n
 * EMSGSIZE : 入参msg->msg_iovLen小于0 或 超过1024 \n
 * EINVAL : iovcnt小于等于0或者大于1024 \n
 * EAGAIN : 非阻塞模式下发送报文被阻塞 \n
 * EINTR : 发送报文过程中信号量被中断. \n
 * ECONNRESET : 对端强制关闭了连接. \n
 * ENOTCONN : 尚未建立连接 \n
 * EPIPE : 写关闭或者对端断开了连接 \n
 * ENOMEM : socket缓冲区已满，无法继续添加数据. (注：此部分与linux存在差异) \n
 * EOPNOTSUPP : 不支持对应的flag类型 \n
 * EDESTADDRREQ : 未设置对端地址 \n
 * ENETUNREACH : 找不到对应路由 \n
 * @par 依赖:
 *     <ul><li>dp_posix_socket_api.h：该接口声明所在的头文件。</li></ul>

 */
ssize_t DP_PosixWritev(int fd, const struct iovec *iov, int iovcnt);

/**
 * @ingroup socket
 * @brief 标准socket接收数据接口
 *
 * @par 描述: 标准socket接收数据接口
 * @attention
 * @li 入参buf，由产品保证内存的有效性和独占性，且申请的数据空间大于等于入参len。由产品释放空间
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param buf [IN] 产品接收报文的内存地址<非空且产品自行申请需要收取报文长度的内存>
 * @param len [IN] 需要收取报文长度<大于0>
 * @param flags [IN] 当前只支持MSG_PEEK和MSG_DONTWAIT标志，其他类型不支持
 *
 * @retval 大于0 实际能接收的数据长度
 * @retval 0 TCP链路已中断
 * @retval -1 具体错误通过errno呈现 \n
 * 支持返回的errno \n
 * EBADF : 文件描述符错误 \n
 * ENOTSOCK : 文件描述符找不到协议的socket \n
 * EFAULT : 入参buf指针为空 (注:此部分与linux存在差异) \n
 * EINVAL : 文件描述符异常 (注：此部分与linux存在差异) \n
 * EAGAIN : 非阻塞模式，且当前没有数据可接收 \n
 * ENOTCONN : 尚未建立连接 \n
 * @par 依赖:
 *     <ul><li>dp_posix_socket_api.h：该接口声明所在的头文件。</li></ul>

 */
ssize_t DP_PosixRecv(int sockfd, void* buf, size_t len, int flags);

/**
 * @ingroup socket
 * @brief socket接收数据接口
 *
 * @par 描述: socket接收数据接口,并返回接收数据的源地址信息
 * @attention
 * @li 如果传入的地址空间长度不足以容纳报文数据源地址，则拷贝实际地址空间的源地址信息
 * @li 入参buf，由产品保证内存的有效性和独占性，其待接收数据必须小于0x80000000，且由产品自行释放空间
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param buf [IN] 产品接收报文的内存地址<非空且产品自行申请需要收取报文长度的内存>
 * @param len [IN] 需要收取报文长度<大于0>
 * @param flags [IN] 当前只支持 MSG_PEEK和MSG_DONTWAIT 标志，其他类型不支持
 * @param addrlen [IN/OUT] 接收数据源地址空间大小，接口执行成功后，返回实际地址空间大小 \n
 *                         对于Socket4，addrlen是struct sockaddr的长度
 * @param srcAddr [OUT] 待接收数据的源地址空间，对于Socket4，srcAddr按照struct sockaddr_in赋值<非空指针>
 *
 * @retval 大于0 成功,产品实际能接收的数据长度。
 * @retval 等于0 成功,断链
 * @retval -1 失败，具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 *      EBADF : sockfd 不是有效的文件描述符 \n
 *      ENOTSOCK : sockfd 不是socket类型 \n
 *      EFAULT : 入参buf 为空指针 \n
 *      EOPNOTSUPP : 入参flags 不支持，当前仅支持DP_MSG_DONTWAIT和DP_MSG_PEEK \n
 *      ENOTCONN : (tcp连接)socket未连接或者建链阶段发生异常 \n
 *      ECONNRESET : (tcp连接)连接被对端重置 \n
 *      EAGAIN : 非阻塞socket，没有数据可读 \n
 *      EINTR : 信号量被中断
 * @par 依赖:
 *     <ul><li>dp_posix_socket_api.h：该接口声明所在的头文件。</li></ul>

 */
ssize_t DP_PosixRecvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *srcAddr, socklen_t *addrlen);

/**
 * @ingroup socket
 * @brief 根据消息参数接收socket数据
 *
 * @par 描述: 根据消息参数接收socket数据
 * @attention
 * @li 如果传入的地址空间长度不足以容纳报文数据源地址，则拷贝实际地址空间的源地址信息
 * @li 入参*消息参数中的数据缓存，由产品保证内存的有效性和独占性，其待接收数据必须小于0x80000000，且由产品自行释放空间
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param flags [IN] 当前只支持 MSG_PEEK和MSG_DONTWAIT标志，其他类型不支持
 * @param msg [IN/OUT] 消息地址<非空指针> \n
 *                     msg_iovlen不为0或者超过1024； \n
 *                     待接收数据总长度不为0或者超过7FFFFFFF \n
 *                     TCP下忽略msg_control，msg_controllen参数
 * @retval 大于0 成功,产品实际能接收的数据长度。
 * @retval 等于0 成功,断链
 * @retval -1 失败，具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 *      EBADF : sockfd 不是有效的文件描述符 \n
 *      ENOTSOCK : sockfd 不是socket类型 \n
 *      EFAULT : 入参msg 为空指针; 入参msg->msg_iov \n
 *      EMSGSIZE : 入参msg->msg_iovlen小于0 或 超过1024(与内核保持一致的判断) \n
 *      EINVAL : 入参msg->msg_iov数组中iov_len总和超过SSIZE_MAX \n
 *      EOPNOTSUPP : 入参flags 不支持，当前仅支持DP_MSG_DONTWAIT和DP_MSG_PEEK \n
 *      ENOTCONN : (tcp连接)socket未连接或者建链阶段发生异常 \n
 *      ECONNRESET : (tcp连接)连接被对端重置 \n
 *      EAGAIN : 非阻塞socket，没有数据可读 \n
 *      EINTR : 信号量被中断
 * @par 依赖:
 *     <ul><li>dp_posix_socket_api.h：该接口声明所在的头文件。</li></ul>

 */
ssize_t DP_PosixRecvmsg(int sockfd, struct msghdr *msg, int flags);

/**
 * @ingroup socket
 * @brief 标准socket接收数据接口
 *
 * @par 描述: 标准socket接收数据接口
 * @attention
 * @li 入参*buf，由产品保证内存的有效性和独占性，且申请的数据空间大于等于入参count。由产品释放空间内存
 *
 * @param fd [IN] Socket描述符 <大于0>
 * @param buf [IN] 产品接收报文的内存地址<非空且产品自行申请需要收取报文长度的内存>
 * @param count [IN] 需要收取报文长度<大于0>
 *
 * @retval 大于0 读取的数据字节数
 * @retval 等于0 断链
 * @retval -1 具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 *      EBADF : sockfd 不是有效的文件描述符 \n
 *      ENOTSOCK : sockfd 不是socket类型（注：此部分与linux存在差异 linux返回EINVAL） \n
 *      EFAULT : 入参buf 为空指针（注：此部分与linux存在差异 linux返回ENOTCONN） \n
 *      ENOTCONN : 入参buf 为空指针 或者 (tcp连接)socket未连接或者建链阶段发生异常 \n
 *      ECONNRESET : (tcp连接)连接被对端重置 \n
 *      EAGAIN : 非阻塞socket，没有数据可读 \n
 *      EINTR : 信号量被中断
 * @par 依赖:
 *     <ul><li>dp_posix_socket_api.h：该接口声明所在的头文件。</li></ul>

 */
ssize_t DP_PosixRead(int sockfd, void *buf, size_t count);

/**
 * @ingroup socket
 * @brief 标准socket接收数据接口
 *
 * @par 描述: 标准socket接收数据接口，支持多缓冲区
 * @attention
 * @li IOV数组待接收数据必须小于0x80000000。
 * @li 入参*pstIov，由产品保证内存的有效性和独占性，iove中iov_base申请的数据空间 \n
 *     大于等于结构中的iov_len，且iov_base不为空;iovec个数大于等于 \n
 *     入参iovcnt, i32IovCnt属于[1, 1024]
 *
 * @param fd [IN] Socket描述符 <大于0>
 * @param iov [IN] Iov数组<元素个数大于iovcnt,由产品释放内存>
 * @param iovcnt [IN] Iov数组元素个数<[1, 1024]>
 *
 * @retval 大于0 成功,产品实际能接收的数据长度。
 * @retval 等于0 成功,断链
 * @retval -1 失败，具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 *      EBADF : sockfd 不是有效的文件描述符 \n
 *      ENOTSOCK : sockfd 不是socket类型的 \n
 *      EFAULT : 入参iov 为空指针 \n
 *      EINVAL : 入参iovcnt小于0 或 超过1024(与内核保持一致的判断) \n
 *      EINVAL : 入参iov数组iov_len总和超过SSIZE_MAX \n
 *      ENOTCONN : (tcp连接)socket未连接或者建链阶段发生异常 \n
 *      ECONNRESET : (tcp连接)连接被对端重置 \n
 *      EAGAIN : 非阻塞socket，没有数据可读 \n
 *      EINTR : 信号量被中断
 * @par 依赖:
 *     <ul><li>dp_posix_socket_api.h：该接口声明所在的头文件。</li></ul>

 */
ssize_t DP_PosixReadv(int fd, const struct iovec *iov, int iovcnt);

/**
 * @ingroup socket
 * @brief 关闭Socket。
 *
 * @par 描述: 关闭Socket。
 * @attention
 * NA
 *
 * @param fd [IN] Socket描述符 <大于0>
 *
 * @retval 0 成功
 * @retval -1 具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 *      EBADF : sockfd 不是有效的文件描述符
 * @par 依赖:
 *     <ul><li>dp_posix_socket_api.h：该接口声明所在的头文件。</li></ul>

 */
int DP_PosixClose(int fd);

/**
 * @ingroup socket
 * @brief 半关闭SOCKET
 *
 * @par 描述: 支持TCP半关闭读和半关闭写功能
 * @attention
 * @li 只能在建链完成之后调用
 * @li 调用DP_PosixClose之后不允许调用
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param how [IN] 半关闭对应的操作(读、写、同时关闭读写) <SHUT_RD, SHUT_WR, SHUT_RDWR>
 *
 * @retval 0 成功
 * @retval -1 具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 *      EBADF : sockfd 不是有效的文件描述符 \n
 *      ENOTSOCK : sockfd 不是socket类型 \n
 *      EINVAL : 入参how 不支持 \n
 *      ENOTCONN : 当前sock不支持shutdown操作 或 (tcp连接)socket未连接或者建链阶段发生异常
 * @par 依赖:
 *     <ul><li>dp_posix_socket_api.h：该接口声明所在的头文件。</li></ul>

 */
int DP_PosixShutdown(int sockfd, int how);

/**
 * @ingroup socket
 * @brief socket绑定
 *
 * @par 描述: 通过DP_Socket创建新的socket后，需要调用此接口将该socket与本端地址及端口绑定。
 * @attention
 * @li 当前支持socket选项为SO_REUSEADDR、SO_REUSEPORT时可以在同一端口，绑定不同的IP地址。可参见DP_SetSockOpt。
 * @li REUSEADDR支持设置但功能未实现。
 * @li REUSEPORT部分实现，与Linux行为不一致。支持多个socket bind相同的地址和端口，但是TCP监听场景不支持accept负载均衡， \n
 * UDP场景不支持接收UDP报文场景的负载均衡。
 * @li 不允许socket重复绑定地址；
 * @li 端口范围，可绑定的端口范围[0,65535]。
 * @li 当端口为0时，协议栈随机端口，随机端口空间为[49152-65535],注意多协议栈worker实例场景，空间为各worker均分，
 * 即每个worker的地址范围为[65535-49152]/worker最大数量。"
 * @li addr 参数中地址和端口赋值，需要使用网络序
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param addr [IN] 对于Socket4，addr按照struct sockaddr_in的定义来赋值，调用本接口时再转换 \n
 *                  成struct sockaddr *类型<非空指针>
 * @param addrlen [IN] 对于Socket4，i32AddrLen是struct sockaddr_in的长度 <大于0>
 *
 * @retval 0 成功
 * @retval -1 具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 *      EBADF : sockfd 不是有效的文件描述符 \n
 *      ENOTSOCK : sockfd 不是socket类型 \n
 *      EINVAL : 该socket已经被bind，不支持绑定新地址 \n
 *      EINVAL : 该socket被shutdown \n
 *      EFAULT : 入参addr为空指针 \n
 *      EINVAL : 入参addrlen小于sizeof(struct sockaddr) \n
 *      EAFNOSUPPORT : 入参addr->sa_family不支持(当前仅支持AF_INET) \n
 *      EISCONN : socket已经connect \n
 *      EADDRNOTAVAIL : 地址不正确 \n
 *      EADDRINUSE : 地址不可用，已被绑定或无端口可用
 * @par 依赖:
 *     <ul><li>dp_posix_socket_api.h：该接口声明所在的头文件。</li></ul>

 */
int DP_PosixBind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

/**
 * @ingroup socket
 * @brief 获取socket属性
 *
 * @par 描述: 获取socket属性
 * @attention
 * NA
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param level [IN] 级别<SOL_SOCKET:socket   \n
 *                   IPPROTO_IP:IP，i32Level为IPPROTO_IP的只能对IPv4的socket进行设置  \n
 *                   TCP:TCP  >
 * @param optname [IN] 选项 \n
 *                   SO_REUSEADDR:地址重用，0表示关闭；非0表示开启，默认关闭。 \n
 *                   SO_REUSEPORT:端口重用，0表示关闭；非0表示开启，默认关闭。 \n
 *                   SO_KEEPALIVE:保活选项，0表示关闭；非0表示开启，默认关闭。 \n
 *                   SO_LINGER:linger选项。 \n
 *                   SO_SNDBUF:发送缓存即高水位，默认TCP 8k、UDP 9k。设置值必须比当前大，否则返回错误。 \n
 *                   SO_RCVBUF:接收缓存即高水位，默认TCP 8k、UDP 40k。设置值必须比当前大，否则返回错误。 \n
 *                   SO_SNDTIMEO：阻塞模式发送发送超时时间 \n
 *                   SO_RCVTIMEO：阻塞模式接收等待，不影响accept，connect \n
 *                   TCP_NODELAY:设置是否禁止TCP的Nagle算法，默认开启Nagle算法。 \n
 *                   TCP_KEEPIDLE:在指定的空闲时间后启动保活探测，单位秒，规格为[1, 7200] \n
 *                   TCP_KEEPINTVL:设置保活探测的时间间隔，单位秒，规格为[1, 7200] \n
 *                   TCP_KEEPCNT:设置保活探测的次数, 规格为[1, 127] \n
 *                   TCP_CORK:设置cork选项。,1表示开启, 0表示关闭,默认关闭 \n
 *                   TCP_DEFER_ACCEPT:子socket收到数据再上报监听socket建链完成,单位秒, 规格为[0, 7200] \n
 *                   TCP_MAXSEG:设置TCP最大报文段, 缺省IPV4 1460/IPV6 1440。 \n
 *                   IP_PKTINFO:设置收UDP报文时获取目的IP及入端口信息，保存到CMSG。 \n
 *                   IP_TOS：设置TOS
 * @param optval [IN] 选项值<非空指针>
 * @param optlen [IN] 选项值长度<非空指针>
 *
 * @retval 0 成功
 * @retval -1 具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 *      EBADF : sockfd 不是有效的文件描述符 \n
 *      ENOTSOCK : sockfd 不是socket类型 \n
 *      EFAULT : 入参optval为空指针，或 入参optlen为空指针 \n
 *      EINVAL : 入参optlen长度小于0，或长度小于对应选项的结构体大小 \n
 *      ENOPROTOOPT : 对应level不支持此选项 \n
 *      EOPNOTSUPP : 对应level不支持 （注：此部分与linux保持一致，man标准手册里未提到）
 * @par 依赖:
 *     <ul><li>dp_posix_socket_api.h：该接口声明所在的头文件。</li></ul>

 */
int DP_PosixGetsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);

/**
 * @ingroup socket
 * @brief 设置socket属性。
 *
 * @par 描述: 设置socket属性。
 * @attention
 * NA
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param level [IN] 级别<SOL_SOCKET:socket   \n
 *                   DP_IPPROTO_IP:IP，level为DP_IPPROTO_IP的只能对IPv4的socket进行设置  \n
 *                   DP_IPPROTO_TCP:TCP  >
 * @param optname [IN] 选项参考 DP_PosixGetSockOpt
 * @param optval [IN] 选项值，对于optval为数值的选项设置和获取，当前只支持uint32_t 型输入和输出<用户根据实际情况填充><非空指针>
 * @param optlen [IN] 选项值长度<用户根据实际情况填充>
 *
 * @retval 0 成功
 * @retval -1 具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 * EBADF: sockfd不是有效的文件描述符 \n
 * EDOM: 选项SO_SNDTIMEO、SO_RCVTIMEO设置的超时时间非法 \n
 * EINVAL: 设置的选项无效或套接字已关闭 \n
 * ENOPROTOOPT: 不支持的协议或指定的协议不支持指定的选项 \n
 * ENOTSOCK: 套接字不是socket类型 \n
 * EFAULT: optal为空指针
 * @par 依赖:
 *     <ul><li>dp_posix_socket_api.h：该接口声明所在的头文件。</li></ul>

 */
int DP_PosixSetsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);

/**
 * @ingroup socket
 * @brief socket侦听
 *
 * @par 描述: 侦听对端的连接
 * @attention
 * @li 仅限于TCP服务器端。
 * @li 必须先bind地址
 * @li 不允许对同一个socket重复调用
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param backlog [IN] 插口的排队等待的连接个数门限。 \n
 *                     如果设置值小于5，则内部默认设置为5，如果设置值大于32767，则设置为32767，否则使用设置的值；
 *
 * @retval 0 成功
 * @retval -1 具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 * EBADF: sockfd不是有效的文件描述符 \n
 * EDESTADDRREQ: 套接字没有绑定到本地地址，而协议不支持监听未绑定套接字。 \n
 * EINVAL: 套接字已连接 \n
 * ENOTSOCK: 套接字不是socket类型 \n
 * EOPNOTSUPP: 套接字不支持listen操作 \n
 * EADDRINUSE: 套接字已经在监听
 * @par 依赖:
 *     <ul><li>dp_posix_socket_api.h：该接口声明所在的头文件。</li></ul>

 */
int DP_PosixListen(int sockfd, int backlog);

/**
 * @ingroup socket
 * @brief 接收新连接
 *
 * @par 描述: 接收新连接
 * @attention
 * @li 必须是监听socket
 * @li 不支持阻塞，不受NONBLOCK标识的影响，调用此接口时，如果没有已建立的连接等待获取，会返回-1.errno设置为EAGAIN
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param addrlen [IN/OUT] 对于Socket4，addrlen 指向的是 struct sockaddr_in 的长度；
 * @param addr [OUT] 子socket 远端地址,对于Socket4，addr 按照 struct sockaddr_in 的定义来赋值， \n
 *                   调用本接口时再转换成 struct sockaddr *类型<非空指针>
 *
 * @retval 大于0 子socket fd
 * @retval -1 具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 * EBADF: sockfd不是有效的文件描述符 \n
 * EAGAIN: 套接字设置了非阻塞，且没有新连接到达 \n
 * ECONNABORTED: 套接字已关闭 \n
 * EINTR: 被信号中断 \n
 * EFAULT: addr为空 \n
 * EINVAL: addr不为空，addrlen为空或者addrlen值小于addr的长度 \n
 * EMFILE: 文件描述符资源已耗尽 \n
 * ENOTSOCK: 套接字不是socket类型 \n
 * EOPNOTSUPP: 套接字不支持accept操作
 * @par 依赖:
 *     <ul><li>dp_posix_socket_api.h：该接口声明所在的头文件。</li></ul>

 */
int DP_PosixAccept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

/**
 * @ingroup socket
 * @brief 获取指定ID的socket对应的对端协议地址和端口
 *
 * @par 描述: 获取指定ID的socket对应的对端协议地址和端口
 * @attention
 * NA
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param addr [IN] 对于Socket4，addr按照struct sockaddr_in的定义来赋值，调用本接口时再转换 \n
 *                  成struct sockaddr *类型<非空指针>
 * @param addrlen [IN/OUT] 对于Socket4，i32AddrLen是struct sockaddr_in的长度 <大于0>
 *
 * @retval 0 成功
 * @retval -1 具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 * EBADF: sockfd不是有效的文件描述符 \n
 * EINVAL: 套接字已关闭 \n
 * ENOTCONN: 套接字未处于连接状态 \n
 * ENOTSOCK: 套接字不是socket类型 \n
 * EOPNOTSUPP: 套接字不支持该操作 \n
 * EFAULT: addr或addrlen为空
 * @par 依赖:
 *     <ul><li>dp_posix_socket_api.h：该接口声明所在的头文件。</li></ul>

 */
int DP_PosixGetpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

/**
 * @ingroup socket
 * @brief 获取指定ID的socket对应的本地协议地址和端口
 *
 * @par 描述: 获取指定ID的socket对应的本地协议地址和端口
 * @attention
 * NA
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param addr [IN] 对于Socket4，addr按照struct sockaddr_in的定义来赋值，调用本接口时再转换 \n
 *                  成struct sockaddr *类型<非空指针>
 * @param addrlen [IN/OUT] 对于Socket4，i32AddrLen是struct sockaddr_in的长度 <大于0>
 *
 * @retval 0 成功
 * @retval -1 具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 * EBADF: sockfd不是有效的文件描述符 \n
 * EINVAL: 套接字已关闭 \n
 * ENOTCONN: 套接字未处于连接状态 \n
 * ENOTSOCK: 套接字不是socket类型 \n
 * EOPNOTSUPP: 套接字不支持该操作 \n
 * EFAULT: addr或addrlen为空
 * @par 依赖:
 *     <ul><li>dp_posix_socket_api.h：该接口声明所在的头文件。</li></ul>

 */
int DP_PosixGetsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

/**
 * @ingroup socket
 * @brief 设置或获取socket的各IO方面属性
 *
 * @par 描述: 设置或获取socket的各IO方面属性
 * @attention
 * NA
 *
 * @param fd [IN] Socket描述符 <大于0>
 * @param request [IN] 要设置的选项类型<FIONBIO/FIOASYNC>
 * @param value [IN] 指向传入或传出数据的指针,不同的选项有不同的含义<非空指针>
 *
 * @retval 0 成功
 * @retval -1 具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 * EBADF: sockfd不是有效的文件描述符 \n
 * EINVAL: 请求参数不支持，当前仅支持DP_FIONBIO \n
 * EFAULT: arg为空
 * @par 依赖:
 *     <ul><li>dp_posix_socket_api.h：该接口声明所在的头文件。</li></ul>

 */
int DP_PosixIoctl(int fd, int request, void* arg);

/**
 * @ingroup socket
 * @brief 设置或获取socket文件描述符属性
 *
 * @par 描述: 操作指定socket。当前只支持 DP_F_GETFL、DP_F_SETFL 操作 O_NONBLOCK 标志位。
 *
 * @attention
 * @li 只支持 F_GETFL、F_SETFL两种操作
 * @li 获取/设置标志位时只有 O_NONBLOCK生效，其他标志位不支持
 * @li F_GETFL 不需要参数, value 无意义
 * @li F_SETFL 需要参数,类型为 uint32_t,其中 O_NONBLOCK 置位代表设置socket为非阻塞模式,
 * @li 否则代表设置socket为阻塞模式
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param cmd [IN] 要执行的操作<F_GETFL、F_SETFL>
 * @param value [IN] 输入参数,不同的操作有不同的含义.
 *
 * @retval socket的状态标志位 F_GETFL成功场景: 返回socket的状态标志位,和 NONBLOCK 做按位与运算,结果非零代表该标志位置位
 * @retval 0 F_SETFL成功场景: 0,代表设置成功
 * @retval -1 具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 * EBADF: sockfd不是有效的文件描述符 \n
 * EINVAL: 请求参数不支持，当前仅支持DP_F_GETFL和DP_F_SETFL
 * @par 依赖:
 *     <ul><li>dp_posix_socket_api.h：该接口声明所在的头文件。</li></ul>

 */
int DP_PosixFcntl(int fd, int cmd, int val);

#ifdef __cplusplus
}
#endif
#endif
