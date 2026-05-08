# 日志工具knet_comm.log

## 工具功能

记录K-NET运行期间程序行为，提供错误跟踪、告警记录等基础功能。当K-NET出现异常时，用户能借助日志记录快速定位问题。
knet_comm.log在K-NET安装后即可记录K-NET运行信息。

## 命令格式

```bash
vim /var/log/knet/knet_comm.log
```

```bash
tail <option> /var/log/knet/knet_comm.log
```

## 命令参数

**表 1** **tail**命令参数

|  选项    | 是否必选|   说明 |
|----------|--------|--------|
|-n &lt;number&gt;|否|`-n 10`, 查看最后10行日志。                     |
|-s &lt;time&gt;  |否|需配合-f使用，`-f -s 30`, 30s时间后更新日志显示。|
|        -f       |否|实时显示日志尾部。                            |
|-c &lt;bytenum&gt;|否|`-c 1000`，输出日志最后1000字节。                |

## 使用示例

- 通过vim命令回溯查看knet\_comm.log

    ```bash
    vim /var/log/knet/knet_comm.log
    ```

- 实时查看日志

    ```bash
    tail -f /var/log/knet/knet_comm.log
    ```