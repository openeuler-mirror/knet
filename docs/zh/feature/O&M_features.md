# 运维架构

K-NET为了方便获取定位能力，提供了运维工具：抓包工具、网卡统计信息工具、日志工具以及运维脚本。

![](/figures/om1.png)

-   抓包工具**dumpcap**：为K-NET提供调试抓包功能，提供详细的网络实时监测细节，有两种适配K-NET的抓包模式。
    -   单进程抓包，基于主进程间的socket共享主进程收发队列，将共享报文写入文件，文件可由tcpdump命令和Wireshark工具查看。定位工具在使用抓包获取前需要先编译生成抓包程序。K-NET绑定的抓包获取能力在其他场景抓包可能失败，因此该抓包获取能力仅用于K-NET。
    -   多进程抓包，对多个K-NET加速进程提供抓包能力，可用过滤条件筛选目标进程的隔离数据包。多进程抓包时，仅能抓到在抓包工具启动之前已经启动的业务进程的数据包，即抓包工具启动后再启动的业务进程，无法抓包，需重启抓包工具。

-   网卡统计信息工具**dpdk-telemetry**：提供网卡收发报文统计指标，包括但不限于收发报文，错包等统计指标。

    获取网卡统计信息的工具借助了DPDK提供的统计工具“dpdk-stable-21.11.7/usertools/dpdk-telemetry.py“（DPDK维测统计工具）。对于统计信息所列出的所有功能，网卡可能并没有全部适配。目前该工具的使用场景主要是用于获取网卡队列统计信息，报文统计信息。目前常用的场景如下：

    -   验证流量是否正确到达配置的网卡队列。
    -   检查收发包、错包统计数量。
    -   检查TCP报文统计指标，包括TCP相关统计、TCP连接状态统计、协议栈各类报文统计、协议栈异常打点统计、协议栈内存使用统计、协议栈PBUF使用统计等。

-   日志工具**knet\_comm.log**：记录K-NET运行期间程序行为，提供错误跟踪、告警记录等基础功能，方便问题定位。
-   运维脚本**knet\_ctl.sh**：脚本运行option设置为collect时，可以收集运维信息，方便问题定位。

# 工具使用
## 1  抓包工具dumpcap

