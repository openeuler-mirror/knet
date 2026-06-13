# tperf_knet.patch使用示例

## 简介
tperf_knet.patch基于libtpa原生Tperf工具开发，将原生tpa接口转换为标准POSIX接口，并在此基础上，分别适配了K-NET共线程、零拷贝、共线程加零拷贝特性。
libtpa源码链接为：[https://github.com/bytedance/libtpa/tree/3c9f05df7b7c8ebc46bfebc83c316ec50f149e1c](https://github.com/bytedance/libtpa/tree/3c9f05df7b7c8ebc46bfebc83c316ec50f149e1c)。
## tperf新增/修改命令参数：
server端：

-n ：运行的线程数，每个线程有各自的listen端口，为-p的端口+线程id。如-p 11111 -n 2，则在11111与11112的端口分别有1个线程进行监听；

client端：

-N ：指服务端启动的线程数，客户端将线程均匀散到服务端的N个线程进行建链，如 -N 2，客户端会将1线程与对端1线程建链，2线程与对端2线程建链；

-P ：客户端每个线程bind的端口，必须指定-l 为本端ip，且-P的个数与-n 线程数保持一致。

-d ：打流时间，为0则不打流。

## 编译
Tperf的编译及业务配置可参考[TPerf业务配置](../../docs/zh/feature_guide/environment_configuration.md#可选tperf业务配置)，注意不要遗留前置步骤[配置大页内存](../../docs/zh/feature_guide/environment_configuration.md#配置大页内存)及[通用业务配置](../../docs/zh/feature_guide/environment_configuration.md#通用业务配置)。

## 业务配置
增加大页内存：
> [!NOTE]说明
> 由于tperf零拷贝场景需要在大页中进行pbuf的读写，因此需要参考[配置大页内存](../../docs/zh/feature_guide/environment_configuration.md/#配置NUMA大页)增加大页内存，以20G为例，可根据实际情况分配。
> 以网卡所在node0为例，具体请修改为实际网卡所在NUMA。
```
echo 20 > /sys/devices/system/node/node0/hugepages/hugepages-1048576kB/nr_hugepages
```

## 使用示例

服务端ip以192.168.1.6为例，客户端以192.168.1.7为例；具体需要替换为网卡配置的IP，且与K-NET配置文件中IP保持一致。

> [!NOTE]说明
> 示例运行完成后在服务端按Ctrl+C结束Tperf进程。

### KNET无感加速tperf_os

1并发：

服务端

```bash
taskset -c 17-31 env LD_PRELOAD=/usr/lib64/libknet_frame.so ./tperf_os -s -l 192.168.1.6 -p 11111 -n 1 -S 17
```

客户端：

```bash
taskset -c 16-31 ./tperf_os -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 1 -N 1 -S 16 -t write -d 31
```

服务端回显:

```coldfusion
EAL: Detectd CPU lcores: 128
EAL: Detectd NUMA nodes: 4
EAL: Detectd shared linkage of DPDK
...
...
...
Listening on 192.168.1.6:11111
Accepted connection: fd = 46, cli_addr=192.168.1.7, cli_port=1454
nr_sock :1
```

客户端回显：

```coldfusion
Connection in progress...server prot 11111, sockfd 4, cli_port random
Connection established with sockfd 4
      0 w       0.000 read Gbits/sec  16.424 write Gbits/sec
      1 w       0.000 read Gbits/sec  16.337 write Gbits/sec
      2 w       0.000 read Gbits/sec  16.338 write Gbits/sec
      3 w       0.000 read Gbits/sec  16.368 write Gbits/sec
...
...
...
     30 w       0.000 read Gbits/sec  16.133 write Gbits/sec
---
 0 nr_conn=1 nr_zero_io_conn=0
```

2并发:

服务端:

```bash
taskset -c 16-31 env LD_PRELOAD=/usr/lib64/libknet_frame.so ./tperf_os -s -l 192.168.1.6 -p 11111 -n 2 -S 16
```

客户端:

```bash
taskset -c 16-31 ./tperf_os -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 2 -N 2 -S 16 -t write -d 31
```

服务端回显:

```coldfusion
EAL: Detectd CPU lcores: 128
EAL: Detectd NUMA nodes: 4
EAL: Detectd shared linkage of DPDK
...
...
...
Listening on 192.168.1.6:11111
Listening on 192.168.1.6:11112
Accepted connection: fd = 51, cli_addr=192.168.1.7, cli_port=1456
Accepted connection: fd = 52, cli_addr=192.168.1.7, cli_port=1458
nr_sock :2
```

客户端回显：

```coldfusion
Connection in progress...server prot 11111, sockfd 4, cli_port random
Connection in progress...server prot 11111, sockfd 6, cli_port random
Connection established with sockfd 6
Connection established with sockfd 4
      0 w    0. 0.000 read Gbits/sec  12.845 write Gbits/sec
      0 w    1. 0.000 read Gbits/sec  12.330 write Gbits/sec
      0 w       0.000 read Gbits/sec  25.176 write Gbits/sec

      1 w    0. 0.000 read Gbits/sec  12.827 write Gbits/sec
      1 w    1. 0.000 read Gbits/sec  12.279 write Gbits/sec
      1 w       0.000 read Gbits/sec  25.107 write Gbits/sec

      2 w    0. 0.000 read Gbits/sec  12.822 write Gbits/sec
      2 w    1. 0.000 read Gbits/sec  12.272 write Gbits/sec
      2 w       0.000 read Gbits/sec  25.095 write Gbits/sec
...
...
...
```


### tperf_knetco：
修改双端配置文件：
```"cothread_enable": 1；```
双端先执行：
```echo "1024 36180" > /proc/sys/net/ipv4/ip_local_port_range```

1并发：

服务端打流命令：

```bash
taskset -c 16-31 ./tperf_knetco -s -l 192.168.1.6 -p 11111 -n 1 -S 16
```

客户端命令：

```bash
taskset -c 16-31 ./tperf_knetco -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 1 -N 1 -S 16 -t write -d 31 
```

服务端回显:

```coldfusion
EAL: Detectd CPU lcores: 128
EAL: Detectd NUMA nodes: 4
EAL: Detectd shared linkage of DPDK
...
...
...
Listening on 192.168.1.6:11111
Accepted connection: fd = 42, cli_addr=192.168.1.7, cli_port=49182
nr_sock :1
```

客户端回显：

```coldfusion
EAL: Detectd CPU lcores: 128
EAL: Detectd NUMA nodes: 4
EAL: Detectd shared linkage of DPDK
...
...
...
sp6: Disable allmulticuous succeed, nic_dev: dbdf-000:01:00.5, port_id: 0, promisc: 0
[Client] Thread [281459888938064]: in knet user space thread
sp6: Add fdir tcam rule, fuction_id: 0x22, tcam_block_id: 0, local_index: 0, global_index: 0, queue: 0, tcam_rule_nums: 1 succeed
Connection in progress...server prot 11111, sockfd 4, cli_port random
Connection established with sockfd 4
      0 w       0.000 read Gbits/sec  16.424 write Gbits/sec
      1 w       0.000 read Gbits/sec  16.337 write Gbits/sec
      2 w       0.000 read Gbits/sec  16.338 write Gbits/sec
      3 w       0.000 read Gbits/sec  16.368 write Gbits/sec
...
...
...
     30 w       0.000 read Gbits/sec  16.133 write Gbits/sec
---
 0 nr_conn=1 nr_zero_io_conn=0
```



2并发：

修改双端配置文件：
```text
"max_worker_num": 2,
"queue_num":2,
```

服务端:

```bash
taskset -c 16-31 ./tperf_knetco -s -l 192.168.1.6 -p 11111 -n 2 -S 16
```

客户端:

```bash
taskset -c 16-31 ./tperf_knetco -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 2 -N 2 -S 16 -t write -d 31 -P 49300,49618
```

服务端回显:

```coldfusion
EAL: Detectd CPU lcores: 128
EAL: Detectd NUMA nodes: 4
EAL: Detectd shared linkage of DPDK
...
...
...
sp6: Disable allmulticuous succeed, nic_dev: dbdf-000:01:00.5, port_id: 0
sp6: Add fdir tcam rule, fuction_id: 0x22, tcam_block_id: 0, local_index: 0, global_index: 0, queue: 0, tcam_rule_nums: 1 succeed
Listening on 192.168.1.6:11111
[Server] Thread [281469707809728]: in knet user space thread
sp6: Add fdir tcam rule, fuction_id: 0x22, tcam_block_id: 0, local_index: 1, global_index: 1, queue: 1, tcam_rule_nums: 2 succeed
Listening on 192.168.1.6:11112
Accepted connection: fd = 47, cli_addr=192.168.1.7, cli_port=49618
nr_sock :1
Accepted connection: fd = 48, cli_addr=192.168.1.7, cli_port=49300
nr_sock :1
```

客户端回显：

```coldfusion
EAL: Detectd CPU lcores: 128
EAL: Detectd NUMA nodes: 4
EAL: Detectd shared linkage of DPDK
...
...
...
Connection in progress...server prot 11111, sockfd 41, cli_port 49300
Connection in progress...server prot 11112, sockfd 42, cli_port 49618
Connection established with sockfd 41
Connection established with sockfd 42
      0 w    0. 0.000 read Gbits/sec  0.000 write Gbits/sec
      0 w    1. 0.000 read Gbits/sec  28.055 write Gbits/sec
      0 w       0.000 read Gbits/sec  28.055 write Gbits/sec

      1 w    0. 0.000 read Gbits/sec  21.646 write Gbits/sec
      1 w    1. 0.000 read Gbits/sec  22.274 write Gbits/sec
      1 w       0.000 read Gbits/sec  43.920 write Gbits/sec

      2 w    0. 0.000 read Gbits/sec  21.503 write Gbits/sec
      2 w    1. 0.000 read Gbits/sec  21.719 write Gbits/sec
      2 w       0.000 read Gbits/sec  43.222 write Gbits/sec
...
...
...
```

### tperf_knetzcopy
修改双端配置文件：
```"zcopy_enable": 1,```

1并发：

服务端:

```bash
taskset -c 17-31 ./tperf_knetzcopy -s -l 192.168.1.6 -p 11111 -n 1 -S 17
```

客户端:

```bash
taskset -c 17-31 ./tperf_knetzcopy -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 1 -N 1 -S 17 -t write -d 31 -P 58532
```

服务端回显:

```coldfusion
EAL: Detectd CPU lcores: 128
EAL: Detectd NUMA nodes: 4
EAL: Detectd shared linkage of DPDK
...
...
...
Listening on 192.168.1.6:11111
Accepted connection: fd = 65, cli_addr=192.168.1.7, cli_port=58532
nr_sock :1
```

客户端回显：

```coldfusion
EAL: Detectd CPU lcores: 128
EAL: Detectd NUMA nodes: 4
EAL: Detectd shared linkage of DPDK
...
...
...
sp6: Disable allmulticuous succeed, nic_dev: dbdf-000:01:00.5, port_id: 0
sp6: Add fdir tcam rule, fuction_id: 0x22, tcam_block_id: 0, local_index: 0, global_index: 0, queue: 0, tcam_rule_nums: 1 succeed
Connection in progress...server prot 11111, sockfd 62, cli_port 58532
Connection established with sockfd 4
      0 w       0.000 read Gbits/sec  44.859 write Gbits/sec
      1 w       0.000 read Gbits/sec  46.574 write Gbits/sec
      2 w       0.000 read Gbits/sec  46.259 write Gbits/sec
      3 w       0.000 read Gbits/sec  44.869 write Gbits/sec
...
...
...
```


2并发：

修改双端配置文件：
```
"max_worker_num": 2,
"core_list_global": "16-17",
"queue_num": 2,
```

服务端：

```bash
taskset -c 18-31 ./tperf_knetzcopy -s -l 192.168.1.6 -p 11111 -n 2 -S 18
```

客户端:

```bash
taskset -c 18-31 ./tperf_knetzcopy -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 2 -N 2 -S 18 -t write -d 31 -P 49452,51507
```

服务端回显:

```coldfusion
EAL: Detectd CPU lcores: 128
EAL: Detectd NUMA nodes: 4
EAL: Detectd shared linkage of DPDK
...
...
...
sp6: Disable allmulticuous succeed, nic_dev: dbdf-000:01:00.5, port_id: 0
sp6: Add fdir tcam rule, fuction_id: 0x22, tcam_block_id: 0, local_index: 0, global_index: 0, queue: 3, tcam_rule_nums: 1 succeed
Listening on 192.168.1.6:11111
[Server] Thread [281469707809728]: in knet user space thread
sp6: Add fdir tcam rule, fuction_id: 0x22, tcam_block_id: 0, local_index: 1, global_index: 1, queue: 3, tcam_rule_nums: 2 succeed
Listening on 192.168.1.6:11112
Accepted connection: fd = 74, cli_addr=192.168.1.7, cli_port=49452
nr_sock :1
Accepted connection: fd = 75, cli_addr=192.168.1.7, cli_port=51507
nr_sock :1
```

客户端回显：

```coldfusion
EAL: Detectd CPU lcores: 128
EAL: Detectd NUMA nodes: 4
EAL: Detectd shared linkage of DPDK
...
...
...
sp6: Add fdir tcam rule, fuction_id: 0x22, tcam_block_id: 0, local_index: 0, global_index: 0, queue: 3, tcam_rule_nums: 1 succeed
Connection in progress...server prot 11111, sockfd 66, cli_port 49452
Connection established with sockfd 66
sp6: Add fdir tcam rule, fuction_id: 0x22, tcam_block_id: 0, local_index: 1, global_index: 1, queue: 3, tcam_rule_nums: 2 succeed
Connection in progress...server prot 11112, sockfd 69, cli_port 51507
Connection established with sockfd 69
      0 w    0. 0.000 read Gbits/sec  51.242 write Gbits/sec
      0 w    1. 0.000 read Gbits/sec  42.811 write Gbits/sec
      0 w       0.000 read Gbits/sec  94.053 write Gbits/sec

      1 w    0. 0.000 read Gbits/sec  50.131 write Gbits/sec
      1 w    1. 0.000 read Gbits/sec  42.979 write Gbits/sec
      1 w       0.000 read Gbits/sec  93.109 write Gbits/sec

      2 w    0. 0.000 read Gbits/sec  50.132 write Gbits/sec
      2 w    1. 0.000 read Gbits/sec  42.984 write Gbits/sec
      2 w       0.000 read Gbits/sec  93.116 write Gbits/sec
...
...
...
```

### tperf_knetcozcopy
修改双端配置文件：

```text
"zcopy_enable": 1,
"cothread_enable": 1，
```

双端先执行：
```echo "1024 36180" > /proc/sys/net/ipv4/ip_local_port_range```

1并发：

服务端:

```bash
taskset -c 16-31 ./tperf_knetcozcopy -s -l 192.168.1.6 -p 11111 -n 1 -S 16
```

客户端:

```bash
taskset -c 16-31 ./tperf_knetcozcopy -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 1 -N 1 -S 16 -t write -d 31 -P 49631
```

服务端回显:

```coldfusion
EAL: Detectd CPU lcores: 128
EAL: Detectd NUMA nodes: 4
EAL: Detectd shared linkage of DPDK
...
...
...
Listening on 192.168.1.6:11111
Accepted connection: fd = 60, cli_addr=192.168.1.7, cli_port=49631
nr_sock :1
```

客户端回显：

```coldfusion
EAL: Detectd CPU lcores: 128
EAL: Detectd NUMA nodes: 4
EAL: Detectd shared linkage of DPDK
...
...
...
sp6: Disable allmulticuous succeed, nic_dev: dbdf-000:01:00.5, port_id: 0
sp6: Add fdir tcam rule, fuction_id: 0x22, tcam_block_id: 0, local_index: 0, global_index: 0, queue: 0, tcam_rule_nums: 1 succeed
Connection in progress...server prot 11111, sockfd 57, cli_port 49631
Connection established with sockfd 4
      0 w       0.000 read Gbits/sec  80.219 write Gbits/sec
      1 w       0.000 read Gbits/sec  80.461 write Gbits/sec
      2 w       0.000 read Gbits/sec  80.280 write Gbits/sec
      3 w       0.000 read Gbits/sec  80.094 write Gbits/sec
...
...
...
```

2并发：

双端修改配置文件：

```text
"max_worker_num": 2,
"queue_num": 2,
```

服务端:

```bash
taskset -c 16-31 ./tperf_knetcozcopy -s -l 192.168.1.6 -p 11111 -n 2 -S 16
```

客户端:

```bash
taskset -c 16-31 ./tperf_knetcozcopy -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 2 -N 2 -S 16 -t write -d 31 -P 49154,49162
```

服务端回显:

```coldfusion
EAL: Detectd CPU lcores: 128
EAL: Detectd NUMA nodes: 4
EAL: Detectd shared linkage of DPDK
...
...
...
sp6: Disable allmulticuous succeed, nic_dev: dbdf-000:01:00.5, port_id: 0
sp6: Add fdir tcam rule, fuction_id: 0x22, tcam_block_id: 0, local_index: 0, global_index: 0, queue: 1, tcam_rule_nums: 1 succeed
sp6: Add fdir tcam rule, fuction_id: 0x22, tcam_block_id: 0, local_index: 1, global_index: 1, queue: 0, tcam_rule_nums: 2 succeed
Listening on 192.168.1.6:11112
Listening on 192.168.1.6:11111
Accepted connection: fd = 66, cli_addr=192.168.1.7, cli_port=49162
Accepted connection: fd = 65, cli_addr=192.168.1.7, cli_port=49154
nr_sock :1
nr_sock :1
```

客户端回显：

```coldfusion
EAL: Detectd CPU lcores: 128
EAL: Detectd NUMA nodes: 4
EAL: Detectd shared linkage of DPDK
...
...
...
sp6: Add fdir tcam rule, fuction_id: 0x22, tcam_block_id: 0, local_index: 0, global_index: 0, queue: 1, tcam_rule_nums: 1 succeed
sp6: Add fdir tcam rule, fuction_id: 0x22, tcam_block_id: 0, local_index: 1, global_index: 1, queue: 0, tcam_rule_nums: 2 succeed
Connection in progress...server prot 11111, sockfd 66, cli_port 49154
Connection in progress...server prot 11112, sockfd 69, cli_port 49162
Connection established with sockfd 60
Connection established with sockfd 59
      0 w    0. 0.000 read Gbits/sec  44.071 write Gbits/sec
      0 w    1. 0.000 read Gbits/sec  49.551 write Gbits/sec
      0 w       0.000 read Gbits/sec  93.622 write Gbits/sec

      1 w    0. 0.000 read Gbits/sec  44.196 write Gbits/sec
      1 w    1. 0.000 read Gbits/sec  49.707 write Gbits/sec
      1 w       0.000 read Gbits/sec  93.903 write Gbits/sec
...
...
...
```

### （可选）tperf_os

若想对比内核协议栈Tperf性能，可执行以下步骤运行tperf_os：

参考[DPDK接管网卡](../../docs/zh/feature_guide/environment_configuration.md#DPDK接管网卡)说明中的取消接管网卡步骤。

```bash
dpdk-devbind.py -b "hisdk3" 0000:06:00.0
```

1并发：

服务端：

```bash
taskset -c 16-31 ./tperf_os -s -l 192.168.1.6 -p 11111 -n 1 -S 16
```

客户端：

```bash
taskset -c 16-31 ./tperf_os -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 1 -N 1 -S 16 -t write -d 31
```

服务端回显:

```coldfusion
Listening on 192.168.1.6:11111
Accepted connection: fd = 6, cli_addr=192.168.1.6, cli_port=1446
nr_sock :1
```

客户端回显：

```coldfusion
Connection in progress...server prot 11111, sockfd 4, cli_port random
Connection established with sockfd 4
      0 w       0.000 read Gbits/sec  11.819 write Gbits/sec
      1 w       0.000 read Gbits/sec  16.347 write Gbits/sec
      2 w       0.000 read Gbits/sec  17.650 write Gbits/sec
      3 w       0.000 read Gbits/sec  17.555 write Gbits/sec
...
...
...
     30 w       0.000 read Gbits/sec  17.652 write Gbits/sec
---
 0 nr_conn=1 nr_zero_io_conn=0
```

2并发:

服务端：

```bash
taskset -c 16-31 ./tperf_os -s -l 192.168.1.6 -p 11111 -n 2 -S 16
```

客户端：

```bash
taskset -c 16-31 ./tperf_os -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 2 -N 2 -S 16 -t write -d 31
```

服务端回显:

```coldfusion
Listening on 192.168.1.6:11111
Listening on 192.168.1.6:11112
Accepted connection: fd = 9, cli_addr=192.168.1.7, cli_port=1450
Accepted connection: fd = 10, cli_addr=192.168.1.7, cli_port=1448
nr_sock :2
```

客户端回显：

```coldfusion
Connection in progress...server prot 11111, sockfd 6, cli_port random
Connection in progress...server prot 11111, sockfd 5, cli_port random
Connection established with sockfd 5
Connection established with sockfd 6
      0 w    0. 0.000 read Gbits/sec  17.126 write Gbits/sec
      0 w    1. 0.000 read Gbits/sec  17.208 write Gbits/sec
      0 w       0.000 read Gbits/sec  34.334 write Gbits/sec

      1 w    0. 0.000 read Gbits/sec  17.231 write Gbits/sec
      1 w    1. 0.000 read Gbits/sec  17.225 write Gbits/sec
      1 w       0.000 read Gbits/sec  34.456 write Gbits/sec

      2 w    0. 0.000 read Gbits/sec  17.227 write Gbits/sec
      2 w    1. 0.000 read Gbits/sec  17.232 write Gbits/sec
      2 w       0.000 read Gbits/sec  34.459 write Gbits/sec
...
...
...
```

使用内核协议栈测试后，若需重新使用K-NET特性，需参考[DPDK接管网卡](../../docs/zh/feature_guide/environment_configuration.md#DPDK接管网卡)重新接管网卡。

```bash
dpdk-devbind.py -b vfio-pci 0000:06:00.0
dpdk-devbind.py -s                 #确认是否接管
```
