# recv

## 接口名称

**recv(int sockfd, void \*buf, size\_t len, int flags\)**

## 接口描述

接收数据。

## 参数说明

|参数|说明|备注|
|--|--|--|
|sockfd|通信节点描述符|传入socket描述符。|
|*buf|接收缓冲区|非空且调用者需自行申请需要接收报文长度的内存。|
|len|接收缓冲区长度|调用者保证长度的有效性，待接收数据总长度可设置范围为(0, SSIZE_MAX]。|
|flags|指定接收标志|支持MSG_PEEK和MSG_DONTWAIT标志位。如果携带有其他flags，直接返回失败。|


## 返回值

类型：ssize\_t

-   正数：实际接收的数据长度，表示成功
-   -1：表示失败，并设置errno以指示错误类型
-   0：表示链路已中断

## 错误码

|错误码|描述|
|--|--|
|EAGAINEWOULDBLOCK|文件描述符设置了O_NONBLOCK标志，读请求被阻塞。|
|ECONNRESET|连接被对端终止。|
|ENOTCONN|套接字未连接。|
|EOPNOTSUPP|flags不支持，当前仅支持MSG_DONTWAIT、MSG_PEEK。|
|EFAULT|入参buf在用户的地址空间之外。|
|EINTR|读取操作被信号中断。|
|EINVAL|入参len大于SSIZE_MAX。|
|ELIBBAD|系统符号加载失败。|
|EINVAL|套接字存在，但是套接字对应的数据结构存在异常。|
|EINVAL|共线程部署模式时，sockfd的worker id和当前线程的worker id不一致，即socket跨worker线程调用。|


