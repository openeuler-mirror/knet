# 抓包工具dumpcap

>**说明：** 
>
>- 请用户先参见[安装抓包工具](../installation/installation.md#可选安装抓包工具)后再参考本章节进行使用。
>- K-NET使用的DPDK版本必须与抓包依赖的DPDK版本保持一致。当前仅支持21.11.7版本，该程序会随版本变更，需确保在正确版本下使用抓包工具。
>- 仅SP670网卡用户使用抓包时需要使用**LD\_PRELOAD=/usr/lib64/librte\_net\_hinic3.so**，TM280网卡用户无需使用LD\_PRELOAD加载驱动。

## 命令格式

```shell
LD_PRELOAD=/usr/lib64/librte_net_hinic3.so ./dumpcap [-D] [-h] [-i <pci bdf port>] [-c <packet_num_>] [-f "<filter expression>"] [-w <file_name>] [-a <stop_condition>_][-g] [-n] [-q] [-v] [-d] [-s] [-p] [-P]
```

## 参数说明

**高频参数说明**

|        选项       |         是否必选  |            说明 |
|-------------------|------------------|----------------|
|      -D           |        否        | 显示可用PCI网口。  | 
|      -h           |         否       | 使用帮助信息。    | 
| -i \<pci_bdf_port>  |  否   | `-i 0000:06:00.0`：抓取精确指定网口的数据包。例如：`-i 0000:06:00.0`。不加`-i`则查找默认的一个DPDK接管网口。 |
| -c \<packet_num>  | 否  | `-c 6000`：统计6000包后结束。例如：-c 6000。    |
|-w \<file_name>    |否  |`-w ./tx.pcap`：将文件写入到当前目录的tx.pcap文件中。例如：`-w ./tx.pcap`。<p>若未指定文件路径，默认写入/tmp目录，例如`/tmp/dumpcap_xxxx.pcap`。</p>|
|-f \<filter_expression>|否|`-f "host 192.168.1.1 \|\| port 6390"`：抓取数据包中有192.168.1.1或者端口为6390的数据包。

**其他参数说明**

|  选项    | 是否必选|   说明 |
|----------|--------|--------|
|-a \<stop_condition>|否|`-a 100`，例如抓取100kB的包停止，`-a filesize:100`。|
|      -g |    否   |支持linux群组用户访问抓包文件。|
|   -q    |    否    |不报告抓包数量。|
|   -v    |    否   |版本信息。|
|   -d    |    否   |打印过滤条件码。|
|    -s   |    否    |抓包大小，该参数对K-NET应用不可用，因此出于安全起见，K-NET仅会抓取数据包头。|
|  -p   |  否  |默认配置参数，关闭混杂。|
|  -P   |  否  |默认配置参数，抓包文件格式pcap。|
|  -n   |  否  |未使用，是预留参数。|

## 使用示例

>**说明：** 
>使用抓包前请先启动K-NET。抓包工具启动前已经运行的业务进程才能被抓包，如果新增业务进程，请重启抓包工具。业务启动命令参见[特性指南](../feature_guide/network_acceleration_verification.md)中各功能示例。

- 进入“dpdk-stable-21.11.7/app/dumpcap”目录再使用抓包定位K-NET劫持的业务。

    ```bash
    LD_PRELOAD=librte_net_hinic3.so ./dumpcap -w /home/KNET_USER/tx.pcap # 使用默认DPDK接管网口，抓取K-NET业务数据包，写入/home/KNET_USER用户目录下，文件名为tx.pcap
    LD_PRELOAD=librte_net_hinic3.so ./dumpcap -w /home/KNET_USER/tx.pcap -f "host 192.168.1.11 && port 6380" # 使用默认DPDK接管网口，在以上条件基础上新增host和port过滤条件, 192.168.1.11为抓包想要过滤的主机IP地址，6380为想要过滤主机的端口
    ```

- 如果dumpcap被意外终止，例如被执行**pkill -9 dumpcap**或**pkill dumpcap**命令。为了恢复使用，请启动-关闭-重启dumpcap，以恢复抓包定位能力。

    以下是意外终止并恢复模拟：

    ```bash
    pkill -9 dumpcap
    LD_PRELOAD=librte_net_hinic3.so ./dumpcap -w /home/KNET_USER/tx.pcap # 第一次启动
    ```

    “Ctrl+C”正常退出：

    ```bash
    LD_PRELOAD=librte_net_hinic3.so ./dumpcap -w /home/KNET_USER/tx.pcap # 重启后恢复
    ```

### 获取网络包

1. 确保已完成[配置大页内存](../feature_guide/environment_configuration.md#配置大页内存)，并在“dpdk-stable-21.11.7/app/dumpcap”目录执行下列操作可开启K-NET抓包。

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
