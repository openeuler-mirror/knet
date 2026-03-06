# 时延故障

## TCP单连接小报文传输时延较大

### 现象描述

K-NET加速应用，TCP单连接在传输小于最大报文长度（MSS）的报文时出现较大时延，可能是write-write-read模式导致的延迟确认（Delay Ack）超时问题。

### 原因

采用write-write-read模式，在数据包payload小于最大报文长度的小数据包场景下，因为客户端Nagle算法和服务端Delay Ack机制的存在，第二次write的包需要等到服务端Delay Ack超时才能发送，将显著增加端到端时延。

### 处理步骤

在使用TCP socket进行数据收发时，应避免采用write-write-read模式。建议采用write-read或write-write-write模式均可。
