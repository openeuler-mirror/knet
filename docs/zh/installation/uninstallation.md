# 卸载K-NET

介绍卸载K-NET的方法，需根据安装方式选择对应的卸载方式，包括命令行卸载和批量卸载两种。阅读本指南后，您将能根据实际安装方式完成K-NET卸载。

请根据安装方式选择以下卸载方式的一种。

## 命令行卸载

```bash
rpm -e knet-1.4.0.aarch64
```

## Computing ToolKit批量卸载

对于Computing ToolKit方式的卸载方法，请参见[批量运维](../reference/common_operations/batch_om.md)，将卸载命令替换为如下。

- 鲲鹏架构：

    ```bash
    rpm -e knet-1.4.0.aarch64
    ```

- x86架构：

    ```bash
    rpm -e knet-1.4.0.x86_64
    ```

卸载后可使用以下命令查看，若返回结果为空则表示卸载成功。

```bash
rpm -aq | grep knet
```
