# fcntl

## 接口名称

**fcntl(int fd, int cmd, ...\)**

## 接口描述

文件控制。

## 参数说明

|参数|说明|备注|
|--|--|--|
|fd|文件描述符|传入socket描述符。|
|cmd|控制命令|支持F_GETFL、F_SETFL、F_GETFD、F_SETFD。其他控制命令不支持，如FIOREAD和FIOWRITE等。|
|...|可变参数值|支持O_NONBLOCK、FD_CLOEXEC（支持SETFD，不起实际作用），O_RDONLY（支持SETFL，不起实际作用）、O_WRONLY（支持SETFL，不起实际作用）、O_RDWR（支持SETFL，不起实际作用）。|

## 返回值

类型：int

- 0：表示成功
- -1：表示失败，并设置errno以指示错误类型

## 错误码

|错误码|描述|
|--|--|
|EINVAL|入参arg大于INT32_MAX或小于0|
|EINVAL|入参cmd不支持，当前仅支持F_GETFL、F_SETFL、F_GETFD和F_SETFD。|
|ELIBBAD|系统符号加载失败。|
|EINVAL|套接字存在，但是套接字对应的数据结构存在异常。|
