# 网卡卸载调优

通过开启网卡的LRO（Large Receive Offload）与TSO（TCP Segmentation Offload）卸载功能，可降低CPU收发报文的处理开销，提升网络吞吐性能。需在双端（服务端与客户端）均执行以下配置。

> [!IMPORTANT]适用场景约束
>本调优仅适用于以下场景：
>
>- **流量分叉场景下的K-NET端**：流量分叉模式下网卡仍由内核协议栈接管，开启LRO/TSO卸载可提升收发性能。
>- **未部署K-NET的一端**：对端未使用K-NET时，建议开启LRO/TSO以降低CPU收发报文开销。
>
>对于**非流量分叉场景下的K-NET端**，K-NET通过DPDK接管网卡后，网卡已从内核协议栈剥离，内核侧无该网卡设备，此时对K-NET端执行`ethtool`卸载配置不生效，无需也不必执行本调优。
>
>**概念说明：**
>
>- LRO：网卡硬件将收到的多个小报文聚合为一个大报文上送协议栈，减少收包中断与协议栈处理次数。
>- TSO：网卡硬件将大块TCP数据按MSS分段发送，减少CPU的分段开销。
>- 以下命令中的网口名以enp6s0为例，请根据实际使用的网口替换。

1. 查看当前网口的卸载功能状态。

    ```bash
    ethtool -k enp6s0 | grep -E "large-receive-offload|tcp-segmentation-offload"
    ```

    回显示例：

    ```bash
    large-receive-offload: on
    tcp-segmentation-offload: on
    ```

    若LRO与TSO均已为on，可跳过后续步骤。

2. 开启LRO与TSO。

    ```bash
    ethtool -K enp6s0 lro on tso on
    ```

3. 确认配置是否生效。

    ```bash
    ethtool -k enp6s0 | grep -E "large-receive-offload|tcp-segmentation-offload"
    ```

    回显中两项均为on则代表配置成功。

    > [!NOTE]说明
    > 部分网卡驱动或固件版本可能不支持LRO，执行开启命令后会回显错误，此时仅开启TSO即可。若使用流量分叉模式，LRO/TSO由网卡硬件处理，无需DPDK接管。
