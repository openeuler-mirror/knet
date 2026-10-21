# socket

介绍K-NET的socket接口，面向应用开发工程师。涵盖创建通信节点的功能说明、参数说明、返回值及错误码，并注明支持的通信域与协议类型。适用于网络应用开发中创建套接字的场景。

## 接口名称

**socket(int domain, int type, int protocol)**

## 接口描述

创建通信节点并返回描述符。

## 参数说明

|参数|说明|备注|
|--|--|--|
|domain|通信域|支持AF_INET。|
|type|通信语义类型|支持SOCK_STREAM、SOCK_DGRAM、SOCK_CLOEXEC（默认支持该type，且不可修改）、SOCK_NONBLOCK。|
|protocol|协议类型|支持IPPROTO_TCP（type必须为SOCK_STREAM）。支持IPPROTO_UDP（type必须为SOCK_DGRAM）。支持IPPROTO_IP，即设置为0。|

### socket选项

以下为<term>K-NET</term>支持的socket选项列举。

### SOL_SOCKET

|option name|K-NET支持程度|说明|
|--|--|--|
|SO_ERROR|支持|只支持两种返回错误的场景，接收数据时TCP连接已断开（ENOTCONN），UDP发送数据时目的地址不可达（ECONNREFUSED）。|
|SO_KEEPALIVE|支持|-|
|SO_LINGER|支持|在关闭连接时直接发送RST（Reset，重置标志位），不支持设置超时时间，阻塞close接口，发送剩余数据。|
|SO_RCVBUF|支持|K-NET配置文件/etc/knet/knet_comm.conf中def_recvbuf设置默认值，max_recvbuf约束最大值。|
|SO_SNDBUF|支持|K-NET配置文件/etc/knet/knet_comm.conf中def_sendbuf设置默认值，max_sendbuf约束最大值。|
|SO_RCVTIMEO|部分支持|秒和微秒时间全为0时，永久阻塞。|
|SO_SNDTIMEO|部分支持|秒和微秒时间全为0时，永久阻塞。|
|SO_REUSEADDR|部分支持|K-NET支持设置，但无实际处理。|
|SO_REUSEPORT|部分支持|K-NET支持多个socket bind相同的地址和端口，但是TCP侦听场景不支持accept负载均衡，UDP场景不支持接收UDP报文场景的负载均衡。|
|SO_PROTOCOL|支持|K-NET支持获取协议。|
|SO_PRIORITY|部分支持|int类型[0, 6]。K-NET支持设置，但无实际处理。|
|SO_RCVLOWAT|支持|unsigned int类型 [0, SO_RCVBUF / 2]。K-NET支持设置。|

### IPPROTO_IP

|option name|value|K-NET支持程度|说明|
|--|--|--|--|
|IP_TOS|int类型 [0, 0xff]|支持|-|

### IPPROTO_TCP

|option name|value|K-NET支持程度|说明|
|--|--|--|--|
|TCP_CORK|int类型，0关闭，非0开启|支持|-|
|TCP_DEFER_ACCEPT|int类型，小于等于0关闭功能，大于0则根据时间计算重传次数，最大不超过255次重传|支持|-|
|TCP_INFO|-|支持|只支持获取mss，rtt，发送窗口字段。|
|TCP_KEEPCNT|int类型 [1, 127]|支持|-|
|TCP_KEEPIDLE|int类型 [1, 32767]|支持|-|
|TCP_KEEPINTVL|int类型 [1, 32767]|支持|-|
|TCP_MAXSEG|int类型[88, 32767]|支持|-|
|TCP_NODELAY|int类型，0关闭，非0开启|支持|-|
|TCP_USER_TIMEOUT|int类型|支持|-|

## 返回值

类型：int

- 正数：socketfd，表示成功
- -1：表示失败，并设置errno以指示错误类型

## 错误码

|错误码|描述|
|--|--|
|EINVAL|未知协议，或协议族不可用。|
|EMFILE|进程的file数量超过max_tcpcb。|
|ENOMEM|内存申请失败。|
|EPROTONOSUPPORT|domain不支持指定协议。|
|ENAVAIL|KNET资源初始化失败。|
|EACCES|进程没有适当的权限。|
|ENOBUFS|系统中的可用资源不足，无法执行该操作。|
|EMFILE|进程的file数量超过最大文件描述符数。通过ulimit -n查看。|
|ENFILE|系统的file数量超过上限。|
|ELIBBAD|系统库符号加载失败。|
|EPERM|信号退出流程中，不允许调用该函数接口。|
|EINVAL|共线程部署模式时，socketfd的worker id和当前线程的worker id不一致，导致socket跨worker线程调用。|
