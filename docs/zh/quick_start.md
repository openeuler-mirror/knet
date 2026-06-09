# 快速入门

## K-NET介绍

Data Acceleration Kit K-NET（K-Network，网络加速套件）旨在打造一款网络加速套件，提供统一的软件框架，发挥软硬协同优势，释放网卡硬件性能。
本快速入门基于SP670网卡，以在物理机环境上，root用户运行Redis单进程加速任务为例，教您如何快速使用K-NET进行网络加速。

## 快速安装K-NET

本文提供在物理机上快速安装K-NET的操作，如果您需要详细或在虚拟机上安装的操作步骤请参考[《安装指南》](./installation/installation_menu.md)。单进程加速至少需要一个服务端和一个客户端，请注意操作平台。

### 环境要求

- 已完成服务端和客户端的Yum源配置。未配置请参考[配置物理机Yum源](./installation/environment_setup.md#可选配置物理机yum源)。
- 软硬件要求满足[环境要求](./release_note.md#版本配套关系)，本文以openEuler 22.03 LTS SP4为例，需保持服务端与客户端一致。
- 已在服务端和客户端安装SP670驱动和固件。安装方式请参见[《SP220&SP600 标准网卡 用户指南》](https://support.huawei.com/enterprise/zh/doc/EDOC1100309168/a7897085?idPath=23710424|251364417|9856629|253287505)中“软件安装”章节。网卡模板为“0”或者“3”。可执行以下命令检查：

    ```bash
    hinicadm3 cfg_template -i hinic0
    ```

    回显示例如下：

    ![](./figures/zh-cn_image_0000002487030394.png)

    “Current Info”字段中的“Cfg template index”显示为“0”或者“3”表示模板正确。若模板不对需要自行切换，这里以切换到“0”为例：

    ```bash
    hinicadm3 cfg_template -i hinic0 -s 0
    ```

    切换后执行**reboot**重启。
- 服务端glibc版本为2.10以上。可执行以下命令检查：

    ```bash
    ldd --version
    ```

    若查询出来的版本低于2.10，建议升级至2.10以上。这里以2.28版本为例：

    ```bash
    yum update glibc-2.28
    ```

- 服务端已开启ASLR（地址空间随机化）。可执行以下命令检查：
    
    ```bash
    cat /proc/sys/kernel/randomize_va_space
    ```
    
    若结果不为2，请执行以下命令开启ASLR：
    
    ```bash
    bash -c 'echo 2 >/proc/sys/kernel/randomize_va_space'
    ```

### 安装DPDK驱动

请在服务端和客户端均执行以下操作：

1. 安装DPDK 21.11.7。
    1. 安装编译依赖。
        
        ```bash
        yum install -y git gcc libatomic python3-devel meson ninja-build python3-pyelftools libibverbs numactl numactl-devel zlib-devel
        ```

    2. 下载DPDK软件包，以安装路径“/home/opt”为例。
        
        ```bash
        mkdir /home/opt
        cd /home/opt
        wget https://fast.dpdk.org/rel/dpdk-21.11.7.tar.xz
        ```

        > [!NOTE]说明 
        > 若执行**wget**命令出现错误“ERROR: The certificate of ‘xxxxx’ is not trusted”，请在命令末尾增加“--no-check-certificate”。

    3. 解压软件包。
        
        ```bash
        tar -xf dpdk-21.11.7.tar.xz
        cd dpdk-stable-21.11.7
        ```

    4. 安装DPDK驱动。
        
        ```bash
        meson -Ddisable_drivers=net/cnxk -Dibverbs_link=dlopen -Dplatform=generic -Denable_kmods=false -Dprefix=/usr build
        ```

        预期结果如下：

        ![](./figures/zh-cn_image_0000002503958012.png)
       
        ```bash
        ninja -C build
        ```

        预期结果如下：
        
        ![](./figures/zh-cn_image_0000002535517975.png)

        ```bash
        ninja install -C build
        ```

        预期结果如下：

        ![](./figures/zh-cn_image_0000002503798182.png)

2. 安装dpdk-hinic3驱动。
    1. 获取hinic3 PMD源码。
       
        ```bash
        cd /home/opt
        git clone https://atomgit.com/openeuler/dpdk.git -b hinic3_master dpdk-hinic3_master
        ```

        预期结果如下：
       
        ```text
        Cloning into 'dpdk-hinic3_master'...
        remote: Enumerating objects: 3643, done.
        remote: Counting objects: 100% (1483/1483), done.
        remote: Compressing objects: 100% (338/338), done.
        remote: Total 3643 (delta 1381), reused 1145 (delta 1145), pack-reused 2160 (from 1)
        Receiving objects: 100% (3643/3643), 35.14 MiB | 1.36 MiB/s, done.
        Resolving deltas: 100% (2374/2374), done.
        ```

    2. 获取配套版本的tag。
        配套的dpdk-hinic3版本为[hinic3-26.0.rc1-0331.r1](https://atomgit.com/openeuler/dpdk/tags/hinic3-26.0.rc1-0331.r1)，跳转查看对应的commitid。
        示意如下：

        ![](./figures/hinic3p2.PNG)

    3. 切换到配套版本tag，此处commitid为6601cc18。
        
        ```bash
        cd dpdk-hinic3_master
        git checkout 6601cc18
        ```

    4. 编译dpdk-hinic3驱动。

        ```bash
        sh install.sh ../dpdk-stable-21.11.7 install
        sh install.sh ../dpdk-stable-21.11.7 build
        ```

    5. 安装dpdk-hinic3驱动。

        ```bash
        cp -d ./../dpdk-stable-21.11.7/build/drivers/librte_net_hinic3.so{,.22,.22.0} /usr/lib64/
        ls -l /usr/lib64/librte_net_hinic3.so*
        ldconfig
        ```

        > [!NOTE]说明 
        > {,.22,.22.0} 根据实际DPDK版本替换，以DPDK 21.11.7版本为例，此处DPDK so版本为21 + 1，即为22。

### 安装K-NET软件包

请在服务端执行以下操作：

1. 安装系统依赖。
    
    ```bash
    yum install -y libcap-devel tar gzip vim jq rpm-build python cmake
    ```

2. 安装libboundscheck依赖。

    ```bash
    yum install -y libboundscheck
    ```

3. 下载K-NET源码包，以安装路径“/home/knet-repo”为例。

    ```bash
    mkdir -p /home/knet-repo
    cd /home/knet-repo
    git clone https://atomgit.com/openeuler/knet.git
    ```

4. 切换到配套版本tag。K-NET版本为[26.0.RC1](https://gitcode.com/openeuler/knet/tags/knet-26.0.rc1-0331)，跳转查看commitid为95c4ef17。
    
    ```bash
    cd knet
    git checkout 95c4ef17
    ```

5. 构建RPM包。
    
    ```bash 
    python3 build.py rpm
    ```

6. 安装RPM包。
   
    ```bash
    rpm -ivh build/rpmbuild/RPMS/knet-1.0.0.aarch64.rpm
    ```

    安装成功，预期结果如下：

    ```text
    Verifying...                          ################################# [100%]
    Preparing...                          ################################# [100%]
    Updating / installing...
        1:knet-1.0.0-1                     ################################# [100%]
    ```

## 使用K-NET进行单进程加速

以验证Redis单进程加速任务为例，通过与内核态协议栈测试性能进行对比，展示使用K-NET后用户态协议栈（DPDK接管网卡）的性能提升效果。

### 业务配置

1. 安装Redis。
    请在服务端和客户端均安装Redis，若环境上已有Redis 6.0.20版本，可以跳过此步骤。以安装路径“/home/opt”为例。

    ```bash
    cd /home/opt
    yum install -y libatomic
    git clone https://github.com/redis/redis.git --branch 6.0.20
    cd redis
    make
    make install
    ```

    安装完成后，查看版本信息：

    ```bash
    redis-server -v
    ```
    
    预期结果如下：

    ```text
    Redis server v=6.0.20 sha=de0d9632:0 malloc=jemalloc-5.1.0 bits=64 build=be9cd4aae109acff
    ```

2. 查看网卡信息。
    获取服务端网卡相关信息，用于后续配置大页内存、填写配置文件和性能测试。
    1. 确定要用的网卡。
        
        ```bash
        hinicadm3 info
        ```

        回显示例如下：

        ```text
        Card num:1
        Device Information:
            Card         PCIe Function
        |----hinic0(CAL_2X100G)
        |--------0000:04:00.0(NIC:enp4s0f0)
        |--------0000:04:00.1(NIC:enp4s0f1)
        ```

        通过回显获取BDF和网口信息，请根据实际情况选用网口，这里选择BDF号：0000:04:00.0，网口名：enp4s0f0。
    2. 查看网卡IP地址和MAC地址。
        
        ```bash
        ip a
        ```

        回显示例片段如下：

        ```text
        ...
        2: enp4s0f0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc mq state UP group default qlen 1000
            link/ether ac:dc:ca:xx:xx:xx brd ff:ff:ff:ff:ff:ff
            inet 192.168.1.58/24 scope global enp4s0f0
                valid_lft forever preferred_lft forever
        ...
        ```

        > [!NOTE]说明 
        > 若无IP地址，请用户手动添加，以给enp4s0f0网口配置IP地址192.168.1.58为例：
        >
        > ```bash
        > ip addr add 192.168.1.58/24 dev enp4s0f0
        > ```

        根据网口enp4s0f0获取得到IP地址：192.168.1.58，MAC地址：ac:dc:ca:xx:xx:xx。
    3. 查看网卡所在的NUMA节点与CPU信息。
        查看NUMA信息，以BDF 0000:04:00.0为例：
        
        ```bash
        lspci -vvs 0000:04:00.0 | grep -i numa
        ```

        回显示例如下，表示网卡所在NUMA节点为node 0：
        
        ```text
        NUMA node: 0
        ```

        查看NUMA对应CPU信息：
        
        ```bash
        lscpu | grep -i numa
        ```

        回显示例如下，NUMA node0所用CPU为0-23：

        ```text
        NUMA node(s):                       4
        NUMA node0 CPU(s):                  0-23
        NUMA node1 CPU(s):                  24-47
        NUMA node2 CPU(s):                  48-71
        NUMA node3 CPU(s):                  72-95
        ```

3. 配置大页内存。
    请在服务端配置大页内存，具体node编号根据查询到的网卡所在NUMA进行更改，大页数量与单个大页大小根据实际情况配置，此处为node 0分配2个大小为1048576kB（1GB）的大页。
    
    ```bash
    echo never > /sys/kernel/mm/transparent_hugepage/enabled # 关闭透明大页
    echo 2 > /sys/devices/system/node/node0/hugepages/hugepages-1048576kB/nr_hugepages # 为指定节点分配2个大小为1048576kB（1GB）的大页
    # 挂载大页
    mkdir -p /home/hugepages1G
    mount -t hugetlbfs -o pagesize=1G hugetlbfs /home/hugepages1G
    ```

    配置完成后查看大页：

    ```bash
    dpdk-hugepages.py -s
    ```

    回显示例如下：

    ```text
    Node Pages Size Total
    0    2     1Gb    2Gb
        
    Hugepages mounted on /home/hugepages1G
    ```

### 单进程加速性能测试

通过测试客户端和服务端之间Redis业务set/get的性能数据，来对比使用K-NET进行单进程网络加速的性能提升。

#### 内核态性能测试

1. 测试网络连通性。在客户端ping服务端IP地址，确保网络是连通的，此处192.168.1.58为前述查询所得服务端IP。
    
    ```bash
    ping 192.168.1.58
    ```

    回显示例如下，表示网络是连通的。

    ```text
    PING 192.168.1.58 (192.168.1.58) 56(84) bytes of data.
    64 bytes from 192.168.1.58: icmp_seq=1 ttl=64 time=0.197 ms
    64 bytes from 192.168.1.58: icmp_seq=2 ttl=64 time=0.041 ms
    ```

2. 服务端启动Redis业务。
    
    ```bash
    taskset -c 0-3 redis-server /home/opt/redis/redis.conf --port 6380 --bind 192.168.1.58
    ```

    > [!NOTE]说明 
    > - 0-3：绑定CPU到编号0-3的CPU核上运行，前述查询得到NUMA node0所用CPU核为0-23，需要保证在此范围。
    > - /home/opt：Redis安装路径。
    > - 192.168.1.58：bind的IP地址，前面获取的服务端IP地址。

    观察到如下输出，表示启动成功：

    ```text
     * Ready to accept connections
    ```

3. 客户端执行redis-benchmark测试性能。

    1. 测试set值。
    
        ```bash
        taskset -c 0-3 /home/opt/redis/src/redis-benchmark -h 192.168.1.58 -p 6380 -c 1000 -n 10000000 -r 100000 -t set --threads 15
        ```

        > [!NOTE]说明 
        > - 0-3：绑定CPU到编号0-3的CPU核上运行，前述查询得到NUMA node0所用CPU核为0-23，需要保证在此范围。
        > - /home/opt：Redis安装路径。
        > - 192.168.1.58：bind的IP地址，前面获取的服务端IP地址。
    
        回显示例如下：
        
        ```text
        ====== SET ======
        10000000 requests completed in 60.53 seconds
        1000 parallel clients
        3 bytes payload
        keep alive: 1
        host configuration "save": 900 1 300 10 60 10000
        host configuration "appendonly": no
        multi-thread: yes
        threads: 15

        0.00% <= 0.4 milliseconds
        0.00% <= 0.5 milliseconds
        0.00% <= 0.6 milliseconds
        0.00% <= 0.7 milliseconds
        ...
        165196.42 requests per second
        ```

        则set性能为165196.42rps，实际性能以运行为准。

    2. 测试get值。
    
        ```bash
        taskset -c 0-3 /home/opt/redis/src/redis-benchmark -h 192.168.1.58 -p 6380 -c 1000 -n 10000000 -r 100000 -t get --threads 15
        ```

        回显示例如下：
        
        ```text
        ====== GET ======
        100000000 requests completed in 562.22 seconds
        1000 parallel clients
        3 bytes payload
        keep alive: 1
        host configuration "save": 900 1 300 10 60 10000
        host configuration "appendonly": no
        multi-thread: yes
        threads: 15

        0.00% <= 0.4 milliseconds
        0.00% <= 0.5 milliseconds
        0.00% <= 0.6 milliseconds
        0.00% <= 0.7 milliseconds
        ...
        177866.94 requests per second
        ```
        
        则get性能为177866.94rps，实际性能以运行为准。
4. 测试完成后在服务端结束K-NET进程，按CTRL+C杀掉Redis进程。

#### 用户态性能测试（使用K-NET进行加速）

1. 请在服务端修改K-NET配置文件。运行K-NET进行网络加速时，会根据配置文件读取运行模式、网卡信息等内容，请根据前述查询得到信息填写。
    
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
2. DPDK接管网卡。
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

3. 服务端启动Redis业务。
    
    ```bash
    taskset -c 0-3 env LD_PRELOAD=/usr/lib64/libknet_frame.so redis-server /home/opt/redis/redis.conf --port 6380 --bind 192.168.1.58
    ```

    > **说明：** 
    >- 0-3：绑定CPU到编号0-3的CPU核上运行，前述查询得到NUMA node0所用CPU核为0-23，需要保证在此范围。
    >- /home/opt：Redis安装路径。
    >- 192.168.1.58：bind的IP地址，请根据实际情况替换为K-NET配置文件中填写的IP地址。

    观察到如下输出，表示启动成功：

    ```text
     * Ready to accept connections
    ```

4. 客户端执行redis-benchmark测试性能。
    1. 测试set值。
    
        ```bash
        taskset -c 0-3 /home/opt/redis/src/redis-benchmark -h 192.168.1.58 -p 6380 -c 1000 -n 10000000 -r 100000 -t set --threads 15
        ```

        > **说明：** 
        >- 0-3：绑定CPU到编号0-3的CPU核上运行，前述查询得到NUMA node0所用CPU核为0-23，需要保证在此范围。
        >- /home/opt：Redis安装路径。
        >- 192.168.1.58：bind的IP地址，请根据实际情况替换为K-NET配置文件中填写的IP地址。
        
        回显示例如下：
        
        ```text
        ====== SET ======
        10000000 requests completed in 30.68 seconds
        1000 parallel clients
        3 bytes payload
        keep alive: 1
        host configuration "save": 900 1 300 10 60 10000
        host configuration "appendonly": no
        multi-thread: yes
        threads: 15

        0.00% <= 0.2 milliseconds
        0.00% <= 0.3 milliseconds
        20.77% <= 0.4 milliseconds
        92.43% <= 0.5 milliseconds
        ...
        325913.38 requests per second
        ```

        则set性能为325913.38rps，实际性能以运行为准。
    2. 测试get值。
        
        ```bash
        taskset -c 0-3 /home/opt/redis/src/redis-benchmark -h 192.168.1.58 -p 6380 -c 1000 -n 10000000 -r 100000 -t get --threads 15
        ```

        回显示例如下：
        
        ```text
        ====== GET ======
        100000000 requests completed in 300.59 seconds
        1000 parallel clients
        3 bytes payload
        keep alive: 1
        host configuration "save": 900 1 300 10 60 10000
        host configuration "appendonly": no
        multi-thread: yes
        threads: 15

        0.00% <= 0.2 milliseconds
        0.00% <= 0.3 milliseconds
        35.39% <= 0.4 milliseconds
        93.07% <= 0.5 milliseconds

        ...
        332677.97 requests per second
        ```

        则get性能为332677.97rps，实际性能以运行为准。

    通过对比可以看到，使用K-NET进行网络加速后，set性能从165196.42rps提升到325913.38rps，get性能从177866.94rps提升到332677.97rps。
5. 测试完成后在服务端结束K-NET进程，按CTRL+C杀掉Redis进程。
6. （可选）测试完成后，若需要恢复到内核态，请在服务端取消DPDK接管网卡。
    
    ```bash
    dpdk-devbind.py -b "hisdk3" 0000:04:00.0
    ```

## 更多功能

关于K-NET多进程加速、零拷贝、共线程等更多特性详细使用方式请参见[特性使用](./feature_guide/feature_menu.md)。
