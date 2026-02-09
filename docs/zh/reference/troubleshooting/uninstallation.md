# 卸载故障

## 现象描述

在服务端通过以下命令卸载K-NET软件包：

```
sh knet_ctl.sh --uninstall comm histack
```

回显如下：

```
[2025-02-10 11:18:12][ERROR] Failed to uninstall the histackdp package.
[2025-02-10 11:18:12][ERROR] An error occurred during the uninstallation.
See log(/var/log/knet/deploy/knet_deploy.log) for more details.
```

在服务端查看K-NET部署日志：

```
vim /var/log/knet/deploy/knet_deploy.log
```

回显如下：

```
[2025-02-10 11:18:12][INFO] sh knet_ctl.sh --uninstall comm histack
error: Failed dependencies:
	histackdp >= 1.4.4 is needed by (installed) knet-libknet-1.0.0-1.aarch64
[2025-02-10 11:18:12][ERROR] Failed to uninstall the histackdp package.
[2025-02-10 11:18:12][ERROR] An error occurred during the uninstallation.
```

## 原因

仅卸载用户态TCP/IP协议栈软件包（histackdp软件包），未卸载通信协议加速软件包（knet-libknet软件包）。

## 处理步骤

参考[卸载K-NET](../../installation/uninstallation.md)卸载K-NET。
