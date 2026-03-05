# pselect

## 接口名称

**pselect(int nfds, fd\_set \*readfds, fd\_set \*writefds, fd\_set \*exceptfds, const struct timespec \*timeout, const sigset\_t \*sigmask\)**

## 接口描述

一段指定时间内，侦听用户感兴趣的文件描述符的可读、可写和异常事件。同时，允许程序在调用pselect前禁止递交某些信号。

## 参数说明

|参数|说明|备注|
|--|--|--|
|nfds|集合中所有文件描述符的范围，即所有文件描述符的最大值加1|最大值1024。|
|*readfds|待监测读取操作的文件描述符集合|支持为NULL。包含了需要检查是否可读的描述符，输出时表示哪些描述符可读。|
|*writefds|待监测写入操作的文件描述符集合|支持为NULL。包含了需要检查是否可写的描述符，输出时表示哪些描述符可写。|
|*exceptfds|待监测异常条件的文件描述符集合|支持为NULL。包含了需要检查是否出错的描述符，输出时表示哪些描述符出错。|
|*timeout|一个时间结构体，用来设置超时时间|仅支持到ms级别精度。为NULL时，为阻塞永久等待。|
|*sigmask|调用期间应该被阻塞的信号集|支持为NULL。|

## 返回值

类型：int

- 正数：就绪的文件描述符的个数，表示成功
- 0：表示在达到超时之前，没有准备好的文件描述符
- -1：表示失败，并设置errno以指示错误类型

## 错误码

|错误码|描述|
|--|--|
|EINTR|被信号中断。|
|EINVAL|入参timeout无效；入参nfds小于0或大于FD_SETSIZE。|
|ENOMEM|mbuf内存申请失败。|
|ELIBBAD|系统符号加载失败。|
|ENOMEM|系统内存申请失败。|
