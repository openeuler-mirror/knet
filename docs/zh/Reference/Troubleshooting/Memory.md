# 内存故障

## 现象描述

找不到挂载大页，启动失败，日志报错为DPDK初始化失败：

![](../../Figures/zh-cn_image_0000002535824421.png)

## 原因

未挂载大页。

## 处理步骤

参照[配置大页内存](../../Feature_Guide/Environment_Configuration.md#配置大页内存)确保root用户为系统配置完大页后，当前用户完成挂载大页操作。
