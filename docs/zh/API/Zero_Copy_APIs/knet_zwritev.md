# knet\_zwritev

## 接口名称

**knet\_zwritev(int sockfd, const struct knet\_iovec \*iov, int iovcnt\);**

## 接口描述

零拷贝写。

## 参数说明

|参数|说明|备注|
|--|--|--|
|sockfd|套接字文件描述符|支持TCP，仅支持非阻塞模式。|
|*iov|写缓冲区iov数组|元素个数不小于iovcnt，iov的iov_base需要由knet_mp_alloc申请而来，iov的free_cb不能为空，若写失败，用户需要手动调用iov的free_cb，保证写缓冲区的正常释放。|
|iovcnt|写缓冲区iov数量|取值范围为[0, 1024]。|

## 返回值

类型：ssize\_t

- 非负数：实际写入的数据长度，表示成功
- -1：表示失败，并设置errno以指示错误类型

## 错误码

|错误码|描述|
|--|--|
|EINVAL|入参iovcnt小于0，或大于1024。|
|EFAULT|入参iov为空指针。|
|EINVAL|套接字存在，但是套接字对应的数据结构存在异常。|
|EINVAL|共线程部署模式时，sockfd的worker id和当前线程的worker id不一致，即socket跨worker线程调用|
|EINVAL|iov的iov_len小于0；入参iov中iov_len之和大于SSIZE_MAX。|
|EFAULT|输入iov的成员knet_iov_free_cb_t free_cb为空。|
|EINTR|收到任何数据之前，函数调用被信号中断。|
|ENOTCONN|连接已关闭。|
|ECONNRESET|连接被拒绝。|
|EPIPE|套接字关闭写入，或者套接字为连接模式，不再连接。|
|ENETDOWN|用于到达目的地的本地网络接口已关闭。|
|ENOMEM|内存申请失败。|
|EAGAIN|发送缓冲区不足。|
