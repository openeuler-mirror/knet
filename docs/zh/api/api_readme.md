# API 接口

## 摘要
knet库(libknet_frame.so libknet_core.so)，完全实现标准 POSIX 接口，所有 API 的使用方式、参数和返回值均遵循 POSIX 标准规范。

完整的 POSIX 接口文档请参考：
[POSIX.1-2017 标准](https://pubs.opengroup.org/onlinepubs/9699919799/)
[Linux man-pages](https://man7.org/linux/man-pages/)

以下为knet库的接口：
# POSIX接口

-   **[socket](POSIX_interface/socket.md)**  

-   **[listen](POSIX_interface/listen.md)**  

-   **[bind](POSIX_interface/bind.md)**  

-   **[connect](POSIX_interface/connect.md)**  

-   **[getpeername](POSIX_interface/getpeername.md)**  

-   **[getsockname](POSIX_interface/getsockname.md)**  

-   **[send](POSIX_interface/send.md)**  

-   **[sendto](POSIX_interface/sendto.md)**  

-   **[writev](POSIX_interface/writev.md)**  

-   **[sendmsg](POSIX_interface/sendmsg.md)**  

-   **[recv](POSIX_interface/recv.md)**  

-   **[recvfrom](POSIX_interface/recvfrom.md)**  

-   **[recvmsg](POSIX_interface/recvmsg.md)**  

-   **[readv](POSIX_interface/readv.md)**  

-   **[getsockopt](POSIX_interface/getsockopt.md)**  

-   **[setsockopt](POSIX_interface/setsockopt.md)**  

-   **[accept](POSIX_interface/accept.md)**  

-   **[accept4](POSIX_interface/accept4.md)**  

-   **[close](POSIX_interface/close.md)**  

-   **[shutdown](POSIX_interface/shutdown.md)**  

-   **[read](POSIX_interface/read.md)**  

-   **[write](POSIX_interface/write.md)**  

-   **[epoll\_create](POSIX_interface/epoll_create.md)**  

-   **[epoll\_create1](POSIX_interface/epoll_create1.md)**  

-   **[epoll\_ctl](POSIX_interface/epoll_ctl.md)**  

-   **[epoll\_wait](POSIX_interface/epoll_wait.md)**  

-   **[epoll\_pwait](POSIX_interface/epoll_pwait.md)**  

-   **[fcntl](POSIX_interface/fcntl.md)**  

-   **[fcntl64](POSIX_interface/fcntl64.md)**  

-   **[poll](POSIX_interface/poll.md)**  

-   **[ppoll](POSIX_interface/ppoll.md)**  

-   **[ioctl](POSIX_interface/ioctl.md)**  

-   **[select](POSIX_interface/select.md)**  

-   **[pselect](POSIX_interface/pselect.md)**  

-   **[signal](POSIX_interface/signal.md)**  

-   **[sigaction](POSIX_interface/sigaction.md)**  

-   **[fork](POSIX_interface/fork.md)**  

## 共线程接口

-   **[knet\_init](extension_interface/shared_thread/knet_init.md)**  

-   **[knet\_worker\_init](extension_interface/shared_thread/knet_worker_init.md)**  

-   **[knet\_worker\_run](extension_interface/shared_thread/knet_worker_run.md)**  

-   **[knet\_is\_worker\_thread](extension_interface/shared_thread/knet_is_worker_thread.md)**  

## 零拷贝接口

-   **[knet\_iovec](extension_interface/zero_copy/knet_iovec.md)**  

-   **[knet\_zreadv](extension_interface/zero_copy/knet_zreadv.md)**  

-   **[knet\_zwritev](extension_interface/zero_copy/knet_zwritev.md)**  

-   **[knet\_mp\_alloc](extension_interface/zero_copy/knet_mp_alloc.md)**  

-   **[knet\_mp\_free](extension_interface/zero_copy/knet_mp_free.md)**  