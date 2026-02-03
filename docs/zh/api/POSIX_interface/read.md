# read

## 接口名称

**read(int fd, void \*buf, size\_t count\)**

## 接口描述

读取数据。

## 参数说明

|参数|说明|备注|
|--|--|--|
|fd|文件描述符|传入socket描述符。|
|*buf|接收缓冲区|非空且调用者自行申请需要接收报文长度的内存。|
|count|接收缓冲区长度|调用者保证长度的有效性，待接收数据总长度范围为(0, SSIZE_MAX]。|


## 返回值

类型：ssize\_t

-   正数：实际读取的数据长度，表示成功
-   -1：表示失败，并设置errno以指示错误类型
-   0：表示链路已中断

## 错误码

|错误码|描述|
|--|--|
|EAGAINEWOULDBLOCK|当文件描述符设置O_NONBLOCK标志时，非阻塞读取操作无法立即完成。|
|ECONNRESET|连接被对端终止。|
|ENOTCONN|套接字未连接。|
|EFAULT|入参buf在用户的地址空间之外。|
|EINTR|读取操作被信号中断。|
|ELIBBAD|系统符号加载失败。|
|EINVAL|套接字存在，但是套接字对应的数据结构存在异常。|
|EINVAL|入参count大于SSIZE_MAX。|
|EINVAL|共线程部署模式时，sockfd的worker id和当前线程的worker id不一致，即socket跨worker线程调用。|


