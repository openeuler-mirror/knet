# sendmsg

## 接口名称

**sendmsg(int sockfd, const struct msghdr \*msg, int flags\)**

## 接口描述

发送数据。

> **说明：** 
>K-NET用户态TCP/IP协议栈数据发送端支持TCP协议优雅断链，当对端异常关闭时，发送端需要借用TCP重传超时机制/保活机制断链。

## 参数说明

|参数|说明|备注|
|--|--|--|
|sockfd|通信节点描述符|传入socket描述符。|
|*msg|指定发送数据消息指针|会忽略struct msghdr中的msg_flags参数。忽略msg_control，msg_controllen参数。msg_iovlen范围(0, 1024]。待发送数据总长度可设置范围为(0, SSIZE_MAX]。|
|flags|指定发送方式|flags参数支持MSG_DONTWAIT、MSG_NOSIGNAL（默认支持该flag，且不可修改）、MSG_MORE，其它类型不支持。如果携带有其他flags，直接返回失败。MSG_DONTWAIT：启用非阻塞操作。MSG_NOSIGNAL：防止在发送数据到已关闭连接的socket时触发SIGPIPE信号。MSG_MORE：延迟发送数据。|

## 返回值

类型：ssize\_t

- 非负数：实际发送的数据长度，表示成功

- -1：表示失败，并设置errno以指示错误类型

## 错误码

|错误码|描述|
|--|--|
|EAGAINEWOULDBLOCK|套接字被标记为非阻塞，请求的操作将阻塞。|
|ECONNRESET|连接被对端终止。|
|EDESTADDRREQ|套接字不是连接模式，也没有设置对端地址。|
|EFAULT|参数在用户的地址空间之外。|
|EINTR|被信号中断。|
|EMSGSIZE|消息太大，无法一次性全部发送，或者消息指向的msghdr结构体中的msg_iovlen成员大于{IOV_MAX}。|
|ENOMEM|内存申请失败。|
|ENOTCONN|套接字未连接。|
|EPIPE|套接字关闭写入，或者套接字为连接模式，不再连接。|
|EOPNOTSUPP|flags参数中的某些位不适合套接字类型，当前只支持MSG_DONTWAIT。|
|ENETUNREACH|网络上暂无可用路由。|
|EAFNOSUPPORT|所指定的地址对于所指定套接字的地址族而言，不是一个有效的地址。|
|EINVAL|入参msg->msg_iov.iov_len大于SSIZE_MAX，或者入参msg中每个msg_iov的iov_len之和大于SSIZE_MAX。|
|ENETDOWN|用于到达目的地的本地网络接口已关闭。|
|ELIBBAD|系统符号加载失败。|
|EINVAL|套接字存在，但是套接字对应的数据结构存在异常。|
|EINVAL|共线程部署模式时，sockfd的worker id和当前线程的worker id不一致，即socket跨worker线程调用。|
