# close

## 接口名称

**close(int fd)**

## 接口描述

关闭文件描述符，释放对应资源。

## 参数说明

|参数|说明|备注|
|--|--|--|
|fd|文件描述符|传入socket描述符或者Epoll描述符。|

## 返回值

类型：int

- 0：表示成功
- -1：表示失败，并设置errno以指示错误类型

## 错误码

|错误码|描述|
|--|--|
|EIO|读取或写入文件系统时发生I/O错误。|
|EINTR|被信号中断。|
|ELIBBAD|系统符号加载失败。|
|EINVAL|共线程部署模式时，sockfd的worker id和当前线程的worker id不一致，即socket跨worker线程调用。|
