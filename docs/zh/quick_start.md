# 快速入门

K-NET（K-Network，网络加速套件）旨在打造一款网络加速套件，提供统一的软件框架，发挥软硬件协同优势，释放网卡硬件性能，详细介绍请参见[产品描述](./introduct/introduct_menu.md)。

## 1、安装K-NET前置准备

编译前请确保Glibc版本及ASLR是否开启。

### 检查Glibc版本

```shell
ldd --version
```

Glibc 2.10及以上版本会开启堆栈保护，若查询出来的版本低于2.10，建议升级至2.10以上。这里以2.28版本为例。

```shell
yum update glibc-2.28
```

### 检查ASLR是否开启

ASLR是一种针对缓冲区溢出的安全保护技术，通过地址布局的随机化，增加攻击者预测目的地址的难度。

```shell
cat /proc/sys/kernel/randomize_va_space
```

若结果不为2，请执行以下命令开启ASLR。

```shell
bash -c 'echo 2 >/proc/sys/kernel/randomize_va_space'
```

### 安装依赖

<term>K-NET</term>编译依赖用户态协议栈的实现，当前以一个示例用户态协议栈为依赖编译K-NET。

<term>K-NET</term>在代码仓中提供了统一的编译构建脚本build.py，可以直接执行该脚本进行编译构建RPM包，这一步会自动拉取开源securec、cJSON和dpdk。

```shell
python build.py rpm #生成Release版本
```

若需要调试版本，执行以下命令:

```shell
python build.py debug rpm  
```

执行以下操作进入构建目录找到rpm包并安装，这里以ARM环境为例。

```shell
cd ./build/rpmbuild/RPMS
rpm -ivh ./knet-1.0.0.aarch64.rpm
```

若安装过<term>K-NET</term>可执行以下命令升级:

```shell
rpm -Uvh ./knet-1.0.0.aarch64.rpm --force --nodeps
```

### Redis安装

上传源码，解压并进入源码目录。

```shell
cd redis-6.0.20
make && make install
```

## 2、配置运行环境

使用用户态协议栈需要配置相关环境, 比如大页等。

### 加载vfio驱动

硬直通模式：

```shell
modprobe vfio enable_unsafe_noiommu_mode=1
modprobe vfio-pci
```

### 配置大页内存 

服务端为物理机场景：

```shell
cat /sys/class/net/ens6f0/device/numa_node # ens6f0根据实际使用的网口替换
# 假设回显所在NUMA为1
echo never > /sys/kernel/mm/transparent_hugepage/enabled # 关闭透明大页
echo 2 > /sys/devices/system/node/node1/hugepages/hugepages-1048576kB/nr_hugepages # 具体node编号根据查询到的网口所在NUMA进行更改
echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages # 根据实际配置的大页调整
```

服务端为虚拟机场景：

```shell
echo never > /sys/kernel/mm/transparent_hugepage/enabled # 关闭透明大页
echo 2 > /sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages
```

确认大页配置成功：

```shell
grep Huge /proc/meminfo
# 也可以用dpdk查看当前大页内存配置信息
dpdk-hugepages.py -s
```

### 配置动态库查找路径环境变量

```shell
echo "/<K-NET path>/opensource/dpdk/output/lib64" >> /etc/ld.so.conf
echo "/usr/local/lib64" >> /etc/ld.so.conf
echo "/usr/local/lib" >> /etc/ld.so.conf
echo "/usr/lib64" >> /etc/ld.so.conf
echo "/usr/lib" >> /etc/ld.so.conf
ldconfig
```

### DPDK接管网卡

如果是mlx网卡硬直通场景不需要执行dpdk-devbind。DPDK的安装请参见[安装DPDK](./installation/installation.md#dpdk安装)。

以华为SP670网口enp6s0为例，具体以网口function名为准。

```shell
ip link set dev enp6s0 down
dpdk-devbind.py -b vfio-pci enp6s0
```

## 3、修改K-NET配置文件并试用单进程加速能力

### K-NET配置

```shell
vim /etc/knet/knet_comm.conf
```

MAC填绑定网卡的MAC地址。IP填绑定网卡的IP地址。
ctrl_vcpu_ids为控制面线程绑核，需要在有效核号内，且需要与数据面分开。数据面绑核见dpdk配置项core_list_global参数。其他配置参数可参考[配置参考](./configuration_item_reference.md)。

### 服务端中运行Redis

```shell
LD_PRELOAD=/usr/lib64/libknet_frame.so redis-server /usr/local/redis/redis.conf --port 6380 --bind 192.168.1.6
```

>说明：
>bind的ip 192.168.1.6 替换为具体网卡配置的IP地址。

### 客户端运行Redis

```shell
taskset -c 33-62 /path/redis-6.0.20/src/redis-benchmark -h 192.168.1.6 -p 6380 -c 1000 -n 10000000 -r 100000 -t set --threads 15
taskset -c 33-62 /path/redis-6.0.20/src/redis-benchmark -h 192.168.1.6 -p 6380 -c 1000 -n 100000000 -r 10000000 -t get --threads 15
```

>说明：
>bind的ip 192.168.1.6 替换为具体网卡配置的IP地址。

### 清理set数据，提升get性能

```shell
redis-cli -h 192.168.1.6 -p 6380 flushall
```

>说明：
>bind的ip 192.168.1.6 替换为具体网卡配置的IP地址。

关于K-NET的安装详情请参见[安装](./installation/install_menu.md)。
K-NET更多特性详细使用方式请参见[特性指南](./feature/feature_menu.md)。
