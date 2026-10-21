# signal

介绍K-NET的signal接口，面向应用开发工程师。涵盖设置信号处理方式的功能、参数说明、返回值及错误码，适用于为进程注册信号处理函数的开发场景。

## 接口名称

**signal(int signum, sighandler_t handler)**

## 接口描述

 设置信号处理方式。

## 参数说明

|参数|说明|备注|
|--|--|--|
|signum|要检查或修改的信号编号|支持所有信号；注册SIGINT、SIGQUIT和SIGTERM三种信号的处理函数不支持重入。|
|handler|要设置的对信号的新处理方式|支持所有的处理方式；支持为NULL。|

## 返回值

类型：sighandler_t

- 对信号的旧处理方式：表示成功
- SIG_ERR：表示失败，并设置errno以指示错误类型

## 错误码

|错误码|描述|
|--|--|
|EINVAL|入参signum为无效信号。|
