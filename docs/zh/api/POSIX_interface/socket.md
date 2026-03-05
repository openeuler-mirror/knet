# socket

## 接口名称

**socket(int domain, int type, int protocol\)**

## 接口描述

创建通信节点并返回描述符。

## 参数说明

|参数|说明|备注|
|--|--|--|
|domain|通信域|支持AF_INET。|
|type|通信语义类型|支持SOCK_STREAM、SOCK_DGRAM、SOCK_CLOEXEC (默认支持该type，且不可修改)、SOCK_NONBLOCK。|
|protocol|协议类型|支持IPPROTO_TCP（type必须为SOCK_STREAM）。支持IPPROTO_UDP（type必须为SOCK_DGRAM）。支持IPPROTO_IP，即设置为0。|

## 返回值

类型：int

- 正数：socketfd，表示成功
- -1：表示失败，并设置errno以指示错误类型

## 错误码

|错误码|描述|
|--|--|
|EINVAL|未知协议，或协议族不可用。|
|EMFILE|进程的file数量超过max_tcpcb。|
|ENOMEM|内存申请失败。|
|EPROTONOSUPPORT|domain不支持指定协议。|
|ENAVAIL|KNET资源初始化失败。|
|EACCES|进程没有适当的权限。|
|ENOBUFS|系统中的可用资源不足，无法执行该操作。|
|EMFILE|进程的file数量超过最大文件描述符数。通过ulimit -n查看。|
|ENFILE|系统的file数量超过上限。|
|ELIBBAD|系统库符号加载失败。|
|EPERM|信号退出流程中，不允许调用该函数接口。|
|EINVAL|共线程部署模式时，sockfd的worker id和当前线程的worker id不一致，导致socket跨worker线程调用。|
