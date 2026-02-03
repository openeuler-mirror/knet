# 安装故障
## 安装时显示报错Failed to install the knet-libknet package

### 现象描述

在服务端通过以下命令安装K-NET软件包：

```
sh knet_ctl.sh --install comm knet
```

报错回显如下：

```
[2025-02-10 10:52:10][ERROR] Failed to install the knet-libknet package.
[2025-02-10 10:52:11][ERROR] An error occurred during the installation.
See log(/var/log/knet/deploy/knet_deploy.log) for more details.
```

在服务端查看K-NET部署日志：

```
vim /var/log/knet/deploy/knet_deploy.log
```

回显如下：

```
[2025-02-10 10:52:10][INFO] sh knet_ctl.sh --install comm knet
error: Failed dependencies:
	histackdp >= 1.4.4 is needed by knet-libknet-1.0.0-1.aarch64
[2025-02-10 10:52:10][ERROR] Failed to install the knet-libknet package.
[2025-02-10 10:52:11][ERROR] An error occurred during the installation.
```

### 原因

因为本版本的通信协议加速软件包（knet-libknet软件包）依赖1.4.4及以上版本的用户态TCP/IP协议栈软件包（histackdp软件包），当单独安装通信协议加速软件包（knet-libknet软件包）时，若未安装依赖的版本的用户态TCP/IP协议栈软件包（histackdp软件包），将导致安装报错。

### 处理步骤

参考[installation](/installation/installation.md)安装K-NET。

## 安装时显示软件包已安装

### 现象描述

在服务端通过以下命令安装K-NET软件包：

```
sh knet_ctl.sh --install comm all
```

报错回显如下：

```
[2025-02-10 10:58:55][ERROR] The histackdp package has been installed. Please use upgrade option.
[2025-02-10 10:58:55][ERROR] The knet-libknet package has been installed. Please use upgrade option.
[2025-02-10 10:58:55][ERROR] Failed to install the histackdp package.
[2025-02-10 10:58:55][ERROR] Failed to install the knet-libknet package.
[2025-02-10 10:58:55][ERROR] An error occurred during the installation.
See log(/var/log/knet/deploy/knet_deploy.log) for more details.
```

### 原因

环境已经安装K-NET软件包，不允许重复安装。

### 处理步骤

若用户仅安装25.2.0版本软件包，请直接执行升级命令，重新安装重复软件包：

```
sh knet_ctl.sh --upgrade comm all
```
