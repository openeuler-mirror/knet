# 运维特性

##### 查看网卡收发包

dpdk-telemetry适配后除了查看网口收发包、错包、丢包之外，还能查看TCP等网络状态。K-NET应用启动后，运行`dpdk-telemetry.py -f knet -i 1`进入命令状态，quit可退出。

常用运维命令：

- 查看网卡收发包、错包、丢包状态。

    ```bash
    /ethdev/stats,<port_id> 
    ```

    >**说明：** 
    >port\_id为网口BDF号的port\_id，不是Redis侦听端口。
    >执行/ethdev/list命令可查看DPDK接管网口BDF号的port\_id。

- 查看K-NET的TCP相关统计：TCP连接状态统计、各类报文统计、异常打点统计、内存使用统计。

    ```bash
    /knet/stack/tcp_stat,[pid]
    /knet/stack/conn_stat,[pid]
    /knet/stack/pkt_stat,[pid]
    /knet/stack/abn_stat,[pid]
    /knet/stack/mem_stat,[pid]
    ```

    主要查看imissed, ierrors, oerrors等丢包、错包数据，示例如下，正常情况应为0。

    ```json
    --> /ethdev/stats,0
    {"/ethdev/stats": {"ipackets": 937835, "opackets": 934833, "ibytes": 95559003, "obytes": 66376175, "imissed": 0, "ierrors": 0, "oerrors": 0, "rx_nombuf": 0, "q_ipackets": [937835, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "q_opackets": [934835, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "q_ibytes": [95559003, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "q_obytes": [66376317, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "q_errors": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]}}
    --> /knet/stack/tcp_stat
    {"/knet/stack/tcp_stat": {"Accepts": 1001, "Closed": 1, "Connects": 1001, "DelayedAck": 5717013, "RcvAckBytes": 28580120, "RcvAckPkts": 5719020, "RcvBytes": 205812653, "RcvBytesPassive": 205812653, "RcvTotal": 5720023, "RcvPkts": 5717017, "RcvFIN": 1, "RTTUpdated": 5717020, "SndBytes": 28581869, "SndBytesPassive": 28581869, "SndCtl": 1003, "SndPkts": 5716366, "SndAcks": 1003, "SndTotal": 5717369, "SndFIN": 1, "TcpUserAccept": 1001, "TcpRcvOutBytes": 205796561}}
    ...
    
    ```

    若存在丢包、错包，则表示网络存在异常，可结合dumpcap抓包工具进一步排查异常。

##### 查看协议栈测统计信息

K-NET应用启动后，运行`dpdk-telemetry.py -f knet -i 1`进入命令状态，quit可退出。

查看协议栈测统计信息命令（单进程模式下pid参数可忽略，多进程模式下需要指定pid参数）：

```bash
/knet/stack/tcp_stat,[pid]     # TCP相关统计
/knet/stack/conn_stat,[pid]    # TCP连接状态统计
/knet/stack/pkt_stat,[pid]     # 协议栈各类报文统计
/knet/stack/abn_stat,[pid]     # 协议栈异常打点统计
/knet/stack/mem_stat,[pid]     # 协议栈内存使用统计
/knet/stack/pbuf_stat,[pid]    # 协议栈内存使用统计
```

> 说明：tcp相关状态统计返回时，字段的值为0则不会显示该字段；异常信息打点统计返回时，字段的值为0则不会显示该字段
> 
运行样例：

```json
--> /knet/stack/tcp_stat
{"/knet/stack/tcp_stat": {"Accepts": 38, "Closed": 36, "Connects": 38, "DelayedAck": 147512040, "RcvAckBytes": 2895, "RcvAckPkts": 306764079, "RcvNoTcpHash": 163, "RstByRcvErrAckFlags": 755, "RcvBytes": 6579120164411, "RcvBytesPassive": 6585309755644, "RcvDupBytes": 19257, "RcvDupPkts": 23, "RcvAfterWndPkts": 26, "RcvAfterWndBytes": 53048, "RcvPartDupBytes": 4048, "RcvPartDupPkts": 16, "RcvOutOrderPkts": 344404, "RcvOutOrderBytes": 5883122608, "RcvTotal": 306764892, "RcvPkts": 306366193, "RcvWndUpdate": 1, "RcvFIN": 8, "RTTUpdated": 59, "SndBytes": 2889, "SndBytesPassive": 2889, "SndCtl": 159278679, "SndPkts": 24, "SndAcks": 159277761, "SndTotal": 159278703, "SndWndUpdate": 25698, "DropCtlPkts": 755, "DropDataPkts": 8, "SndRST": 948, "SndFIN": 6, "RespChallAcks": 12, "RstTimeWaitTimerDrops": 1, "TcpUserAccept": 38, "RstRcvNonRstPkt": 163, "RstRcvBufNotClean": 30, "TcpReassSucBytes": 6175640088, "TcpRcvOutBytes": 6556257641765, "TcpCloseNoRcvDataLen": 7824846}}
```

