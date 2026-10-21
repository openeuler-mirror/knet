# 网卡队列调优

介绍K-NET的网卡队列调优方法，针对发送队列深度已满导致的丢包问题，说明如何通过日志识别该问题及调整相关配置项进行优化。阅读本指南后，您将能定位此类丢包原因并完成队列深度调整。

在运行环境查看knet_comm.log日志：

```bash
vim /var/log/knet/knet_comm.log
```

如果观察到如下日志信息：

```output
Tx burst total 0 of cnt 1
```

出现此日志的原因是：网卡发送队列深度已满，导致发生丢包。

可尝试增加网卡发送队列深度，可参考[DPDK配置项](../../configuration_reference_items.md)中`tx_cache_size`，调整该值大小使其与并发数量接近。
