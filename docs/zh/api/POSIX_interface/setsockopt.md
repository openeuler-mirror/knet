# setsockopt

## 接口名称

**setsockopt(int sockfd, int level, int optname, const void \*optval, socklen\_t optlen\)**

## 接口描述

设置socket选项值。

## 参数说明

|参数|说明|备注|
|--|--|--|
|sockfd|通信节点描述符|传入socket描述符。|
|level|选项级别|支持SOL_SOCKET、IPPROTO_IP、IPPROTO_TCP。|
|optname|选项名称|<li>SO_REUSEADDR：地址重用，0表示关闭；非0表示开启，默认关闭。<li>SO_REUSEPORT：端口重用，0表示关闭；非0表示开启，默认关闭。<li>SO_KEEPALIVE：保活选项，0表示关闭；非0表示开启，默认关闭。<li>SO_LINGER：linger选项，默认关闭 。<li>SO_SNDBUF：获取发送缓存高水位，默认TCP 8k、UDP 9k。设置值必须比默认值大，否则返回错误。<li>SO_RCVBUF：获取接收缓存高水位，默认TCP 8k、UDP 40k。设置值必须比默认值大，否则返回错误。<li>SO_SNDTIMEO：阻塞模式发送超时时间 。<li>SO_RCVTIMEO：阻塞模式接收等待时间。<li>SO_PRIORITY：设置套接字发送数据包的优先级，规格为[0，6]。<li>SO_RCVLOWAT：设置recv()返回所需的最小字节数（低水位标记）。<li>TCP_NODELAY：设置是否禁止TCP的Nagle算法，默认开启Nagle算法。<li>TCP_KEEPIDLE：在指定的空闲时间后启动保活探测，单位秒，规格为[1, 32767] 。<li>TCP_KEEPINTVL：设置保活探测的时间间隔，单位秒，规格为[1, 32767] 。<li>TCP_KEEPCNT：设置保活探测的次数，规格为[1, 127] 。<li>TCP_CORK：设置cork选项。1表示开启，0表示关闭，默认关闭。<li>TCP_DEFER_ACCEPT：子socket收到数据再上报侦听socket建链完成，单位秒。<li>TCP_MAXSEG:设置TCP最大报文段，缺省IPV4 1460，范围[88, 32767]。<li>TCP_USER_TIMEOUT:设置用户超时时间，单位毫秒。<li>IP_TOS：设置TOS。|
|*optval|选项值|非空指针。|
|optlen|选项长度|长度随optval的类型而定。|


## 返回值

类型：int

-   0：表示成功
-   -1：表示失败，并设置errno以指示错误类型

## 错误码

|错误码|描述|
|--|--|
|EFAULT|入参optval为空指针。|
|ENOPROTOOPT|协议不支持该选项。|
|EINVAL|指定的选项在指定的套接字级别无效。入参optlen的值大于INT_MAX，或者入参optlen的值小于对应长度。|
|EDOM|发送和接收超时值太大，无法放入套接字结构中的超时值字段。|
|EISCONN|套接字已经连接，且在套接字处于连接状态时不能设置指定的选项。|
|ELIBBAD|系统符号加载失败。|
|EINVAL|套接字存在，但是套接字对应的数据结构存在异常。|
|EINVAL|共线程部署模式时，sockfd的worker id和当前线程的worker id不一致，即socket跨worker线程调用。|