```json
--> /knet/stack/conn_stat
{"/knet/stack/conn_stat": {"Listen": 1, "SynSent": 0, "SynRcvd": 0, "PAEstablished": 1000, "ACEstablished": 0, "PACloseWait": 0, "ACCloseWait": 0, "PAFinWait1": 0, "ACFinWait1": 0, "PAClosing": 0, "ACClosing": 0, "PALastAck": 0, "ACLastAck": 0, "PAFinWait2": 0, "ACFinWait2": 0, "PATimeWait": 0, "ACTimeWait": 0, "Abort": 0}}
```

```json
--> /knet/stack/pkt_stat
{"/knet/stack/pkt_stat": {"LinkInPkts": 0, "EthInPkts": 306765055, "NetInPkts": 306765055, "IcmpOutPkts": 0, "ArpDeliverPkts": 0, "IpBroadcastDeliverPkts": 0, "NonFragDelverPkts": 306765055, "UptoCtrlPlanePkts": 0, "ReassInFragPkts": 0, "ReassOutReassPkts": 0, "NetOutPkts": 159278703, "EthOutPkts": 159278703, "FragInPkts": 0, "FragOutPkts": 0, "ArpMissResvPkts": 0, "ArpSearchInPkts": 0, "ArpHaveNormalPkts": 0, "RcvIcmpPkts": 0, "NetBadVersionPkts": 0, "NetBadHdrLenPkts": 0, "NetBadLenPkts": 0, "NetTooShortPkts": 0, "NetBadChecksumPkts": 0, "NetNoProtoPkts": 0, "NetNoRoutePkts": 0, "TcpReassPkts": 397814, "UdpInPkts": 0, "UdpOutPkts": 0, "TcpInPkts": 306765218, "SndBufInPkts": 49, "SndBufOutPkts": 24, "SndBufFreePkts": 1, "RcvBufInPkts": 306764000, "RcvBufOutPkts": 305385285, "RcvBufFreePkts": 0, "Ip6InPkts": 0, "Ip6TooShortPkts": 0, "Ip6BadVerPkts": 0, "Ip6BadHeadLenPkts": 0, "Ip6BadLenPkts": 0, "Ip6MutiCastDeliverPkts": 0, "Ip6ExtHdrCntErrPkts": 0, "Ip6ExtHdrOverflowPkts": 0, "Ip6HbhHdrErrPkts": 0, "Ip6NoUpperProtoPkts": 0, "Ip6ReassInFragPkts": 0, "Ip6FragHdrErrPkts": 0, "Ip6OutPkts": 0, "Ip6FragOutPkts": 0, "KernelFdirCacheMiss": 0, "IpDevTypeNoMatch": 0, "IpCheckAddrFail": 0, "IpLenOverLimit": 0, "IpReassOverTblLimit": 0, "IpReassMallocFail": 0, "IpReassNodeOverLimit": 0, "IpIcmpAddrNotMatch": 0, "IpIcmpPktLenShort": 0, "IpIcmpPktBadSum": 0, "IpIcmpNotPortUnreach": 0, "IpIcmpUnreachTooShort": 0, "IpIcmpUnreachTypeErr": 0, "Ip6DevTypeErr": 0, "Ip6CheckAddrFail": 0, "Ip6ReassOverTblLimit": 0, "Ip6ReassMallocFail": 0, "Ip6ReassNodeOverLimit": 0, "Ip6ProtoErr": 0, "Ip6IcmpTooShort": 0, "Ip6IcmpBadSum": 0, "Ip6IcmpNoPayload": 0, "Ip6CodeNomatch": 0, "Icmpv6TooBigShort": 0, "Icmpv6TooBigSmall": 0, "Icmpv6TooBigExthdrErr": 0, "Icmpv6TooBigNotTcp": 0, "IpBadOffset": 0, "NfPreRoutingDrop": 0, "NfLocaInDrop": 0, "NfForwardDrop": 0, "NfLocalOutDrop": 0, "NfPostRoutingDrop": 0, "UdpIcmpUnReachShort": 0, "Ip6IcmpUnReachTooShort": 0, "Icmp6UnReachExthdrErr": 0, "Icmp6UnReachNotUdp": 0, "UdpIcmp6UnReachShort": 0, "GsoOutPkts": 0}}
```

