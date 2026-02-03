# ppoll

## 接口名称

**ppoll(struct pollfd \*fds, nfds\_t nfds, const struct timespec \*timeout\_ts, const sigset\_t \*sigmask\)**

## 接口描述

等待在异步描述符上事件触发。

## 参数说明

|参数|说明|备注|
|--|--|--|
|*fds|侦听的文件描述符指针|-|
|nfds|文件描述符个数|-|
|*timeout_ts|阻塞等待文件描述符的超时时间间隔|ms级精度。|
|*sigmask|需要执行信号屏蔽操作|-|


## 返回值

类型：int

-   正数：有事件触发或者错误上报的socket描述符个数，表示成功
-   0：调用超时没有文件描述符就绪
-   -1：表示失败，并设置errno以指示错误类型

## 错误码

|错误码|描述|
|--|--|
|EINVAL|入参timeout_ts无效。|
|EINVAL|入参nfds无效。|
|EINVAL|共线程部署模式时，sockfd的worker id和当前线程的worker id不一致，即socket跨worker线程调用。|
|EFAULT|入参fds为空。|
|EMFILE|进程的file数量超过max_tcpcb。|
|ENFILE|系统的file数量超过上限。|
|ENOSPC|尝试在主机上注册（EPOLL_CTL_ADD）新文件描述符时遇到/proc/sys/fs/epoll/max_user_watches施加的限制。|
|EEXIST|入参fds中的fd已经被注册（EPOLL_CTL_ADD）。|
|EINVAL|入参fds中的fd与内部创建的fd相同。|
|ENOMEM|mbuf内存申请失败。|
|ENOMEM|系统内存申请失败。|
|EINTR|被信号中断。|
|ELIBBAD|系统符号加载失败。|
|EPERM|信号退出流程中，不允许调用该函数接口。|


