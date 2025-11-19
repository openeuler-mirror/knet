# tperf_knet.patch使用示例

## 简介
tperf_knet.patch基于libtpa原生tperf工具开发，将原生tpa接口转换为标准POSIX接口，并在此基础上，分别适配了K-NET共线程、零拷贝、共线程加零拷贝特性。

## tperf新增/修改命令参数：
* server端：
-n ：运行的线程数，每个线程有各自的listen端口，为-p的端口+线程id。如-p 11111 -n 2，则在11111与11112的端口分别有1个线程进行监听；

* clinet端：
-N ：指服务端启动的线程数，客户端将线程均匀散到服务端的N个线程进行建链，如 -N 2，客户端会将1线程与对端1线程建链，2线程与对端2线程建链；
-P ：客户端每个线程bind的端口，必须指定-l 为本端ip，且-P的个数与-n 线程数保持一致。
-d ：打流时间，为0则不打流；

## 编译
注意：需要安装K-NET支持共线程、零拷贝特性版本后编译和使用。

将patch放到libtpa-main/app下，安装patch：
```
cd libtpa-main/app
patch -p1 -d tperf/ < tperf_knet.patch
```
需安装K-NET支持共线程以及零拷贝版本后编译tperf：
```
cd tperf
make
cd build/bin
```
在build/bin下为4个可执行demo，分别如下：
tperf_os：标准POSIX接口的tperf dmeo；
tperf_knetco：使用K-NET共线程特性的tperf demo；
tperf_knetzcopy:使用K-NET零拷贝特性的tperf demo；
tperf_knetcozocpy:使用K-NET共线程+零拷贝特性的tperf demo。


撤销patch：
```
cd libtpa-main/app
patch -p1 -Rd tperf/ < tperf_knet.patch
```
## 修改双端配置文件
```
vi /etc/knet/knet_comm.conf
```

```
{
    "hw_offload": {
        "tso": 1,
        "lro": 1,
        "tcp_checksum": 1,
        "bifur_enable": 1
     },
    "proto_stack": {
        "max_mbuf": 204800,
        "def_sendbuf": 1048576,
        "def_recvbuf": 1048576,
        "zcopy_sge_len": 4096,
        "zcopy_sge_num": 2097152,
    },
    "dpdk": {
        "tx_cache_size": 1024,
        "rx_cache_size": 1024,
        "socket_mem": "--socket-mem=4096",
        "socket_limit": "--socket-limit=4096",
    }
}
```
## 使用示例
服务端ip以192.168.1.6为例，客户端以192.168.1.7为例；具体需要替换为网卡配置的ip，且与K-NET配置文件中ip保持一致。
### tperf_os
1并发：
```
taskset -c 16-31 ./tperf_os -s -l 192.168.1.6 -p 11111 -n 1 -S 16
taskset -c 16-31 ./tperf_os -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 1 -N 1 -S 16 -t write -d 31
```

2并发:
```
taskset -c 16-31 ./tperf_os -s -l 192.168.1.6 -p 11111 -n 2 -S 16
taskset -c 16-31 ./tperf_os -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 2 -N 2 -S 16 -t write -d 31
```

### KNET无感加速tperf_os
1并发：
```
taskset -c 17-31 env LD_PRELOAD=/usr/lib64/libknet_frame.so ./tperf_os -s -l 192.168.1.6 -p 11111 -n 1 -S 17
taskset -c 16-31 ./tperf_os -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 1 -N 1 -S 16 -t write -d 31
```

2并发:
```
taskset -c 16-31 env LD_PRELOAD=/usr/lib64/libknet_frame.so ./tperf_os -s -l 192.168.1.6 -p 11111 -n 2 -S 16
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
taskset -c 16-31 ./tperf_knetco -s -l 192.168.1.6 -p 11111 -n 1 -S 16
taskset -c 16-31 ./tperf_knetco -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 1 -N 1 -S 16 -t write -d 31 
```
2并发：
修改双端配置文件：
```
"max_worker_num": 2,
"queue_num":2,
```
```
taskset -c 16-31 ./tperf_knetco -s -l 192.168.1.6 -p 11111 -n 2 -S 16
taskset -c 16-31 ./tperf_knetco -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 2 -N 2 -S 16 -t write -d 31 -P 49300,49618
```

### tperf_knetzcopy
修改双端配置文件：
```"zcopy_enable": 1,```

1并发：
```
taskset -c 17-31 ./tperf_knetzcopy -s -l 192.168.1.6 -p 11111 -n 1 -S 17
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
taskset -c 18-31 ./tperf_knetzcopy -s -l 192.168.1.6 -p 11111 -n 2 -S 18
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
taskset -c 16-31 ./tperf_knetcozcopy -s -l 192.168.1.6 -p 11111 -n 1 -S 16
taskset -c 16-31 ./tperf_knetcozcopy -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 1 -N 1 -S 16 -t write -d 31 -P 49631
```
2并发：
双端修改配置文件：
```
"max_worker_num": 2,
"queue_num": 2,
```
```
taskset -c 16-31 ./tperf_knetcozcopy -s -l 192.168.1.6 -p 11111 -n 2 -S 16
taskset -c 16-31 ./tperf_knetcozcopy -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 2 -N 2 -S 16 -t write -d 31 -P 49154,49162
```