```json
--> /knet/stack/abn_stat
{"/knet/stack/abn_stat": {"FdGetClosed": 12, "SockReadBufchainShort": 32913714, "CpdSyncTableRecvErr": 23432272}}
```

```json
--> /knet/stack/mem_stat
{"/knet/stack/mem_stat": {"InitInitMem": 0, "InitFreeMem": 0, "CpdInitMem": 0, "CpdFreeMem": 0, "DebugInitMem": 9360, "DebugFreeMem": 16400, "NetdevInitMem": 664, "NetdevFreeMem": 527264, "NamespaceInitMem": 5760, "NamespaceFreeMem": 0, "PbufInitMem": 0, "PbufFreeMem": 0, "PmgrInitMem": 0, "PmgrFreeMem": 0, "ShmInitMem": 0, "ShmFreeMem": 0, "TbmInitMem": 176, "TbmFreeMem": 33192, "UtilsInitMem": 0, "UtilsFreeMem": 0, "WorkerInitMem": 1584, "WorkerFreeMem": 0, "FdInitMem": 528448, "FdFreeMem": 192, "EpollInitMem": 0, "EpollFreeMem": 0, "PollInitMem": 0, "PollFreeMem": 0, "SelectInitMem": 0, "SelectFreeMem": 0, "SocketInitMem": 0, "SocketFreeMem": 0, "NetlinkInitMem": 0, "NetlinkFreeMem": 0, "PacketInitMem": 0, "PacketFreeMem": 0, "EthInitMem": 0, "EthFreeMem": 0, "IprawInitMem": 0, "IprawFreeMem": 0, "IpInitMem": 432, "IpFreeMem": 0, "Ip6InitMem": 0, "Ip6FreeMem": 0, "TcpInitMem": 1573904, "TcpFreeMem": 525432, "UdpInitMem": 0, "UdpFreeMem": 168}}
```

```json
--> /knet/stack/pbuf_stat
{"/knet/stack/pbuf_stat": {"ipFragPktNum": 0, "tcpReassPktNum": 397814, "sendBufPktNum": 24, "recvBufPktNum": 1378715}}
```

##### 查看流表信息

K-NET应用启动后，运行`dpdk-telemetry.py -f knet -i 1`进入命令状态，quit可退出。

查看流表命令：

```bash
/knet/flow/list,<start_flow_index> <flow_cnt>
```

可查看K-NET应用下的索引号从`start_flow_index`开始`flow_cnt`条流表信息。受到dpdk限制，`flow_cnt`参数值最大为256。当`flow_cnt`为0时，会在dpdk输出长度限制内返回最多的流表信息。运行时参数都需要指定。

```json
-->/knet/flow/list,0 1
{"/knet/flow/list": {"flow0": {"dip": "192.168.1.98", "dipMask": "0xffffffff","dport": "6380", "dportMask": "0xffff", "sip": "0.0.0.0", "sipMask": "0x0", "sport": "0", "sportMask": "0","protocol":"EHT IPV4 TCP","action":"RSS-0,1,2,3"}}}
```

`flow0`代表流表索引号从0开始的流表，其中`dip`表示流表目的ip，`dipMask`表示目的ip掩码，`dport`表示目的端口，`dportMask`表示目的端口掩码，`sip`表示源ip，`sipMask`表示源ip掩码，`sport`表示源端口，`sportMask`表示源端口掩码，`protocol`表示协议匹配类型，`action`表示该五元组匹配使用的队列。

##### 查看网卡队列分配情况

K-NET应用启动后，运行`dpdk-telemetry.py -f knet -i 1`进入命令状态，quit可退出。

查看流表命令：

```bash
/knet/ethdev/queue
```

可查看K-NET应用下的所有队列使用信息。

```json
-->/knet/ethdev/queue
{"/knet/ethdev/queue": {"queue0": {"pid": 5837, "tid": 5840, "lcoreId": 18}, "queue1": {"pid": 5837, "tid": 5841, "lcoreId": 38}, "queue2": {"pid": 5837, "tid": 5842, "lcoreId": 68}, "queue3": {"pid": 5837, "tid": 5843, "lcoreId": 98}}}
```

