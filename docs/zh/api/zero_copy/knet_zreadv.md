# knet\_zreadv

## 接口名称

**knet\_zreadv(int sockfd, struct knet\_iovec \*iov, int iovcnt\)**

## 接口描述

零拷贝读。

## 参数说明

|参数|说明|备注|
|--|--|--|
|sockfd|socket（套接字）文件描述符|支持tcp、阻塞模式和非阻塞模式。|
|*iov|读缓冲区iov数组|元素个数不小于iovcnt；应用不需要申请iov中iov_base指向的数据空间；应用无需设置iov_len。使用完后，应用应调用iov的free_cb，释放iov的数据空间。|
|iovcnt|读缓冲区iov数量|取值范围为[0, 1024]。|

## 返回值

类型：ssize\_t

- 非负数：实际读取的数据长度，表示成功
- -1：表示失败，并设置errno以指示错误类型

## 错误码

|错误码|描述|
|--|--|
|EINVAL|入参iovcnt小于0，或大于1024。|
|EFAULT|入参iov为空指针。|
|EINVAL|套接字存在，但是套接字对应的数据结构存在异常。|
|EINVAL|共线程部署模式时，sockfd的worker id和当前线程的worker id不一致，即socket跨worker线程调用。|
|EINTR|收到任何数据之前，函数调用被信号中断。|
|ENOTCONN|连接已关闭。|
|ECONNRESET|连接被拒绝。|