>**说明：** 
>-   请用户先参见[安装抓包工具](../installation/installation.md#抓包工具)后再参考本章节进行使用。
>-   K-NET使用的DPDK版本必须与抓包依赖的DPDK版本保持一致。当前仅支持21.11.7版本，该程序会随版本变更，需确保在正确版本下使用抓包工具。
>-   仅SP670网卡用户使用抓包时需要使用**LD\_PRELOAD=/usr/lib64/librte\_net\_sp600.so**，TM280网卡用户无需使用LD\_PRELOAD加载驱动。

**命令格式**
```shell
LD_PRELOAD=/usr/lib64/librte_net_sp600.so ./dumpcap [-D] [-h] [-i <pci bdf port>] [-c <packet_num_>] [-f "<filter expression>"] [-w <file_name>] [-a <stop_condition>_][-g] [-n] [-q] [-v] [-d] [-s] [-p] [-P]
```
**高频参数说明**

|        选项       |         是否必选  |            说明 |
|-------------------|------------------|----------------|
|      -D           |        否        | 显示可用PCI网口。  | 
|      -h           |         否       | 使用帮助信息。    | 
| -i <pci bdf port&gt;   |  否   | `-i 0000:06:00.0`：抓取精确指定网口的数据包。例如：`-i 0000:06:00.0`。不加`-i`则查找默认的一个DPDK接管网口。 |
| -c &lt;packet_num&gt;  | 否  | `-c 6000`：统计6000包后结束。例如：-c 6000。    |
|-w &lt;file_name&gt;    |否  |`-w ./tx.pcap`：将文件写入到当前目录的tx.pcap文件中。例如：`-w ./tx.pcap`。</p>若未指定文件路径，默认写入/tmp目录，例如`/tmp/dumpcap_xxxx.pcap`。|
|-f &lt;filter_expression&gt;</em></p>|否|`-f "host 192.168.1.1 \|\| port 6390"`：抓取数据包中有192.168.1.1或者端口为6390的数据包。

**其他参数说明**
|  选项    | 是否必选|   说明 |
|----------|--------|--------|
|-a &lt;stop_condition&gt;</p>|否|`-a 100`，例如抓取100kB的包停止，`-a filesize:100`。|
|      -g |    否   |支持linux群组用户访问抓包文件。|
|   -q    |    否    |不报告抓包数量。|
|   -v    |    否   |版本信息。|
|   -d    |    否   |打印过滤条件码。|
|    -s   |    否    |抓包大小，该参数对K-NET应用不可用，因此出于安全起见，K-NET仅会抓取数据包头。|
|  -p   |  否  |默认配置参数，关闭混杂。|
|  -P   |  否  |默认配置参数，抓包文件格式pcap。|
|  -n   |  否  |未使用，是预留参数。|

**使用示例**

>**说明：** 
>使用抓包前请先启动K-NET。抓包工具启动前已经运行的业务进程才能被抓包，如果新增业务进程，请重启抓包工具。业务启动命令参见“docs/feature”中各功能示例。

-   进入“dpdk-stable-21.11.7/app/dumpcap”目录再使用抓包定位K-NET劫持的业务。

    ```
    LD_PRELOAD=librte_net_sp600.so ./dumpcap -w /home/KNET_USER/tx.pcap # 使用默认DPDK接管网口，抓取K-NET业务数据包，写入/home/KNET_USER用户目录下，文件名为tx.pcap
    LD_PRELOAD=librte_net_sp600.so ./dumpcap -w /home/KNET_USER/tx.pcap -f "host 192.168.1.11 && port 6380" # 使用默认DPDK接管网口，在以上条件基础上新增host和port过滤条件, 192.168.1.11为抓包想要过滤的主机ip，6380为想要过滤主机的端口
    ```

-   如果dumpcap被意外终止，例如被执行**pkill -9 dumpcap**或**pkill dumpcap**命令。为了恢复使用，请启动-关闭-重启dumpcap，以恢复抓包定位能力。

    以下是意外终止并恢复模拟：

    ```
    pkill -9 dumpcap
    LD_PRELOAD=librte_net_sp600.so ./dumpcap -w /home/KNET_USER/tx.pcap # 第一次启动
    ```

    “ctrl+C”正常退出：

    ```
    LD_PRELOAD=librte_net_sp600.so ./dumpcap -w /home/KNET_USER/tx.pcap # 重启后恢复
    ```
## 2 网卡统计信息工具dpdk-telemetry

**使用前配置**

dpdk-telemetry会在DPDK安装后自动安装到系统可执行目录。

1.  将配置文件knet\_comm.conf中的“telemetry”参数设置为1后，启动业务进程。

    ```
    vim /etc/knet/knet_comm.conf
    ```

    按“i“进入编辑模式：

    ```
    {
        "dpdk": {
            "telemetry": 1
        }
    }
    ```

    按“Esc”键退出编辑模式，输入 **:wq!**，按“Enter”键保存并退出文件。

2.  启动K-NET。

    业务启动命令参见“docs/feature”中各功能示例。

3.  服务端运行脚本。

    >**说明：** 
    >-   普通用户进入工具使用界面前需设置“XDG\_RUNTIME\_DIR”环境变量，如果新开终端，需要在新起的终端中导入。环境变量路径涉及的权限及安全需要用户保证。参考[4](zh-cn_topic_0000002477080226.md#zh-cn_topic_0000002259824104_zh-cn_topic_0000002164538360_li1653223006)进行设置。
    >-   服务端环境关闭或重启后需要重新执行步骤。
    >-   通过设置环境变量指定运行时目录，路径依据不同用户名会有差异。
    >-   退出普通用户再重新切换到该用户需要重新配置。

    检查脚本是否安装到当前系统目录中：

    ```
    whereis dpdk-telemetry.py
    ```

    显示示例如下，表示存在。

    ```
    dpdk-telemetry.py: /usr/bin/dpdk-telemetry.py /usr/local/bin/dpdk-telemetry.py
    ```

    -   回显存在后，可直接使用：

        ```
        dpdk-telemetry.py -f knet -i 1 # 启动telemetry
        ```

    -   若脚本未安装到系统目录中，则从脚本实际位置使用：

        ```
        python3 <your-dpdk-path>/usertools/dpdk-telemetry.py  -f knet -i 1
        ```

        >**说明：** 
        ><your-dpdk-path\>表示脚本实际安装的位置。

**使用方法**

**dpdk-telemetry**详细使用方法参考[dpdk-telemetry.py](/reference/script_reference/dpdk-telemetry.md)。

## 3 日志工具knet\_comm.log

knet\_comm.log在K-NET安装后即可记录K-NET运行信息。

**命令格式**
```
vim /var/log/knet/knet\_comm.log
```
```
tail **<option\>** /var/log/knet/knet\_comm.log
```
**命令参数**

**表 1** **tail**命令参数
|  选项    | 是否必选|   说明 |
|----------|--------|--------|
|-n &lt;number&gt;|否|`-n 10`, 查看最后10行日志。                     |
|-s &lt;time&gt;  |否|需配合-f使用，`-f -s 30`, 30s时间后更新日志显示。|
|        -f       |否|实时显示日志尾部。                            |
|-c &lt;bytenum&gt|否|`-c 1000`，输出日志最后1000字节。                |

**使用示例**

-   通过vim命令回溯查看knet\_comm.log

    ```
    vim /var/log/knet/knet_comm.log
    ```

-   实时查看日志

    ```
    tail -f /var/log/knet/knet_comm.log
    ```

# 日常运维

## 1 网卡检查

1.  检测网卡驱动是否存在。

    ```
    rpm -qa | grep hi
    ```

    检查显示结果，确保hinic3、hisdk3、hinicadm3等网卡驱动已安装。

    ![](/figures/zh-cn_image_0000002519162195.png)

    若不存在请参考[《华为 SP600 智能网卡 用户指南》](https://support.huawei.com/enterprise/zh/doc/EDOC1100309168/426cffd9?idPath=23710424|251364417|9856629|253287505)或[《SP200&SP600 网卡 驱动源码 编译指南》](https://support.huawei.com/enterprise/zh/doc/EDOC1100429557/edc0a769)进行驱动的安装。

2.  网卡驱动存在后还需要进一步确认驱动已经加载到系统。

    检测驱动是否已加载。

    ```
    lsmod | grep hi
    ```

    正常情况是包含hinic3、hisdk3、hiudk3等网卡驱动。结果如下：

    ![](/figures/zh-cn_image_0000002486922342.png)

    若驱动不存在则执行如下命令加载驱动：

    ```
    modprobe hiudk3
    modprobe hisdk3
    modprobe hinic3
    ```

3.  查看网卡模板。

    ```
    hinicadm3 cfg_template -i hinic0
    ```

    ![](/figures/zh-cn_image_0000002487082318.png)

    “Current Info”字段中显示的为“0”表示模板正确，如果为其他值，请按照以下操作修改并重启：

    1.  切换网卡为模板0。

        ```
        hinicadm3 cfg_template -i hinic0 -s 0
        ```

    2.  重启。

        ```
        reboot
        ```

        > 重启后请再次执行查看命令查看当前网卡模板。

        >**说明：** 
        >若使用流量分叉功能，需将模板切换为ROCE\_2X100G\_UN\_ADAP。

## 2 业务状态检查

1.  检查DPDK接管状态。

    ```
    dpdk-devbind.py -s
    ```

    查询结果以SP670网卡VF为例，如下所示：

    ```
    Network devices using DPDK-compatible driver
    ============================================
    0000:06:00.0 'Device 375f' drv=vfio-pci unused=hisdk3
    Network devices using kernel driver
    ===================================
    0000:01:00.0 'Virtio network device 1041' if=enp1s0 drv=virtio-pci unused=virtio_pci,vfio-pci *Active*
    No 'Baseband' devices detected
    ==============================
    No 'Crypto' devices detected
    ============================
    No 'DMA' devices detected
    =========================
    No 'Eventdev' devices detected
    ==============================
    No 'Mempool' devices detected
    =============================
    No 'Compress' devices detected
    ==============================
    No 'Misc (rawdev)' devices detected
    ===================================
    No 'Regex' devices detected
    ===========================
    ```

    确认检查结果：

    -   若SP670网卡查询信息出现在“DPDK-compatible driver”一栏，则检测通过。
    -   若上述查询SP670网卡并未出现在“DPDK-compatible driver”一栏，参考[配置大页内存](/feature/preparations.md#配置大页内存)接管网卡部分继续配置。

2.  检查大页情况。

    ```
    dpdk-hugepages.py -s
    ```

    显示的示例结果如下：

    ```
    Node Pages Size Total
    0    2     1Gb    2Gb
    Hugepages mounted on /dev/hugepages /dev/hugepages1G
    ```

    若不存在对应大页，需要挂载相应大小大页，建议配置1G大页或者512MB大页，大页配置参考[配置大页内存](/feature/preparations.md#配置大页内存)配置大页内存部分。

3.  检查熵池。
    1.  检查是否安装rng-tools：

        ```
        rpm -q rng-tools
        ```

        回显示例如下：

        ```
        rng-tools-6.14-5.oe2203sp4.aarch64
        ```

        如果没有就安装：

        ```
        yum install -y rng-tools
        ```

    2.  检查rng-tools状态。

        ```
        systemctl status rngd
        ```

        -   若状态显示为“active”，表示状态正常：

            ```
            rngd.service - Hardware RNG Entropy Gatherer Daemon
                 Loaded: loaded (/usr/lib/systemd/system/rngd.service; enabled; vendor preset: enabled)
                 Active: active (running) since Fri 2024-12-27 11:07:45 CST; 4 days ago
               Main PID: 935 (rngd)
                  Tasks: 3 (limit: 42430)
                 Memory: 4.0M
                 CGroup: /system.slice/rngd.service
                         └─ 935 /sbin/rngd -f
            ```

        -   若服务不正常，考虑重启rngd服务：

            ```
            systemctl daemon-reload
            systemctl restart rngd
            ```

            再次查看状态是否为“active”。

## 3 K-NET状态检查

### 日志检查

通过日志查看K-NET状态。

-   正常日志情况不应存在ERR记录，下列示例展示了K-NET正常工作状况：

    ![](/figures/zh-cn_image_0000002535748361.png)

-   若存在ERR日志，则表明存在异常情况，常见异常情况包含如下：
    -   如出现以下日志报错，表示DPDK初始化失败，通常由于大页内存未挂载或未找到被接管的网卡。

        ![](/figures/zh-cn_image_0000002535828393.png)

        处理方式：参照[配置大页内存](/feature/preparations.md#配置大页内存)配置大页内存并接管网卡。

    -   网卡BDF号不正确，通常在“/etc/knet/knet\_comm.conf”编辑BDF配置时可能配置了不可用网卡，请再次检查填入被DPDK接管网卡的BDF号。

        ![](/figures/zh-cn_image_0000002504028410.png)

        处理方式：编辑  “/etc/knet/knet\_comm.conf” 配置文件，配置正确网卡BDF号。

    -   “/etc/knet/knet\_comm.conf ”配置不符合json字符串，可能存在符号错误，如未加逗号，引号等，请检查后再次运行K-NET，并检查knet\_comm.log是否还存在报错。

        ![](/figures/zh-cn_image_0000002503868572.png)

        处理方式：检查去掉“/etc/knet/knet\_comm.conf ”错误符号，常见排查方法是确保花括号“\{\}”在/etc/knet/knet\_comm.conf中配对正确， 引号""配对正确，逗号“,”没有多余或遗漏。

### 查看网卡收发包

dpdk-Telemetry适配后除了查看网口收发包、错包、丢包之外，还能查看TCP等网络状态。K-NET应用启动后，运行`dpdk-telemetry.py -f knet -i 1`进入命令状态，quit可退出。

常用运维命令：

-   查看网卡收发包、错包、丢包状态。

    ```
    /ethdev/stats,<port_id> 
    ```

    >**说明：** 
    >port\_id为网口BDF号的port\_id，不是Redis侦听端口。
    >执行**/ethdev/list**命令可查看DPDK接管网口BDF号的port\_id。

-   查看K-NET的TCP相关统计：TCP连接状态统计、各类报文统计、异常打点统计、内存使用统计。

    ```
    /knet/ethstats,{tcp | conn | pkt | abn | mem} 
    ```

    主要查看imissed, ierrors, oerrors等丢包、错包数据，示例如下，正常情况应为0。

    ![](/figures/zh-cn_image_0000002535748363.png)

    若存在丢包、错包，则表示网络存在异常，可结合dumpcap抓包工具进一步排查异常。

### 获取网络包

1.  确保已完成[配置大页内存](/feature/preparations.md#配置大页内存)，并在“dpdk-stable-21.11.7/app/dumpcap”目录执行下列操作可开启K-NET抓包。

    ```
    chmod a+s /usr/lib64/librte_net_sp600.so 
    setcap cap_sys_rawio,cap_dac_read_search,cap_sys_admin+ep dumpcap  
    LD_PRELOAD=librte_net_sp600.so ./dumpcap -w /home/<username>/tx.pcap
    ```

2.  抓包完成后，“Ctrl + C”结束，在/home/**_<username\>_**/下生成tx.pcap。
3.  可使用`tcpdump -r /home/_<username\>_/tx.pcap -v`查看抓包文件或使用Wireshark打开查看。
4.  使用`tcpdump -r /home/<username\>/tx.pcap`读取数据包，查看数据包详情，操作示例如下：

    ![](/figures/zh-cn_image_0000002535828395.png)

    -   正常情况应该有ARP建链包，如上图存在ARP请求和回应。
    -   若存在无法建链、丢包、无法接收数据包等异常情况，请先在ping场景下抓包测试，确保网络链路正常，进一步再通过数据包细节排查。

## 日志管理

日志记录于/var/log/knet/knet\_comm.log当中，根据/etc/knet/knet\_comm.conf配置中log\_level配置输出相应级别日志，可选级别包括ERROR、WARNING、INFO、DEBUG。日志仅会输出小于等于设置的级别。例如设置WARNING级别，仅会记录ERROR和WARNING级别日志。

1.  检查系统日志健康状态。

    ```
    systemctl status rsyslog
    ```

    示例显示如下：

    ```
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

2.  重启rsyslog服务。

    ```
    systemctl restart rsyslog
    ```

    重启后再次检查状态。


