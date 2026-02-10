# 安装前配置

-   **[安装依赖包](#安装依赖包)**  

-   **[（可选）安装DPDK](#可选安装dpdk)**  



## 下载软件包

**表 1**  patch获取列表

|软件包（压缩包）|说明|获取地址|
|--|--|--|
|dpdk-21.11.7-dumpcap.patch|抓包工具patch|https://gitee.com/openeuler/dpdk/blob/575def3e5f5be8da8662d442c6ecd46e9ec82acf/patch/dpdk-21.11.7-dumpcap.patch|


## 安装依赖包

1.  安装系统依赖。

    ```
    yum install -y libcap-devel tar gzip vim
    ```

2.  安装部署工具依赖。

    ```
    yum install -y jq
    ```

## （可选）安装DPDK

>**说明：** 
>如果已经安装21.11.7版本的DPDK，且不需要抓包功能，可跳过此章节。

### DPDK安装

在安装DPDK时应避免直接使用Yum源，因为Yum源安装的版本存在不可控风险。

1.  安装前先安装DPDK需要的依赖。

    ```
    yum install -y gcc # 安装编译工具
    yum install -y meson ninja-build numactl-devel python3-pyelftools libnl3 libnl3-devel# DPDK依赖
    ```

2.  按照[版本配套关系](../release_note.md)获取DPDK软件包。
3.  上传压缩包至服务器并传输到虚拟机，解压后进入解压文件夹目录。<a id="step3"></a>

    ```
    scp root@remote_host:/path/to/remote/dpdk-21.11.7.tar.xz /path/to/local/directory
    cd /path/to/local/directory
    tar -xf dpdk-21.11.7.tar.xz
    cd dpdk-stable-21.11.7
    ```

    > **说明：** 
    >-   remote\_host：物理机192.168.122.\*对应的IP地址。
    >-   /path/to/remote/：软件包的路径，需要根据实际情况替换。
    >-   /path/to/local/directory：虚拟机中的保存路径。

4.  执行如下命令安装驱动程序。

    ```
    meson -Ddisable_drivers=net/cnxk -Dibverbs_link=dlopen -Dplatform=generic -Denable_kmods=false -Dprefix=/usr build
    ```

    回显示例：

    ![](/figures/zh-cn_image_0000002503958012.png)

    ```
    ninja -C build
    ```

    回显示例：

    ![](figures/zh-cn_image_0000002535517975.png)

    ```
    ninja install -C build
    ```

    回显示例：

    ![](/figures/zh-cn_image_0000002503798182.png)

### （可选）安装抓包工具

>**说明：** 
>启用K-NET抓包功能才需要参考以下步骤安装，无需抓包可直接跳过以下步骤。
>以下提到的“dpdk-stable-21.11.7”为[步骤3](#step3)中DPDK解压所得目录，其他版本DPDK需自行适配。

1.  安装抓包工具依赖。

    ```
    yum install -y libpcap-devel libpcap make
    ```

2.  确保“dpdk-stable-21.11.7/app/dumpcap”目录下只有DPDK示例程序main.c和meson.build。若该目录下有其他文件，建议用户迁移至其他路径。
3.  请参见[下载软件包](#下载软件包)获取dpdk-21.11.7-dumpcap.patch并上传至“dpdk-stable-21.11.7/app”目录。
4.  进入“dpdk-stable-21.11.7/app”目录，应用patch。

    ```
    patch -p1 -d dumpcap/ < dpdk-21.11.7-dumpcap.patch
    ```

5.  进入dumpcap目录，执行make得到适配K-NET的dumpcap。

    ```
    cd dumpcap
    make
    ```

    >**说明：** 
    >如果编译失败，是由于缺少头文件或动态库，请检查Makefile中DPDK头文件路径_INCLUDEDIR_、DPDK动态库路径_LDDIR_、libpcap动态库路径_LIBPCAPDIR_下是否存在相应库或头文件，若不存在，安装后修改路径确保该路径下有对应文件。

6.  授予驱动和编译抓包程序执行权限。

    > **说明：** 
    >若为root用户可跳过此步骤。

    ```
    chmod a+s /usr/lib64/librte_net_sp600.so
    setcap cap_sys_rawio,cap_dac_read_search,cap_sys_admin+ep dumpcap
    ```

7.  （可选）若需要编译DPDK应用于其他业务时，请消除dpdk-21.11.7-dumpcap.patch的影响，操作顺序如下：
    1.  请先确保在“dpdk-stable-21.11.7”目录下。

        ```
        cd ./app/dumpcap
        ```

    2.  删除文件使得最后保留main.c Makefile meson.build三个文件。

        ```
        make clean 
        rm *.pcap
        ```

    3.  回退到“dpdk-stable-21.11.7/app”目录。

        ```
        cd ../
        ```

    4.  撤销patch变更。

        ```
        patch -p1 -Rd dumpcap/ < dpdk-21.11.7-dumpcap.patch
        ```

    5.  撤销后“dpdk-stable-21.11.7/app/dumpcap”恢复到源码刚解压后的状态，即只包含main.c和meson.build。

        ```
        ls dumpcap
        ```

        回显示例：

        ```
        main.c	meson.build
        ```

# 安装K-NET

## 命令行安装

1.  获取K-NET软件包上传至服务器。如果是虚拟机，则需要拷贝到虚拟机。
2.  解压软件包，并进入解压缩后的目录。
    -   鲲鹏架构：

        ```
        tar -xzvf Data-Acceleration-Kit-KNET_25.2.0_Arm.tar.gz
        cd Data-Acceleration-Kit-KNET_25.2.0_Arm
        ```

    -   x86架构：

        ```
        tar -xzvf Data-Acceleration-Kit-KNET_25.2.0_X86.tar.gz
        cd Data-Acceleration-Kit-KNET_25.2.0_X86
        ```

3.  安装K-NET软件包。

    >**须知：** 
    >建议使用knet\_ctl.sh脚本进行安装/升级K-NET。若手动执行rpm包安装相关命令，可能导致配置文件的丢失。

    ```
    sh knet_ctl.sh --install comm all # 安装K-NET，适用于未安装过或者已卸载了K-NET的环境场景
    ```

    回显示例如下所示。

    ```
    [2025-02-08 10:03:40][INFO] The histackdp package is installed.
    [2025-02-08 10:03:45][INFO] The knet-libknet package is installed.
    ```

## SmartKit批量安装

对于SmartKit方式的安装部署方法，请参见[批量运维](../reference/FAQs/batch_om.md)，将安装命令替换为如下。

-   鲲鹏架构：

    ```
    cd /path; tar -xzvf Data-Acceleration-Kit-KNET_25.2.0_Arm.tar.gz; cd Data-Acceleration-Kit-KNET_25.2.0_Arm; sh knet_ctl.sh --install comm all
    ```

-   x86架构：

    ```
    cd /path; tar -xzvf Data-Acceleration-Kit-KNET_25.2.0_X86.tar.gz; cd Data-Acceleration-Kit-KNET_25.2.0_X86; sh knet_ctl.sh --install comm all
    ```

    > **说明：** 
    >“/path”为用户上传K-NET软件包的路径，请根据实际填写。

