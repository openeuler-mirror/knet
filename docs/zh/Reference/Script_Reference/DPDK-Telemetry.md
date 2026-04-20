# dpdk-telemetry.py遥测信息获取脚本

## 命令功能

获取网卡统计信息。

## 命令格式

> **说明：** 
>
>- 普通用户进入工具使用界面前需设置“XDG\_RUNTIME\_DIR”环境变量，如果新开终端，需要在新起的终端中导入。环境变量路径涉及的权限及安全需要用户保证。参考[相关业务配置](../../Feature_Guide/Environment_Configuration.md)进行设置。
>- 服务端环境关闭或重启后需要重新执行步骤。
>- 通过设置环境变量指定运行时目录，路径依据不同用户名会有差异。
>- 退出普通用户再重新切换到该用户需要重新配置。

服务端进入工具使用界面：

```bash
dpdk-telemetry.py -f knet -i 1
```

> **说明：** 
>
>- -f ：指定knet为DPDK运行时目录提供文件前缀。
>- -i 1：指定DPDK应用程序实例号为1。

## 命令参数

**表 1**  命令参数说明（dpdk-telemetry.py）

|命令参数|是否必选|示例|说明|
|--|--|--|--|
|`/ethdev/list`|否|-|获取ethdev端口列表。|
|`/ethdev/stats,<port>`|否|/ethdev/stats,0|获取ethdev端口的基本统计信息。port取值为/ethdev/list命令获取到的列表项。|
|`/ethdev/xstats,<port>`|否|/ethdev/xstats,0|获取ethdev端口的扩展统计信息。port取值为/ethdev/list命令获取到的列表项。|
|`/knet/stack/tcp_stat,[pid]`|单进程模式可不带pid, 多进程模式下必填pid|/knet/stack/tcp_stat<br> /knet/stack/tcp_stat,12345|TCP相关统计。pid取值必须为有效的进程ID。|
|`/knet/stack/conn_stat,[pid]`|单进程模式可不带pid, 多进程模式下必填pid|/knet/stack/conn_stat<br>/knet/stack/conn_stat,12345|TCP连接状态统计。pid取值必须为有效的进程ID。|
|`/knet/stack/pkt_stat,[pid]`|单进程模式可不带pid, 多进程模式下必填pid|/knet/stack/pkt_stat<br>/knet/stack/pkt_stat,12345|协议栈各类报文统计。pid取值必须为有效的进程ID。|
|`/knet/stack/abn_stat,[pid]`|单进程模式可不带pid, 多进程模式下必填pid|/knet/stack/abn_stat<br>/knet/stack/abn_stat,12345|协议栈异常打点统计。pid取值必须为有效的进程ID。|
|`/knet/stack/mem_stat,[pid]`|单进程模式可不带pid, 多进程模式下必填pid|/knet/stack/mem_stat<br>/knet/stack/mem_stat,12345|协议栈内存使用统计。pid取值必须为有效的进程ID。|
|`/knet/stack/pbuf_stat,[pid]`|单进程模式可不带pid, 多进程模式下必填pid|/knet/stack/pbuf_stat<br>/knet/stack/pbuf_stat,12345|协议栈PBUF使用统计。pid取值必须为有效的进程ID。|
|`/knet/stack/fd_count,[pid] <type>`|单进程模式可不带pid, 多进程模式下必填pid, type为必填项|/knet/stack/fd_count,tcp<br>/knet/stack/fd_count,12345 tcp|查询各个类型的fd数量。pid取值必须为有效的进程ID，type支持tcp,udp,epoll。|
|`/knet/stack/net_stat,<pid> <start_fd> <fd_cnt>`|所有参数必填|/knet/stack/net_stat,12345 0 10|查询fd从start_fd起始的最多fd_cnt条有效连接信息。pid取值必须为有效的进程ID。|
|`/knet/stack/socket_info,[pid] <fd>`|单进程模式可不带pid多进程模式下必填pid, fd为必填项|/knet/stack/socket_info,1<br>/knet/stack/socket_info,12345 1|查询指定socket fd的详细信息。pid取值必须为有效的进程id，fd是有效的被劫持的内核套接字描述符值，可通过/knet/stack/net_stat查询选择。|
|`/knet/flow/list,<start_flow_index> <flow_cnt>`|所有参数必填|/knet/flow/list,0 1|获取从索引start_flow_index开始的最多flow_cnt条流表信息。|
|`/knet/ethdev/queue`|否|-|获取队列被分配使用的进程、线程号。|
|`/knet/stack/epoll_stat,<pid> <start_epoll_fd> <epoll_fd_cnt> <start_socket_fd> <socket_fd_cnt>`|所有参数必填|/knet/stack/epoll_stat,12345 0 1 0 1|获取从start_epoll_fd开始的epoll_fd_cnt个epoll实例的详细信息，每个epoll实例中包含从start_socket_fd开始、最多socket_fd_cnt个有效的socket描述符信息。pid取值必须为有效的进程ID。|
|`/knet/ethdev/usage,<port> <time>`|是|/knet/ethdev/usage,0 1|port 为网口号，time表示统计带宽、包率的时间段，1表示统计接下来1秒内的的带宽包率，回显输出一条 "0-1s" 的内容。若time 为2，将会输出两条，即 "0-1s"  和 "1-2s" 的内容。|

> **说明：** 
>SP670网卡与TM280网卡当前获取ethdev端口的扩展统计信息使用 /ethdev/xstats,<port\>。当没有客户端产生通信时/knet/stack/tcp\_stat和/knet/stack/abn\_stat命令查询到的信息回显为空。

## 使用示例

