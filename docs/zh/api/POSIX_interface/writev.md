# writev

## 接口名称

**writev(int fd, const struct iovec \*iov, int iovcnt\)**

## 接口描述

写入数据到指定文件描述符。

> **说明：** 
>K-NET用户态TCP/IP协议栈数据发送端支持TCP协议优雅断链，当对端异常关闭时，发送端需要借用TCP重传超时机制/保活机制断链。

## 参数说明

|参数|说明|备注|
|--|--|--|
|fd|文件描述符|传入socket描述符。|
|*iov|iov指针|调用者保证长度的有效性，待发送数据总长度可设置范围为(0, SSIZE_MAX]。|
|iovcnt|iov个数|范围[0, 1024]。|


## 返回值

类型：ssize\_t

-   非负数：实际写入的数据长度，表示成功

-   -1：表示失败，并设置errno以指示错误类型

## 错误码

|错误码|描述|
|--|--|
|EINVAL|入参iovcnt小于0，或大于1024；入参iov中iov_len之和大于SSIZE_MAX。|
|EFAULT|入参iovcnt不为0时，iov为空指针。|
|EDESTADDRREQ|套接字不是连接模式，也没有设置对端地址。|
|ENOTCONN|套接字未连接。|
|EMSGSIZE|入参iov结构体中的iov_len之和大于65507。|
|ECONNRESET|连接被对端终止。|
|EAGAIN orEWOULDBLOCK|套接字被标记为非阻塞，写操作将阻塞。|
|EPIPE|套接字关闭写入，或者套接字为连接模式，不再连接。|
|EINTR|被信号中断。|
|ENETDOWN|用于到达目的地的本地网络接口已关闭。|
|ENOMEM|内存申请失败。|
|ELIBBAD|系统符号加载失败。|
|EINVAL|套接字存在，但是套接字对应的数据结构存在异常。|
|EINVAL|共线程部署模式时，sockfd的worker id和当前线程的worker id不一致，即socket跨worker线程调用。|