`queue0`表示队列号从0开的分配队列，`pid`表示队列0分配给了进程5837使用，`tid`表示队列0分配给了线程5840使用, `lcoreId`表示队列0分配给了dpdk的18号逻辑核使用。

##### 获取tcp/udp/epoll句柄个数

K-NET应用启动后，运行`dpdk-telemetry.py -f knet -i 1`进入命令状态，quit可退出。

获取tcp/udp/epoll句柄个数命令：

```bash
/knet/stack/fd_count,[pid] <type>
```

单进程模式下pid参数可忽略，多进程模式下需要指定pid参数，type支持tcp、udp和epoll三种类型。

```json
-->/knet/stack/fd_count,2351 tcp
{"/knet/stack/fd_count": "3"}
```

回显为对应套接字类型句柄个数。

##### 获取所有tcp/udp连接信息

K-NET应用启动后，运行`dpdk-telemetry.py -f knet -i 1`进入命令状态，quit可退出。

持获取所有tcp/udp连接信息命令：

```bash
/knet/stack/net_stat,<pid> <start_fd> <fd_cnt>
```

`pid`为进程id，`start_fd`为获取连接信息的起始fd，`fd_cnt`为从`start_fd`开始获取连接信息的最多fd个数。由于dpdk输出限制，`fd_cnt`最大为256，当`fd_cnt`参数为0时，会在dpdk输出长度限制内输出最多的连接信息。运行时参数都需要指定。

```json
-->/knet/stack/net_stat,2351 56 10
{"/knet/stack/net_stat": {"osFd 56": {"pf": "AF_INET", "proto": "TCP", "lAddr": "192.168.1.98", "lPort": 4890, "rAddr": "0.0.0.0", "rPort": 0, "state": "LISTEN", **"worker_tid": 0, "innerFd": 20}, "osFd 59": {"pf": "AF_INET", "proto": "TCP", "lAddr": "192.168.1.98", "lPort": 4890, "rAddr": "192.168.1.166", "rPort": 51440, "state": "ESTABLISHED", "worker_tid": "44885", "innerFd": 21}, "osFd 60": {"pf": "AF_INET", "proto": "UDP", "lAddr": "192.168.1.98", "lPort": 4890, "rAddr": "192.168.1.166", "rPort": 55658, "state": "INVALID", "worker_tid": 0, "innerFd": 22}}}
```

##### 查看单个tcp/udp连接详细信息

K-NET应用启动后，运行`dpdk-telemetry.py -f knet -i 1`进入命令状态，quit可退出。

支持获取所有tcp/udp连接信息命令：

```bash
/knet/stack/socket_info,[pid] <fd>
```

单进程模式下`pid`参数可忽略，多进程模式下需要指定`pid`参数。`fd`为被劫持的文件描述符，运行时需要指定，可通过获取所有tcp/udp连接信息命令选择osFd值填入。

```bash
-->/knet/stack/socket_info,2351 56
{"/knet/stack/socket_info": {"SockInfo": {"protocol": "TCP", "isLingerOnoff": 0, "isNonblock": 1, "isReuseAddr": 1, "isReusePort": 0, "isBroadcast": 0, "isKeepAlive": 0, "isBindDev": 0, "isDontRoute": 0, "options": 6, "error": 0, "pf": "AF_INET", "linger": 0, "flags": 80, "state": 0, "rdSemCnt": 0, "wrSemCnt": 0, "rcvTimeout": -1, "sndTimeout": -1, "sndDataLen": 0, "rcvDataLen": 0, "sndLowat": 1, "sndHiwat": 1048576, "rcvLowat": 1, "rcvHiwat": 1048576, "bandWidth": 0, "priority": 0, "associateFd": 0, "notifyType": 1, "wid": -1}, "InetSkInfo": {"ttl": 0, "tos": 0, "mtu": 0, "isIncHdr": 0, "isTos": 0, "isTtl": 0, "isMtu": 0, "isPktInfo": 0, "isRcvTos": 0, "isRcvTtl": 0}, "TcpBaseInfo": {"state": "Listen", "connType": "Passive", "noVerifyCksum": 0, "ackNow": 0, "delayAckEnable": 1, "nodelay": 0, "rttRecord": 0, "cork": 0, "deferAccept": 0, "flags": 0, "wid": -1, "txQueid": -1, "childCnt": 0, "backlog": 511, "accDataCnt": 0, "accDataMax": 2, "dupAckCnt": 0, "caAlgId": 0, "caState": 0, "cwnd": 0, "ssthresh": 0, "seqRecover": 0, "reorderCnt": 3, "rttStartSeq": 0, "srtt": 0, "rttval": 0, "tsVal": 0, "tsEcho": 0, "lastChallengeAckTime": 0, "fastMode": 0, "sndQueSize": 0, "rcvQueSize": 0, "rexmitQueSize": 0, "reassQueSize": 0}, "TcpTransInfo": {"lport": 0, "pport": 0, "synOpt": 15, "negOpt": 0, "rcvWs": 0, "sndWs": 0, "rcvMss": 0, "mss": 1460, "iss": 0, "irs": 0, "sndUna": 0, "sndNxt": 0, "sndMax": 0, "sndWnd": 0, "sndUp": 0, "sndWl1": 0, "sndSml": 0, "rcvNxt": 0, "rcvWnd": 0, "rcvMax": 0, "rcvWup": 0, "idleStart": 0, "keepIdle": 14400, "keepIntvl": 150, "keepProbes": 9, "keepProbeCnt": 0, "keepIdleLimit": 0, "keepIdleCnt": 0, "backoff": 0, "maxRexmit": 0, "rexmitCnt": 0, "userTimeout": 0, "userTimeStartFast": 0, "userTimeStartSlow": 0, "fastTimeoutTick": 32768, "slowTimeoutTick": 32768, "delayAckTimoutTick": 32768, "synRetries": 0}}}
```

