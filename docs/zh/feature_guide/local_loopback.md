# 本地流量环回功能

## 功能描述

提供本地流量环回能力，支持K-NET进程与本地进程网络通信的功能。

> [!NOTE]说明 
>
> - 本地回环功能是K-NET内部自动实现的机制，无需用户额外配置。
> - 当K-NET进程bind `0.0.0.0` 时，默认替换为配置文件中的IP地址。
> - 当K-NET进程connect目标IP地址为 `0.0.0.0`、`127.0.0.1` 或配置文件中的本机IP地址时，连接自动转为内核fd，走内核协议栈完成本地通信。
> - K-NET启动时创建tap口并将配置IP地址注册到内核，内核据此识别为本地地址，本地回环才能命中。K-NET退出时tap口自动销毁，本地回环随之失效。
 
## 使用示例

本章示例以iPerf3单为例。

### 示例一：同机进程连接K-NET服务端

K-NET进程作为TCP服务端监听端口时，同一台机器上的其他进程可以直接通过该端口与K-NET进程建立TCP连接，无需额外配置。

以iPerf3为例：

1.K-NET进程启动iPerf3服务端。

   服务端ip以192.168.1.6为例；具体需要替换为网卡配置的IP地址，且与K-NET配置文件中IP地址保持一致。

   ```bash
   LD_PRELOAD=/usr/lib64/libknet_frame.so iperf3 -s -4 -p 10001 --bind 192.168.1.6
   ```

2.同机启动另一个iPerf3客户端，连接K-NET服务端。

   ```bash
   iperf3 -c 192.168.1.6 -p 10001 -t 10
   ```

   客户端与K-NET服务端成功建立连接并打流。

服务端输出结果如下回显：

```bash
.......
Accepted connection from 192.168.1.6, port 36182
[ 70] local 192.168.1.6 port 10001 connected to 192.168.1.6 port 36184
[ ID] Interval           Transfer     Bitrate
[ 70]   0.00-1.00   sec  5.15 GBytes  44.2 Gbits/sec                  
[ 70]   1.00-2.00   sec  5.33 GBytes  45.8 Gbits/sec                  
[ 70]   2.00-3.00   sec  5.32 GBytes  45.7 Gbits/sec                  
[ 70]   3.00-4.00   sec  5.50 GBytes  47.3 Gbits/sec                  
[ 70]   4.00-5.00   sec  4.96 GBytes  42.6 Gbits/sec                  
[ 70]   5.00-6.00   sec  5.33 GBytes  45.8 Gbits/sec                  
[ 70]   5.00-6.00   sec  5.33 GBytes  45.8 Gbits/sec                  
- - - - - - - - - - - - - - - - - - - - - - - - -
[ ID] Interval           Transfer     Bitrate
[ 70]   0.00-6.00   sec  36.3 GBytes  51.9 Gbits/sec                  receiver
-----------------------------------------------------------
Server listening on 10001 (test #2)
-----------------------------------------------------------
```

### 示例二：K-NET服务端 bind `0.0.0.0`，同机客户端连接

K-NET进程 bind `0.0.0.0` 时，内核侧监听所有本地IP地址，同一台机器上的其他进程可直接通过配置IP地址连接。

以iPerf3为例：

1.K-NET进程启动iPerf3服务端，bind `0.0.0.0`。

   ```bash
   LD_PRELOAD=/usr/lib64/libknet_frame.so iperf3 -s -4 -p 10001 --bind 0.0.0.0
   ```

2.同机启动另一个iPerf3客户端，连接配置IP地址。

   ```bash
   iperf3 -c 192.168.1.6 -p 10001 -t 10
   ```

   客户端与K-NET服务端成功建立连接并打流。

服务端输出结果如下回显：
       
```bash
.......
Accepted connection from 192.168.1.6, port 36182
[ 70] local 192.168.1.6 port 10001 connected to 192.168.1.6 port 36184
[ ID] Interval           Transfer     Bitrate
[ 70]   0.00-1.00   sec  5.15 GBytes  44.2 Gbits/sec                  
[ 70]   1.00-2.00   sec  5.93 GBytes  45.8 Gbits/sec                  
[ 70]   2.00-3.00   sec  5.32 GBytes  45.7 Gbits/sec                  
[ 70]   3.00-4.00   sec  5.45 GBytes  45.3 Gbits/sec                  
[ 70]   4.00-5.00   sec  4.96 GBytes  42.6 Gbits/sec                  
[ 70]   5.00-6.00   sec  5.31 GBytes  45.7 Gbits/sec                  
[ 70]   5.00-6.00   sec  5.33 GBytes  45.4 Gbits/sec                  
- - - - - - - - - - - - - - - - - - - - - - - - -
[ ID] Interval           Transfer     Bitrate
[ 70]   0.00-6.00   sec  36.3 GBytes  51.7 Gbits/sec                  receiver
-----------------------------------------------------------
Server listening on 10001 (test #2)
-----------------------------------------------------------
```
