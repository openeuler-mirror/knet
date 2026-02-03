# bind

## 接口名称

**bind(int sockfd, const struct sockaddr \*addr, socklen\_t addrlen\)**

## 接口描述

给socket绑定地址信息。

> **说明：** 
>-   设置REUSEADDR后的行为与设置REUSEPORT一致，以下只描述REUSEPORT。
>-   socket绑定一个地址，未开启REUSEPORT选项，另外一个socket绑定重复地址，返回绑定失败。
>-   socket设置REUSEPORT后，允许多个socket绑定完全相同的IP和端口。
>-   REUSEPORT不支持抢占TIME\_WAIT状态的连接地址，Linux支持。
>-   socket设置绑定0.0.0.0和端口后，其它侦听socket不能够绑定到任何一个本地接口和相同端口，除非开启REUSEPORT。
>-   REUSEPORT不支持socket间的负载均衡，与Linux存在行为不一致。存在多个socket bind相同的地址和端口的socket时，TCP侦听场景不支持accept链接时候的负载均衡，只有1个socket可以收到新的连接，UDP场景不支持接收UDP报文场景的负载均衡，只有1个socket可以收到报文。
>-   当端口为0时，协议栈分配随机端口，随机端口空间默认为[49152-65535]，可以通过预配置修改范围。
>-   socket绑定一个地址时，如果已存在相同本地地址的连接，Linux上为绑定失败，K-NET允许Bind成功，但是在建链时，如果目的地址与已有连接相同，才返回失败。注意：在上一点说明未指定端口号进行随机端口时，可能会随机的端口与已有连接的本地端口相同，建议主动建链场景，如果不指定本地IP端口则不调用Bind，直接在Connect时候随机IP端口。

## 参数说明

|参数|说明|备注|
|--|--|--|
|sockfd|通信节点描述符|传入socket描述符。|
|*addr|通信本端地址|支持IPv4地址，为struct sockaddr_in类型。地址为网络序。默认支持的端口范围为[49152, 65535]。|
|addrlen|地址长度|地址长度，必须与第二个参数的地址长度一致。|


## 返回值

类型：int

-   0：表示成功
-   -1：表示失败，并设置errno以指示错误类型

## 错误码

|错误码|描述|
|--|--|
|EADDRINUSE|指定的地址在使用中，或者无法绑定。|
|EADDRNOTAVAIL|地址不可用，包括广播地址、异常地址（大于255）、无路由信息地址、路由信息异常情况。|
|EAFNOSUPPORT|所指定的地址对于所指定套接字的地址族而言，不是一个有效的地址。|
|EINVAL|套接字已绑定地址，或套接字已被shutdown。|
|EINVAL|入参addrlen不是地址族的有效长度。入参addrlen小于对应地址族长度，或大于INT_MAX。|
|EISCONN|套接字已连接。|
|ELIBBAD|系统符号加载失败。|
|EINVAL|套接字存在，但是套接字对应的数据结构存在异常。|
|EINVAL|共线程部署模式时，sockfd的worker id和当前线程的worker id不一致，即socket跨worker线程调用。|


