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

> [!NOTE]说明
>
> 安装K-NET后，会将带有`hinic3:`、`EAL:`、`TELEMETRY:` 和`libknet:`关键字的内核日志重定向到`/var/log/knet/knet_comm.log`。

## 命令参数

**表 1** **tail**命令参数

|  选项    | 是否必选|   说明 |
|----------|--------|--------|
| -n \<number> |否|`-n 10`, 查看最后10行日志。                     |
| -s \<time>  |否|需配合-f使用，`-f -s 30`, 30s时间后更新日志显示。|
| -f    |否|实时显示日志尾部。                            |
| -c \<bytenum> |否|`-c 1000`，输出日志最后1000字节。                |

## 使用示例

- 通过vim命令回溯查看knet\_comm.log

    ```bash
    vim /var/log/knet/knet_comm.log
    ```

- 实时查看日志

    ```bash
    tail -f /var/log/knet/knet_comm.log
    ```

## 日志管理

日志记录于/var/log/knet/knet\_comm.log当中，根据/etc/knet/knet\_comm.conf配置中log\_level配置输出相应级别日志，可选级别包括ERROR、WARNING、INFO、DEBUG。日志仅会输出小于等于设置的级别。例如设置WARNING级别，仅会记录ERROR和WARNING级别日志。

1. 检查系统日志健康状态。

    ```bash
    systemctl status rsyslog
    ```

    示例显示如下：

    ```ColdFusion
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

2. 重启rsyslog服务。

    ```bash
    systemctl restart rsyslog
    ```

    重启后再次检查状态。
