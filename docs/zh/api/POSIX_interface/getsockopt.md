# getsockopt

## 接口名称

**getsockopt(int sockfd, int level, int optname, void \*optval, socklen\_t \*optlen\)**

## 接口描述

获取socket选项值。

## 参数说明

|参数|说明|备注|
|--|--|--|
|sockfd|通信节点描述符|传入socket描述符。|
|level|选项级别|支持SOL_SOCKET、IPPROTO_IP、IPPROTO_TCP。|
|optname|选项名称|<li>SO_REUSEADDR：地址重用，0表示关闭；非0表示开启，默认关闭。</li><li>SO_REUSEPORT：端口重用，0表示关闭；非0表示开启，默认关闭。</li><li>SO_KEEPALIVE：保活选项，0表示关闭；非0表示开启，默认关闭。</li><li>SO_LINGER：linger选项，默认关闭。</li><li>SO_SNDBUF：获取发送缓存即高水位。</li><li>SO_RCVBUF：获取接收缓存即高水位。</li><li>SO_SNDTIMEO：获取阻塞模式发送超时时间 。</li><li>SO_RCVTIMEO：获取阻塞模式接收等待超时时间 。</li><li>SO_ERROR：获取socket的错误信息，读取后内部清零。</li><li>SO_PROTOCOL：获取套接字协议类型。</li><li>TCP_NODELAY：获取是否禁止TCP的Nagle算法，默认开启Nagle算法。</li><li>TCP_KEEPIDLE：获取指定的空闲时间后启动保活探测的时间，单位秒。</li><li>TCP_KEEPINTVL：获取保活探测的时间间隔，单位秒。</li><li>TCP_KEEPCNT：获取保活探测的次数。</li><li>TCP_CORK：获取cork选项。1表示开启，0表示关闭，默认关闭。</li><li>TCP_DEFER_ACCEPT：获取延迟建链的超时时间，单位秒。</li><li>TCP_MAXSEG：获取设置的TCP最大报文段。</li><li>TCP_INFO：获取TCP信息，只能获取部分信息。</li><li>IP_TOS：获取设置的TOS。</li>|
|*optval|选项值|非空指针。|
|*optlen|选项长度|非空指针，长度随optval的类型而定。|

## 返回值

类型：int

- 0：表示成功
- -1：表示失败，并设置errno以指示错误类型

## 错误码

|错误码|描述|
|--|--|
|EFAULT|入参optval为空指针，或入参optlen为空指针。|
|ENOPROTOOPT|协议不支持该选项。|
|EINVAL|指定的选项在指定的套接字级别无效。入参optlen的值大于INT_MAX，或者入参optlen的值小于对应长度。|
|EOPNOTSUPP|对应level不支持。|
|ELIBBAD|系统符号加载失败。|
|EINVAL|套接字存在，但是套接字对应的数据结构存在异常。|
|EINVAL|共线程部署模式时，sockfd的worker id和当前线程的worker id不一致，即socket跨worker线程调用。|