##### 查看Epoll详细信息

K-NET应用启动后，运行`dpdk-telemetry.py -f knet -i 1`进入命令状态，quit可退出。

查看Epoll信息命令：

```bash
/knet/stack/epoll_stat,<pid> <start_epoll_fd> <epoll_fd_cnt> <start_socket_fd> <socket_fd_cnt>
```

`pid`为目标进程ID。`start_epoll_fd`为遍历epoll实例的起始文件描述符。`epoll_fd_cnt`为从`start_epoll_fd`开始最多返回的有效epoll描述符数量，当`epoll_fd_cnt`参数为0时，会在dpdk输出长度限制内返回最多的有效epoll描述符数量。`start_socket_fd`为在单个epoll监听集合中，待查询socket的起始文件描述符。`socket_fd_cnt`为从`start_socket_fd`开始最多返回的有效socket描述符数量，当`socket_fd_cnt`参数为0时，会在dpdk输出长度限制内返回最多的有效socket描述符信息。`epoll_fd_cnt`和`socket_fd_cnt`受到dpdk限制，最大为256。

运行时所有参数需要指定。

>说明：查询epoll详细信息时，由于 dpdk-telemetry 响应存在最大消息长度限制，当返回数据过长时，可能会导致响应被截断，造成 details 字段丢失或 JSON 格式损坏。
>可通过减小 epoll_fd_cnt 和 socket_fd_cnt 参数值，降低单次查询的输出长度，确保响应完整返回。
>当连接数过多时，推荐设置socket_fd_cnt参数范围为1-100

```json
-->/knet/stack/epoll_stat,26058 0 1 0 1
{"/knet/stack/epoll_stat": {"epoll_73": {"pid": "26058", "tid": "-", "osFd": "73", "inner_fd": "0", "details": {"socket_2": {"fd": "2", "expectEvents": "0x1", "readyEvents": "0", "notifiedEvents": "0", "shoted": "0"}}}}}
```

tid仅在开启共线程时有意义，主要查看details条目中每个连接的套接字的监听事件expectedEvents，就绪事件readyEvents，上报事件notifiedEvents（边缘触发模式下有效），进行问题定位。

##### 查看网卡带宽、包率

K-NET应用启动后，运行`dpdk-telemetry.py -f knet -i 1`进入命令状态，quit可退出。

查看网卡带宽、包率命令：

```bash
/knet/ethdev/usage,<port> <time>
```

>说明：/knet/ethdev/usage统计的带宽包含以太网帧中的数据，包括各层协议头部。

一般使用流程如下：

1.第一个参数值网卡的port号，通过如下命令查看当前dpdk管理了port：

```bash
/ethdev/list
```

回显：

```json
-->/ethdev/list
{"ethdev_list": [0]}
```

2.根据查询到的port号，查询带宽核包率：

```bash
/knet/ethdev/usage,0 1
```

回显：

```json
--> /knet/ethdev/usage,0 1
{"/knet/ethdev/usage": {"0-1s": {"tx": "9.74 Mbit/s, 18453 p/s", "rx": "640.22 Mbit/s, 50450 p/s"}}}

```

3.第二个输出参数 time 可以控制查看多长时间段的带宽和包率：

