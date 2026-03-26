# 共线程故障

## 内核与K-NET端口范围冲突

### 现象描述

启动业务进程失败，knet\_comm.log有如下日志输出：

```ColdFusion
[ERR] K-NET port range [49152, 65024], which may conflict with Kernel local port range [1024, 49999]. Please modify knet_comm.conf or Kernel port range.
```

### 原因

内核和K-NET端口相互冲突。

### 处理步骤

上述问题可以通过两个方法解决：

- 方案1：修改内核端口范围，使其不与K-NET端口范围冲突。

    ```bash
    echo "1024 36180" > /proc/sys/net/ipv4/ip_local_port_range
    ```

- 方案2：修改K-NET配置文件中"min\_port"与"max\_port"配置项，使其不与内核端口范围冲突。

用户可以根据实际情况选择相应的方法，并再次启动业务即可。

## 共线程与内核流量转发同时启用导致内核态应用无法建链、打流

### 现象描述

K-NET配置文件启用如下功能：

```json
 "common": {
    ...
    "cothread_enable": 1
}
"hw_offload": {
    ...
    "bifur_enable": 2
}
"proto_stack": {
    ...
    "max_worker_num": 4,
    ...
}
"dpdk": {
    ...
    "queue_num": 4,
    ...
}
```

启用共线程与内核流量转发功能，且配置4个worker与4个queue。

在服务端启用共线程业务后，又启用内核态业务，以iPerf3为例：

```bash
taskset -c 0-4 iperf3 -s -4 -p 10000 --bind 192.168.*.*
```

在客户端启动iPerf3客户端进行打流，打流无回显：

```bash
taskset -c 32-63  iperf3 -c 192.168.*.* -b 0 -p 10000 -P 1 -t 0 -l 65535
```

过一段时间后客户端回显错误信息：

```ColdFusion
iperf3: error - unable to connect to server - server may have stopped running or use a different port, firewall issue, etc.: Connection timed out
```

### 原因

共线程业务线程的个数小于配置文件中"max\_worker\_num"，使得未使用全部队列，即创建了小于"max\_worker\_num"个数的共线程业务线程。因为共线程模式下仅能在共线程业务线程的队列上收发包，所以内核态的iPerf3应用打流随机散列到了不是worker线程的其他队列上，导致该队列的数据包无法处理，从而内核应用无法建链打流。

### 处理步骤

需要用户保证配置文件中"max\_worker\_num"与实际启用的worker线程数目强一致，即实际使用几个共线程业务线程，"max\_worker\_num"就设置为多少。避免实际使用线程数小于"max\_worker\_num"的情况。
