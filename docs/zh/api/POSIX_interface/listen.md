# listen

## 接口名称

**listen(int sockfd, int backlog\)**

## 接口描述

侦听通信连接。

## 参数说明

|参数|说明|备注|
|--|--|--|
|sockfd|通信节点描述符|socket类型为SOCK_STREAM。需要先bind地址。不允许对同一个socket重复调用。|
|backlog|侦听socket可以容纳的最大长度|如果设置值小于4，则内部默认设置为4，如果设置值大于4096，则设置为4096，否则使用设置的值。|


## 返回值

类型：int

-   0：表示成功
-   -1：表示失败，并设置errno以指示错误类型

## 错误码

|错误码|描述|
|--|--|
|EOPNOTSUPP|套接字类型不支持侦听。|
|EDESTADDRREQ|套接字没有绑定本地地址，协议不支持侦听非绑定套接字。|
|EINVAL|套接字已连接。|
|EINVAL|套接字已被shutdown。|
|ELIBBAD|系统符号加载失败。|
|EINVAL|套接字存在，但是套接字对应的数据结构存在异常。|


