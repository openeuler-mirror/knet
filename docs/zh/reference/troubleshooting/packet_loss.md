# 丢包故障

## 大流量下丢包，telemetry出现imissed字段非零
### 现象描述

在大流量场景下，比如Redis 10k，执行`dpdk-telemetry.py -f knet -i 1`，输入：

```
/ethdev/stats,0
```
示例回显如下：
```
{"/ethdev/stats": {"ipackets": 2392249, "opackets": 2367358, "ibytes": 242181285, "obytes": 177452921, "imissed": 21168, "ierrors": 0, "oerrors": 0, "rx_nombuf": 0, "q_ipackets": [2392249, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "q_opackets": [2367358, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "q_ibytes": [242181285, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "q_obytes": [177452921, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "q_errors": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]}}
```
观测到"imissed"字段持续增长，便已经出现丢包。

### 原因

网卡资源不足出现丢包。

### 操作步骤

将配置文件knet_comm.conf中"tx_cache_size"与"rx_cache_size"进行调整，在原本数据上继续增大队列深度。