```bash
/knet/ethdev/usage,0 5
```

回显：

```json
--> /knet/ethdev/usage,0 5
{"/knet/ethdev/usage": {"0-1s": {"tx": "9.74 Mbit/s, 18455 p/s", "rx": "643.83 Mbit/s, 50792 p/s"}, "1-2s": {"tx": "9.73 Mbit/s, 18434 p/s", "rx": "648.21 Mbit/s, 51287 p/s"}, "2-3s": {"tx": "9.76 Mbit/s, 18477 p/s", "rx": "649.77 Mbit/s, 51331 p/s"}, "3-4s": {"tx": "9.75 Mbit/s, 18464 p/s", "rx": "645.81 Mbit/s, 50949 p/s"}, "4-5s": {"tx": "9.75 Mbit/s, 18460 p/s", "rx": "643.58 Mbit/s, 50752 p/s"}}}
```

##### 查看持久化统计信息

无需运行dpdk-telemetry.py，可在终端直接查看。

查看持久化统计信息命令：

```bash
jq . /etc/knet/run/stats/knet-persist.json
```

回显：

```json
{
  "/ethdev/xstats/port0": {
    "rx_good_packets": 43898,
    "tx_good_packets": 70,
    "rx_good_bytes": 2633880,
    "tx_good_bytes": 5876,
    "rx_missed_errors": 0,
    "rx_errors": 0,
    "tx_errors": 0,
    ...
```

统计信息包含日期，DPDK统计信息xstats，K-NET相关TCP统计，TCP连接状态统计，协议栈各类报文统计，协议栈异常打点统计，协议栈内存使用统计，协议栈PBUF使用统计。K-NET启动后每秒定期写入到文件，以便异常退出时拿到统计信息方便定位。

#### 获取网络包

1. 确保已完成[配置大页内存](./environment_configuration.md#配置大页内存)，并在“dpdk-stable-21.11.7/app/dumpcap”目录执行下列操作可开启K-NET抓包。

    ```bash
    chmod a+s /usr/lib64/librte_net_hinic3.so 
    setcap cap_sys_rawio,cap_dac_read_search,cap_sys_admin+ep dumpcap  
    LD_PRELOAD=librte_net_hinic3.so ./dumpcap -w /home/<username>/tx.pcap
    ```

2. 抓包完成后，“Ctrl + C”结束，在/home/**_<username\>_**/下生成tx.pcap。
3. 可使用`tcpdump -r /home/_<username\>_/tx.pcap -v`查看抓包文件或使用Wireshark打开查看。
4. 使用`tcpdump -r /home/<username\>/tx.pcap`读取数据包，查看数据包详情，操作示例如下：

    ![](../figures/zh-cn_image_0000002535828395.png)

    - 正常情况应该有ARP建链包，如上图存在ARP请求和回应。
    - 若存在无法建链、丢包、无法接收数据包等异常情况，请先在ping场景下抓包测试，确保网络链路正常，进一步再通过数据包细节排查。

## 日志管理

日志记录于/var/log/knet/knet\_comm.log当中，根据/etc/knet/knet\_comm.conf配置中log\_level配置输出相应级别日志，可选级别包括ERROR、WARNING、INFO、DEBUG。日志仅会输出小于等于设置的级别。例如设置WARNING级别，仅会记录ERROR和WARNING级别日志。

1. 检查系统日志健康状态。

    ```bash
    systemctl status rsyslog
    ```

    示例显示如下：

    ```ColdFusion
    rsyslog.service - System Logging Service
         Loaded: loaded (/usr/lib/systemd/system/rsyslog.service; enabled; vendor preset: enabled)
         Active: active (running) since Mon 2024-11-25 17:24:56 CST; 4h 56min ago
           Docs: man:rsyslogd(8)
                 https://www.rsyslog.com/doc/
        Process: 1651 ExecStartPost=/bin/bash /usr/bin/timezone_update.sh (code=exited, status=0/SUCCESS)
       Main PID: 1030 (rsyslogd)
          Tasks: 3 (limit: 93973)
         Memory: 5.9M
         CGroup: /system.slice/rsyslog.service
                 └─ 1030 /usr/sbin/rsyslogd -n -i/var/run/rsyslogd.pid
    ```

    若“Active”显示为“active\(running\)”，表示正常。否则表示rsyslog服务异常，请参见后续操作恢复。

2. 重启rsyslog服务。

    ```bash
    systemctl restart rsyslog
    ```

    重启后再次检查状态。
