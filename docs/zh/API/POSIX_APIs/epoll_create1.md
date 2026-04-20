# epoll\_create1

## 接口名称

**epoll\_create1(int flags)**

## 接口描述

创建epoll文件描述符，并设置其标志位。

## 参数说明

|参数|说明|备注|
|--|--|--|
|flags|标志位|支持标志位0，EPOLL_CLOEXEC。|

## 返回值

类型：int

- 非负数：epoll句柄，表示成功
- -1：表示失败，并设置errno以指示错误类型

## 错误码

|错误码|描述|
|--|--|
|ENAVAIL|KNET资源初始化失败。|
|EINVAL|存在除EPOLL_CLOEXEC以外的标志位。|
|EINVAL|共线程部署模式时，sockfd的worker id和当前线程的worker id不一致，即socket跨worker线程调用。|
|ENOMEM|系统内存申请失败。|
|ENOMEM|mbuf内存申请失败。|
|EMFILE|进程的file数量超过max_tcpcb。|
|ENFILE|系统的file数量超过上限。|
|ENOSPC|尝试在主机上注册（EPOLL_CTL_ADD）新文件描述符时遇到/proc/sys/fs/epoll/max_user_watches限制。|
|EIO|读取或写入文件系统时发生I/O错误。|
|EINTR|被信号中断。|
|ELIBBAD|系统符号加载失败。|
|EPERM|信号退出流程中，不允许调用该函数接口。|
