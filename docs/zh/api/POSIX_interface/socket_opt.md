# socket选项

以下为K-NET支持的socket选项列举。

# SOL\_SOCKET

|option name|K-NET支持程度|说明|
|--|--|--|
|SO_ERROR|支持|只支持两种返回错误的场景，接收数据时TCP连接已断开(ENOTCONN)，UDP发送数据时目的地址不可达(ECONNREFUSED)。|
|SO_KEEPALIVE|支持|-|
|SO_LINGER|支持|在关闭连接时直接发送RST，不支持设置超时时间，阻塞close接口，发送剩余数据。|
|SO_RCVBUF|支持|K-NET配置文件/etc/knet/knet_comm.conf中def_recvbuf设置默认值，max_recvbuf约束最大值。|
|SO_SNDBUF|支持|K-NET配置文件/etc/knet/knet_comm.conf中def_sendbuf设置默认值，max_sendbuf约束最大值。|
|SO_RCVTIMEO|部分支持|秒和微秒时间全为0时，永久阻塞。|
|SO_SNDTIMEO|部分支持|秒和微秒时间全为0时，永久阻塞。|
|SO_REUSEADDR|部分支持|K-NET支持设置，但无实际处理。|
|SO_REUSEPORT|部分支持|K-NET支持多个socket bind相同的地址和端口，但是TCP侦听场景不支持accept负载均衡，UDP场景不支持接收UDP报文场景的负载均衡。|
|SO_PROTOCOL|支持|K-NET支持获取协议。|
|SO_PRIORITY|部分支持|int类型[0, 6]。K-NET支持设置，但无实际处理。|
|SO_RCVLOWAT|支持|unsigned int类型 [0, SO_RCVBUF / 2]。K-NET支持设置。|

# IPPROTO\_IP

|option name|value|K-NET支持程度|说明|
|--|--|--|--|
|IP_TOS|int类型 [0, 0xff]|支持|-|

# IPPROTO\_TCP

|option name|value|K-NET支持程度|说明|
|--|--|--|--|
|TCP_CORK|int类型，0关闭，非0开启|支持|-|
|TCP_DEFER_ACCEPT|int类型，小于等于0关闭功能，大于0则根据时间计算重传次数，最大不超过255次重传|支持|-|
|TCP_INFO|-|支持|只支持获取mss，rtt，发送窗口字段。|
|TCP_KEEPCNT|int类型 [1, 127]|支持|-|
|TCP_KEEPIDLE|int类型 [1, 32767]|支持|-|
|TCP_KEEPINTVL|int类型 [1, 32767]|支持|-|
|TCP_MAXSEG|int类型[256, 9600]|支持|-|
|TCP_NODELAY|int类型 , 0关闭，非0开启|支持|-|
|TCP_USER_TIMEOUT|int类型|支持|-|
