# dpdk-telemetry.py网卡统计信息获取脚本

**命令功能**

获取网卡统计信息。

**命令格式**

> **说明：** 
>-   普通用户进入工具使用界面前需设置“XDG\_RUNTIME\_DIR”环境变量，如果新开终端，需要在新起的终端中导入。环境变量路径涉及的权限及安全需要用户保证。参考[相关业务配置](../../feature/preparations.md)进行设置。
>-   服务端环境关闭或重启后需要重新执行步骤。
>-   通过设置环境变量指定运行时目录，路径依据不同用户名会有差异。
>-   退出普通用户再重新切换到该用户需要重新配置。

服务端进入工具使用界面：

**dpdk-telemetry.py -f knet -i 1**

> **说明：** 
>-   -f ：指定knet为DPDK运行时目录的文件前缀。
>-   -i 1：指定DPDK应用程序实例号为1。

**命令参数**

**表 1**  命令参数说明（dpdk-telemetry.py）

|命令参数|是否必选|示例|说明|
|--|--|--|--|
|**/ethdev/list**|否|-|获取ethdev端口列表。|
|**/ethdev/stats,***<port>*|否|/ethdev/stats,0|获取ethdev端口的基本统计信息。port取值为/ethdev/list命令获取到的列表项。|
|**/ethdev/xstats,***<port>*|否|/ethdev/xstats,0|获取ethdev端口的扩展统计信息。port取值为/ethdev/list命令获取到的列表项。|
|**/knet/ethstats,***<param>*|否|/knet/ethstats,tcp/knet/ethstats,conn/knet/ethstats,pkt/knet/ethstats,abn/knet/ethstats,mem/knet/ethstats,pbuf|TCP相关统计TCP连接状态统计协议栈各类报文统计协议栈异常打点统计协议栈内存使用统计协议栈PBUF使用统计|


> **说明：** 
>SP670网卡与TM280网卡当前获取ethdev端口的扩展统计信息使用 /ethdev/xstats,<port\>。

**使用示例**

