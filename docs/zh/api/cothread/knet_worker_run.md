# knet\_worker\_run

## 接口名称

**knet\_worker\_run(void\)**

## 接口描述

<term>K-NET</term>共线程worker运行。

> **说明：** 
>
>- 每个线程中需在knet\_worker\_init后执行。
>- 需保证此接口持续循环调用。
>- 在关闭文件描述符（fd）断链时，需要再次调用此接口，才会发送断链报文。

## 参数说明

无

## 返回值

无
