# 配置项参考

<term>K-NET</term>配置文件在目录“/etc/knet/knet\_comm.conf”。

> **说明：** 
>由于解析配置文件涉及到cJSON库数字存储机制（所有数字类型配置项在解析时都会先被存储为双精度浮点数），因此会出现无法区分1和1.0、1e0、1E0等实际数值相等但是表现形式不同的数字的现象，为了避免歧义，建议用户修改配置文件时使用常规整数，例如1。

版本信息配置项如下：

|  配置项    |   说明 |缺省值 |
|----------|--------|-----|
|  version |  K-NET版本信息|"2.0.0"|

## 通用配置项

|  配置项    |   说明 |缺省值 | 取值范围|
|---------- |--------|-----|-----|
|mode              |     运行模式。<li>0：表示单进程模式。</li><li>1：表示多进程模式。</li>|  0 |0，1|
|log_level         |日志级别。|"WARNING"|"ERROR"，"WARNING"，"INFO"，"DEBUG" <p>**说明：** </p> 支持小写、大小写混写。|
|ctrl_vcpu_nums    |控制面绑核数量。<p>**约束：**</p><li>仅在单进程模式下设置多核生效。</li><li>需要与ctrl_vcpu_ids实际核数保持一致。</li>|1|1~8|
|ctrl_ring_per_vcpu|每个控制线程的内核转发队列数。<p> **约束：**</p> 仅在单进程模式下设置生效。|1|1~8|
|ctrl_vcpu_ids     |控制面绑核，主要将控制面绑定的核号固定。如果需要绑多个核，可以设置为： `"ctrl_vcpu_ids": [`<p>`0,1,2,3`</p>`],`<p> **说明：**</p> <li>控制面绑核核号小于最大vCPU个数。如果vCPU个数小于最大值：127，那么最大值取最大vCPU个数-1。</li><li>物理机场景：vCPU个数为实际CPU个数。</li><li>虚拟机场景：vCPU个数为配置虚拟机的个数。</li>|[0]|0~127|
|zcopy_enable      |<p>零拷贝使能开关。</p><li>0：表示不开启。</li><li>1：表示开启。</li>|0|0，1|
|cothread_enable   |<p>共线程使能开关。</p><li>0：表示不开启。</li><li>1：表示开启。</li>|0|0，1|

## 网络接口配置项

|  配置项    |   说明 |缺省值 | 取值范围|
|----------|--------|-----|-----|
|user_bond_enable|     <p>Bond功能使能开关。</p><li>0：表示不开启。</li><li>1：表示开启。</li>|  0 |0，1|
|  user_bond_mode  |Bond模式。当前仅支持模式4。|4|4|
|bdf_nums|需要DPDK驱动接管的接口BDF号。需要配置为非空，如果为空则返回错误。<p> **说明：**</p> bond_enable为0时，此配置项仅填写1个BDF号；bond_enable为1时，多个网口需按如下形式填写BDF号。<p> `"bdf_nums": [` <p> `"0000:06:00.0",` `"0000:06:00.1"`</p><p>`],`</p>|["0000:06:00.0"]|非空|
|mac    |网络设备的MAC地址。需要配置为非空，如果为空则返回错误。 <p>**约束：**</p> MAC与DPDK接管网卡的MAC地址保持一致。|"52:54:00:2e:1b:a0"|非空|
|ip     |网络设备需要配置的IP地址。需要配置为非空，如果为空则返回错误。|"192.168.1.6"|非空|
|netmask|网络设置配置的子网掩码。需要配置为非空，如果为空则返回错误。|"255.255.255.0"|非空|
|gateway|网络设备需要配置的网关地址。需要配置为非空，如果为空则返回错误。|"0.0.0.0"|非空|
|mtu    |链路协商MTU配置，单位byte。</p> **约束：**<p>受限于网卡MTU的取值范围。|1500|256~9600|

## 硬件卸载配置项

本节所有配置项均仅支持TCP，不支持UDP。

|  配置项    |   说明 |缺省值 | 取值范围|
|----------|--------|-----|-----|
| tso          |  TCP Segmentation Offload使能标志，默认关闭。<li> 0：表示不使能<term>TSO</term>。</li><li> 1：表示使能TSO，且需要确保本节配置中的tcp_checksum处于使能状态。</li>   | 0 |0，1|
| lro          | Large Receive Offload使能标志，默认关闭。<li> 0：表示不使能<term>LRO</term>。</li><li> 1：表示使能LRO，且需要确保本节配置中的tcp_checksum处于使能状态。</li>  | 0 |0，1|
| tcp_checksum | TCP/IP硬件校验和特性开关，默认关闭。<li> 0：表示关闭。</li><li>  1：表示使能。 </li>| 0 |0，1|
| bifur_enable | 流量分叉特性开关，默认关闭。<li>0：表示关闭。</li><li> 1：表示使能网卡硬件流量分叉功能。 </li><li>  2：表示使能软件内核流量转发功能。 </li>| 0 |0，1，2|

