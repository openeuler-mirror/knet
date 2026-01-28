# Summary
提供Socket透明替换接口支撑业务零侵入修改；可提供用户态TCP/IP高性能协议栈，实现数据面高性能加速功能。面向存在网络瓶颈的业务。向下接入多种协议栈，向上提供透明POSIX接口。

# Usage Example
API参考POSIX接口
部署文档参考[README](../README.md)
配置参考如下：
| 配置项 | 说明 | 默认值 | 取值范围 | 约束说明 |
|--------|------|--------|----------|----------|
| **mode** | 运行模式<br>• 0：表示单进程模式<br>• 1：表示多进程模式，仅用户态协议栈可用 | 0 | 0, 1 | |
| **common** | 通用配置项 | - | - | - |
| **log_level** | 日志级别 | "WARNING" | "ERROR", "WARNING", "INFO", "DEBUG" | 支持大小写混写 |
| **ctrl_vcpu_nums** | 控制面CPU核数 | 1 | 1~n | K-NET控制核处理核数<br> |
| **ctrl_ring_per_vcpu** | 控制面每个cpu分配多少缓存 | 1 | 1~n | 控制面每个cpu分配多少缓存 |
| **ctrl_vcpu_ids** | 控制面使用核号 | 0 | 0~n | 控制面使用核号 |
| **zcopy_enable** | 开启零拷贝特性 | 0 | 0,1 | / |
| **cothread_enable** | 开启共线程特性 | 0 | 0,1 | / |
| **interface** | 网络接口配置项 | - | - | - |
| **user_bond_enable** | 开启bond模式 | 0 | 0，1 | / |
| **user_bond_mode** | mode模式 | 4 | 4 | 当前只支持bond4 |
| **bdf_nums** | 需要DPDK驱动接管的接口BDF号 | "0000:06:00.0" | / | 非空 |
| **mac** | 网络设备的MAC地址 | "52:54:00:2e:1b:a0" | / | 非空 |
| **ip** | 网络设备需要配置的IP地址 | "192.168.1.6" | / | 非空 |
| **netmask** | 网络设置配置的子网掩码 | "255.255.255.0" | / | 非空 |
| **gateway** | 网络设备需要配置的网关地址 | "0.0.0.0" | / | 非空 |
| **mtu** | 链路协商MTU配置，单位byte | 1500 | / | 256~9600 |
| **hw_offload** | 硬件卸载配置项 | - | - | - |
| **tso** | TCP Segmentation Offload使能标志，默认关闭 | 0 | 0, 1 | 1：表示使能TSO，且需要确保tcp_checksum必须使能 |
| **lro** | Large Receive Offload使能标志，默认关闭 | 0 | 0, 1 | 1：表示使能LRO，且需要确保tcp_checksum必须使能 |
| **tcp_checksum** | TCP/IP硬件校验和特性开关，默认关闭 | 0 | 0, 1 | / |
| **bifur_enable** | 流分叉开关 | 0 | 0, 1,2 | 0关闭分叉，1开启流分叉，2内核流量软件转发 |
| **proto_stack** | 用户态TCP/IP协议栈配置项 | - | - | - |
| **max_mbuf** | MBUF初始化时规模大小，单位个 | 20480 | 8192~2147483647 | / |
| **max_worker_num** | 整个进程最大用户态TCP/IP协议栈实例数量 | 1 | 1~32 | / |
| **max_route** | 最大路由数量 | 1024 | 1~100000 | / |
| **max_arp** | 最大已解析ARP表项数量 | 1024 | 8~8192 | / |
| **max_tcpcb** | 最大TCP控制块数量，一个TCP socket对应一个TCP控制块 | 4096 | 32~1000000 | 约束max_tcpcb+max_udpcb<=ulimit -n |
| **max_udpcb** | 最大UDP控制块数量，一个UDP socket对应一个UDP控制块 | 4096 | 32~1000000 | 约束max_tcpcb+max_udpcb<=ulimit -n |
| **tcp_sack** | TCP支持SACK的标志，默认开启 | 1 | 0,1 | / |
| **tcp_dack** | TCP延迟ACK功能开关，默认开启 | 1 | 0,1 | / |
| **msl_time** | MSL老化时间，单位秒 | 30 | 1-30 | / |
| **fin_timeout** | FIN_WAIT_2定时器超时时间，单位秒 | 600 | 1-600 | / |
| **min_port** | 不指定端口号进行绑定时，随机的端口号范围最小值 | 49152 | 1~49152 | / |
| **max_port** | 不指定端口号进行绑定时，随机的端口号范围最大值 | 65536 |  | / |
| **msl_time** | 不指定端口号进行绑定时，随机的端口号范围最大值 | 65535 | 50000~65535 | / |
| **max_sendbuf** | 允许的TCP socket发送缓冲区最大的大小，单位byte | 10485760 | 8192~2147483647 | / |
| **def_sendbuf** | TCP socket发送缓冲区默认大小，单位byte，且不能大于max_sendbuf | 8192 | 8192~max_sendbuf | / |
| **max_recvbuf** | 允许的TCP socket接收缓冲区最大的大小，单位byte | 10485760 | 8192~2147483647 | / |
| **def_recvbuf** | TCP socket接收缓冲区默认大小，单位byte，且不能大于max_recvbuf | 8192 | 8192~max_recvbuf | / |
| **tcp_cookie** | TCP是否支持COOKIE功能开关,1表示支持COOKIE功能。开启COOKIE功能后如果同时建链达到门限则触发COOKIE机制 | 0 | 0,1 | / |
| **reass_max** | 系统缓存真重组节点总个数，单位个 | 1000 | 1-4096 | / |
| **reass_timeout** | 真重组节点超时时间，单位秒 | 30 | 1-30 | / |
| **synack_retries** | synack重传最大次数 | 5 | 1~n | / |
| **zcopy_sge_len** | 零拷贝单片申请内存长度 | 65535 | 1-65535 | / |
| **zcopy_sge_num** | 零拷贝内存片数量 | 8192 | 1~n | 不超过定长内存池最大申请内存 |
| **dpdk** | DPDK配置项 | - | - | - |
| **core_list_global** | 数据面绑核，将数据面绑定的核号固定。用数字列表设置应用程序使用的CPU核，例如：“0,1”，表示绑定0号核和1号核。多进程模式下，绑多个核时，可以使用“-”，如“1-10”，表示绑定1号核到10号核 | "1" | 0~服务器的CPU个数-1 | / |
| **tx_cache_size** | 发送缓存大小，单位个 | 256 | 256-16384 | / |
| **rx_cache_size** | 接收缓存大小，单位个| 256 | 256-16384 | / |
| **socket_mem** | 预分配每个socket大页内存大小，单位MB | "--socket-mem=1024" | 0~服务器分配现有的可用大页内存总量 | / |
| **socket_limit** | 限制每个socket上可分配的最大内存。不支持传统内存模式。单位MB | "--socket-limit=1024" | 0~服务器分配现有的可用大页内存总量 | / |
| **external_driver** | 不同场景填写不同的pmd驱动。注意前面有个-d | "-dlibrte_net_hinic3.so" | "-dlibrte_net_hinic3.so"、置空 | / |
| **telemetry** | 统计信息的开关 | 1 | 0,1 | |
| **huge_dir** | 大页挂载路径 | / | / | / |
| **base-virtaddr** | dpdk启动基地址 | / | / | / |


