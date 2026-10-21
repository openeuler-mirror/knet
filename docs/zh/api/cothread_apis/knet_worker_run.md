# knet_worker_run

介绍K-NET的knet_worker_run接口，用于共线程worker运行。说明接口的调用约束与断链报文发送时机，本接口无参数与返回值。适用于基于K-NET共线程模式进行业务开发的应用场景。

## 接口名称

**knet_worker_run(void)**

## 接口描述

<term>K-NET</term>共线程worker运行。

> [!NOTE]说明
>
>- 每个线程中需在knet_worker_init后执行。
>- 需保证此接口持续循环调用。
>- 在关闭文件描述符（fd）断链时，需要再次调用此接口，才会发送断链报文。

## 参数说明

无

## 返回值

无
