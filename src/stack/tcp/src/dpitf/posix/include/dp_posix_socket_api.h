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
 * @file dp_posix_socket_api.h
 * @brief 定义了基于POSIX的socket对外接口
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
 * EAFNOSUPPORT: 入参domain不支持创建socket \n
 * EMFILE: 创建socket的数量达到设置的TCPCB/UDPCB数量上限（配置项） \n
 * EPROTONOSUPPORT: 入参domain不支持对应protocol创建socket \n
 * EPROTOTYPE: 入参type中无flags，且当前type类型不支持（只支持SOCK_STREAM、SOCK_DGRAM） \n
 * ENOMEM: 创建socket时，内存不足 \n
 * EINVAL: 1.入参protocol非法（负数，大于最大值IPPROTO_MAX） \n
 * 	       2.type中带有flags，当前不支持type中带有flags
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
 * @li 此接口调用要在协议栈启动初始化完成之后才能正常使用。
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param addr [IN] 对于Socket4，addr按照struct sockaddr_in的定义来赋值，调用本接口时再转换 \n
 *                  成struct sockaddr *类型<非空指针> \n
 *                  对于Socket6，addr按照struct sockaddr_in6的定义来赋值，调用本接口时再转换 \n
 *                  成struct sockaddr *类型<非空指针>
 * @param addrlen [IN] 对于Socket4，addrlen是struct sockaddr_in的长度 <大于0> \n
 *                     对于Socket6，addrlen是struct sockaddr_in6的长度 <大于0>
 *
 * @retval 0 成功
 * @retval -1 具体错误通过errno呈现 \n
 * 支持返回的errno: \n
 * EADDRNOTAVAIL: 未绑定端口时，尝试随机绑定端口，无可用端口 \n
 * EAFNOSUPPORT: 入参addr->sa_family不支持(当前仅支持AF_INET) \n
 * EALREADY: socket正在创建连接中，且未被拒绝连接（收到RST/超时） \n
 * EBADF: 入参sockfd无效 \n
 * ECONNREFUSED: 对端拒绝服务（收到对端的RST/超时） \n
 * EINPROGRESS: 设置nonblock，正在异步connect \n
 * EINTR: 被信号中断 \n
 * EISCONN: Tcp被重复connect \n
 * ENETUNREACH: socket未绑定dev，且路由信息为空 \n
 * ENOTSOCK: 入参sockfd不是socket类型fd \n
 * EPROTOTYPE: 协议不支持connect（当前tcp、udp均支持） \n
 * ETIMEDOUT: 建链连接超过等待时间 \n
 * EADDRINUSE: 1.端口被重复bind，在connect时判断，返回此错误码 \n
 *             2.已经绑定成功，但连接失败 \n
 * EINVAL: 入参addrlen不是地址的有效长度 \n
 * 	       fd存在，但是fd对应的socketcb有异常。 \n
 * ENETDOWN: 调用对外接口将netdev给down掉后，调用该接口 \n
 * EOPNOTSUPP: socket处于listen状态
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
 * @li 此接口调用要在协议栈启动初始化完成之后才能正常使用。
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param buf [IN] 产品发送报文的内存地址<非空>
 * @param len [IN] 需要发送的报文长度<大于0>
 * @param flags [IN] 当前只支持 MSG_DONTWAIT标志，其他类型不支持
 *
 * @retval 大于0 实际能发送的数据长度
 * @retval -1 具体错误通过errno呈现 \n
 * 支持返回的errno: \n
 * EAGAIN/EWOULDBLOCK: 1.非阻塞情况下，写请求被阻塞 \n
 *                     2.阻塞情况下，设置超时时间，请求超时 \n
 * EBADF: 入参socket无效 \n
 * ECONNRESET: 当前没有连接，且被拒绝连接（收到过RST/超时） \n
 * EDESTADDRREQ: UDP未执行connect，直接调用send \n
 * EINTR: 被信号中断 \n
 * EMSGSIZE: udp入参len大于65507 \n
 * ENOTCONN: Tcp当前没有连接，且没有被拒绝连接 \n
 * ENOTSOCK: 入参sockfd不是套接字类型的fd \n
 * EOPNOTSUPP: 入参flags中有不支持的，当前只支持MSG_DONTWAIT \n
 * EPIPE: Tcp的socket被关闭写 \n
 * ENETDOWN: 调用对外接口将netdev给down掉后，调用该接口 \n
 * EFAULT: 入参len不为0时，入参buf指针为空 \n
 * EINVAL: 1.fd存在，但是fd对应的socketcb有异常 \n
 * 	       2.入参length大于SSIZE_MAX \n
 * ENOMEM: 内存申请失败
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
 * @li 此接口调用要在协议栈启动初始化完成之后才能正常使用。
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param buf [IN] 产品发送报文的内存地址<非空>
 * @param len [IN] 需要发送的报文长度<大于0>
 * @param flags [IN] 当前只支持 MSG_DONTWAIT 标志，其他类型不支持
 * @param destAddr [IN] 数据发往的目的地址。对于Socket4，pstSockAddr按照struct sockaddr_in \n
 *                      的定义来赋值，调用本接口时再转换成struct sockaddr *类型；<非空指针> \n
 *                      数据发往的目的地址。对于Socket6，pstSockAddr按照struct sockaddr_in6 \n
 *                      的定义来赋值，调用本接口时再转换成struct sockaddr *类型；<非空指针>
 * @param addrlen [IN] 目的地址长度。对于Socket4，addrlen是struct sockaddr_in的长度； <非0> \n
 *                     对于Socket6，addrlen是struct sockaddr_in6的长度； <非0>
 *
 * @retval 大于0 成功,产品实际能发送的数据长度
 * @retval -1 失败，具体错误通过errno呈现 \n
 * 支持返回的errno \n
 * EAFNOSUPPORT: udp：入参destAddr不为空情况下，(struct DP_Sockaddr*)destAddr->sa_family不是AF_INET \n
 * EAGAIN/EWOULDBLOCK: 1.非阻塞情况下，写请求被阻塞 \n
 *                     2.阻塞情况下，设置超时时间，请求超时 \n
 * EBADF: 入参sockfd无效 \n
 * ECONNRESET: 当前没有连接，且被拒绝连接（收到过RST/超时） \n
 * EINTR: 被信号中断 \n
 * EMSGSIZE: udp入参len大于65507 \n
 * ENOTCONN: Tcp当前没有连接，且没有被拒绝连接 \n
 * ENOTSOCK: 入参sockfd不是套接字类型的fd \n
 * EOPNOTSUPP: 入参flags中有不支持的，当前只支持MSG_DONTWAIT \n
 * EPIPE: Tcp的socket被关闭写 \n
 * EDESTADDRREQ: udp未connect、入参destAddr为空 \n
 * EINVAL: 1.udp中，入参addrlen小于其对应地址族长度或大于INT_MAX \n
 * 	       2.udp中入参destAddr不对已经connecnt的对端 \n
 * 	       3.入参length大于SSIZE_MAX \n
 * 	       4.fd存在，但是fd对应的socketcb有异常 \n
 * ENETDOWN: 调用对外接口将netdev给down掉后，调用该接口 \n
 * ENETUNREACH: 1.UDP场景，发送目标地址无匹配的路由 \n
 *              2.UDP发送广播 \n
 * EAGAIN: UDP未bind，随机分配端口时，无可用端口 \n
 * ENOMEM: 内存申请失败 \n
 * EFAULT: 入参len不为0时，入参mssage为空
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
 * @li 此接口调用要在协议栈启动初始化完成之后才能正常使用。
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
 * EAGAIN/EWOULDBLOCK: 1.非阻塞情况下，写请求被阻塞 \n
 *                     2.阻塞情况下，设置超时时间，请求超时 \n
 * EAFNOSUPPORT: udp：入参msg->msg_name不为空情况下，msg->msg_name->sa_family中的family不是AF_INET \n
 * EBADF: 入参sockfd无效 \n
 * ECONNRESET: 当前没有连接，且被拒绝连接（收到过RST/超时） \n
 * EINTR: 被信号中断 \n
 * EINVAL: 1.入参msg中每个msg_iov的iov_len之和大于SSIZE_MAX \n
 * 	       2.入参msg->msg_iov.iov_len大于SSIZE_MAX \n
 * 	       3.fd存在，但是fd对应的socketcb有异常。 \n
 * EMSGSIZE: 1.入参msg->msg_iovlen大于1024 \n
 * 	         2.udp中iov_len之和大于65507 \n
 * ENOTCONN: Tcp当前没有连接，且没有被拒绝连接 \n
 * ENOTSOCK: 入参sockfd不是套接字类型的fd \n
 * EOPNOTSUPP: 入参flags中有不支持的，当前只支持MSG_DONTWAIT \n
 * EPIPE: Tcp的socket被关闭写 \n
 * EDESTADDRREQ: udp未connect、入参msg->msg_name为空 \n
 * ENETDOWN: 调用对外接口将netdev给down掉后，调用该接口 \n
 * ENETUNREACH: 1.UDP场景，发送目标地址无匹配的路由 \n
 *              2.UDP发送广播 \n
 * EAGAIN: UDP未bind，随机分配端口时，无可用端口 \n
 * ENOMEM: 内存申请失败 \n
 * EFAULT: 1.入参msg->msg_iov为空 \n
 * 	       2.入参msg为空 \n
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
 * @li 此接口调用要在协议栈启动初始化完成之后才能正常使用。
 *
 * @param fd [IN] Socket描述符 <大于0>
 * @param buf [IN] 产品发送报文的内存地址<非空>
 * @param count [IN] 需要发送的字节数<大于0>
 *
 * @retval 大于0 发送的数据字节数
 * @retval -1 具体错误通过errno呈现 \n
 * 支持返回的errno \n
 * EAGAIN/EWOULDBLOCK: 1.非阻塞情况下，写请求被阻塞 \n
 *                     2.阻塞情况下，设置超时时间，请求超时 \n
 * EBADF: 入参sockfd无效 \n
 * EINTR: 被信号中断 \n
 * ECONNRESET: 当前没有连接，且被拒绝连接（收到过RST/超时） \n
 * EPIPE: Tcp的socket被关闭写 \n
 * EINVAL: 入参count大于SSIZE_MAX \n
 * ENETDOWN: 调用对外接口将netdev给down掉后，调用该接口 \n
 * EDESTADDRREQ: UDP未执行connect，直接调用write \n
 * EFAULT: 入参count不为0时，buf为空指针 \n
 * 出现其他错误码返回时，详见send接口
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
 * @li 此接口调用要在协议栈启动初始化完成之后才能正常使用。
 *
 * @param fd [IN] Socket描述符 <大于0>
 * @param iov [IN] Iov数组<元素个数大于iovcnt,由产品释放内存>
 * @param iovcnt [IN] Iov数组元素个数<[1, 1024]>
 *
 * @retval 大于0 成功,产品实际能发送的数据长度。
 * @retval -1 失败，具体错误通过errno呈现 \n
 * 支持返回的errno \n
 * EINVAL: 1.入参iov中iov_len之和大于SSIZE_MAX \n
 *         2.入参iovcnt大于1024 \n
 *         3.入参iovcnt小于0 \n
 *         4.fd存在，但是fd对应的socketcb有异常 \n
 * EFAULT: 入参iovcnt不为0时，iov为空指针 \n
 * 出现其他错误码返回时，详见sendmsg接口
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
 * @li 此接口调用要在协议栈启动初始化完成之后才能正常使用。
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
 * EAGAIN/EWOULDBLOCK: 1.非阻塞情况下，读请求被阻塞 \n
 *                     2.阻塞情况下，设置超时时间，请求超时 \n
 * EBADF: 入参sockfd无效 \n
 * ECONNRESET: 当前没有连接，且被拒绝连接（收到过RST/超时） \n
 * EINTR: 被信号中断 \n
 * EINVAL: 1.入参length大于SSIZE_MAX \n
 * 	       2.fd存在，但是fd对应的socketcb有异常 \n
 * ENOTCONN: Tcp当前没有连接，且没有被拒绝连接 \n
 * ENOTSOCK: 入参sockfd不是套接字类型的fd \n
 * EOPNOTSUPP: flags不支持，当前仅支持MSG_DONTWAIT、MSG_PEEK \n
 * EFAULT: 入参buf为空指针
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
 * @li 此接口调用要在协议栈启动初始化完成之后才能正常使用。
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param buf [IN] 产品接收报文的内存地址<非空且产品自行申请需要收取报文长度的内存>
 * @param len [IN] 需要收取报文长度<大于0>
 * @param flags [IN] 当前只支持 MSG_PEEK和MSG_DONTWAIT 标志，其他类型不支持
 * @param addrlen [IN/OUT] 接收数据源地址空间大小，接口执行成功后，返回实际地址空间大小 \n
 *                         对于Socket4，addrlen是struct sockaddr_in的长度 \n
 *                         对于Socket6，addrlen是struct sockaddr_in6的长度
 * @param srcAddr [OUT] 待接收数据的源地址空间，对于Socket4，srcAddr按照struct sockaddr_in赋值<非空指针> \n
 *                      对于Socket6，srcAddr按照struct sockaddr_in6赋值<非空指针>
 *
 * @retval 大于0 成功,产品实际能接收的数据长度。
 * @retval 等于0 成功,断链
 * @retval -1 失败，具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 * EAGAIN/EWOULDBLOCK: 1.非阻塞情况下，读请求被阻塞 \n
 *                     2.阻塞情况下，设置超时时间，请求超时 \n
 * EBADF: 入参sockfd无效 \n
 * ECONNRESET: 当前没有连接，且被拒绝连接（收到过RST/超时） \n
 * EINTR: 被信号中断 \n
 * EINVAL: 1.入参length大于SSIZE_MAX \n
 * 	       2.udp中，入参srcAddr不为空时，addrlen比对应地址族的长度小或大于INT_MAX \n
 * 	       3.fd存在，但是fd对应的socketcb有异常 \n
 * ENOTCONN: Tcp当前没有连接，且没有被拒绝连接 \n
 * ENOTSOCK: 入参sockfd不是套接字类型的fd \n
 * EOPNOTSUPP: flags不支持，当前仅支持MSG_DONTWAIT、MSG_PEEK \n
 * EFAULT: 入参buf为空指针
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
 * @li 此接口调用要在协议栈启动初始化完成之后才能正常使用。
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
 * EAGAIN/EWOULDBLOCK: 1.非阻塞情况下，读请求被阻塞 \n
 *                     2.阻塞情况下，设置超时时间，请求超时 \n
 * EBADF: 入参socket无效 \n
 * ECONNRESET: 当前没有连接，且被拒绝连接（收到过RST/超时） \n
 * EINTR: 被信号中断 \n
 * EINVAL: 1.iov_len或iov_len之和大于SSIZE_MAX \n
 * 	       2.fd存在，但是fd对应的socketcb有异常 \n
 * EMSGSIZE: msg->msg_iovlen大于1024 \n
 * ENOTCONN: Tcp当前没有连接，且没有被拒绝连接 \n
 * ENOTSOCK: 入参sockefd不是套接字类型的fd \n
 * EOPNOTSUPP: flags不支持，当前仅支持MSG_DONTWAIT、MSG_PEEK \n
 * EFAULT: 1.入参mssage为空指针 \n
 * 	       2.入参msg->msg_iov为空 \n
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
 * @li 此接口调用要在协议栈启动初始化完成之后才能正常使用。
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
 * EBADF: 入参sockfd无效 \n
 * EINTR: 被信号中断 \n
 * EINVAL: 1.入参count大于SSIZE_MAX \n
 * 	       2.fd存在，但是fd对应的socketcb有异常 \n
 * EAGAIN: 1.非阻塞情况下，读请求被阻塞 \n
 *         2.阻塞情况下，设置超时时间，请求超时" \n
 * ECONNRESET: 当前没有连接，且被拒绝连接（收到过RST/超时） \n
 * ENOTCONN: Tcp当前没有连接，且没有被拒绝连接 \n
 * EFAULT入参buf为空指针 \n
 * 出现其他错误码返回时，详见recv接口 \n
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
 * @li 此接口调用要在协议栈启动初始化完成之后才能正常使用。
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
 * EINVAL: 1.入参iov->iov_len值之和大于SSIZE_MAX \n
 * 	       2.入参iovcnt小于0或大于1024 \n
 * 	       3.fd存在，但是fd对应的socketcb有异常 \n
 * EFAULT: 入参iov为空指针 \n
 * 出现其他错误码返回时，详见recvmsg接口，不支持情况已标注 \n
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
 * @li 此接口调用要在协议栈启动初始化完成之后才能正常使用。
 *
 * @param fd [IN] Socket描述符 <大于0>
 *
 * @retval 0 成功
 * @retval -1 具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 * EBADF: 入参fd无效 \n
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
 * @li 此接口调用要在协议栈启动初始化完成之后才能正常使用。
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param how [IN] 半关闭对应的操作(读、写、同时关闭读写) <SHUT_RD, SHUT_WR, SHUT_RDWR>
 *
 * @retval 0 成功
 * @retval -1 具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 * EBADF: 入参sockfd无效
 * EINVAL: 1.入参how非法
 *         2.fd存在，但是fd对应的socketcb有异常
 * ENOTCONN: 1.Tcp当前没有连接
 * 	         2.socket类型不支持shutdown（只支持tcp）
 * ENOTSOCK: 入参sockfd不是socket类型fd
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
 * @li 此接口调用要在协议栈启动初始化完成之后才能正常使用。
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param addr [IN] 对于Socket4，addr按照struct sockaddr_in的定义来赋值，调用本接口时再转换 \n
 *                  成struct sockaddr *类型<非空指针> \n
 *                  对于Socket6，addr按照struct sockaddr_in6的定义来赋值，调用本接口时再转换 \n
 *                  成struct sockaddr *类型<非空指针>
 * @param addrlen [IN] 对于Socket4，addrlen是struct sockaddr_in的长度 <大于0> \n
 *                     对于Socket6，addrlen是struct sockaddr_in6的长度 <大于0>
 *
 * @retval 0 成功
 * @retval -1 具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 * EADDRINUSE: 1.地址不可用，已被bind \n
 * 	           2.无可用端口去bind \n
 * EADDRNOTAVAIL: 地址不正确，包括广播地址、异常地址（大于255）、无路由信息地址、路由信息异常情况 \n
 * EAFNOSUPPORT: 入参addr->sa_family不支持(当前仅支持AF_INET) \n
 * EBADF: 入参sockfd无效 \n
 * EINVAL: 1.该socket已经被bind \n
 * 	       2.该socket被shutdown \n
 * 	       3.入参addrlen小于对应地址族长度或大于INT_MAX \n
 * 	       4.fd存在，但是fd对应的socketcb有异常 \n
 * ENOTSOCK: 入参sockfd不是套接字类型fd \n
 * EISCONN: socket已经被connect \n
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
 * @li 此接口调用要在协议栈启动初始化完成之后才能正常使用。
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
 * EBADF: 入参sockfd无效 \n
 * EINVAL: 1.入参optlen的值大于INT_MAX \n
 * 	       2.入参optlen的值小于对应长度 \n
 * 	       3.fd存在，但是fd对应的socketcb有异常 \n
 * ENOPROTOOPT: 入参optname不支持 \n
 * ENOTSOCK: 入参sockfd不是套接字类型fd \n
 * EFAULT: 入参optlen或optval是空指针 \n
 * EOPNOTSUPP: 1.入参level不支持 \n
 * 	           2.当前socket类型没有getsockopt实现 \n
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
 * @li 此接口调用要在协议栈启动初始化完成之后才能正常使用。
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param level [IN] 级别<SOL_SOCKET:socket   \n
 *                   DP_IPPROTO_IP:IP，level为DP_IPPROTO_IP的只能对IPv4的socket进行设置  \n
 *                   DP_IPPROTO_TCP:TCP  >
 * @param optname [IN] 选项参考 DP_PosixGetSockOpt
 *                  SO_SNDTIMEO: 阻塞模式发送发送超时时间 \n
 *                  SO_RCVTIMEO: 阻塞模式接收发送超时时间 \n
 *                  SO_REUSEADDR: 地址重用开关 \n
 *                  SO_REUSEPORT: 端口重用开关 \n
 *                  SO_KEEPALIVE: 保活选项开关 \n
 *                  SO_LINGER: linger选项开关 \n
 *                  SO_SNDBUF: 发送缓存即高水位 \n
 *                  SO_RCVBUF: 接收缓存即高水位 \n
 *                  TCP_KEEPIDLE: 在指定的空闲时间后启动保活探测，单位秒，规格为[1, 7200] \n
 *                  TCP_KEEPINTVL: 设置保活探测的时间间隔，单位秒，规格为[1, 7200] \n
 *                  TCP_KEEPCNT: 设置保活探测的次数, 规格为[1, 127] \n
 *                  TCP_NODELAY: 设置是否禁止TCP的Nagle算法，默认开启Nagle算法 \n
 *                  TCP_CORK: 设置cork选项。,1表示开启, 0表示关闭,默认关闭 \n
 *                  TCP_MAXSEG: 设置TCP最大报文段 \n
 *                  TCP_DEFER_ACCEPT: 子socket收到数据再上报监听socket建链完成,单位秒, 规格为[0, 7200] \n
 *                  IP_TTL: ime To Live \n
 *                  IP_TOS: 设置TOS \n
 *                  IP_PKTINFO: 设置收UDP报文时获取目的IP及入端口信息，保存到CMSG \n
 * @param optval [IN] 选项值，对于optval为数值的选项设置和获取，当前只支持uint32_t 型输入和输出<用户根据实际情况填充><非空指针>
 * @param optlen [IN] 选项值长度<用户根据实际情况填充>
 *
 * @retval 0 成功
 * @retval -1 具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 * EBADF: 入参sockfd无效 \n
 * EDOM: 入参optname为超时时间情况，入参val中usec不在0~（1000 * 1000 - 1）中 \n
 * EINVAL: 1.入参optlen大于INT_MAX \n
 * 	       2.入参optlen小于对应长度 \n
 * 	       3.入参*optval值超过限制 \n
 * 	       4.fd存在，但是fd对应的socketcb有异常 \n
 * EISCONN: mss、deferaccept在连接状态下不会设置成功 \n
 * ENOPROTOOPT: 入参optname不支持 \n
 * ENOTSOCK: 入参sockfd不是套接字类型fd \n
 * EFAULT: 入参optval是空指针 \n
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
 * @li 此接口调用要在协议栈启动初始化完成之后才能正常使用。
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param backlog [IN] 插口的排队等待的连接个数门限。 \n
 *                     如果设置值小于5，则内部默认设置为5，如果设置值大于32767，则设置为32767，否则使用设置的值；
 *
 * @retval 0 成功
 * @retval -1 具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 * EBADF: 入参sockfd无效 \n
 * EDESTADDRREQ: socket未bind \n
 * ENOTSOCK: 入参sockfd不是套接字类型fd \n
 * EOPNOTSUPP: 该socket类型不支持listen（只支持tcp） \n
 * EINVAL: 1.socket已经被connect \n
 * 	       2.socket被shutdown \n
 * 	       3.fd存在，但是fd对应的socketcb有异常 \n
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
 * @li 此接口调用要在协议栈启动初始化完成之后才能正常使用。
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param addrlen [IN/OUT] 对于Socket4，addrlen 指向的是 struct sockaddr_in 的长度； \n
 *                         对于Socket6，addrlen 指向的是 struct sockaddr_in6 的长度；
 * @param addr [OUT] 子socket 远端地址,对于Socket4，addr 按照 struct sockaddr_in 的定义来赋值， \n
 *                   调用本接口时再转换成 struct sockaddr *类型<非空指针> \n
 *                   子socket 远端地址,对于Socket6，addr 按照 struct sockaddr_in6 的定义来赋值， \n
 *                   调用本接口时再转换成 struct sockaddr *类型<非空指针>
 *
 * @retval 大于0 子socket fd
 * @retval -1 具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 * EAGAIN/EWOULDBLOCK: 设置了非阻塞，且没有新连接到达 \n
 * EBADF: 入参sockfd无效 \n
 * EINTR: 被信号中断 \n
 * EINVAL: 1.入参addrlen的值小于对应地址族长度或大于INT_MAX \n
 * 	       2.socket未listen \n
 * 	       3.fd存在，但是fd对应的socketcb有异常 \n
 * EMFILE: socket数量达到设置的TCPCB/UDPCB数量上限 \n
 * ENOMEM : 创建子socket时，内存不足 \n
 * ENOTSOCK: 入参sockfd不是套接字类型fd \n
 * EOPNOTSUPP: 该socket类型不支持accept \n
 * EFAULT: 入参addrlen为空 且 addr不为空 \n
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
 * @li 此接口调用要在协议栈启动初始化完成之后才能正常使用。
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param addr [IN] 对于Socket4，addr按照struct sockaddr_in的定义来赋值，调用本接口时再转换 \n
 *                  成struct sockaddr *类型<非空指针> \n
 *                  对于Socket6，addr按照struct sockaddr_in6的定义来赋值，调用本接口时再转换 \n
 *                  成struct sockaddr *类型<非空指针>
 * @param addrlen [IN/OUT] 对于Socket4，addrlen是struct sockaddr_in的长度 <大于0> \n
 *                     对于Socket6，addrlen是struct sockaddr_in6的长度 <大于0>
 *
 * @retval 0 成功
 * @retval -1 具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 * EBADF: 入参sockfd无效 \n
 * EINVAL: 1.入参addrlen大于INT_MAX \n
 * 	       2.socket被shutdown \n
 * 	       3.fd存在，但是fd对应的socketcb有异常 \n
 * ENOTCONN: socket未connect \n
 * ENOTSOCK: 入参sockfd不是套接字类型fd \n
 * EFAULT: 1.入参addr为空指针 \n
 * 	       2.入参addrlen为空指针 \n
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
 * @li 此接口调用要在协议栈启动初始化完成之后才能正常使用。
 *
 * @param sockfd [IN] Socket描述符 <大于0>
 * @param addr [IN] 对于Socket4，addr按照struct sockaddr_in的定义来赋值，调用本接口时再转换 \n
 *                  成struct sockaddr *类型<非空指针> \n
 *                  对于Socket6，addr按照struct sockaddr_in6的定义来赋值，调用本接口时再转换 \n
 *                  成struct sockaddr *类型<非空指针>
 * @param addrlen [IN] 对于Socket4，addrlen是struct sockaddr_in的长度 <大于0> \n
 *                     对于Socket6，addrlen是struct sockaddr_in6的长度 <大于0>
 *
 * @retval 0 成功
 * @retval -1 具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 * EBADF: 入参sockfd无效 \n
 * ENOTSOCK: 入参sockfd不是套接字类型fd \n
 * EINVAL: 1.入参addrlen大于INT_MAX \n
 * 	       2.socket被shutdown \n
 * 	       3.fd存在，但是fd对应的socketcb有异常 \n
 * EFAULT: 1.入参addr为空指针 \n
 * 	       2.入参addrlen为空指针 \n
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
 * @li 此接口调用要在协议栈启动初始化完成之后才能正常使用。
 *
 * @param fd [IN] Socket描述符 <大于0>
 * @param request [IN] 要设置的选项类型<FIONBIO/FIOASYNC>
 * @param value [IN] 指向传入或传出数据的指针,不同的选项有不同的含义<非空指针>
 *
 * @retval 0 成功
 * @retval -1 具体错误通过errno呈现 \n
 * \n
 * 支持返回的errno: \n
 * EBADF: 入参fd无效 \n
 * EINVAL: 1.入参request不支持，当前仅支持DP_FIONBIO \n
 * 	       2.fd存在，但是fd对应的socketcb有异常 \n
 * EFAULT: 入参arg为空指针 \n
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
 * @li 此接口调用要在协议栈启动初始化完成之后才能正常使用。
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
 * EBADF: 入参fd无效 \n
 * EINVAL: 1.入参cmd不支持，当前仅支持DP_F_GETFL和DP_F_SETFL \n
 * 	       2.fd存在，但是fd对应的socketcb有异常 \n
 * @par 依赖:
 *     <ul><li>dp_posix_socket_api.h：该接口声明所在的头文件。</li></ul>

 */
int DP_PosixFcntl(int fd, int cmd, int val);

#ifdef __cplusplus
}
#endif
#endif