## 用户态TCP/IP协议栈配置项

|  配置项    |   说明 |缺省值 | 取值范围|
|----------|--------|-----|-----|
| max_mbuf   |   MBUF初始化时规模大小，单位个。<p> **说明：** </p><p>max_mbuf推荐配置如下公式所得值：</p><p> max_mbuf = tx_cache_size *max_worker_num + rx_cache_size* max_worker_num + (max_tcpcb + max_udpcb) *业务实例个数* 4 + 2048 * 客户端业务实例个数。<li> 最大值受限于[DPDK配置项](#dpdk配置项)中socket_limit。</li><li> 如果将此配置项调大，须同时将[DPDK配置项](#dpdk配置项)中socket_limit调大。</li>| 20480 |8192~1073741823|
| max_worker_num|  整个进程最大用户态TCP/IP协议栈实例数量。例如配置为2，则有效workerId为[0,1]。 </p> **说明：** </p> TM280用户使用的max_worker_num个数需为4的倍数。 | 1 |1~32|
|max_route    | 最大路由数量。在多路由表的情况下，这里指所有路由表路由数量总和。 |1024|1~100000|
|max_arp      |最大已解析ARP表项数量。 |1024|8~8192|
| max_tcpcb   |最大TCP控制块数量，一个TCP socket对应一个TCP控制块。<p> **说明：** </p> 约束max_tcpcb+max_udpcb<=ulimit -n -1024。在终端查看OS限制的文件句柄数量：<p>`ulimit -n`</p> 如果回显小于max_tcpcb+max_udpcb+1024，可以通过如下命令设置：<p>`ulimit -n 20480`</p>将文件句柄的数量限制提高到20480。用户根据自己需求设置具体的数值。-n：选项后面通常跟一个数字，用来设置可以打开的文件描述符的最大数量。|4096|32~1000000|
|max_udpcb    |最大UDP控制块数量，一个UDP socket对应一个UDP控制块。<p> **说明：** </p> 约束max_tcpcb+max_udpcb<=ulimit -n -1024。在终端查看OS限制的文件句柄数量：<p>`ulimit -n`</p> 如果回显小于max_tcpcb+max_udpcb+1024，可以通过如下命令设置：<p>`ulimit -n 20480`</p>将文件句柄的数量限制提高到20480。用户根据自己需求设置具体的数值。-n：选项后面通常跟一个数字，用来设置可以打开的文件描述符的最大数量。 |4096|32~1000000|
| tcp_sack    | TCP支持SACK的标志，默认开启。<li>0：表示关闭。</li><li> 1：表示使能。</li>| 1 |0，1|
| tcp_dack    |  TCP延迟ACK功能开关，默认开启。<li>0：表示关闭。</li><li> 1：表示使能。 </li>| 1 |0，1|
|msl_time     | MSL老化时间，单位秒。 | 30 |1~30|
|fin_timeout  | FIN_WAIT_2定时器超时时间，单位秒。若用户设置该超时时间在范围1~600s内时，超时后，会发送RST并上报DISCONNECTED事件，并关闭链接；否则按照协议正常超时断链，不会发送RST。  | 600|1~600|
|min_port     |不指定端口号进行绑定时，随机的端口号范围最小值。|49152|1~49152|
|max_port     |不指定端口号进行绑定时，随机的端口号范围最大值。|65,535|50000~65,535|
|max_sendbuf  |允许的TCP socket发送缓冲区最大的大小，单位byte。此配置会影响setsockopt、SO_SNDBUF可配置的大小。|10,485,760|8192~2147483647|
|def_sendbuf  |TCP socket发送缓冲区默认大小，单位byte，且不能大于max_sendbuf。|8192|8192~max_sendbuf|
|max_recvbuf  |允许的TCP socket接收缓冲区最大的大小，单位byte。此配置会影响setsockopt、SO_RCVBUF可配置的大小。|10,485,760|8192~2147483647|
|def_recvbuf  |TCP socket接收缓冲区默认大小，单位byte，且不能大于max_recvbuf。|8192|8192~max_recvbuf|
|tcp_cookie   |TCP是否支持COOKIE功能开关。<li> 0：表示不支持COOKIE功能。</li><li> 1：表示支持COOKIE功能。开启COOKIE功能后如果同时建链达到门限则触发COOKIE机制。</li>|0|0，1|
|reass_max    |系统缓存真重组节点总个数，单位个。一个节点缓存一条流的分片报文，目前缓存分片报文的最大个数为系统缓存的真重组节点个数的两倍。|1000|1~4096|
|reass_timeout|真重组节点超时时间，单位秒。|30|1~30|
|synack_retries|SYN-ACK重传次数。|5|1~255|
|zcopy_sge_len|零拷贝写缓冲区最大长度，单位为字节。表示应用能申请的零拷贝iov的iov_len的最大值。|65535|0~512*1024|
|zcopy_sge_num|零拷贝写缓冲区个数，即写缓冲区内存池中内存单元的数量。<p> **说明：** </p> zcopy_sge_num推荐配置如下公式所得值：<p>`zcopy_sge_num = (def_sendbuf / iov_len * 3) * tcp最大链接数 + tx_cache_size * max_worker_num  + 2048`</p><li> iov_len表示应用申请的写缓冲区iov的iov_len的最大值，建议将应用中的iov_len的最大值作为配置项zcopy_sge_len的值。</li><li> 最大值受限于[DPDK配置项](#dpdk配置项)中socket_limit。</li><li> 如果将此配置项调大，须同时将[DPDK配置项](#dpdk配置项)中socket_limit调大。</li>|8192|8192~1073741823|
|epoll_data|epoll特有标识数据，需确保和业务使用的epoll_event.data.u64不同。用户需要保证此配置项的值与业务使用的epoll_event.data.u64不同，否则会漏掉该内核事件。|"0"|"0"~"18446744073709551615"|

## DPDK配置项

|  配置项    |   说明 |缺省值 | 取值范围|
|----------|--------|-----|-----|
|core_list_global|数据面绑核，将数据面绑定的核号固定。<p>用数字列表设置应用程序使用的CPU核，例如：“0,1”，表示绑定0号核和1号核。</p><p>多进程模式下，绑多个核时，可以使用“-”，如“1-10”，表示绑定1号核到10号核。</p><p> **注意：**</p> <li>绑定核的个数必须等于max_worker_num，仅除开启cothread_enable后，此配置项不会进行读取与校验。</li><li>ctrl_vcpu_ids指定的控制线程核号和此配置项指定的核号必须不同。</li><li>TM280用户使用core_list_global绑核的个数需为4的倍数。</li>|"1"|0~服务器的CPU个数-1|
|queue_num|所有worker使用的队列数。<li>单进程：总的队列个数，均分到每个worker上。</li><li>多进程：无效，每个从进程默认一个队列。</li><p> **说明：**</p> 单进程模式下：<p>- 仅单进程，可配置`queue_num>=max_worker_num`，此时worker会共享使能的队列。</p><p>  - 开共线程时，即“cothread_enable”: 1时：max_worker_num为1，queue_num可配置大于等于1；max_worker_num大于1，queue_num需要与max_worker_num一致。</p><p>- 开流分叉时，即“bifur_enable”：1时：默认queue_num最大值为8；用户可根据实际需要配置32队列，参考[流量分叉支持配置32队列](./Feature_Guide/Traffic_Bifurcation.md#流量分叉支持配置32队列)支持配置32队列，此时queue_num最大值为32。</p>|1|1~64|
|tx_cache_size|发送缓存大小，单位个。<p> **约束：**</p>tx_cache_size *业务实例个数 + rx_cache_size* 业务实例个数 < max_mbuf。业务实例个数为实际启动的业务进程个数。|256|256~16384|
|rx_cache_size|接收缓存大小，单位个。<p> **约束：**</p>tx_cache_size *业务实例个数 + rx_cache_size* 业务实例个数 < max_mbuf。业务实例个数为实际启动的业务进程个数。|256|256~16384|
|socket_mem   |预分配每个socket大页内存大小，单位MB。<p>多个参数间请使用“,”分隔。例如：</p>`"socket_mem" : "--socket-mem=1024,2048"`<p>表示在0号socket上预分配1024M，在1号socket上分配2048M。</p>|--socket-mem=1024|0~服务器分配现有的可用大页内存总量|
|socket_limit |限制每个socket上可分配的最大内存。不支持传统内存模式。单位MB。<p> **约束：**</p>socket_limit大于等于socket_mem的内存数。|--socket-limit=1024|0~服务器分配现有的可用大页内存总量|
|external_driver|不同场景填写不同的PMD驱动。注意前面有个-d。<p>SP670</p>`"external_driver" : "-dlibrte_net_hinic3.so"`<p> TM280</p>`"external_driver" : ""`|-dlibrte_net_hinic3.so|-dlibrte_net_hinic3.so，置空|
|telemetry    |统计信息的开关。<li>0：表示不开启。</li><li>1：表示开启。</li>|1|0，1|
|huge_dir     |大页挂载路径。<p>例如：</p>`"huge_dir" : "--huge-dir=/home/username/hugepages"`|-|-|
|base-virtaddr|DPDK主进程内存映射起始虚拟地址基地址。<p>例如：</p>`"base-virtaddr": "--base-virtaddr=0x100000000"`|-|-|
