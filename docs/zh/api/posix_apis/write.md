# write

## 接口名称

**write(int fd, const void \*buf, size\_t count\)**

## 接口描述

写入数据到指定文件描述符。

> **说明：** 
>K-NET用户态TCP/IP协议栈数据发送端支持TCP协议优雅断链，当对端异常关闭时，发送端需要借用TCP重传超时机制/保活机制断链。

## 参数说明

|参数|说明|备注|
|--|--|--|
|fd|文件描述符|传入socket描述符。|
|*buf|发送缓冲区地址|非空且调用者自行申请需要发送报文长度的内存。|
|count|即将发送的数据长度|调用者保证长度的有效性，待发送数据总长度范围为(0, SSIZE_MAX]。|

## 返回值

类型：ssize\_t

- 非负数：实际写入的数据长度，表示成功
- -1：表示失败，并设置errno以指示错误类型

## 错误码

|错误码|描述|
|--|--|
|EAGAINEWOULDBLOCK|套接字被标记为非阻塞，写操作将阻塞。|
|EDESTADDRREQ|套接字不是连接模式，也没有设置对端地址。|
|EINTR|被信号中断。|
|ECONNRESET|连接被对端终止。|
|EPIPE|套接字关闭写入，或者套接字为连接模式，不再连接。|
|EFAULT|入参count不为0时，参数在用户的地址空间之外。|
|ENETDOWN|用于到达目的地的本地网络接口已关闭。|
|ELIBBAD|系统符号加载失败。|
|EINVAL|入参count大于SSIZE_MAX。|
|EINVAL|套接字存在，但是套接字对应的数据结构存在异常。|
|EINVAL|共线程部署模式时，sockfd的worker id和当前线程的worker id不一致，即socket跨worker线程调用。|
|ENOMEM|内存申请失败。|
