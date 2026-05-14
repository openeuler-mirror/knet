# tperf_knet.patch使用示例

## 简介
tperf_knet.patch基于libtpa原生tperf工具开发，将原生tpa接口转换为标准POSIX接口，并在此基础上，分别适配了K-NET共线程、零拷贝、共线程加零拷贝特性。
libtpa源码链接为：[https://github.com/bytedance/libtpa/tree/3c9f05df7b7c8ebc46bfebc83c316ec50f149e1c](https://github.com/bytedance/libtpa/tree/3c9f05df7b7c8ebc46bfebc83c316ec50f149e1c)。
## tperf新增/修改命令参数：
* server端：
-n ：运行的线程数，每个线程有各自的listen端口，为-p的端口+线程id。如-p 11111 -n 2，则在11111与11112的端口分别有1个线程进行监听；

* clinet端：
-N ：指服务端启动的线程数，客户端将线程均匀散到服务端的N个线程进行建链，如 -N 2，客户端会将1线程与对端1线程建链，2线程与对端2线程建链；
-P ：客户端每个线程bind的端口，必须指定-l 为本端ip，且-P的个数与-n 线程数保持一致。
-d ：打流时间，为0则不打流；

## 编译
Tperf的编译及业务配置可参考[TPerf业务配置](../../docs/zh/feature_guide/environment_configuration.md#可选tperf业务配置)。

## 使用示例

服务端ip以192.168.1.6为例，客户端以192.168.1.7为例；具体需要替换为网卡配置的ip，且与K-NET配置文件中ip保持一致。

### tperf_os

1并发：
```
# 服务端
taskset -c 16-31 ./tperf_os -s -l 192.168.1.6 -p 11111 -n 1 -S 16

# 客户端
taskset -c 16-31 ./tperf_os -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 1 -N 1 -S 16 -t write -d 31
```
若执行成功，回显示例如下：

```
# 服务端
listening on 192.168.1.54:11111
Acepted connection: fd = 6, cli_addr=192.168.1.56, cli_port=1446
nr_sock :1

# 客户端
Connection in progress...server port 11111, sockfd 4, cli_port random
Conection established with sockfd 4
    0 W     0.000 read Gbits/sec    11.819 write Gbits/sec
    1 W     0.000 read Gbits/sec    16.347 write Gbits/sec
    2 W     0.000 read Gbits/sec    17.650 write Gbits/sec
    3 W     0.000 read Gbits/sec    17.555 write Gbits/sec
    ...
    29 W    0.000 read Gbits/sec    17.626 write Gbits/sec
    30 W    0.000 read Gbits/sec    17.652 write Gbits/sec

---
 0 nr_conn=1 nr_zero_io_conn=0
```

2并发:
```
# 服务端
taskset -c 16-31 ./tperf_os -s -l 192.168.1.6 -p 11111 -n 2 -S 16

# 客户端
taskset -c 16-31 ./tperf_os -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 2 -N 2 -S 16 -t write -d 31
```

### KNET无感加速tperf_os
1并发：
```
# 服务端
taskset -c 17-31 env LD_PRELOAD=/usr/lib64/libknet_frame.so ./tperf_os -s -l 192.168.1.6 -p 11111 -n 1 -S 17

# 客户端
taskset -c 16-31 ./tperf_os -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 1 -N 1 -S 16 -t write -d 31
```

2并发:
```
# 服务端
taskset -c 16-31 env LD_PRELOAD=/usr/lib64/libknet_frame.so ./tperf_os -s -l 192.168.1.6 -p 11111 -n 2 -S 16

# 客户端
taskset -c 16-31 ./tperf_os -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 2 -N 2 -S 16 -t write -d 31
```

### tperf_knetco：
修改双端配置文件：
```"cothread_enable": 1；```
双端先执行：
```echo "1024 36180" > /proc/sys/net/ipv4/ip_local_port_range```

1并发：
打流命令：
```
# 服务端
taskset -c 16-31 ./tperf_knetco -s -l 192.168.1.6 -p 11111 -n 1 -S 16

# 客户端
taskset -c 16-31 ./tperf_knetco -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 1 -N 1 -S 16 -t write -d 31 
```
2并发：
修改双端配置文件：
```
"max_worker_num": 2,
"queue_num":2,
```
```
# 服务端
taskset -c 16-31 ./tperf_knetco -s -l 192.168.1.6 -p 11111 -n 2 -S 16

# 客户端
taskset -c 16-31 ./tperf_knetco -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 2 -N 2 -S 16 -t write -d 31 -P 49300,49618
```

### tperf_knetzcopy
修改双端配置文件：
```"zcopy_enable": 1,```

1并发：
```
# 服务端
taskset -c 17-31 ./tperf_knetzcopy -s -l 192.168.1.6 -p 11111 -n 1 -S 17

# 客户端
taskset -c 17-31 ./tperf_knetzcopy -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 1 -N 1 -S 17 -t write -d 31 -P 58532
```
2并发：
修改双端配置文件：
```
"max_worker_num": 2,
"core_list_global": "16-17",
"queue_num": 2,
```
```
# 服务端
taskset -c 18-31 ./tperf_knetzcopy -s -l 192.168.1.6 -p 11111 -n 2 -S 18

# 客户端
taskset -c 18-31 ./tperf_knetzcopy -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 2 -N 2 -S 18 -t write -d 31 -P 49452,51507
```
### tperf_knetcozcopy
修改双端配置文件：
```
"zcopy_enable": 1,
"cothread_enable": 1，
```

双端先执行：
```echo "1024 36180" > /proc/sys/net/ipv4/ip_local_port_range```
1并发：
```
# 服务端
taskset -c 16-31 ./tperf_knetcozcopy -s -l 192.168.1.6 -p 11111 -n 1 -S 16

# 客户端
taskset -c 16-31 ./tperf_knetcozcopy -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 1 -N 1 -S 16 -t write -d 31 -P 49631
```
2并发：
双端修改配置文件：
```
"max_worker_num": 2,
"queue_num": 2,
```
```
# 服务端
taskset -c 16-31 ./tperf_knetcozcopy -s -l 192.168.1.6 -p 11111 -n 2 -S 16

# 客户端
taskset -c 16-31 ./tperf_knetcozcopy -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 2 -N 2 -S 16 -t write -d 31 -P 49154,49162
```