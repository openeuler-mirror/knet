# POSIX接口列表

| API名称 | 说明  |
|----|-------|
| [socket](socket.md)| 创建通信节点并返回描述符。|
| [listen](listen.md)| 侦听通信连接。|
| [bind](bind.md)| 给socket绑定地址信息。|
| [connect](connect.md)| 发起socket连接。|
| [getpeername](getpeername.md)| 获取socket连接对端地址信息。|
| [getsockname](getsockname.md)| 获取socket连接地址信息。|
| [send](send.md)|发送数据。 |
| [sendto](sendto.md)| 发送数据。|
| [writev](writev.md)|写入数据到指定文件描述符。 |
| [sendmsg](sendmsg.md)| 发送数据。|
| [recv](recv.md)| 接收数据。|
| [recvfrom](recvfrom.md)| 接收数据。|
| [recvmsg](recvmsg.md)| 接收数据。|
| [readv](readv.md)| 从文件描述符读取数据到缓冲区。|
| [getsockopt](getsockopt.md)| 获取socket选项值。|
| [setsockopt](setsockopt.md)| 设置socket选项值。|
| [accept](accept.md)| 在侦听socket接收新的连接。|
| [accept4](accept4.md)| 在侦听socket上接收新的连接，并设置socketFd属性。|
| [close](close.md)| 关闭文件描述符，释放对应资源。|
| [shutdown](shutdown.md)| 根据指定的关闭方式关闭socket连接。|
| [read](read.md)| 读取数据。|
| [write](write.md)|写入数据到指定文件描述符。 |
| [epoll_create](epoll_create.md)|创建epoll文件描述符（epoll file descriptor）。 |
| [epoll_create1](epoll_create1.md)| 创建epoll文件描述符，并设置其标志位。|
| [epoll_ctl](epoll_ctl.md)| 根据选项给epoll文件描述符配置对应的事件信息。|
| [epoll_wait](epoll_wait.md)| 在epoll文件描述符上等待I/O事件。|
| [epoll_pwait](epoll_pwait.md)| 在epoll文件描述符上等待I/O事件。与epoll_wait相比可以设置屏蔽的信号。|
| [fcntl](fcntl.md)| 文件控制。|
| [fcntl64](fcntl64.md)| 文件控制。|
| [poll](poll.md)| 等待异步描述符上事件触发。|
| [ppoll](ppoll.md)| 等待在异步描述符上事件触发。|
| [ioctl](ioctl.md)|设备驱动程序中对设备的I/O通道进行管理的函数。 |
| [select](select.md)| 一段指定时间内，侦听用户感兴趣的文件描述符的可读、可写和异常事件。|
| [pselect](pselect.md)| 一段指定时间内，侦听用户感兴趣的文件描述符的可读、可写和异常事件。同时，允许程序在调用pselect前禁止递交某些信号。|
| [signal](signal.md)| 设置信号处理方式。|
| [sigaction](sigaction.md)| 设置信号处理方式。|
| [fork](fork.md)| 创建子进程。|
