# ioctl

介绍K-NET的ioctl接口，面向应用开发工程师。接口用于对设备的I/O通道进行管理，涵盖参数说明、返回值及错误码。适用于业务开发中配置和查询设备选项的场景。

## 接口名称

**ioctl(int fd, unsigned long request, ...)**

## 接口描述

设备驱动程序中对设备的I/O通道进行管理的函数。

## 参数说明

|参数|说明|备注|
|--|--|--|
|fd|文件描述符|传入socket描述符。|
|request|要设置的选项|支持FIONBIO、FIONREAD、SIOCETHTOOL、SIOCGIFCONF、SIOCGIFFLAGS、SIOCGIFNETMASK、SIOCGIFINDEX、SIOCGIFHWADDR、SIOCGIFBRDADDR，其他不支持。|
|...|可变参数|实际类型与request相关。|

## 返回值

类型：int

- 0：表示成功
- -1：表示失败，并设置errno以指示错误类型

## 错误码

|错误码|描述|
|--|--|
|EFAULT|入参argp在用户的地址空间之外。|
|EINVAL|入参request不支持，当前支持FIONBIO、FIONREAD、SIOCETHTOOL、SIOCGIFCONF、SIOCGIFFLAGS、SIOCGIFNETMASK、SIOCGIFINDEX、SIOCGIFHWADDR、SIOCGIFBRDADDR，其他不支持。|
|EINVAL|共线程部署模式时，socket描述符的worker id和当前线程的worker id不一致，即socket跨worker线程调用。|
|ELIBBAD|系统符号加载失败。|
|EINVAL|套接字存在，但是套接字对应的数据结构存在异常。|
