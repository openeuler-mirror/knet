# epoll\_pwait

## 接口名称

**epoll\_pwait(int epfd, struct epoll\_event \*events, int maxevents, int timeout, const sigset\_t \*sigmask)**

## 接口描述

在epoll文件描述符上等待I/O事件。与epoll\_wait相比可以设置屏蔽的信号。

## 参数说明

|参数|说明|备注|
|--|--|--|
|epfd|epoll文件描述符|-|
|*events|文件句柄需要感知的事件|events值作为出参，会覆盖数组的原数据内容，如后续需使用原数据，必须先保存到其他地方。|
|maxevents|可以获取的最大的事件个数|取值范围为[1，0xFFFFFFFF/sizeof(epoll_event)]，请用户确保此参数比pstEvents数组个数要小。|
|timeout|阻塞场景设置的超时时间间隔|超时值。<li>非阻塞模式：0 ，获取不到事件也立即返回。</li><li>阻塞模式：超时等待，[1, 0x7FFFFFFF]，单位为毫秒，指定等待时间，如果指定时间内获取不到事件，也立即返回。</li><li>无超时等待，-1，不限制等待时间，只有获取到事件才返回。</li>|
|*sigmask|需要屏蔽信号的掩码，可以避免wait时被信号打断。|允许为NULL。|

## 返回值

类型：int

- 非负数：产生事件的fd个数，表示成功

- -1：表示失败，并设置errno以指示错误类型

## 错误码

|错误码|定义|
|--|--|
|EINVAL|入参events为空或者maxevents非法。|
|EINVAL|共线程部署模式时，sockfd的worker id和当前线程的worker id不一致，即socket跨worker线程调用。|
|ENOMEM|内存申请失败。|
|EFAULT|event非法或不能写。|
|ELIBBAD|系统符号加载失败。|
