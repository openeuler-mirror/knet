# knet\_worker\_init

## 接口名称

**knet\_worker\_init(void\)**

## 接口描述

<term>K-NET</term>共线程时为每个线程进行worker初始化。

> **说明：** 
>
>- 该接口需在执行knet\_init后执行。
>- 在每个线程中仅能执行一次。一个进程中的调用次数等于线程数max\_worker\_num。
>- 在调用前需设置线程的CPU亲和性，与配置文件中的ctrl\_vcpu\_ids不同。

## 参数说明

无

## 返回值

类型：int

- 0：表示成功。
- -1 ：表示失败。