> **说明：** 
>使用前需要开启该功能，步骤参考[使用前配置](../../Feature_Guide/OM_Features.md#2-遥测工具dpdk-telemetry)。

```ColdFusion
# dpdk-telemetry.py -f knet -i 1
Connecting to /var/run/dpdk/rte/dpdk_telemetry.v2
{"version": "DPDK 21.11.7", "pid": 2631, "max_output_len": 16384}
Connected to application: "redis-server 192.168.*.*:6379"
--> /ethdev/list
{"/ethdev/list": [0]}
--> /ethdev/stats,0
{"/ethdev/stats": {"ipackets": 10006228, "opackets": 10005020, "ibytes": 1020453075, "obytes": 710344388, "imissed": 0, "ierrors": 0, "oerrors": 0, "rx_nombuf": 0, "q_ipackets": [5014719, 4991509, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "q_opackets": [5014032, 4990988, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "q_ibytes": [511405477, 509047598, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "q_obytes": [355990207, 354354181, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "q_errors": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]}}

--> /knet/stack/tcp_stat
{"/knet/stack/tcp_stat": {"Accepts": 102, "Closed": 102, "Connects": 102, "DelayedAck": 284294, "Drops": 101, "RcvAckBytes": 1421032, "RcvAckPkts": 284501, "RcvBytes": 12793262, "RcvBytesPassive": 12793262, "RcvTotal": 284704, "RcvPkts": 284294, "RcvFIN": 1, "RTTUpdated": 126847, "SndBytes": 1421536, "SndBytesPassive": 1421536, "SndCtl": 104, "SndPkts": 284295, "SndAcks": 104, "SndTotal": 284399, "SndFIN": 1, "TcpUserAccept": 102}}
/knet/stack/conn_stat
{"/knet/stack/conn_stat": {"Listen": 1, "SynSent": 0, "SynRcvd": 0, "PAEstablished": 0, "ACEstablished": 0, "PACloseWait": 0, "ACCloseWait": 0, "PAFinWait1": 0, "ACFinWait1": 0, "PAClosing": 0, "ACClosing": 0, "PALastAck": 0, "ACLastAck": 0, "PAFinWait2": 0, "ACFinWait2": 0, "PATimeWait": 0, "ACTimeWait": 0, "Abort": 0}}
--> /knet/stack/abn_stat
{"/knet/stack/abn_stat": {"AcceptNoChild": 3, "RcvConnRefused": 101, "AbnormalRcvRst": 101, "AcceptCreateErr": 3, "SockSendmsgFailed": 284295, "RecvfromFailed": 101, "SockRecvmsgFailed": 101, "SockReadBufchainZero": 1, "SockReadBufchainShort": 284294}}
--> /knet/stack/pkt_stat
{"/knet/stack/pkt_stat": {"LinkInPkts": 0, "EthInPkts": 68282, "NetInPkts": 0, "IcmpOutPkts": 0, "ArpDeliverPkts": 68282, "IpBroadcastDeliverPkts": 0, "NonFragDelverPkts": 0, "UptoCtrlPlanePkts": 68282, "ReassInFragPkts": 0, "ReassOutReassPkts": 0, "NetOutPkts": 0, "EthOutPkts": 0, "FragInPkts": 0, "FragOutPkts": 0, "ArpMissResvPkts": 0, "ArpSearchInPkts": 0, "ArpHaveNormalPkts": 0, "RcvIcmpPkts": 0, "NetBadVersionPkts": 0, "NetBadHdrLenPkts": 0, "NetBadLenPkts": 0, "NetTooShortPkts": 0, "NetBadChecksumPkts": 0, "NetNoProtoPkts": 0, "NetNoRoutePkts": 0, "TcpReassPkts": 0, "UdpInPkts": 0, "UdpOutPkts": 0, "TcpInPkts": 0, "SndBufInPkts": 0, "SndBufOutPkts": 0, "SndBufFreePkts": 0, "RcvBufInPkts": 0, "RcvBufOutPkts": 0, "RcvBufFreePkts": 0, "Ip6InPkts": 0, "Ip6TooShortPkts": 0, "Ip6BadVerPkts": 0, "Ip6BadHeadLenPkts": 0, "Ip6BadLenPkts": 0, "Ip6MutiCastDeliverPkts": 0, "Ip6ExtHdrCntErrPkts": 0, "Ip6ExtHdrOverflowPkts": 0, "Ip6HbhHdrErrPkts": 0, "Ip6NoUpperProtoPkts": 0, "Ip6ReassInFragPkts": 0, "Ip6FragHdrErrPkts": 0, "Ip6OutPkts": 0, "Ip6FragOutPkts": 0, "KernelFdirCacheMiss": 0}}
--> /knet/stack/mem_stat
{"/knet/stack/mem_stat": {"InitInitMem": 0, "InitFreeMem": 0, "CpdInitMem": 0, "CpdFreeMem": 0, "DebugInitMem": 2664, "DebugFreeMem": 16400, "NetdevInitMem": 104, "NetdevFreeMem": 66200, "NamespaceInitMem": 5760, "NamespaceFreeMem": 0, "PbufInitMem": 0, "PbufFreeMem": 0, "PmgrInitMem": 0, "PmgrFreeMem": 0, "ShmInitMem": 0, "ShmFreeMem": 0, "TbmInitMem": 232, "TbmFreeMem": 49576, "UtilsInitMem": 0, "UtilsFreeMem": 0, "WorkerInitMem": 344, "WorkerFreeMem": 0, "FdInitMem": 528448, "FdFreeMem": 128, "EpollInitMem": 0, "EpollFreeMem": 208, "PollInitMem": 0, "PollFreeMem": 0, "SelectInitMem": 0, "SelectFreeMem": 0, "SocketInitMem": 0, "SocketFreeMem": 0, "NetlinkInitMem": 0, "NetlinkFreeMem": 0, "EthInitMem": 0, "EthFreeMem": 0, "IpInitMem": 168, "IpFreeMem": 0, "Ip6InitMem": 40, "Ip6FreeMem": 0, "TcpInitMem": 393488, "TcpFreeMem": 132848, "UdpInitMem": 0, "UdpFreeMem": 168}}
--> /knet/stack/pbuf_stat
{"/knet/stack/pbuf_stat": {"ipFragPktNum": 0, "tcpReassPktNum": 0, "sendBufPktNum": 0, "recvBufPktNum": 0}}
--> /knet/stack/fd_count,tcp
{"/knet/stack/fd_count": "1"}
--> /knet/stack/net_stat,0 1
{"/knet/stack/net_stat": {"osFd 36": {"pf": "AF_INET", "proto": "TCP", "lAddr": "192.168.14.104", "lPort": 60696, "rAddr": "0.0.0.0", "rPort": 0, "state": "LISTEN", "tid": 4294967295, "innerFd": 1}}}
--> /knet/stack/socket_info,36
{"/knet/stack/socket_info": {"SockInfo": {"protocol": "TCP", "isLingerOnoff": 0, "isNonblock": 1, "isReuseAddr": 1, "isBroadcast": 0, "isKeepAlive": 0, "isBindDev": 0, "isDontRoute": 0, "options": 6, "error": 0, "pf": "AF_INET", "linger": 0, "flags": 80, "state": 0, "rdSemCnt": 0, "wrSemCnt": 0, "rcvTimeout": -1, "sndTimeout": -1, "sndDataLen": 0, "rcvDataLen": 0, "sndLowat": 1, "sndHiwat": 8192, "rcvLowat": 1, "rcvHiwat": 8192, "bandWidth": 0, "priority": 0, "associateFd": 0, "notifyType": 1, "wid": -1}, "InetSkInfo": {"ttl": 0, "tos": 0, "mtu": 0, "isIncHdr": 0, "isTos": 0, "isTtl": 0, "isMtu": 0, "isPktInfo": 0, "isRcvTos": 0, "isRcvTtl": 0}, "TcpBaseInfo": {"state": 1, "connType": 0, "noVerifyCksum": 0, "ackNow": 0, "delayAckEnable": 1, "nodelay": 0, "rttRecord": 0, "cork": 0, "deferAccept": 0, "flags": 0, "wid": -1, "txQueid": -1, "childCnt": 0, "backlog": 511, "accDataCnt": 0, "accDataMax": 2, "dupAckCnt": 0, "caAlgId": 0, "caState": 0, "cwnd": 0, "ssthresh": 0, "seqRecover": 0, "reorderCnt": 3, "rttStartSeq": 0, "srtt": 0, "rttval": 0, "tsVal": 0, "tsEcho": 0, "lastChallengeAckTime": 0, "fastMode": 0, "sndQueSize": 0, "rcvQueSize": 0, "rexmitQueSize": 0, "reassQueSize": 0}, "TcpTransInfo": {"lport": 0, "pport": 0, "synOpt": 15, "negOpt": 0, "rcvWs": 0, "sndWs": 0, "rcvMss": 0, "mss": 1460, "iss": 0, "irs": 0, "sndUna": 0, "sndNxt": 0, "sndMax": 0, "sndWnd": 0, "sndUp": 0, "sndWl1": 0, "sndSml": 0, "rcvNxt": 0, "rcvWnd": 0, "rcvUp": 0, "rcvWup": 0, "idleStart": 0, "keepIdle": 14400, "keepIntvl": 150, "keepProbes": 9, "keepProbeCnt": 0, "keepIdleLimit": 0, "keepIdleCnt": 0, "backoff": 0, "maxRexmit": 0, "rexmitCnt": 0, "userTimeout": 0, "userTimeStartFast": 0, "userTimeStartSlow": 0, "fastTimeoutTick": 32768, "slowTimeoutTick": 32768, "delayAckTimoutTick": 32768, "synRetries": 0}}}

```

以下给出查询到的字段说明对照表：

**表 2**  /ethdev/stats,0 获取ethdev端口的基本统计信息

| 字段名     | 说明                                                           |
| ---------- | -------------------------------------------------------------- |
| ipackets   | 接收到的总数据包数量。                                         |
| opackets   | 发送的总数据包数量。                                           |
| ibytes     | 接收到的总字节数。                                             |
| obytes     | 发送的总字节数。                                               |
| imissed    | 接收到的丢失的数据包数量。                                     |
| ierrors    | 接收错误的数据包数量。                                         |
| oerrors    | 发送错误的数据包数量。                                         |
| rx_nombuf  | 接收时因没有足够缓冲区而丢弃的包数量。                         |
| q_ipackets | 各个队列接收到的数据包数量（一个数组，表示不同队列的接收量）。 |
| q_opackets | 各个队列发送的数据包数量（一个数组，表示不同队列的发送量）。   |
| q_ibytes   | 各个队列接收到的字节数（一个数组，表示不同队列的接收字节量）。 |
| q_obytes   | 各个队列发送的字节数（一个数组，表示不同队列的发送字节量）。   |
| q_errors   | 各个队列的错误数量（一个数组，表示不同队列的错误次数）。       |

**表 3**  /knet/stack/tcp\_stat查询字段说明

| 字段名                    | 说明                                                     |
| :------------------------ | :------------------------------------------------------- |
| Accepts                   | 被动打开的连接数。                                       |
| Closed                    | 关闭的连接数（包括丢弃的连接）。                         |
| ConnAttempt               | 试图建立连接的次数（调用connect）。                      |
| ConnDrops                 | 在连接建立阶段失败的连接次数（SYN收到之前）。            |
| Connects                  | 建链成功的次数。                                         |
| DelayedAck                | 延迟发送的ACK数。                                        |
| Drops                     | 意外丢失的连接数（收到SYN之后）。                        |
| RstKeepDrops              | 在保活阶段丢失的连接数（已建立或正等待SYN）。            |
| KeepProbe                 | 保活探测报文发送次数。                                   |
| KeepTMO                   | 保活定时器或者连接建立定时器超时次数。                   |
| RstPersistDrops           | 持续定时器超时次数达到最大值的次数。                     |
| PersistTMO                | 持续定时器超时次数。                                     |
| RcvAckBytes               | 由收到的ACK报文确认的发送字节数。                        |
| RcvAckPkts                | 收到的ACK报文数。                                        |
| RcvAckTooMuch             | 收到对未发送数据进行的ACK报文数。                        |
| RcvDupAck                 | 收到的重复ACK数。                                        |
| RcvBadOff                 | 收到的首部长度无效的报文数。                             |
| RcvBadSum                 | 收到的校验和错误的报文数。                               |
| RcvLocalAddr              | 因地址为本机地址而丢弃的报文数。                         |
| RcvNoTcpHash              | 因找不到TCP而丢弃的报文数。                              |
| RstByRcvErrAckFlags       | 因异常ackFlag而丢弃的报文数。                            |
| RcvNotSynInListen         | (监听状态下)收到非SYN报文的数量。                        |
| RcvInvalidWidInPassive    | 因共线程收到的异常wid报文而丢弃的报文数。                |
| RcvDupSyn                 | 因收到重复SYN而丢弃的报文数。                            |
| RcvNonAckInSynRcv         | 因SYN_RCV下收到不带ACK而丢弃的报文数。                   |
| RstByRcvInvalidAck        | 因SYN_RCV下收到异常序号ACK而丢弃的报文数。               |
| RcvErrSeq                 | 因SYN_RCV下收到异常seq序号而丢弃的报文数量。             |
| RcvErrOpt                 | 因收到异常tcpOpt的报文数量。                             |
| RcvDataSyn                | 因SYN报文带数据而丢弃的报文数量。                        |
| RcvBytes                  | 连续收到的字节数。                                       |
| RcvBytesPassive           | 流量统计：被动建链收到的总字节数。                       |
| RcvBytesActive            | 流量统计：主动建链收到的总字节数。                       |
| RcvDupBytes               | 完全重复报文中的重复字节数。                             |
| RcvDupPkts                | 完全重复报文的报文数。                                   |
| RcvAfterWndPkts           | 携带数据超出滑动窗口通告值的报文数。                     |
| RcvAfterWndBytes          | 在滑动窗口已满时收到的字节数。                           |
| RcvPartDupBytes           | 部分数据重复的报文重复字节数。                           |
| RcvPartDupPkts            | 部分数据重复的报文数。                                   |
| RcvOutOrderPkts           | 收到失序的报文数。                                       |
| RcvOutOrderBytes          | 收到失序的字节数。                                       |
| RcvShort                  | 长度过短的报文数。                                       |
| RcvTotal                  | 收到的报文总数。                                         |
| RcvPkts                   | 顺序接收的报文数。                                       |
| RcvWndProbe               | 收到的窗口探测报文数。                                   |
| RcvWndUpdate              | 收到的窗口更新报文数。                                   |
| RcvInvalidRST             | 收到的无效的RST报文。                                    |
| RcvSynEstab               | 建链完成后收到序号合法的SYN报文。                        |
| RcvFIN                    | 收到第一个FIN报文个数。                                  |
| RcvRxmtFIN                | 收到重传FIN报文个数。                                    |
| RexmtTMO                  | 重传超时次数。                                           |
| RTTUpdated                | RTT估算值更新次数。                                      |
| SndBytes                  | 发送的字节数。                                           |
| SndBytesPassive           | 流量统计：被动建链发送的总字节数。                       |
| SndBytesActive            | 流量统计：主动建链发送的总字节数。                       |
| SndCtl                    | 发送的控制报文数（SYN、FIN、RST）。                      |
| SndPkts                   | 发送的数据报文数（数据长度大于0）。                      |
| SndProbe                  | 发送的窗口探测次数。                                     |
| SndRexmtBytes             | 重传的数据字节数。                                       |
| SndAcks                   | 发送的纯ACK报文数（数据长度为0）。                       |
| SndRexmtPkts              | 重传的报文数。                                           |
| SndTotal                  | 发送的报文总数。                                         |
| SndWndUpdate              | 只携带窗口更新信息的报文数。                             |
| TMODrop                   | 由于重传超时而丢失的连接数。                             |
| RcvExdWndRst              | 收到超窗reset报文。                                      |
| DropCtlPkts               | 丢弃的控制报文。                                         |
| DropDataPkts              | 丢弃的数据报文。                                         |
| SndRST                    | 发送的RST报文数。                                        |
| SndFIN                    | 发送的FIN报文数。                                        |
| FinWait2Drops             | 默认FIN_WAIT_2定时器超时断链次数。                       |
| RespChallAcks             | 回复挑战ACK个数。                                        |
| OnceDrivePassiveTsqCnt    | 单次被动调度tsq循环次数。                                |
| AgeDrops                  | 由于老化而丢弃的连接数。                                 |
| BbrSampleCnt              | BBR拥塞控制算法采样次数。                                |
| BbrSlowBWCnt              | BBR拥塞控制算法触发慢速带宽次数。                        |
| FrtoSpurios               | 前向重传超时判断假超时次数。                             |
| FrtoReal                  | 前向重传超时判断真超时次数。                             |
| TimewaitReuse             | TIMEWAIT状态复用计数。                                   |
| SynSentConnDrops          | SYN_SENT时收到RST断链数。                                |
| SynRecvConnDrops          | SYN_RECV时收到RST断链数。                                |
| RstUserTimeOutDrops       | User_timeout超时断链数。                                 |
| RstSynRetriesDrops        | SYNRetries超过导致断链数（包括SYN、SYN/ACK）。           |
| RstSynAckRetriesDrops     | SYNACK_RETRIES超过导致断链数。                           |
| RcvOldAck                 | 收到的老旧ACK数。                                        |
| RcvErrSynOpt              | 收到的SYN报文选项异常的数量。                            |
| RcvErrSynAckOpt           | 收到的SYN/ACK报文选项异常的数量。                        |
| RcvErrEstabAckOpt         | 收到的建连ACK报文选项异常的数量。                        |
| RexmitSyn                 | 重传的SYN报文数量。                                      |
| RexmitSynAck              | 重传的SYN/ACK报文数量。                                  |
| RexmitFin                 | 重传的FIN报文数量。                                      |
| RstTimeWaitTimerDrops     | 2msl超时断链的连接数量。                                 |
| XmitGetNullDev            | 发送报文获取dev为空的次数。                              |
| TcpUserAccept             | 被用户accept拿走的socket数。                             |
| TcpRcvErrSackopt          | 收到的带异常SACK选项的报文数量。                         |
| TcpRcvDataAfterFin        | 收到的FIN之后收到的数据报文数量。                        |
| TcpRcvAfterClosed         | TCP状态为CLOSED之后收到数据报文数量。                    |
| TcpCheckDefferAcceptDrops | TCP校验DefferAccept失败拒绝连接的数量。                  |
| TcpOverBackLogDrops       | 因达到Server的backlog限制丢弃的连接数量。                |
| RstPassiveEstOverMaxCb    | Server端因最大TCPCB数量限制丢弃的连接数量。              |
| TcpSndSyn                 | 发送的SYN报文数。                                        |
| TcpSndLimitedByCork       | 因Cork限制未发送的报文数。                               |
| TcpSndLimitedByMsgMore    | 因MsgMore限制未发送的报文数。                            |
| TcpSndLimitedByNagle      | 因Nagle限制未发送的报文数。                              |
| TcpSndLimitedByBbr        | 因BBR算法PACING限制未发送的报文数。                      |
| TcpSndLimitedByPacing     | 因PACING限制未发送的报文数。                             |
| TcpSndLimitedByCwnd       | 因拥塞窗口限制未发送的报文数。                           |
| TcpSndLimitedBySwnd       | 因对端窗口限制未发送的报文数。                           |
| TcpDropReassByte          | 丢弃的重组报文字节数。                                   |
| TcpRexmitSackPkt          | SACK重传的报文数。                                       |
| TcpFastRexmitPkt          | 快速重传的报文数。                                       |
| RstPersistUserDrops       | 坚持定时器超过用户配置时间丢弃的链接数。                 |
| RstSynSentRcvErrAck       | SYNSENT状态下接受到报文ACK异常发送RST。                  |
| RstCookieAfterClosed      | 已经被关闭的socket处理cookie异常发送RST。                |
| RstParentClosed           | 父socket被关闭异常发送RST。                              |
| RstRcvNonRstPkt           | 没有五元组状态下接受到不包含RST的报文发送RST。           |
| RstCloseChild             | (子连接)关闭时发送RST。                                  |
| RstLingerClose            | Linger模式关闭时发送RST。                                |
| RstRcvDataAfterClose      | 在关闭socket后接受到数据报文，发送RST报文。              |
| RstRexmit                 | 重传RST报文。                                            |
| RstRcvBufNotClean         | RCVBUF有报文时close发送RST报文。                         |
| SynSentRcvInvalidRst      | SYNSENT状态下收到无效RST。                               |
| TcpConnKeepDrops          | 连接保活阶段丢弃数。                                     |
| TcpRcvPktNoSyn            | SYNSENT状态下接受到报文不带SYN标志。                     |
| TcpReassSucBytes          | TCP重组完成的报文字节数。                                |
| TcpRcvOutBytes            | TCP用户接收走的字节数。                                  |
| TcpIcmpTooBigShort        | TCP层处理ICMP TOO BIG时报文长度不足。                    |
| TcpIcmpTooBigNoTcp        | TCP层处理ICMP TOO BIG时找不到对应TCP。                   |
| TcpIcmpTooBigErrSeq       | TCP层处理ICMP TOO BIG时不符合TCP序号。                   |
| TcpIcmpTooBigLargeMss     | TCP层处理ICMP TOO BIG时传入mtu计算的mss比当前大。        |
| TcpIcmpTooBigErrState     | TCP层处理ICMP TOO BIG时TCP状态不正确。                   |
| TcpCloseNoRcvDataLen      | TCP层处理CLOSE时候，接收缓冲区数据长度。                 |
| TcpPktWithoutAck          | 建链后接收到不带ACK的报文。                              |
| TcpSynRecvDupPacket       | SYN RECV状态下收到重复的报文。                           |
| TcpSynRecvUnexpectSyn     | SYN RECV状态下收到非预期的SYN报文。                      |
| TcpRcvAckErrCookie        | SYN Cookie场景收到ACK校验cookie值异常。                  |
| TcpRcvCookieCreateFailed  | SYN Cookie场景创建socket失败。                           |
| TcpRcvRstInRfc1337        | TIME WAIT状态下，使能RFC1337配置项收到的RST报文。        |
| TcpDeferAcceptDrop        | 因重传超过DeferAccept设置的重传次数丢弃的报文。         |

**表 4**  /knet/stack/conn\_stat连接状态统计查询字段说明

| 字段名        | 说明                                |
| ------------- | ----------------------------------- |
| Listen        | 侦听socket的状态计数。              |
| SynSent       | 主动建链SYN_SEND状态计数。          |
| SynRcvd       | 被动建链SYN_RCVD状态计数。          |
| PAEstablished | 被动建链ESTABLISHED状态计数。       |
| ACEstablished | 主动建链ESTABLISHED状态计数。       |
| PACloseWait   | 被动建链CLOSE_WAIT状态计数。        |
| ACCloseWait   | 主动建链CLOSE_WAIT状态计数。        |
| PAFinWait1    | 被动建链FIN_WAIT_1状态计数。        |
| ACFinWait1    | 主动建链FIN_WAIT_1状态计数。        |
| PAClosing     | 被动建链CLOSING状态计数。           |
| ACClosing     | 主动建链CLOSING状态计数。           |
| PALastAck     | 被动建链LAST_ACK状态计数。          |
| ACLastAck     | 主动建链LAST_ACK状态计数。          |
| PAFinWait2    | 被动建链FIN_WAIT_2状态计数。        |
| ACFinWait2    | 主动建链FIN_WAIT_2状态计数。        |
| PATimeWait    | 被动建链TIME_WAIT状态计数。         |
| ACTimeWait    | 主动建链TIME_WAIT状态计数。         |
| Abort         | 被动建链接收到RST报文断链状态计数。 |

**表 5**  /knet/stack/pkt\_stat协议栈各类报文统计查询字段说明

| 字段名                 | 说明                                                             |
| :--------------------- | :--------------------------------------------------------------- |
| LinkInPkts             | 接收入口统计。                                                   |
| EthInPkts              | Eth接收入口统计。                                                |
| NetInPkts              | Net接收入口统计。                                                |
| IcmpOutPkts            | 数据面产生的icmp差错报文统计。                                   |
| ArpDeliverPkts         | ARP上送处理统计。                                                |
| IpBroadcastDeliverPkts | 广播报文上送统计。                                               |
| NonFragDelverPkts      | 本地接收的非分片非OSPF报文上交FWD分发统计。                      |
| UptoCtrlPlanePkts      | 上送控制面的报文统计。                                           |
| ReassInFragPkts        | 进行真重组的分片报文个数。                                       |
| ReassOutReassPkts      | 真重组成功返回的完整报文个数。                                   |
| NetOutPkts             | Net发送出口报文统计。                                            |
| EthOutPkts             | Eth发送出口报文统计，交给驱动发送。                              |
| FragInPkts             | 分片报文入口总数。                                               |
| FragOutPkts            | 分片报文出口总数。                                               |
| ArpMissResvPkts        | ARP查找失败，缓存报文并返回去保序的报文统计。                    |
| ArpSearchInPkts        | ARP查找入口报文统计。                                            |
| ArpHaveNormalPkts      | ARP查找成功报文统计（存在正常ARP）。                             |
| RcvIcmpPkts            | 收到ICMP报文的数量统计。                                         |
| NetBadVersionPkts      | IP版本号错误的报文统计。                                         |
| NetBadHdrLenPkts       | IP首部长度无效的报文统计。                                       |
| NetBadLenPkts          | IP首部和IP数据长度不一致的报文统计。                             |
| NetTooShortPkts        | 具有无效数据长度的报文统计。                                     |
| NetBadChecksumPkts     | 校验和错误的报文统计。                                           |
| NetNoProtoPkts         | 具有不支持的协议的报文统计。                                     |
| NetNoRoutePkts         | 路由查找失败的报文统计。                                         |
| TcpReassPkts           | TCP重组乱序队列中的报文DB统计。                                  |
| UdpInPkts              | UDP接收入口统计。                                                |
| UdpOutPkts             | UDP发送报文统计。                                                |
| TcpInPkts              | TCP接收入口统计。                                                |
| SndBufInPkts           | 进入发送缓冲区报文个数统计。                                     |
| SndBufOutPkts          | 从发送缓冲区释放的报文个数统计（ACK掉的报文统计）。              |
| SndBufFreePkts         | 从发送缓冲区释放的报文个数统计（释放socket节点释放的报文统计）。 |
| RcvBufInPkts           | 进入接收缓冲区报文个数统计。                                     |
| RcvBufOutPkts          | 从接收缓冲区释放的报文个数统计（用户接收走的报文统计）。         |
| RcvBufFreePkts         | 从接收缓冲区释放的报文个数统计。                                 |
| Ip6InPkts              | IPv6接收入口统计。                                                |
| Ip6TooShortPkts        | 具有无效数据长度的IPv6报文统计。                                  |
| Ip6BadVerPkts          | IPv6版本号错误的报文统计。                                        |
| Ip6BadHeadLenPkts      | IPv6首部长度无效的报文统计。                                      |
| Ip6BadLenPkts          | IPv6首部和数据长度不一致的报文统计。                              |
| Ip6MutiCastDeliverPkts | IPv6组播报文上送统计。                                            |
| Ip6ExtHdrCntErrPkts    | IPv6扩展首部数量异常的报文统计。                                  |
| Ip6ExtHdrOverflowPkts  | IPv6扩展首部长度异常的报文统计。                                  |
| Ip6HbhHdrErrPkts       | IPv6 Hop-by-Hop扩展首部异常的报文统计。                           |
| Ip6NoUpperProtoPkts    | IPv6报文不携带上层协议数据的报文统计。                            |
| Ip6ReassInFragPkts     | 进行IPv6真重组的分片统计。                                        |
| Ip6FragHdrErrPkts      | IPv6分片扩展首部异常的报文统计。                                  |
| Ip6OutPkts             | IPv6发送出口报文统计。                                            |
| Ip6FragOutPkts         | IPv6分片发送出口报文统计。                                        |
| KernelFdirCacheMiss    | Netdev使用缓存miss统计。                                         |
| IpDevTypeNoMatch       | Netdev类型不匹配丢弃报文统计。                                   |
| IpCheckAddrFail        | IPv4校验地址失败统计。                                           |
| IpLenOverLimit         | 报文长度超过最大值统计。                                         |
| IpReassOverTblLimit    | 重组节点超过表最大值统计。                                       |
| IpReassMallocFail      | 重组节点内存申请失败统计。                                       |
| IpReassNodeOverLimit   | 重组节点报文数量达到限制统计。                                   |
| IpIcmpAddrNotMatch     | ICMP地址不匹配统计。                                             |
| IpIcmpPktLenShort      | ICMP报文长度过小统计。                                           |
| IpIcmpPktBadSum        | ICMP校验和错误统计。                                             |
| IpIcmpNotPortUnreach   | ICMP报文非PORT_UNREACH统计。                                     |
| IpIcmpUnreachTooShort  | PORT_UNREACH报文长度过小统计。                                   |
| IpIcmpUnreachTypeErr   | UNREACH上层协议非UDP统计。                                       |
| Ip6DevTypeErr          | IPv6设备类型不匹配统计。                                         |
| Ip6CheckAddrFail       | IPv6地址校验错误统计。                                           |
| Ip6ReassOverTblLimit   | IPv6重组节点超过表限制统计。                                     |
| Ip6ReassMallocFail     | IPv6重组节点内存申请失败统计。                                   |
| Ip6ReassNodeOverLimit  | IPv6重组节点报文超限统计。                                       |
| Ip6ProtoErr            | IPv6找不到上层协议统计。                                         |
| Ip6IcmpTooShort        | IPv6 ICMP长度过小统计。                                          |
| Ip6IcmpBadSum          | IPv6 ICMP校验和错误统计。                                        |
| Ip6IcmpNoPayload       | IPv6 ICMP没有Payload统计。                                       |
| Ip6CodeNomatch         | IPv6 ICMP类型不匹配统计。                                        |
| Icmpv6TooBigShort      | ICMPv6 TOO BIG报文ip层长度过短统计。                             |
| Icmpv6TooBigSmall      | ICMPv6 TOO BIG报文mtu信息过小统计。                              |
| Icmpv6TooBigExthdrErr  | ICMPv6 TOO BIG报文内部扩展首部异常统计。                         |
| Icmpv6TooBigNotTcp     | ICMPv6 TOO BIG报文内部四层非TCP统计。                            |
| IpBadOffset            | IP报文offset错误统计。                                           |
| NfPreRoutingDrop       | Nf PreRouting丢弃报文统计。                                      |
| NfLocaInDrop           | Nf LocalIn丢弃报文统计。                                         |
| NfForwardDrop          | Nf Forward丢弃报文统计。                                         |
| NfLocalOutDrop         | Nf LocalOut丢弃报文统计。                                        |
| NfPostRoutingDrop      | Nf PostRouting丢弃报文统计。                                     |
| UdpIcmpUnReachShort    | UNREACH报文UDP层长度过短统计。                                   |
| Ip6IcmpUnReachTooShort | ICMPv6 UNREACH ip层过短统计。                                    |
| Icmp6UnReachExthdrErr  | ICMPv6 UNREACH报文内部扩展首部异常统计。                         |
| Icmp6UnReachNotUdp     | ICMPv6 UNREACH报文内部四层非UDP统计。                            |
| UdpIcmp6UnReachShort   | ICMPv6 UNREACH报文长度过短统计。                                 |

**表 6**  /knet/stack/abn\_stat协议栈异常打点统计查询字段说明

| 字段名                        | 说明                                               |
| :---------------------------- | :------------------------------------------------- |
| AbnBase                       | 预留异常基础统计项。                               |
| ConnByListenSk                | 侦听套接字不能发起connect。                            |
| RepeatConn                    | 已建链的套接字不能重复建链。                           |
| RefusedConn                   | 连接被拒绝。                                       |
| ConnInProg                    | 连接正在进行中。                                   |
| AcceptNoChild                 | Accept没有有效的子socket。                         |
| SetOptInval                   | 设置TCP选项参数非法。                              |
| KpIdInval                     | 设置TCP选项keepidle参数非法。                      |
| KpInInval                     | 设置TCP选项keepintvl参数非法。                     |
| KpCnInval                     | 设置TCP选项keepcnt参数非法。                       |
| MaxsegInval                   | 设置TCP选项maxseg参数非法。                        |
| MaxsegDisStat                 | 设置TCP选项maxseg当前状态异常。                    |
| DeferAcDisStat                | 设置TCP选项defer accept当前状态异常。              |
| SetOptNotSup                  | 设置TCP选项opt参数不支持。                         |
| TcpInfoInval                  | 获取TCP选项tcpInfo参数非法。                       |
| GetOptInval                   | 获取TCP选项参数非法。                              |
| GetOptNotSup                  | 获取TCP选项opt参数不支持。                         |
| SndConnRefused                | 连接已中断。                                       |
| SndCantSend                   | 当前连接不能再发送数据。                           |
| SndConnClosed                 | 当前连接被关闭。                                   |
| SndNoSpace                    | 发送缓冲区不足。                                   |
| SndbufNoMem                   | 发送空间无法写入数据。                             |
| RcvConnRefused                | TCP已收到RST报文，用户执行socket接收操作。         |
| RcvConnClosed                 | TCP未建连或建连未完成，用户执行socket接收操作。    |
| TimerNodeExist                | 定时器已经在链表中。                               |
| TimerExpiredInval             | 定时器在无效状态下被触发的异常次数。                |
| TimerActiveExcept             | 激活定时器异常。                                   |
| WorkerMissMatch               | 共线程部署模式下，socket/epoll被跨线程使用。       |
| PortIntervalPutErr            | 端口释放时，查找端口区间失败。                     |
| PortIntervalCntErr            | 端口区间计数异常。                                 |
| TimerCycle                    | 定时器链表环。                                     |
| PbufHookAllocErr              | 使用hook申请内存失败。                             |
| PbufBuildParamErr             | DP_PbufBuild入参异常。                             |
| PbufCopyParamErr              | DP_PbufCopy入参异常。                              |
| NotifyRcvsynErr               | 被动建连收到SYN事件回调失败。                      |
| NotifyPassiveConnectedErr     | 被动建连成功事件回调失败。                         |
| NotifyPassiveConnectedFailErr | 被动建连失败事件回调失败。                         |
| NotifyActiveConnectFailErr    | 主动建连失败事件回调失败。                         |
| NotifyRcvfinErr               | 收到FIN通知事件回调失败。                          |
| NotifyRcvrstErr               | 收到RST通知事件回调失败。                          |
| NotifyDisconnectedErr         | 老化断链通知事件回调失败。                         |
| NotifyWriteErr                | 写事件回调失败。                                   |
| NotifyReadErr                 | 读事件回调失败。                                   |
| NotifyFreeSockErr             | Sock资源即将释放事件回调失败。                     |
| SocketFdErr                   | 创建socket时fd失败。                               |
| FdMemErr                      | 创建fd时内存申请失败。                             |
| FdNodeFull                    | 创建fd时无可用node。                               |
| SocketCreateErr               | 创建socket失败。                                   |
| SocketDomainErr               | 创建socket时不支持对应domain。                     |
| SocketNoCreateFn              | 创建socket时无法找到创建钩子。                     |
| SocketTypeWithFlags           | 创建socket时type带有flags。                        |
| SocketTypeErr                 | 创建socket时type不支持。                           |
| SocketProtoInval              | 创建socket时proto非法。                            |
| SocketNoSupp                  | 创建socket时入参组合不支持。                       |
| TcpCreateInval                | 创建TCP socket时proto和domain不对应。                  |
| TcpCreateFull                 | 创建TCP socket时tcpcb已满。                            |
| TcpCreateMemErr               | 创建TCP socket时tcpsk申请内存失败。                    |
| UdpCreateInval                | 创建UDP socket时proto和domain不对应。                  |
| UdpCreateFull                 | 创建UDP socket时udpcb已满。                            |
| UdpCreateMemErr               | 创建UDP socket时udpsk申请内存失败。                    |
| EpollCreateFull               | 创建epoll socket时数量已满。                       |
| BindGetSockErr                | Bind时获取套接字失败。                                 |
| SocketGetFdErr                | 获取fd失败。                                       |
| FdGetInval                    | 因fd非法获取失败。                                 |
| FdGetClosed                   | 因fd获取node已关闭而失败。                         |
| FdGetInvalType                | 因获取fd时type不符而失败。                         |
| FdGetRefErr                   | 因获取fd时ref异常而失败。                          |
| SockGetSkNull                 | 因获取套接字时套接字为空或ops为空而失败。                  |
| BindFailed                    | Bind失败。                                         |
| TcpBindRepeat                 | TCP重复bind。                                      |
| TcpBindShutdown               | TCP被shutdown后bind。                              |
| TcpInetBindFailed             | Inet_bind失败导致TCP的bind失败。                    |
| InetBindAddrInval             | Inet检查地址非法。                                 |
| InetAddrNull                  | Inet检查地址为空。                                 |
| Inet6AddrNull                 | Inet6检查地址为空。                                |
| InetAddrlenErr                | Inet检查地址长度错误。                             |
| Inet6AddrlenErr               | Inet6检查地址长度错误。                            |
| InetAddrFamilyErr             | Inet检查地址族错误。                               |
| Inet6AddrFamilyErr            | Inet6检查地址族错误。                              |
| InetBindConnected             | Inet_bind时已经调用过connect。                     |
| InetBindAddrErr               | Inet_bind本机地址失败。                            |
| TcpBindRandPortFailed         | TCP随机绑定端口失败。                              |
| TcpBindPortFailed             | TCP绑定端口失败。                                  |
| ConnGetSockErr                | Connect时获取套接字失败。                              |
| ConnAddrNull                  | Connect地址为空。                                  |
| ConnAddrlenErr                | Connect地址长度异常。                              |
| ConnAddr6lenErr               | Connect地址长度异常。                              |
| ConnFailed                    | Connect失败。                                      |
| ConnFlagsErr                  | Connect时状态不正确。                              |
| TcpInetConnFailed             | Inet_connect失败导致tcp_connect失败。              |
| UdpInetConnFailed             | Inet_connect失败导致udp_connect失败。              |
| InetConnAddrInval             | Inet检查地址非法。                                 |
| UpdateFlowRtFailed            | 更新表项查找IPv4路由表失败。                             |
| UpdateFlowRt6Failed           | 更新表项查找IPv6路由表失败。                            |
| UpdateFlowWrongAddr           | 表项更新前后地址异常。                             |
| UpdateFlowWrongAddr6          | 表项更新前后地址异常。                             |
| InetFlowRtFailed              | 初始化flow查找IPv4路由表失败。                           |
| Inet6FlowRtFailed             | 初始化flow查找IPv6路由表失败。                          |
| TcpConnRtNull                 | Flow中路由表为空。                                   |
| TcpConnDevDown                | Connect时设备已经down。                            |
| TcpConnViAny                  | VI设备地址为全0。                                  |
| TcpConnAddrErr                | 路由表对应地址不正确。                             |
| TcpConnRandPortFailed         | 插入连接表时随机端口失败。                         |
| TcpConnPortFailed             | 插入连接表时绑定端口失败。                         |
| NetdevRxhashFailed            | 处理netdev的rxhash失败。                           |
| TcpRxhashWidErr               | Netdev获取rxwid失败。                              |
| UdpConnSelf                   | UDP的flow中本端对端地址一致。                      |
| UdpConnRandPortFailed         | UDP随机绑定连接端口失败。                          |
| UdpConnPortFailed             | UDP绑定连接端口失败。                              |
| TimeoutAbort                  | 因超时导致链路断开。                               |
| SynStateRcvRst                | 因建连期间收到RST而断链。                          |
| CloseWaitRcvRst               | 因CLOSE_WAIT状态下收到RST而断链。                  |
| AbnormalRcvRst                | 因其他状态下收到RST而断链。                        |
| AcceptGetSockErr              | Accept时获取套接字失败。                               |
| AcceptFdErr                   | Accept创建socket时fd失败。                         |
| AcceptCreateErr               | Accept创建socket失败。                             |
| AcceptAddrlenNull             | Accept时addr不为空但addrlen为空。                  |
| AcceptAddrlenInval            | Accept时addr不为空但addrlen非法。                  |
| AcceptNoSupport               | 套接字不支持accept。                                   |
| AcceptClosed                  | 因已经被close而accept失败。                        |
| AcceptNotListened             | 因没有被监听而accept失败。                         |
| AcceptGetAddrFailed           | 因获取对端地址失败而accept失败。                   |
| GetDstAddrlenInval            | 因获取对端地址长度非法而accept失败。               |
| SendtoGetSockErr              | Sendto时获取套接字失败。                               |
| SendtoBufNull                 | Sendto时buf为空。                                  |
| SendFlagsInval                | Send时flags不支持。                                |
| SendGetDatalenFailed          | Send时获取数据长度失败。                           |
| SendGetDataLenZero            | Send时发送数据长度为0。                            |
| SockCheckMsgNull              | 收发检查时msg为空。                                |
| SockCheckMsgiovNull           | 收发检查时msg_iov为空。                            |
| SockCheckMsgiovInval          | 收发检查时msg_iov非法。                            |
| GetIovlenInval                | 获取到iov_len异常。                                |
| GetIovBaseNull                | 获取到iov_base为空。                               |
| ZiovCbNull                    | 零拷贝获取ziov中freeCb或cb为空。                   |
| GetTotalIovlenInval           | 获取到iov_len总长异常。                            |
| SockSendmsgFailed             | Send相关接口失败。                                 |
| TcpSndDevDown                 | Send时dev不可用。                                  |
| TcpSndBufZcopyNomem           | 零拷贝写sndbuf失败。                               |
| TcpPbufConstructFailed        | 构建间接pbuf失败。                                 |
| TcpPushSndPbufFailed          | TCP写sndbuf失败。                                  |
| UdpSndLong                    | UDP发送长度超过限制。                              |
| UdpCheckDstAddrErr            | UDP检查发送地址失败。                              |
| UdpCheckDstAddr6Err           | UDP6检查发送地址失败。                             |
| UdpSndAddrInval               | UDP目的地址与连接地址不一致。                      |
| UdpSndNoDst                   | UDP发送无对端信息。                                |
| UdpFlowBroadcast              | UDP的flow为广播类型但不支持。                      |
| UdpSndNoRt                    | UDP没有路由信息且未绑定dev。                       |
| UdpSndDevDown                 | UDP发送dev不可用。                                 |
| UdpSndFlagsNoSupport          | UDP发送时flags不支持。                             |
| UdpAutoBindFailed             | UDP发送随机绑定端口失败。                          |
| FromMsgBuildPbufFailed        | 由msg构建pbuf时build失败。                         |
| FromMsgAppendPbufFailed       | 由msg构建pbuf时append失败。                        |
| SendmsgGetSockErr             | Sendmsg时获取套接字失败。                              |
| ZSendmsgGetSockErr            | Zsendmsg时获取套接字失败。                             |
| RcvfromGetSockErr             | Rcvfrom时获取套接字失败。                              |
| RcvfromFailed                 | Rcvfrom失败。                                      |
| RcvfromBufNull                | Rcvfrom时buf为空。                                 |
| RecvFlagsInval                | Recv的flags不支持。                                |
| RecvGetDatalenFailed          | Recv时获取数据长度失败。                           |
| RecvCheckMsgFailed            | 零拷贝检查msg失败。                                |
| SockRecvmsgFailed             | Recv相关接口失败。                                 |
| TcpRcvBufFailed               | TCP接收数据失败。                                  |
| RcvGetAddrFailed              | Recv时获取目的地址失败。                           |
| SockReadBufchainZrro          | 读取bufchain为0。                                  |
| SockReadBufchainShort         | 读取bufchain长度不足。                             |
| RcvZcopyGetAddrFailed         | 零拷贝recv时获取目的地址失败。                     |
| RcvZcopyChainReadFailed       | 零拷贝读取bufchain失败。                           |
| RcvmsgFailed                  | Rcvmsg失败。                                       |
| ZRcvmsgFailed                 | 零拷贝rcvmsg失败。                                 |
| SendIpHookFailed              | 产品注册钩子发送IP报文失败。                       |
| PbufRefErr                    | PBUF引用计数错误。                                 |
| InetReassTimeOut              | IPv4重组定时器超时。                               |
| Inet6ReassTimeOut             | IPv6重组定时器超时。                               |
| WorkerGetErrWid               | Worker获取对应wid异常。                            |
| InitPbufMpFailed              | Pbuf内存池申请失败，未使用。                       |
| InitPbufHookRegFailed         | Pbuf内存池注册失败，未使用。                       |
| InitZcopyMpFailed             | Zcopy内存池申请失败，未使用。                      |
| InitZcopyHookRegFailed        | Zcopy内存池注册失败，未使用。                      |
| InitRefMpFailed               | Ref内存池申请失败，未使用。                        |
| InitRefHookRegFailed          | Ref内存池注册失败，未使用。                        |
| CpdDelayEnqueErr              | Cpd delay转发入队异常。                            |
| CpdDelayDequeErr              | Cpd delay转发出队异常。                            |
| CpdSyncTableRecvErr           | Cpd同步表项收内核报文异常。                        |
| CpdSyncTableSendErr           | Cpd给内核发送获取表项更新报文失败。                |
| CpdSendIcmpErr                | Cpd发送给内核icmp报文失败。                        |
| CpdTransMallocErr             | Cpd转发报文内存申请失败。                          |
| CpdFindTapFailed              | Cpd获取tap口异常。                                 |
| CpdFdWriteFailed              | Cpd转发报文write失败。                             |
| CpdFdWritevFailed             | Cpd转发报文writev失败。                            |
| CpdFdReadFailed               | Cpd同步内核报文read失败。                          |
| UtilsTimerErr                 | 基础时钟返回值异常。                               |
| PbufWidErr                    | PBUF wid与worker id不一致。                        |

**表 7**  /knet/stack/mem\_stat协议栈内存使用统计查询字段说明

| 字段名           | 说明                                   |
| ---------------- | -------------------------------------- |
| InitInitMem      | 初始内存分配的内存量（未释放的内存）。 |
| InitFreeMem      | 初始内存释放的内存量。                 |
| CpdInitMem       | 复制内存分配的内存量。                 |
| CpdFreeMem       | 复制内存释放的内存量。                 |
| DebugInitMem     | 调试模块初始分配的内存量。             |
| DebugFreeMem     | 调试模块释放的内存量。                 |
| NetdevInitMem    | 网络设备相关的内存分配量。             |
| NetdevFreeMem    | 网络设备释放的内存量。                 |
| NamespaceInitMem | 命名空间管理的内存分配量。             |
| NamespaceFreeMem | 命名空间管理释放的内存量。             |
| PbufInitMem      | 数据包缓冲区（PBUF）分配的内存量。     |
| PbufFreeMem      | 数据包缓冲区（PBUF）释放的内存量。     |
| PmgrInitMem      | 进程管理模块初始内存分配的内存量。     |
| PmgrFreeMem      | 进程管理模块释放的内存量。             |
| ShmInitMem       | 共享内存（SHM）分配的内存量。          |
| ShmFreeMem       | 共享内存释放的内存量。                 |
| TbmInitMem       | 传输控制管理（TCM）相关内存分配量。    |
| TbmFreeMem       | 传输控制管理（TCM）释放的内存量。      |
| UtilsInitMem     | 实用工具模块分配的内存量。             |
| UtilsFreeMem     | 实用工具模块释放的内存量。             |
| WorkerInitMem    | 工作线程相关内存分配量。               |
| WorkerFreeMem    | 工作线程释放的内存量。                 |
| FdInitMem        | 文件描述符相关的内存分配量。           |
| FdFreeMem        | 文件描述符释放的内存量。               |
| EpollInitMem     | Epoll相关的内存分配量。                |
| EpollFreeMem     | Epoll释放的内存量。                    |
| PollInitMem      | Poll相关的内存分配量。                 |
| PollFreeMem      | Poll释放的内存量。                     |
| SelectInitMem    | Select相关的内存分配量。               |
| SelectFreeMem    | Select释放的内存量。                   |
| SocketInitMem    | 套接字相关的内存分配量。               |
| SocketFreeMem    | 套接字释放的内存量。                   |
| NetlinkInitMem   | Netlink相关的内存分配量。              |
| NetlinkFreeMem   | Netlink释放的内存量。                  |
| EthInitMem       | Ethernet接口相关内存分配量。           |
| EthFreeMem       | Ethernet接口释放的内存量。             |
| IpInitMem        | IPv4协议初始化内存。                   |
| IpFreeMem        | IPv4协议释放的内存。                   |
| Ip6InitMem       | IPv6协议初始化内存。                   |
| Ip6FreeMem       | IPv6协议释放的内存。                   |
| TcpInitMem       | TCP层相关的内存分配量。                |
| TcpFreeMem       | TCP层释放的内存量。                    |
| UdpInitMem       | UDP层相关的内存分配量。                |
| UdpFreeMem       | UDP层释放的内存量。                    |

**表 8**  /knet/stack/pbuf\_stat协议栈PBUF使用统计查询字段说明

| 字段名         | 说明                                                   |
| -------------- | ------------------------------------------------------ |
| ipFragPktNum   | IP分片包的数量，表示在网络中发生的IP分片数据包的数量。 |
| tcpReassPktNum | TCP重组包的数量，表示TCP数据包在接收端进行重组的次数。 |
| sendBufPktNum  | 发送缓冲区中数据包的数量。                             |
| recvBufPktNum  | 接收缓冲区中数据包的数量。                             |

**表 9**  /knet/stack/net\_stat,\<pid\> \<start_fd\> \<fd_cnt\>所有连接信息查询字段说明

| 字段名    | 说明                                         |
| --------- | -------------------------------------------- |
| osFd_\<fd> | 单个连接信息的键名，\<fd>为内核fd文件描述符。 |
| pf        | 协议族。                                     |
| proto     | 协议类型。                                   |
| lAddr     | 本地地址。                                   |
| lPort     | 本地端口。                                   |
| rAddr     | 远程地址。                                   |
| rPort     | 远程端口。                                   |
| state     | TCP连接状态。                                |
| tid       | 套接字绑定的线程ID。                         |
| innerFd   | 内部文件描述符。                             |

**表 10**  /knet/stack/socket\_info,[pid] <fd\>对应套接字详细连接信息查询字段说明

表10.1 SockInfo - 套接字基础信息

| 字段名            | 说明                 |
| :---------------- | :------------------- |
| protocol      | 传输层协议类型。       |
| pf            | 协议族。              |
| state         | socket事件状态，0x1/0x2/0x4 表示可读、可写、异常；0x10/0x20 表示发送/接收已关闭；0x40/0x80 表示读/写边缘触发模式      |
| isLingerOnoff | SO_LINGER选项。        |
| isNonblock    | 阻塞选项。       |
| isReuseAddr   | SO_REUSEADDR选项，是否允许地址在释放后立即被重新绑定。     |
| isReusePort   | SO_REUSEPORT选项，是否允许端口重用。      |
| isBroadcast   | SO_BROADCAST选项，是否允许套接字发送广播消息。      |
| isKeepAlive   |SO_KEEPALIVE选项。      |
| isBindDev     | 是否将socket绑定到特定的网络设备。       |
| isDontRoute   | SO_DONTROUTE选项。      |
| options       | 以上8个选项套接字位图       |
| error         | socket错误缓存       |
| linger        | SO_LINGER的超时时间。           |
| flags         | socket连接状态，0x1=可发送，0x2=可接收，0x4=正在连接，0x8=已连接，0x10=已绑定，0x20=连接被拒，0x40=处于监听，0x80=已关闭，0x100=读端关闭，0x200=写端关闭，0x8000=有更多数据待发送。       |
| rdSemCnt      | 读信号量的计数器。     |
| wrSemCnt      | 写信号量的计数器。     |
| rcvTimeout    | 接收超时时间。         |
| sndTimeout    | 发送超时时间。         |
| sndDataLen    | 当前在发送缓冲区中等待处理的数据长度。       |
| rcvDataLen    | 当前在接收缓冲区中等待处理的数据长度。       |
| sndLowat      | 发送缓冲区低水位标记。 |
| sndHiwat      | 发送缓冲区高水位标记。 |
| rcvLowat      | 接收缓冲区低水位标记。 |
| rcvHiwat      | 接收缓冲区高水位标记。 |
| bandWidth     | 带宽限制。             |
| priority      | SO_PRIORITY选项，设置socket发送优先级的选项。              |
| associateFd   | 关联的文件描述符。       |
| notifyType    | 内部通知类型。        |
| wid           | 套接字所属worker。            |

表10.2 InetSkInfo - 网络层信息

| 字段名      | 说明                   |
| :-------- | :------------------------- |
| ttl       | 出站数据包的默认生存时间。 |
| tos       | 出站数据包的默认服务类型。 |
| mtu       | 路径最大传输单元。         |
| isIncHdr  | IP_HDRINCL选项。           |
| isTos     | IP_TOS选项。               |
| isTtl     | IP_TTL选项。               |
| isMtu     | IP_MTU选项。               |
| isPktInfo | IP_PKTINFO选项。           |
| isRcvTos  | IP_RECVTOS选项。           |
| isRcvTtl  | IP_RECVTTL选项。           |

表10.3 TcpBaseInfo - TCP协议基础信息

| 字段名                 | 说明                                                 |
| :------------------- | :------------------------------------------------------- |
| state                | TCP连接状态。                                            |
| connType             | 连接类型（Active：主动建链 Passive：被动建链）。         |
| noVerifyCksum        | 是否跳过TCP校验和验证。                                  |
| ackNow               | 是否要求立即发送ACK。                                    |
| delayAckEnable       | 是否启用延迟确认。                                       |
| nodelay              | TCP_NODELAY选项，用于配置是否禁用 Nagle 算法             |
| rttRecord            | 是否进行RTT测量。                                        |
| cork                 | TCP_CORK选项。                                           |
| deferAccept          | 是否启用TCP_DEFER_ACCEPT。                               |
| flags                | TCP socket状态标志                                       |
| wid                  | TCP所属worker。                                          |
| txQueid              | 发送队列ID。                                             |
| childCnt             | 子socket数量，只有监听socket有效。                       |
| backlog              | 监听socket的完整连接队列的最大长度，只有监听socket有效。 |
| accDataCnt           | 当前累积的数据包数量。                                   |
| accDataMax           | 最大允许累积数据包数量。                                 |
| dupAckCnt            | 当前收到的重复ACK数量。                                  |
| caAlgId              | 拥塞控制算法ID。                                         |
| caState              | 拥塞控制算法状态。                                       |
| cwnd                 | 拥塞窗口阈值。                                           |
| ssthresh             | 慢启动阈值。                                             |
| seqRecover           | 用于快速恢复的序列号。                                   |
| reorderCnt           | 重新排序的计数。                                         |
| rttStartSeq          | 记录往返时延评估开始时的序号。                                |
| srtt                 | 平滑往返时延。                                                |
| rttval               | 信道往返时延。                                           |
| tsVal                | TCP时间戳选项的值。                                      |
| tsEcho               | TCP时间戳回显值。                                        |
| lastChallengeAckTime | TCP挑战ACK保护机制。                                     |
| fastMode             | 快速处理模式。                                           |
| sndQueSize           | TCP发送队列的大小。                                        |
| rcvQueSize           | TCP接收队列的大小。                                        |
| rexmitQueSize        | TCP重传队列的大小。                                        |
| reassQueSize         | TCP重组队列的大小。                                        |

表10.4 TcpTransInfo - TCP传输控制信息

| 字段名               | 说明                                   |
| :----------------- | :----------------------------------------- |
| lport              | 本地端口。                                 |
| pport              | 对端端口。                                 |
| synOpt             | SYN报文选项位图，可协商的选项。            |
| negOpt             | 已协商的选项。                             |
| rcvWs              | 接收窗口缩放因子。                         |
| sndWs              | 发送窗口缩放因子。                         |
| rcvMss             | 从对端通告的MSS。                          |
| mss                | 本端的最大段大小。                         |
| iss                | 初始发送序列号。                           |
| irs                | 初始接收序列号。                           |
| sndUna             | 最早未确认序列号。                         |
| sndNxt             | 下一个发送序列号。                         |
| sndMax             | 最大发送序列号。                           |
| sndWnd             | 发送窗口大小。                             |
| sndUp              | 发送紧急指针。                             |
| sndWl1             | 用于上次窗口更新的段序列号。               |
| sndSml             | 发送小包的结束序列号。                     |
| rcvNxt             | 下一个期望接收序列号。                     |
| rcvWnd             | 接收窗口大小。                             |
| rcvMax             | 收到FIN后，记录此时的最大序号。            |
| rcvWup             | 最后一次发送窗口更新时的rcvNxt。          |
| idleStart          | 链路空间起始时间。                         |
| keepIdle           | TCP_KEEPIDLE选项，开始发送保活探测包时间。 |
| keepIntvl          | TCP_KEEPINTVL选项，探测包间隔。            |
| keepProbes         | TCP_KEEPCNT选项，最大保活探测次数。        |
| keepProbeCnt       | 当前保活探测计数。                         |
| keepIdleLimit      | 保活超时上限。                             |
| keepIdleCnt        | 保活超时次数。                             |
| backoff            | 重传退避指数。                             |
| maxRexmit          | 最大重传次数。                             |
| rexmitCnt          | 当前重传计数。                             |
| userTimeout        | TCP_USER_TIMEOUT的值。                     |
| userTimeStartFast  | 快超时定时器起始时间。                     |
| userTimeStartSlow  | 慢超时定时器起始时间。                     |
| fastTimeoutTick    | 快超时定时器超时时间。                     |
| slowTimeoutTick    | 慢超时定时器超时时间。                     |
| delayAckTimeoutTick | 延迟ack定时器超时时间。                    |
| synRetries         | TCP_SYNCNT选项，TCP 连接建立过程中 SYN 重传的次数。   |

**表 11**  /knet/stack/epoll\_stat,\<pid> \<start_epoll_fd> \<epoll_fd_cnt> \<start_socket_fd> \<socket_fd_cnt>  Epoll连接详细信息查询字段说明

|字段名|说明|
|--|--|
|epoll_<epoll_fd>|单个Epoll条目的键名，<epoll_fd>为内核Epoll文件描述符。|
|pid|所属进程标识符。|
|tid|所属线程标识符，开启共线程模式该参数有意义。|
|osFd|内核Epoll文件描述符。|
|inner_fd|协议栈Epoll文件描述符。|
|details|套接字集合条目的键名。|
|socket_<socket_fd>|单个套接字条目的键名，<socket_fd>为套接字文件描述符。|
|fd|协议栈Epoll监听的套接字文件描述符。|
|expectedEvents|协议栈Epoll监听的事件。|
|readyEvents|已就绪的事件，边缘触发模式上报事件后将同步至notifiedEvents，readyEvents的值保留至下次事件更新。|
|notifiedEvents|套接字已经上报过的事件（边缘触发模式下有效）。|
|shoted|套接字上报事件后置为1（one shot模式下有效）。|

**表 12**  /knet/flow/list,\<start_flow_index> \<flow_cnt> 流表信息查询字段说明

|字段名|说明|
|--|--|
|dip|目的IP地址。|
|dipMask|目的IP地址掩码。|
|dport|目的端口。|
|dportMask|目的端口掩码。|
|sip|源IP地址。|
|sipMask|源IP地址掩码。|
|sport|源端口。|
|sportMask|源端口掩码。|
|protocol|协议类型。|
|action|流表使用的队列。|

**表 13**  /knet/ethdev/queue 队列分配情况查询字段说明

|字段名|说明|
|--|--|
|pid|队列被使用的K-NET进程号。|
|tid|队列被使用的K-NET的worker线程号。|
|lcoreId|仅单进程会显示，队列被使用的K-NET的worker逻辑核号。|

**表 14**  /knet/ethdev/usage,<port\> <time\> 网卡带宽、包率查询字段说明

|字段名|说明|
|--|--|
|\<time>-<time+1>s|时间段，例如 "0-1s"。|
|tx|tx方向的带宽和包率。|
|rx|rx方向的带宽和包率。|

****