> **说明：** 
>使用前需要开启该功能，步骤参考[使用前配置](../../feature/OM_features.md#2-网卡统计信息工具dpdk-telemetry)。

```
# dpdk-telemetry.py -f knet -i 1
Connecting to /var/run/dpdk/rte/dpdk_telemetry.v2
{"version": "DPDK 21.11.7", "pid": 2631, "max_output_len": 16384}
Connected to application: "redis-server 192.168.*.*:6379"
--> /ethdev/list
{"/ethdev/list": [0]}
--> /ethdev/stats,0
{"/ethdev/stats": {"ipackets": 10006228, "opackets": 10005020, "ibytes": 1020453075, "obytes": 710344388, "imissed": 0, "ierrors": 0, "oerrors": 0, "rx_nombuf": 0, "q_ipackets": [5014719, 4991509, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "q_opackets": [5014032, 4990988, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "q_ibytes": [511405477, 509047598, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "q_obytes": [355990207, 354354181, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "q_errors": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]}}
--> /knet/ethstats,tcp
{"/knet/ethstats": {"Accepts": 0, "Closed": 0, "ConnAttempt": 0, "ConnDrops": 0, "Connects": 0, "DelayedAck": 0, "Drops": 0, "KeepDrops": 0, "KeepProbe": 0, "KeepTMO": 0, "PersistDrops": 0, "PersistTMO": 0, "RcvAckBytes": 0, "RcvAckPkts": 0, "RcvAckTooMuch": 0, "RcvDupAck": 0, "RcvBadOff": 0, "RcvBadSum": 0, "RcvBytes": 0, "RcvDupBytes": 0, "RcvDupPkts": 0, "RcvAfterWndPkts": 0, "RcvAfterWndBytes": 0, "RcvPartDupBytes": 0, "RcvPartDupPkts": 0, "RcvOutOrderPkts": 0, "RcvOutOrderBytes": 0, "RcvShort": 0, "RcvTotal": 0, "RcvPkts": 0, "RcvWndProbe": 0, "RcvWndUpdate": 0, "RcvRST": 0, "RcvInvalidRST": 0, "RcvSynEstab": 0, "RcvFIN": 0, "RcvRxmtFIN": 0, "RexmtTMO": 0, "RTTUpdated": 0, "SegsTimed": 0, "SndBytes": 0, "SndCtl": 0, "SndPkts": 0, "SndProbe": 0, "SndRexmtBytes": 0, "SndAcks": 0, "SndRexmtPkts": 0, "SndTotal": 0, "SndWndUpdate": 0, "TMODrop": 0, "RcvExdWndRst": 0, "DropCtlPkts": 0, "DropDataPkts": 0, "SndRST": 0, "SndFIN": 0, "FinWait2Drops": 0, "RespChallAcks": 0}}
--> 
```

以下给出查询到的字段说明对照表：

**表 2**  /ethdev/stats,0 获取ethdev端口的基本统计信息

|字段名|说明|
|--|--|
|ipackets|接收到的总数据包数量|
|opackets|发送的总数据包数量|
|ibytes|接收到的总字节数|
|obytes|发送的总字节数|
|imissed|接收到的丢失的数据包数量|
|ierrors|接收错误的数据包数量|
|oerrors|发送错误的数据包数量|
|rx_nombuf|接收时因没有足够缓冲区而丢弃的包数量|
|q_ipackets|各个队列接收到的数据包数量（一个数组，表示不同队列的接收量）|
|q_opackets|各个队列发送的数据包数量（一个数组，表示不同队列的发送量）|
|q_ibytes|各个队列接收到的字节数（一个数组，表示不同队列的接收字节量）|
|q_obytes|各个队列发送的字节数（一个数组，表示不同队列的发送字节量）|
|q_errors|各个队列的错误数量（一个数组，表示不同队列的错误次数）|


**表 3**  /knet/ethstats,tcp查询字段说明

|字段名|说明|
|--|--|
|Accepts|被动打开的连接数|
|Closed|关闭的连接数 (包括丢弃的连接)|
|ConnAttempt|试图建立连接的次数（调用connect）|
|ConnDrops|在连接建立阶段失败的连接次数（SYN收到之前）|
|Connects|建链成功的次数|
|DelayedAck|延迟发送的ACK数|
|Drops|意外丢失的连接数（收到SYN之后）|
|KeepDrops|在保活阶段丢失的连接数（已建立或正等待SYN）|
|KeepProbe|保活探测报文发送次数|
|KeepTMO|保活定时器或者连接建立定时器超时次数|
|PersistDrops|持续定时器超时次数达到最大值的次数|
|PersistTMO|持续定时器超时次数|
|RcvAckBytes|由收到的ACK报文确认的发送字节数|
|RcvAckPkts|收到的ACK报文数|
|RcvAckTooMuch|收到对未发送数据进行的ACK报文数|
|RcvDupAck|收到的重复ACK数|
|RcvBadOff|收到的首部长度无效的报文数|
|RcvBadSum|收到的校验和错误的报文数|
|RcvBytes|连续收到的字节数|
|RcvDupBytes|完全重复报文中的重复字节数|
|RcvDupPkts|完全重复报文的报文数|
|RcvAfterWndPkts|携带数据超出滑动窗口通告值的报文数|
|RcvAfterWndBytes|在滑动窗口已满时收到的字节数|
|RcvPartDupBytes|部分数据重复的报文重复字节数|
|RcvPartDupPkts|部分数据重复的报文数|
|RcvOutOrderPkts|收到失序的报文数|
|RcvOutOrderBytes|收到失序的字节数|
|RcvShort|长度过短的报文数|
|RcvTotal|收到的报文总数|
|RcvPkts|顺序接收的报文数|
|RcvWndProbe|收到的窗口探测报文数|
|RcvWndUpdate|收到的窗口更新报文数|
|RcvRST|收到RST报文|
|RcvInvalidRST|收到的无效的RST报文|
|RcvSynEstab|建链完成后收到序号合法的SYN报文|
|RcvFIN|收到第一个FIN报文个数|
|RcvRxmtFIN|收到重传FIN报文个数|
|RexmtTMO|重传超时次数|
|RTTUpdated|RTT估算值更新次数|
|SegsTimed|可用于RTT测算的报文数|
|SndBytes|发送的字节数|
|SndCtl|发送的控制报文数（SYN FIN RST）|
|SndPkts|发送的数据报文数（数据长度大于0）|
|SndProbe|发送的窗口探测次数|
|SndRexmtBytes|重传的数据字节数|
|SndAcks|发送的纯ACK报文数（数据长度为0）|
|SndRexmtPkts|重传的报文数|
|SndTotal|发送的报文总数|
|SndWndUpdate|只携带窗口更新信息的报文数|
|TMODrop|由于重传超时而丢失的连接数|
|RcvExdWndRst|收到超窗reset报文|
|DropCtlPkts|丢弃的控制报文|
|DropDataPkts|丢弃的数据报文|
|SndRST|发送的RST报文数|
|SndFIN|发送的FIN报文数|
|FinWait2Drops|默认FIN_WAIT_2定时器超时断链次数|
|RespChallAcks|回复挑战ACK个数|


**表 4**  /knet/ethstats,conn TCP连接状态统计查询字段说明

|字段名|说明|
|--|--|
|Listen|侦听socket的状态计数|
|SynSent|主动建链SYN_SEND状态计数|
|SynRcvd|被动建链SYN_RCVD状态计数|
|PAEstablished|被动建链ESTABLISHED状态计数|
|ACEstablished|主动建链ESTABLISHED状态计数|
|PACloseWait|被动建链CLOSE_WAIT状态计数|
|ACCloseWait|主动建链CLOSE_WAIT状态计数|
|PAFinWait1|被动建链FIN_WAIT_1状态计数|
|ACFinWait1|主动建链FIN_WAIT_1状态计数|
|PAClosing|被动建链CLOSING状态计数|
|ACClosing|主动建链CLOSING状态计数|
|PALastAck|被动建链LAST_ACK状态计数|
|ACLastAck|主动建链LAST_ACK状态计数|
|PAFinWait2|被动建链FIN_WAIT_2状态计数|
|ACFinWait2|主动建链FIN_WAIT_2状态计数|
|PATimeWait|被动建链TIME_WAIT状态计数|
|ACTimeWait|主动建链TIME_WAIT状态计数|
|Abort|被动建链接收到RST报文断链状态计数|


**表 5**  /knet/ethstats,pkt协议栈各类报文统计查询字段说明

|字段名|说明|
|--|--|
|LinkInPkts|接收入口统计|
|EthInPkts|eth接收入口统计|
|NetInPkts|net接收入口统计|
|IcmpOutPkts|数据面产生的icmp差错报文统计|
|ArpDeliverPkts|ARP上送处理统计|
|IpBroadcastDeliverPkts|广播报文上送统计|
|NonFragDelverPkts|本地接收的非分片非OSPF报文上交FWD分发统计|
|UptoCtrlPlanePkts|上送控制面的报文统计|
|ReassInFragPkts|进行真重组的分片报文个数|
|ReassOutReassPkts|真重组成功返回的完整报文个数|
|NetOutPkts|net发送出口报文统计|
|EthOutPkts|eth发送出口报文统计，交给驱动发送|
|FragInPkts|分片报文入口总数|
|FragOutPkts|分片报文出口总数|
|ArpMissResvPkts|ARP查找失败，缓存报文并返回去保序的报文统计|
|ArpSearchInPkts|ARP查找入口报文统计|
|ArpHaveNormalPkts|ARP查找成功报文统计（存在正常ARP）|
|RcvErrIcmpPkts|收到ICMP差错报文，不产生差错报文的个数|
|NetBadVersionPkts|IP版本号错误的报文统计|
|NetBadHdrLenPkts|IP首部长度无效的报文统计|
|NetBadLenPkts|IP首部和IP数据长度不一致的报文统计|
|NetTooShortPkts|具有无效数据长度的报文统计|
|NetBadChecksumPkts|校验和错误的报文统计|
|NetNoProtoPkts|具有不支持的协议的报文统计|
|NetNoRoutePkts|路由查找失败的报文统计|
|TcpReassPkts|TCP重组乱序队列中的报文DB统计|
|UdpInPkts|UDP接收入口统计|
|UdpOutPkts|UDP发送报文统计|
|TcpInPkts|TCP接收入口统计|
|SndBufInPkts|进入发送缓冲区报文个数统计|
|SndBufOutPkts|从发送缓冲区释放的报文个数统计（ACK掉的报文统计）|
|SndBufFreePkts|从发送缓冲区释放的报文个数统计（释放socket节点释放的报文统计）|
|RcvBufInPkts|进入接收缓冲区报文个数统计|
|RcvBufOutPkts|从接收缓冲区释放的报文个数统计（用户接收走的报文统计）|
|RcvBufFreePkts|从接收缓冲区释放的报文个数统计|


**表 6**  /knet/ethstats,abn协议栈异常打点统计查询字段说明

|字段名|说明|
|--|--|
|AbnBase|预留异常基础统计项|
|ConnByListenSk|侦听sk不能发起connect|
|RepeatConn|已建链的sk不能重复建链|
|RefusedConn|连接被拒绝|
|ConnInProg|连接正在进行中|
|AcceptNoChild|accept没有有效的子socket|
|SetOptInval|设置tcp选项参数非法|
|KpIdInval|设置tcp选项keepidle参数非法|
|KpInInval|设置tcp选项keepintvl参数非法|
|KpCnInval|设置tcp选项keepcnt参数非法|
|MaxsegInval|设置tcp选项maxseg参数非法|
|MaxsegDisStat|设置tcp选项maxseg当前状态异常|
|DeferAcDisStat|设置tcp选项defer accept当前状态异常|
|SetOptNotSup|设置tcp选项opt参数不支持|
|TcpInfoInval|获取tcp选项tcpInfo参数非法|
|GetOptInval|获取tcp选项参数非法|
|GetOptNotSup|设置tcp选项opt参数不支持|
|SndConnRefused|连接已中断|
|SndCantSend|当前连接不能再发送数据|
|SndConnClosed|当前连接被关闭|
|SndNoSpace|发送缓冲区不足|
|SndbufNoMem|发送空间无法写入数据|
|RcvConnRefused|tcp已收到rst报文，用户执行socket接收操作|
|RcvConnClosed|tcp未建连或建连未完成，用户执行socket接收操作|


**表 7**  /knet/ethstats,mem协议栈内存使用统计查询字段说明

|字段名|说明|
|--|--|
|InitInitMem|初始内存分配的内存量（未释放的内存）|
|InitFreeMem|初始内存释放的内存量|
|CpdInitMem|复制内存分配的内存量|
|CpdFreeMem|复制内存释放的内存量|
|DebugInitMem|调试模块初始分配的内存量|
|DebugFreeMem|调试模块释放的内存量|
|NetdevInitMem|网络设备相关的内存分配量|
|NetdevFreeMem|网络设备释放的内存量|
|NamespaceInitMem|命名空间管理的内存分配量|
|NamespaceFreeMem|命名空间管理释放的内存量|
|PbufInitMem|数据包缓冲区（Pbuf）分配的内存量|
|PbufFreeMem|数据包缓冲区（Pbuf）释放的内存量|
|PmgrInitMem|进程管理模块初始内存分配的内存量|
|PmgrFreeMem|进程管理模块释放的内存量|
|ShmInitMem|共享内存（Shm）分配的内存量|
|ShmFreeMem|共享内存释放的内存量|
|TbmInitMem|TCM（TcmBuffer Manager）相关内存分配量|
|TbmFreeMem|TCM（TcmBuffer Manager）释放的内存量|
|UtilsInitMem|实用工具模块分配的内存量|
|UtilsFreeMem|实用工具模块释放的内存量|
|WorkerInitMem|工作线程相关内存分配量|
|WorkerFreeMem|工作线程释放的内存量|
|FdInitMem|文件描述符相关的内存分配量|
|FdFreeMem|文件描述符释放的内存量|
|EpollInitMem|Epoll相关的内存分配量|
|EpollFreeMem|Epoll释放的内存量|
|PollInitMem|Poll相关的内存分配量|
|PollFreeMem|Poll释放的内存量|
|SelectInitMem|Select相关的内存分配量|
|SelectFreeMem|Select释放的内存量|
|SocketInitMem|套接字相关的内存分配量|
|SocketFreeMem|套接字释放的内存量|
|NetlinkInitMem|Netlink相关的内存分配量|
|NetlinkFreeMem|Netlink释放的内存量|
|EthInitMem|Ethernet接口相关内存分配量|
|EthFreeMem|Ethernet接口释放的内存量|
|IpInitMem|IP层相关的内存分配量|
|IpFreeMem|IP层释放的内存量|
|TcpInitMem|TCP层相关的内存分配量|
|TcpFreeMem|TCP层释放的内存量|
|UdpInitMem|UDP层相关的内存分配量|
|UdpFreeMem|UDP层释放的内存量|


**表 8**  /knet/ethstats,pbuf协议栈PBUF使用统计查询字段说明

|字段名|说明|
|--|--|
|ipFragPktNum|IP分片包的数量，表示在网络中发生的IP分片数据包的数量|
|tcpReassPktNum|TCP重组包的数量，表示TCP数据包在接收端进行重组的次数|
|sendBufPktNum|发送缓冲区中数据包的数量|
|recvBufPktNum|接收缓冲区中数据包的数量|