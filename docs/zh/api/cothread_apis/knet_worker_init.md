# knet_worker_init

介绍K-NET的knet_worker_init接口，面向应用开发工程师。涵盖共线程场景下线程worker初始化的功能说明、调用约束与返回值，适用于共线程部署模式的开发场景。

## 接口名称

**knet_worker_init(void)**

## 接口描述

<term>K-NET</term>共线程时为每个线程进行worker初始化。

> [!NOTE]说明
>
>- 该接口需在执行knet_init后执行。
>- 在每个线程中仅能执行一次。一个进程中的调用次数等于线程数max_worker_num。
>- 在调用前需设置线程的CPU亲和性，与配置文件中的ctrl_vcpu_ids不同。

## 参数说明

无

## 返回值

类型：int

- 0：表示成功。
- -1 ：表示失败。
