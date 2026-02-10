# 卸载K-NET

## 命令行卸载

进入K-NET安装目录，卸载K-NET软件包。

```
sh knet_ctl.sh --uninstall comm all
```

回显示例：

```
[2024-01-03 11:27:32][INFO] The knet-libknet package is uninstalled.
[2024-01-03 11:27:33][INFO] The histackdp package is uninstalled.
```

## SmartKit批量卸载

对于SmartKit方式的卸载方法，请参见[批量运维](../reference/FAQs/batch_om.md)，将卸载命令替换为如下。

-   鲲鹏架构：

    ```
    cd /path; tar -xzvf Data-Acceleration-Kit-KNET_25.2.0_Arm.tar.gz; cd Data-Acceleration-Kit-KNET_25.2.0_Arm; sh knet_ctl.sh --uninstall comm all
    ```

-   x86架构：

    ```
    cd /path; tar -xzvf Data-Acceleration-Kit-KNET_25.2.0_X86.tar.gz; cd Data-Acceleration-Kit-KNET_25.2.0_X86; sh knet_ctl.sh --uninstall comm all
    ```

    > **说明：** 
    >/path为用户上传K-NET软件包的路径，请用户根据实际填写。
