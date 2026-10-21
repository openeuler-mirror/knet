# epoll_ctl

介绍K-NET的epoll_ctl接口，面向应用开发工程师。涵盖给epoll文件描述符配置事件信息的功能、参数说明、返回值及错误码，并说明支持的操作与事件类型约束。适用于事件注册与管理场景。

## 接口名称

**epoll_ctl(int epfd, int op, int fd, struct epoll_event \*event)**

## 接口描述

根据选项给epoll文件描述符配置对应的事件信息。

## 参数说明

|参数|说明|备注|
|--|--|--|
|epfd|epoll文件描述符|-|
|op|添加选项方式|支持EPOLL_CTL_ADD、EPOLL_CTL_MOD、EPOLL_CTL_DEL。|
|fd|需要添加到epoll的文件句柄|不支持将一个fd放入不同的epoll中同时监测。|
|*event|文件句柄需要感知的事件|支持EPOLLIN、EPOLLOUT、EPOLLERR、EPOLLHUP、EPOLLRDHUP、EPOLLONESHOT。|

## 返回值

类型：int

- 0：表示成功
- -1：表示失败，并设置errno以指示错误类型

## 错误码

|错误码|定义|
|--|--|
|EINVAL|入参event无效。|
|EINVAL|共线程部署模式时，fd的worker id和当前线程的worker id不一致，即socket跨worker线程调用。|
|EEXIST|入参op为EPOLL_CTL_ADD时，该fd已经在epoll侦听列表中。|
|EINVAL|入参epfd不是epoll描述符，或者入参fd与epfd相同，或者入参op不在支持范围内。|
|ENOENT|op是EPOLL_CTL_MOD或者EPOLL_CTL_DEL时，fd没有注册epoll。|
|ENOMEM|内存申请失败。|
|EFAULT|入参op为EPOLL_CTL_ADD、EPOLL_CTL_MOD时，入参event为空指针。|
|ELIBBAD|系统符号加载失败。|
