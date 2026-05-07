# connect

## 接口名称

**connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)**

## 接口描述

发起socket连接。

> **说明：** 
>允许Bind已存在连接的地址，如果Bind到已存在的本地连接地址，在Connect到同一个对端时会返回失败。

## 参数说明

|参数|说明|备注|
|--|--|--|
|sockfd|通信节点描述符|传入socket描述符。|
|*addr|通信对端地址|支持IPv4地址，类型为struct sockaddr_in。|
|addrlen|地址长度|地址长度，必须与第二个参数的地址长度一致。|

## 返回值

类型：int

- 0：成功
- -1：表示失败，并设置errno以指示错误类型

## 错误码

|错误码|描述|
|--|--|
|EADDRINUSE|地址已被使用。|
|EADDRNOTAVAIL|套接字未绑定地址，且试图绑定临时端口时，当前暂无可用临时端口号。|
|EAFNOSUPPORT|所指定的地址对于所指定套接字的地址族而言，不是一个有效的地址。|
|EALREADY|套接字是非阻塞的，并且之前的连接尝试尚未完成。|
|ECONNREFUSED|目标地址未侦听连接或拒绝连接请求。|
|EINVAL|addrlen参数不是地址族的有效长度。|
|EFAULT|套接字结构地址在用户的地址空间之外。|
|EINPROGRESS|套接字是非阻塞的，连接不能立即完成。|
|EISCONN|套接字已连接。|
|ETIMEDOUT|尝试连接时超时。|
|ENETUNREACH|网络上暂无可用路由。|
|EINTR|被信号中断。|
|EOPNOTSUPP|套接字正在被侦听，无法连接。|
|ENETDOWN|用于到达目的地的本地网络接口已关闭。|
|ELIBBAD|系统符号加载失败。|
|EINVAL|套接字存在，但是套接字对应的数据结构存在异常。|
|EINVAL|共线程部署模式时，sockfd的worker id和当前线程的worker id不一致，即socket跨worker线程调用。|
