# collect.sh（运维信息收集脚本）

**命令功能**

用户可以一键收集软件运维信息，包括：硬件信息（CPU、网卡、内存）、软件版本（K-NET、dpdk、网卡驱动、glibc、内核、os）、日志、业务运行状态（配置文件、持久化统计信息文件）。

**命令格式**

**sh /etc/knet/tools/collect.sh**


**使用示例**

收集运维信息

```
sh /etc/knet/tools/collect.sh
```

输出示例如下：

```
The information is collected and stored in /var/log/knet/info_collect/20260209184221_info_collect.tar.gz
```

如果在执行命令后，未显示上述回显时，用户通过“Ctrl+C”中断命令，回显示例如下所示。此时收集到的部分信息会存放在"/var/log/knet/info\_collect/时间戳\_info\_collect"的文件夹中（没有被压缩）。

> **须知：** 
>执行“Ctrl+C”中断命令后，收集信息的动作被终止，此时保存的运维信息不是完整的，不建议进行此操作。

```
^CInterrupted by user.
```

> **说明：** 
>-   收集的信息会存放在"/var/log/knet/info\_collect"目录下，详情请参见[表1](#OM-table)。
>-   收集到的信息统一打包为tar包，tar包的命名格式为："时间戳\_info\_collect.tar.gz"。
>-   执行一键信息收集脚本之前，请先确认"/var/log/knet"所在目录是否有足够的空间存放收集的信息文件以及日志文件。
>-   当系统目录容量不足时，请观察一下"/var/log/knet"查看是否信息收集文件及日志文件占用空间过大，并建议酌情进行手动转移或删除操作。

**表 1**  运维信息收集清单<a id="OM-table"></a>
  
|文件名称|文件内容|
|--|--|
|hw_info.txt|硬件信息：<li>CPU信息<li>网卡信息<li>内存信息
|sw_info.txt|软件版本：<li>K-NET版本信息<li>DPDK版本信息<li>网卡驱动版本信息<li>Glibc版本信息<li>内核版本信息<li>OS版本信息|
|log|日志信息：<li>K-NET运行日志文件<li>K-NET运行日志转储文件|
|statistic|业务运行状态：<li>配置文件<li>统计信息|

> **说明：** 
>以上文件名称中的时间戳为示例，具体以实际环境信息为准。

