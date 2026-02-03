# accept

## 接口名称

**accept(int sockfd, struct sockaddr \*addr, socklen\_t \*addrlen\)**

## 接口描述

在侦听socket接收新的连接。

## 参数说明

|参数|说明|备注|
|--|--|--|
|sockfd|通信节点描述符|必须是侦听socket。|
|*addr|通信对端地址|可以为空指针。会填写为对端socket的地址，长度根据协议地址类型而定，参考socket接口支持的协议。|
|*addrlen|地址长度|可以为空指针。必须设置为可以包含地址结构体的长度，函数返回时设置为实际的长度。|


## 返回值

类型：int

-   非负数：接收的新socket，表示成功
-   -1：表示失败，并设置errno以指示错误类型

## 错误码

|错误码|描述|
|--|--|
|EAGAINEWOULDBLOCK|套接字标记为非阻塞，并且不存在要接收的连接。|
|EINTR|被信号中断。|
|EINVAL|入参addrlen的值小于对应地址族长度或大于INT_MAX，或者未侦听，导致套接字不能建立连接。|
|EOPNOTSUPP|引用的套接字不是TCP类型。|
|EACCES|进程没有适当的权限。|
|ENOBUFS|系统中可用资源不足。|
|ENOMEM|内存申请失败。|
|EMFILE|进程的file数量超过max_tcpcb。|
|ELIBBAD|加载系统符号失败。|
|EFAULT|入参addrlen为空且addr不为空。|
|EINVAL|套接字存在，但是套接字对应的数据结构存在异常。|
|EPERM|信号退出流程中，不允许调用该函数接口。|
|EINVAL|共线程部署模式时，sockfd的worker id和当前线程的worker id不一致，即socket跨worker线程调用。|


