# 安装K-NET

## 安装依赖
K-NET编译依赖用户态协议栈的实现,当前以一个示例用户态协议栈为依赖编译K-NET。

`K-NET`在代码仓中提供了统一的编译构建脚本build.py，可以直接执行该脚本进行编译构建rpm包，这一步会自动拉取开源securec, cJSON, dpdk
```shell
$ python build.py rpm
```
执行以下操作进入构建目录找到rpm包并安装，这里以ARM环境为例
```shell
$ cd ./build/rpmbuild/RPMS
$ rpm -ivh ./ubs-knet-1.0.0.aarch64.rpm --force --nodeps
```

## redis安装

上传源码、解压并进入源码目录

```shell
$ cd redis-6.0.20
$ make && make install
```

# 运行环境配置

使用用户态协议栈需要配置相关环境, 比如大页等

## 加载vfio驱动

硬直通模式：

```shell
$ modprobe vfio enable_unsafe_noiommu_mode=1
$ modprobe vfio-pci
```

## 配置大页内存 

服务端为物理机场景：
```shell
$ cat /sys/class/net/ens6f0/device/numa_node # ens6f0根据实际使用的网卡名替换
# 假设回显所在numa为1
$ echo never > /sys/kernel/mm/transparent_hugepage/enabled # 关闭透明大页
$ echo 2 > /sys/devices/system/node/node1/hugepages/hugepages-1048576kB/nr_hugepages # 具体node编号根据查询到的网卡所在NUMA进行更改
# $ echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages # 根据实际配置的大页调整
```
服务端为虚拟机场景：
```shell
$ echo never > /sys/kernel/mm/transparent_hugepage/enabled # 关闭透明大页
$ echo 2 > /sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages
```
确认大页配置成功：
```shell
$ grep Huge /proc/meminfo
# 也可以用dpdk查看当前大页内存配置信息
$ dpdk-hugepages.py -s
```

## 配置动态库查找路径环境变量

```shell
$ echo "/<K-NET path>/opensource/dpdk/output/lib64" >> /etc/ld.so.conf
$ echo "/usr/local/lib64" >> /etc/ld.so.conf
$ echo "/usr/local/lib" >> /etc/ld.so.conf
$ echo "/usr/lib64" >> /etc/ld.so.conf
$ echo "/usr/lib" >> /etc/ld.so.conf
$ ldconfig
```

## dpdk接管网卡

如果是mlx网卡硬直通场景不需要执行dpdk-devbind。

以华为SP670网口enp6s0为例，具体以网口function名为准。

```shell
$ ip link set dev enp6s0 down
$ dpdk-devbind.py -b vfio-pci enp6s0
```

# 修改KNET配置文件

## KNET配置

```shell
$ vim /etc/knet/knet_comm.conf
```
mac填绑定网卡的mac。
ip填绑定网卡的ip。
ctrl_vcpu_id 为控制面线程绑核，需要在有效核号内，且需要与数据面分开。数据面绑核见dpdk配置项core_list_global参数。其他配置参数可参考参数[配置参考](./Desgin_docs_Reference.md)


## 服务端中运行redis

```shell
$ LD_PRELOAD=/usr/lib64/libknet_frame.so redis-server /usr/local/redis/redis.conf --port 6380 --bind 192.168.1.6
# 注：bind的ip 192.168.1.6 替换为具体网卡配置的ip
```

## 客户端运行redis

```shell
$ taskset -c 33-62 /path/redis-6.0.20/src/redis-benchmark -h 192.168.1.6 -p 6380 -c 1000 -n 10000000 -r 100000 -t set --threads 15
$ taskset -c 33-62 /path/redis-6.0.20/src/redis-benchmark -h 192.168.1.6 -p 6380 -c 1000 -n 100000000 -r 10000000 -t get --threads 15
# 注：bind的ip 192.168.1.6 替换为具体网卡配置的ip
```

## 清理set数据，提升get性能

```shell
$ redis-cli -h 192.168.1.6 -p 6380 flushall
# 注：ip 192.168.1.6 替换为具体网卡配置的ip
```
