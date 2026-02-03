# knet\_ctl.sh（运维脚本）

**命令功能**

用户可以使用一键化运维脚本实现软件的一键化安装、升级、卸载、收集运维信息的操作。

**命令格式**

**sh knet\_ctl.sh** _--<option\> <feature\> <package\>_

**命令参数**

|参数|是否必选|说明|
|--|--|--|
|--option|是|支持以下参数：<li>--install：安装（适用于未安装过或者已卸载了K-NET的场景）<li>--upgrade：升级<p>**说明：**<p>升级后需要重新配置K-NET动态库、knet_mp_daemon、knet_comm.conf以及Redis相关权限，步骤参考[相关业务配置中的步骤3](../../feature/preparations.md#相关业务配置)。<li>--uninstall：卸载<li>--collect：收集运维信息<li>--help：查看帮助信息|
|feature|是|comm：安装K-NET_COMM文件夹下的通讯特性软件包。仅使用--install、--upgrade、--uninstall、--collect参数时需要设置此参数。|
|package|否|仅使用--install、--upgrade、--uninstall参数时需要设置此参数。支持以下参数：<li>knet：仅安装/升级/卸载K-NET框架与配置文件。<p>**说明：**<p>升级K-NET时会进行配置文件合并，保留之前设置过的属性。<li>histack：仅安装/升级/卸载用户态TCP/IP协议栈。<li>all：安装/升级/卸载全部模块。|


**使用示例**

-   获取运维脚本帮助信息。

    ```
    sh knet_ctl.sh --help
    ```

    回显示例如下所示。

    ```
    Usage: sh knet_ctl.sh --option [feature] [package] 
    
    
    Option:
      install    Install software
      uninstall  Uninstall software
      upgrade    Upgrade software
      collect    Collect information
      help       Show this help information and exit
    
    
    Feature:
      comm       Communication protocol acceleration(K-NET_COMM)
    
    
    Package:
      knet       K-NET Framework rpm package
      histack    K-NET User-mode TCP/IP protocol stack
      all        all rpm packages in the feature
    
    
    Example:
      sh knet_ctl.sh --install comm knet
      sh knet_ctl.sh --collect comm
      sh knet_ctl.sh --help
    ```

-   安装K-NET软件包

    ```
    sh knet_ctl.sh --install comm all
    ```

    回显示例如下所示。

    ```
    [2025-02-08 10:03:40][INFO] The histackdp package is installed.
    [2025-02-08 10:03:45][INFO] The knet-libknet package is installed.
    ```

-   升级K-NET软件包

    ```
    sh knet_ctl.sh --upgrade comm all
    ```

    回显示例如下所示。

    ```
    [2025-02-08 10:00:22][INFO] The histackdp package is upgraded.
    [2025-02-08 10:00:32][INFO] The knet-libknet package is upgraded.
    ```

-   卸载K-NET软件包

    ```
    sh knet_ctl.sh --uninstall comm all
    ```

    回显示例如下所示。

    ```
    [2025-02-08 10:03:09][INFO] The knet-libknet package is uninstalled.
    [2025-02-08 10:03:09][INFO] The histackdp package is uninstalled.
    ```

-   收集运维信息

    > **须知：** 
    >约束：需要运行K-NET业务并将配置文件中telemetry配置项置为1。

    ```
    sh knet_ctl.sh --collect comm
    ```

    输出示例如下：

    ```
    [2025-02-06 20:16:44][INFO] The information is collected and stored in /var/log/knet/info_collect/20250206201643_info_collect.tar.gz
    ```

    如果在执行命令后，未显示上述回显时，用户通过“Ctrl+C”中断命令，回显示例如下所示。此时收集到的部分信息会存放在“/var/log/knet/info\_collect/时间戳\_info\_collect“的文件夹中（没有被压缩）。

    ```
    ^C[2025-02-06 21:21:40][ERROR] Interrupted by user.
    [2025-02-06 21:21:40][ERROR] An error occurred during the collection.
    See log(/var/log/knet/deploy/knet_deploy.log) for more details.
    ```

    > **须知：** 
    >执行“Ctrl+C”中断命令后，收集信息的动作被终止，此时保存的运维信息不是完整的，不建议进行此操作。

    > **说明：** 
    >-   收集的信息会存放在“/var/log/knet/info\_collect”目录下，详情请参见[表1](#OM-table)。
    >-   收集到的信息统一打包为tar包，tar包的命名格式为："时间戳\_info\_collect.tar.gz"。
    >-   执行一键信息收集脚本之前，请先确认“/var/log/knet“所在目录是否有足够的空间存放收集的信息文件以及日志文件。
    >-   当系统目录容量不足时，请观察一下“/var/log/knet“查看是否信息收集文件及日志文件占用空间过大，并建议酌情进行手动转移或删除操作。

    **表 1**  运维信息收集清单<a id="OM-table"></a>
  
  |文件名称|文件内容|
  |--|--|
  |hw_info.txt|硬件信息：<li>CPU信息<li>网卡信息<li>内存信息|
  |sw_info.txt|软件版本：<li>K-NET版本信息<li>HiStack版本信息<li>DPDK版本信息<li>网卡驱动版本信息<li>Glibc版本信息<li>内核版本信息<li>OS版本信息|
  |log|日志信息：<li>deploy文件夹：运维日志<li>K-NET运行日志文件<li>K-NET运行日志转储文件|
  |statistic|业务运行状态：<li>配置文件<li>统计信息|

  > **说明：** 
  >以上文件名称中的时间戳为示例，具体以实际环境信息为准。

