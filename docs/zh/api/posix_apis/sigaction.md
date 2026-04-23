# sigaction

## 接口名称

**sigaction(int signum, const struct sigaction \*act, struct sigaction \*oldact\)**

## 接口描述

 设置信号处理方式。

> **说明：** 
>
>- K-NET首先会注册SIGINT、SIGTERM、SIGQUIT三种信号，行为为exit\(0\)，以达到在destructor中断连的目的。如果constructor之后用户有注册相应信号且行为不是SIG\_DFL，则让用户注册覆盖掉。
>- 目前只支持SIGINT、SIGQUIT、SIGTERM三种信号触发时发送RST报文。
>- 调用\_exit等不会执行用户态析构函数的进程退出函数，SIGINT、SIGQUIT、SIGTERM三种信号无法发送RST报文。
>- SIGINT、SIGQUIT、SIGTERM三种信号的处理函数目前不支持重入，只会调用一次。
>- 带SA\_SIGINFO标记的信号处理在处理完后会强制退出程序。

## 参数说明

|参数|说明|备注|
|--|--|--|
|signum|要检查或修改的信号编号。|支持所有信号。|
|*act|指向新的信号处理动作的指针。如果为NULL，将会填充对应信号旧的处理方式。|支持NULL。|
|*oldact|指向用于保存旧的信号处理动作的指针。如果为NULL，则不保存旧的信号处理动作。|支持NULL。|

## 返回值

类型：int

- 0：表示成功
- -1：表示失败，并设置errno以指示错误类型

## 错误码

|错误码|描述|
|--|--|
|EINVAL|入参signum为无效信号。|
|ELIBBAD|系统符号加载失败。|
