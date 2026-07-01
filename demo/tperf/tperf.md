# tperf_knet.patch使用示例

## 简介

tperf_knet.patch基于libtpa原生Tperf工具开发，将原生tpa接口转换为标准POSIX接口，并在此基础上，分别适配了<term>K-NET</term>共线程和零拷贝特性。
libtpa源码链接为：[https://github.com/bytedance/libtpa/tree/3c9f05df7b7c8ebc46bfebc83c316ec50f149e1c](https://github.com/bytedance/libtpa/tree/3c9f05df7b7c8ebc46bfebc83c316ec50f149e1c)。

## 编译及业务配置

请参考[环境配置](../../docs/zh/feature_guide/environment_configuration.md)配置大页内存、通用环境配置和Tperf业务配置，其中由于Tperf零拷贝场景需要在大页中进行pbuf的读写，因此需要增加大页内存，以20G为例，可根据实际情况分配。

```bash
echo 20 > /sys/devices/system/node/node0/hugepages/hugepages-1048576kB/nr_hugepages
```

> [!NOTE]说明
> 以网卡在node0为例，具体请修改为实际网卡所在NUMA。

## 使用示例

编译完成后在编译目录build/bin下有4个可执行demo。

- tperf_os：标准POSIX接口的Tperf demo；
- tperf_knetco：使用K-NET共线程特性的Tperf demo；
- tperf_knetzcopy：使用K-NET零拷贝特性的Tperf demo；
- tperf_knetcozcopy：使用K-NET共线程+零拷贝特性的Tperf demo。

服务端IP地址以192.168.1.6为例，客户端IP地址以192.168.1.7为例；具体需要替换为网卡配置的IP地址，且与K-NET配置文件中IP地址保持一致。

> [!NOTE]说明
> 示例运行完成后在服务端按Ctrl+C结束Tperf进程。

通过测试客户端和服务端之间Tperf的性能数据，来对比使用K-NET加速和内核协议栈（未使用K-NET加速）的性能提升。

### 内核协议栈Tperf性能测试（未使用K-NET加速）

1. 运行并发连接数为1的tperf_os。

    并发连接数为1，即服务端指定一个线程进行侦听。
    1. 服务端启动Tperf。

        ```bash
        taskset -c 16-31 ./tperf_os -s -l 192.168.1.6 -p 11111 -n 1 -S 16
        ```
        > [!NOTE]说明
        >
        >- taskset -c 16-31：绑定CPU到编号16-31的CPU核上运行，可查询NUMA node所用CPU核的范围，需要保证绑核在此范围内。
        >- -l 192.168.1.6：指定本地侦听的IP地址。
        >- -s：运行模式为服务端。 
        >- -p 11111：指定在11111端口进行侦听。
        >- -n 1：指定一个线程（即一个并发连接数）。
        >- -S 16：指定CPU绑核的起始值。

        服务端回显:

        ```coldfusion
        Listening on 192.168.1.6:11111
        Accepted connection: fd = 6, cli_addr=192.168.1.6, cli_port=11111
        nr_sock :1
        ```

    2. 在客户端测试性能。

        ```bash
        taskset -c 16-31 ./tperf_os -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 1 -N 1 -S 16 -t write -d 31
        ```

        > [!NOTE]说明
        >
        >- taskset -c 16-31：绑定CPU到编号16-31的CPU核上运行，可查询NUMA node所用CPU核的范围，需要保证绑核在此范围内。
        >- -l 192.168.1.7：指定本地侦听的IP地址。
        >- -c 192.168.1.6：运行模式为客户端，并指定服务端的IP地址为192.168.1.6。
        >- -m 4096：指定打流message大小为4096。
        >- -N 1：指定建联的线程数。
        >- -t write：指定测试模式为write。
        >- -d 31：指定测试时间为31秒。

        客户端回显：

        ```coldfusion
        Connection in progress...server prot 11111, sockfd 4, cli_port random
        Connection established with sockfd 4
            0 w       0.000 read Gbits/sec  20.819 write Gbits/sec
            1 w       0.000 read Gbits/sec  20.347 write Gbits/sec
            2 w       0.000 read Gbits/sec  21.650 write Gbits/sec
            3 w       0.000 read Gbits/sec  21.555 write Gbits/sec
        ...
        ...
        ...
            30 w       0.000 read Gbits/sec  21.652 write Gbits/sec
        ---
        0 nr_conn=1 nr_zero_io_conn=0
        ```
        测试值为21Gbits/sec，实际数据以运行为准。

2. 运行并发连接数为2的tperf_os。

    并发连接数为2，即服务端指定两个线程进行侦听。
    1. 服务端启动Tperf。

        ```bash
        taskset -c 16-31 ./tperf_os -s -l 192.168.1.6 -p 11111 -n 2 -S 16
        ```

        > [!NOTE]说明
        >- -n 2：指定并发连接数为2.
        >- -p 11111：指定侦听的端口，因并发连接数为2，所以侦听端口为11111和11112。

        服务端回显:

        ```coldfusion
        Listening on 192.168.1.6:11111
        Listening on 192.168.1.6:11112
        Accepted connection: fd = 9, cli_addr=192.168.1.7, cli_port=11111
        Accepted connection: fd = 10, cli_addr=192.168.1.7, cli_port=11112
        nr_sock :2
        ```

    2. 在客户端测试性能。

        ```bash
        taskset -c 16-31 ./tperf_os -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 2 -N 2 -S 16 -t write -d 31
        ```

        客户端回显：

        ```coldfusion
        Connection in progress...server prot 11111, sockfd 6, cli_port random
        Connection in progress...server prot 11112, sockfd 5, cli_port random
        Connection established with sockfd 5
        Connection established with sockfd 6
            0 w    0. 0.000 read Gbits/sec  21.126 write Gbits/sec
            0 w    1. 0.000 read Gbits/sec  21.208 write Gbits/sec
            0 w       0.000 read Gbits/sec  42.334 write Gbits/sec

            1 w    0. 0.000 read Gbits/sec  21.231 write Gbits/sec
            1 w    1. 0.000 read Gbits/sec  21.225 write Gbits/sec
            1 w       0.000 read Gbits/sec  42.456 write Gbits/sec

            2 w    0. 0.000 read Gbits/sec  21.227 write Gbits/sec
            2 w    1. 0.000 read Gbits/sec  21.232 write Gbits/sec
            2 w       0.000 read Gbits/sec  42.459 write Gbits/sec
        ...
        ...
        ...
        ```
        测试值为42Gbits/sec，实际数据以运行为准。

3. 测试完成后在服务端按Ctrl+C结束Tperf进程。

### K-NET无感加速tperf_os

1. 修改K-NET配置文件<a id="step1"></a>。

    运行K-NET进行网络加速时，会根据配置文件读取运行模式和网卡信息等内容，请根据前述查询得到信息填写。
    
    ```bash
    vim /etc/knet/knet_comm.conf
    ```

    按“i”进入编辑模式，修改配置项，示例如下：

    ```text
    "common": {
        "mode": 0, #1. 运行模式，0表示单进程模式
        ...
    },
    "interface": {
        ...
        "bdf_nums": [
            "0000:04:00.0"
        ], # 2. 填写获取的BDF号
        "mac": "ac:dc:ca:xx:xx:xx", # 3. 填写绑定网卡的MAC地址 
        "ip": "192.168.1.58",        # 4. 填写绑定网卡的IP地址
        ...
    },
        ...
    "dpdk": {
        "core_list_global": "1",  # 5. 数据面绑核，表示使用1号核。
        ...
        "socket_mem": "--socket-mem=1024", # 6. 网卡所在numa_node编号为0，在0号socket上分配1024MB大页内存，用户需要根据实际查看的numa_node编号进行更改，给网卡所在numa_node分配大页内存
        ...
    }
    ```

    > [!NOTE]说明
    > - "mode": 运行模式，0表示单进程模式，1表示多进程模式。此处填0。
    > - "bdf_nums"：填写获取的BDF号，此处以0000:04:00.0为例。
    > - "mac"：填写绑定网卡的MAC地址，此处以ac:dc:ca:xx:xx:xx为例。
    > - "ip"：填写绑定网卡的IP地址，此处以192.168.1.58为例。
    > - "core_list_global"：数据面绑核。需要为网卡所在CPU的中间值，NUMA node0所用CPU为0-23，此处可以填写1，表示使用1号核。
    > - "socket_mem"：给网卡所在numa_node分配的大页内存。网卡所在NUMA node0，在0号socket上分配1024MB大页内存，用户需要根据实际查看的numa_node编号进行更改。如果网卡所在NUMA node1，在0号socket上预分配0MB大页内存，在1号socket上分配1024MB大页内存，请填写为“--socket-mem=0,1024”。

    按“Esc”键退出编辑模式，输入 **:wq!**，按“Enter”键保存并退出文件。

2. DPDK接管网卡<a id="step2"></a>。
    1. 关闭网口。
    
        ```bash
        ip link set dev enp4s0f0 down
        ```
    
        > [!NOTE]说明
        > enp4s0f0：K-NET配置文件中填写的网口名称，后续会由DPDK接管。

    2. 加载VFIO驱动。
    
        ```bash
        modprobe vfio enable_unsafe_noiommu_mode=1
        modprobe vfio-pci
        ```

    3. DPDK接管网卡。
    
        ```bash
        dpdk-devbind.py -b vfio-pci 0000:04:00.0
        dpdk-devbind.py -s
        ```

        回显部分示例如下，表示DPDK接管网卡成功：

        ```text
        Network devices using DPDK-compatible driver
        ============================================
        0000:04:00.0 'Device 0222' drv=vfio-pci unused=hisdk3
        ```

        > [!NOTE]说明
        > 0000:04:00.0：K-NET配置文件中填写的BDF。

3. 进行并发数为1，K-NET无感加速的Tperf。

    并发连接数为1，即服务端指定一个线程进行侦听。
    1. 服务端启动Tperf。

        ```bash
        taskset -c 16-31 env LD_PRELOAD=/usr/lib64/libknet_frame.so ./tperf_os -s -l 192.168.1.6 -p 11111 -n 1 -S 16
        ```

        > [!NOTE]说明
        >
        >- taskset -c 16-31：绑定CPU到编号16-31的CPU核上运行，可查询NUMA node所用CPU核的范围，需要保证绑核在此范围内。
        >- env LD_PRELOAD=/usr/lib64/libknet_frame.so：指定so文件。
        >- -l 192.168.1.6：指定本地侦听的IP地址。
        >- -s：运行模式为服务端。 
        >- -p 11111：指定在11111端口进行侦听。
        >- -n 1：指定一个线程（即一个并发连接数）。
        >- -S 17：指定CPU绑核的起始值。

        示例回显:

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

    2. 在客户端测试性能。

        ```bash
        taskset -c 16-31 ./tperf_os -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 1 -N 1 -S 16 -t write -d 31
        ```

        > [!NOTE]说明
        >
        >- taskset -c 16-31：绑定CPU到编号16-31的CPU核上运行，可查询NUMA node所用CPU核的范围，需要保证绑核在此范围内。
        >- -l 192.168.1.7：指定本地侦听的IP地址。
        >- -c 192.168.1.6：运行模式为客户端，并指定服务端的IP地址为192.168.1.6。
        >- -m 4096：指定打流message大小为4096。
        >- -n 1 -N 1：指定建联的线程数。
        >- -t write：指定测试模式为write。
        >- -d 31：指定测试时间为31秒。

        客户端回显：

        ```coldfusion
        Connection in progress...server prot 11111, sockfd 4, cli_port random
        Connection established with sockfd 4
            0 w       0.000 read Gbits/sec  18.424 write Gbits/sec
            1 w       0.000 read Gbits/sec  18.337 write Gbits/sec
            2 w       0.000 read Gbits/sec  18.338 write Gbits/sec
            3 w       0.000 read Gbits/sec  18.368 write Gbits/sec
        ...
        ...
        ...
            30 w       0.000 read Gbits/sec  18.133 write Gbits/sec
        ---
        0 nr_conn=1 nr_zero_io_conn=0
        ```
        
        测试值为18Gbits/sec，实际数据以运行为准。

4. 进行并发数为2，K-NET无感加速的Tperf。

    并发连接数为2，即服务端指定两个线程进行侦听。
  1. 服务端启动Tperf。

        ```bash
        taskset -c 16-31 env LD_PRELOAD=/usr/lib64/libknet_frame.so ./tperf_os -s -l 192.168.1.6 -p 11111 -n 2 -S 16
        ```
        > [!NOTE]说明
        >
        >- taskset -c 16-31：绑定CPU到编号16-31的CPU核上运行，可查询NUMA node所用CPU核的范围，需要保证绑核在此范围内。
        >- -l 192.168.1.6：指定本地侦听的IP地址。
        >- -s：运行模式为服务端。 
        >- -l：指定侦听的IP地址。
        >- -p 11111 -n 2：指定在11111端口和11112端口进行分别有一个线程侦听。
        >- -S 16：指定CPU绑核的起始值。

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

  2. 在客户端测试性能。

        ```bash
        taskset -c 16-31 ./tperf_os -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 2 -N 2 -S 16 -t write -d 31
        ```

        > [!NOTE]说明
        >
        >- taskset -c 16-31：绑定CPU到编号16-31的CPU核上运行，可查询NUMA node所用CPU核的范围，需要保证绑核在此范围内。
        >- -l 192.168.1.7：指定本地侦听的IP地址。
        >- -c 192.168.1.6：运行模式为客户端，并指定服务端的IP地址为192.168.1.6。
        >- -m 4096：指定打流message大小为4096。
        >- -p 11111 -n 2：指定在11111端口和11112端口进行分别有一个线程侦听。
        >- -N 2：指定建联的线程数，即客户端会将1线程与对端1线程建链，2线程与对端2线程建链。
        >- -t write：指定测试模式为write。
        >- -d 31：指定打流时间为31秒。

        客户端回显：

        ```coldfusion
        Connection in progress...server prot 11111, sockfd 4, cli_port random
        Connection in progress...server prot 11111, sockfd 6, cli_port random
        Connection established with sockfd 6
        Connection established with sockfd 4
            0 w    0. 0.000 read Gbits/sec  17.845 write Gbits/sec
            0 w    1. 0.000 read Gbits/sec  17.330 write Gbits/sec
            0 w       0.000 read Gbits/sec  35.176 write Gbits/sec

            1 w    0. 0.000 read Gbits/sec  17.827 write Gbits/sec
            1 w    1. 0.000 read Gbits/sec  17.279 write Gbits/sec
            1 w       0.000 read Gbits/sec  35.107 write Gbits/sec

            2 w    0. 0.000 read Gbits/sec  17.822 write Gbits/sec
            2 w    1. 0.000 read Gbits/sec  17.272 write Gbits/sec
            2 w       0.000 read Gbits/sec  35.095 write Gbits/sec
        ...
        ...
        ...
        ```
        测试值为35Gbits/sec左右，实际数据以运行为准。

5. 测试完成后在服务端按Ctrl+C结束Tperf进程。

6. （可选）测试完成后，若需要恢复到内核态，请在服务端取消DPDK接管网卡。
    详情可参考[DPDK接管网卡](../../docs/zh/feature_guide/environment_configuration.md#DPDK接管网卡)。
    
    ```bash
    dpdk-devbind.py -b "hisdk3" 0000:04:00.0
    ```

    > [!NOTE]说明
    > 示例使用的“0000:04:00.0”，请以实际BDF号为准。
    > "hisdk3"为SP670网卡使用的驱动。

### K-NET共线程特性加速tperf_knetco

使用K-NET共线程特性的Tperf demo。

1. 已完成K-NET配置文件修改和DPAK网卡接管，可参见[修改K-NET配置文件](#step1)和[DPDK接管网卡](#step2)。

2. 分别在服务端和客户端修改配置文件。

    ```bash
    vim /etc/knet/knet_comm.conf
    ```

    按“i”进入编辑模式，修改以下配置项：

    ```text
    "cothread_enable": 1；
    ```
   按“Esc”键退出编辑模式，输入 **:wq!**，按“Enter”键保存并退出文件。

3. 修改内核协议栈端口范围。

    内核协议栈和K-NET端口的范围有交叉，不修改可能导致端口冲突。

    ```bash
    echo "1024 36180" > /proc/sys/net/ipv4/ip_local_port_range
    ```

4. 运行并发数连接数为1，使用K-NET共线程特性的Tperf。

    1. 服务端启动Tperf。

        ```bash
        taskset -c 16-31 ./tperf_knetco -s -l 192.168.1.6 -p 11111 -n 1 -S 16
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

    2. 在客户端测试性能。
    
        ```bash
        taskset -c 16-31 ./tperf_knetco -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 1 -N 1 -S 16 -t write -d 31 
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
            0 w       0.000 read Gbits/sec  25.424 write Gbits/sec
            1 w       0.000 read Gbits/sec  25.337 write Gbits/sec
            2 w       0.000 read Gbits/sec  25.338 write Gbits/sec
            3 w       0.000 read Gbits/sec  25.368 write Gbits/sec
        ...
        ...
        ...
            30 w       0.000 read Gbits/sec  16.133 write Gbits/sec
        ---
        0 nr_conn=1 nr_zero_io_conn=0
        ```
        测试值为16Gbits/sec，实际数据以运行为准。

5. 进行并发连接数为2，使用K-NET共线程特性的Tperf。

    1. 修改双端配置文件。

        ```bash
        vim /etc/knet/knet_comm.conf
        ```

        按“i”进入编辑模式，修改以下配置项。

        ```text
        "max_worker_num": 2,
        "queue_num":2,
        ```

        按“Esc”键退出编辑模式，输入 **:wq!**，按“Enter”键保存并退出文件。

    2. 服务端启动Tperf。

        ```bash
        taskset -c 16-31 ./tperf_knetco -s -l 192.168.1.6 -p 11111 -n 2 -S 16
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

    3. 在客户端测试性能。

        ```bash
        taskset -c 16-31 ./tperf_knetco -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 2 -N 2 -S 16 -t write -d 31 -P 49300,49618
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
        测试值为43Gbits/sec,实际数据以运行为准。

6. 测试完成后在服务端按Ctrl+C结束Tperf进程。

7. （可选）测试完成后，若需要恢复到内核态，请在服务端取消DPDK接管网卡。
    
    详情可参考[DPDK接管网卡](../../docs/zh/feature_guide/environment_configuration.md#DPDK接管网卡)。
    
    ```bash
    dpdk-devbind.py -b "hisdk3" 0000:04:00.0
    ```

    > [!NOTE]说明
    > 示例使用的“0000:04:00.0”，请以实际BDF号为准。
    > "hisdk3"为SP670网卡使用的驱动。

### K-NET零拷贝特性加速tperf_knetzcopy

使用K-NET零拷贝特性的Tperf demo。
1. 已完成K-NET配置文件修改和DPAK网卡接管，可参见[修改K-NET配置文件](#step1)和[DPDK接管网卡](#step2)。

2. 分别在服务端和客户端修改配置文件。

    ```bash
    vim /etc/knet/knet_comm.conf
    ```

    按“i”进入编辑模式，修改配置项：

    ```text
    "zcopy_enable": 1,
    ```
    按“Esc”键退出编辑模式，输入 **:wq!**，按“Enter”键保存并退出文件。

3. 运行并发连接数为1，使用K-NET零拷贝特性的Tperf。

    1. 服务端启动Tperf。

        ```bash
        taskset -c 17-31 ./tperf_knetzcopy -s -l 192.168.1.6 -p 11111 -n 1 -S 17
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

    2. 在客户端测试性能。

        ```bash
        taskset -c 17-31 ./tperf_knetzcopy -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 1 -N 1 -S 17 -t write -d 31 -P 58532
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
            0 w       0.000 read Gbits/sec  54.859 write Gbits/sec
            1 w       0.000 read Gbits/sec  54.574 write Gbits/sec
            2 w       0.000 read Gbits/sec  54.259 write Gbits/sec
            3 w       0.000 read Gbits/sec  54.869 write Gbits/sec
        ...
        ...
        ...
        ```
        测试值为54Gbits/sec左右，实际数据以运行为准。

4. 运行并发连接数为2，使用K-NET零拷贝特性的Tperf。

    1. 修改双端配置文件。

        ```bash
        vim /etc/knet/knet_comm.conf
        ```

        按“i”进入编辑模式，修改以下配置项：

        ```
        "max_worker_num": 2,
        "core_list_global": "16-17",
        "queue_num": 2,
        ```

        按“Esc”键退出编辑模式，输入 **:wq!**，按“Enter”键保存并退出文件。

    2. 服务端启动Tperf。

        ```bash
        taskset -c 18-31 ./tperf_knetzcopy -s -l 192.168.1.6 -p 11111 -n 2 -S 18
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

    3. 在客户端测试性能。

        ```bash
        taskset -c 18-31 ./tperf_knetzcopy -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 2 -N 2 -S 18 -t write -d 31 -P 49452,51507
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
            0 w    0. 0.000 read Gbits/sec  53.242 write Gbits/sec
            0 w    1. 0.000 read Gbits/sec  52.811 write Gbits/sec
            0 w       0.000 read Gbits/sec  106.053 write Gbits/sec

            1 w    0. 0.000 read Gbits/sec  53.131 write Gbits/sec
            1 w    1. 0.000 read Gbits/sec  52.979 write Gbits/sec
            1 w       0.000 read Gbits/sec  106.109 write Gbits/sec

            2 w    0. 0.000 read Gbits/sec  53.132 write Gbits/sec
            2 w    1. 0.000 read Gbits/sec  52.984 write Gbits/sec
            2 w       0.000 read Gbits/sec  106.116 write Gbits/sec
        ...
        ...
        ...
        ```
        测试值为106Gbits/sec左右，实际数据以运行为准。

5. 测试完成后在服务端按Ctrl+C结束Tperf进程。

6. （可选）测试完成后，若需要恢复到内核态，请在服务端取消DPDK接管网卡。
    
    详情可参考[DPDK接管网卡](../../docs/zh/feature_guide/environment_configuration.md#DPDK接管网卡)。
    
    ```bash
    dpdk-devbind.py -b "hisdk3" 0000:04:00.0
    ```

    > [!NOTE]说明
    > 示例使用的“0000:04:00.0”，请以实际BDF号为准。
    > "hisdk3"为SP670网卡使用的驱动。

### K-NET共线程和零拷贝特性加速tperf_knetcozcopy

使用K-NET共线程加零拷贝特性的Tperf demo。
1. 已完成K-NET配置文件修改和DPAK网卡接管，可参见[修改K-NET配置文件](#step1)和[DPDK接管网卡](#step2)。

2. 分别在服务端和客户端修改配置文件。

    ```bash
    vim /etc/knet/knet_comm.conf
    ```

    按“i”进入编辑模式，修改以下配置项：

    ```text
    "zcopy_enable": 1,
    "cothread_enable": 1；
    ```
   按“Esc”键退出编辑模式，输入 **:wq!**，按“Enter”键保存并退出文件。

3. 修改内核协议栈端口范围。

    内核协议栈和K-NET端口的范围有交叉，可能冲突。

    ```bash
    echo "1024 36180" > /proc/sys/net/ipv4/ip_local_port_range
    ```

4. 运行并发连接数为1，使用K-NET共线程加零拷贝特性的Tperf。

    1. 服务端启动Tperf。

        ```bash
        taskset -c 16-31 ./tperf_knetcozcopy -s -l 192.168.1.6 -p 11111 -n 1 -S 16
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

    2. 在客户端测试性能。

        ```bash
        taskset -c 16-31 ./tperf_knetcozcopy -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 1 -N 1 -S 16 -t write -d 31 -P 49631
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
            0 w       0.000 read Gbits/sec  76.219 write Gbits/sec
            1 w       0.000 read Gbits/sec  76.461 write Gbits/sec
            2 w       0.000 read Gbits/sec  76.280 write Gbits/sec
            3 w       0.000 read Gbits/sec  76.094 write Gbits/sec
        ...
        ...
        ...
        ```
        测试值为76Gbits/sec，实际数据以运行为准。

5. 运行并发连接数为2，使用K-NET共线程加零拷贝特性的Tperf。

    1. 双端修改配置文件。

        ```bash
        vim /etc/knet/knet_comm.conf
        ```

        按“i”进入编辑模式，修改配置项：

        ```text
        "max_worker_num": 2,
        "queue_num": 2,
        ```
        按“Esc”键退出编辑模式，输入 **:wq!**，按“Enter”键保存并退出文件。

    2. 服务端启动Tperf。

        ```bash
        taskset -c 16-31 ./tperf_knetcozcopy -s -l 192.168.1.6 -p 11111 -n 2 -S 16
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

    2. 在客户端测试。

        ```bash
        taskset -c 16-31 ./tperf_knetcozcopy -l 192.168.1.7 -c 192.168.1.6 -p 11111 -m 4096 -n 2 -N 2 -S 16 -t write -d 31 -P 49154,49162
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
            0 w    0. 0.000 read Gbits/sec  72.071 write Gbits/sec
            0 w    1. 0.000 read Gbits/sec  72.551 write Gbits/sec
            0 w       0.000 read Gbits/sec  144.622 write Gbits/sec

            1 w    0. 0.000 read Gbits/sec  72.196 write Gbits/sec
            1 w    1. 0.000 read Gbits/sec  72.707 write Gbits/sec
            1 w       0.000 read Gbits/sec  144.903 write Gbits/sec
        ...
        ...
        ...
        ```
        测试值为144Gbits/sec，实际数据以运行为准。
    通过对比可以看到，使用K-NET进行网络加速后，并发数为1的测试从21Gbits/sec提升到76Gbits/sec，并发数为2的测试值从42Gbits/sec提升到144Gbits/sec。

6. 测试完成后在服务端按Ctrl+C结束Tperf进程。

7. （可选）测试完成后，若需要恢复到内核态，请在服务端取消DPDK接管网卡。
    
    详情可参考[DPDK接管网卡](../../docs/zh/feature_guide/environment_configuration.md#DPDK接管网卡)。
    
    ```bash
    dpdk-devbind.py -b "hisdk3" 0000:04:00.0
    ```

    > [!NOTE]说明
    > 示例使用的“0000:04:00.0”，请以实际BDF号为准。
    > "hisdk3"为SP670网卡使用的驱动。
