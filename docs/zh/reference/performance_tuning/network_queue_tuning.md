# 网卡队列调优

在运行环境查看knet\_comm.log日志：

```bash
vim /etc/knet/knet_comm.log
```

如果观察到如下日志信息：

```ColdFusion
Tx burst total 0 of cnt 1
```

出现此日志的原因是：网卡发送队列深度已满，导致发生丢包。

可尝试增加网卡发送队列深度，即[DPDK配置项](../../configuration_reference_items.md)中tx\_cache\_size大小使其与并发数量接近。
