# knet_is_worker_thread

介绍K-NET的knet_is_worker_thread接口，面向应用开发工程师。接口用于在共线程模式下确定当前线程是否为worker线程，涵盖参数说明及返回值。适用于共线程部署场景下的线程角色判断。

## 接口名称

**knet_is_worker_thread(void)**

## 接口描述

共线程模式下，确定当前线程是否为worker线程。

## 参数说明

无

## 返回值

类型：int

- 0：表示是worker线程。
- -1 ：表示不是worker线程。
