# getpeername

## 接口名称

**getpeername(int sockfd, struct sockaddr \*addr, socklen\_t \*addrlen\)**

## 接口描述

获取socket连接对端地址信息。

## 参数说明

|参数|说明|备注|
|--|--|--|
|sockfd|通信节点描述符|传入socket描述符。|
|*addr|通信对端地址|函数正常返回时填充地址信息，即获取到的地址信息存储在该参数中。|
|*addrlen|地址长度|输入的地址长度必须足够存放地址信息。函数正常返回时修改为实际的地址长度。|

## 返回值

类型：int

- 0：表示成功
- -1：表示失败，并设置errno以指示错误类型

## 错误码

|错误码|描述|
|--|--|
|EFAULT|套接字结构地址在用户的地址空间之外。|
|EINVAL|入参addrlen不是地址族的有效长度，入参addrlen大于INT_MAX，或套接字已被shutdown。|
|ENOTCONN|套接字未连接。|
|ELIBBAD|系统符号加载失败。|
|EINVAL|套接字存在，但是套接字对应的数据结构存在异常。|
|EINVAL|共线程部署模式时，sockfd的worker id和当前线程的worker id不一致，即socket跨worker线程调用。|
