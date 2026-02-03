# shutdown

## 接口名称

**shutdown(int sockfd, int how\)**

## 接口描述

根据指定的关闭方式关闭socket连接。

## 参数说明

|参数|说明|备注|
|--|--|--|
|sockfd|通信节点描述符|传入socket描述符。|
|how|描述符关闭方式|支持SHUT_RD、SHUT_WR、SHUT_RDWR。|


## 返回值

类型：int

-   0：表示成功
-   -1：表示失败，并设置errno以指示错误类型

## 错误码

|错误码|描述|
|--|--|
|EINVAL|参数how非法，仅支持SHUT_RD、SHUT_WR、SHUT_RDWR。|
|ENOTCONN|套接字未连接。|
|ELIBBAD|系统符号加载失败。|
|EINVAL|套接字存在，但是套接字对应的数据结构存在异常。|
|EINVAL|共线程部署模式时，sockfd的worker id和当前线程的worker id不一致，即socket跨worker线程调用。|