# Movitvation
作为兼容用户态协议栈的框架式加速库，主要有以下设计走向
1）资源管理：
    A、提供基础的资源管理能力，如内存、内存池、定时器，配置管理，日志等；
    B、提供底层加速库DPDK接口封装 ；
    C、提供软硬件协同抽象接口使能硬件加速特性；    
2）数据转发转发：用户态DPDK；
3）协议栈适配层：为协议栈提供基本的资源管理能力；该适配层用于适配协议栈南向接口；

# Design constraints
初始化dpdk控制线程直接走os
概述：TrafficResourcesInit初始化时，发现是dpdk控制线程，直接返回走os api
背景/原因：所有线程原先TrafficResourcesInit发现已经在初始化，需要等待初始化完成，然后用dp api。但是bond场景下dpdk控制线程负责bond状态机更新，如果也等待TrafficResourcesInit初始化完成，会导致bond状态机无法更新，所以TrafficResourcesInit初始化线程永远等不到bond状态更新，超时之后bond就初始化失败了。所以修改成TrafficResourcesInit时发现是dpdk控制线程直接走os api

主动建链时，非K-NET的进程不得使用分配给K-NET使用的随机端口，即与K-NET端口区间不要交叉
背景/原因：K-NET主动建链时，会随机选择端口与对端建链，并下区间流表，如果不进行端口隔离，即内核使用已经下区间流表的port进行主动建链，内核业务进程将不会收到包，包都经由流表发送给K-NET用户态协议栈。

# Adoption strategy
Redis应用可以无感劫持使用
