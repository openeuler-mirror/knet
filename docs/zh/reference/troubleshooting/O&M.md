# 收集运维信息故障
## 收集时回显Please check the format of knet\_comm.conf.

#### 现象描述

在服务端通过以下命令收集运维信息：

```
sh knet_ctl.sh --collect comm
```

回显如下：

```
[2025-02-17 16:35:30][ERROR] Failed to collect dpdk-telemetry information. Please check the format of knet_comm.conf.
[2025-02-17 16:35:30][INFO] The information is collected and stored in /var/log/knet/info_collect/20250217163530_info_collect.tar.gz
[2025-02-17 16:35:30][ERROR] An error occurred during the collection.
See log(/var/log/knet/deploy/knet_deploy.log) for more details.
```

在服务端查看K-NET部署日志：

```
vim /var/log/knet/deploy/knet_deploy.log
```

回显如下：

```
[2025-02-17 16:35:30][INFO] sh knet_ctl.sh --collect comm
parse error: Expected separator between values at line 6, column 18
parse error: Expected separator between values at line 6, column 18
[2025-02-17 16:35:30][ERROR] Failed to collect dpdk-telemetry information. Please check the format of knet_comm.conf.
[2025-02-17 16:35:30][INFO] The information is collected and stored in /var/log/knet/info_collect/20250217163530_info_collect.tar.gz
[2025-02-17 16:35:30][ERROR] An error occurred during the collection.
```

#### 原因

/etc/knet/knet\_comm.conf配置文件不符合CJson格式。

#### 处理步骤

根据knet\_deploy.log日志提示信息修改配置文件格式，使其符合CJson格式。

## 收集时回显Please install jq.

#### 现象描述

在服务端通过以下命令收集运维信息：

```
sh knet_ctl.sh --collect comm
```

回显如下：

```
[2025-02-17 16:27:17][ERROR] Failed to collect dpdk-telemetry information. Please install jq.
[2025-02-17 16:27:17][INFO] The information is collected and stored in /var/log/knet/info_collect/20250217162717_info_collect.tar.gz
[2025-02-17 16:27:17][ERROR] An error occurred during the collection.
See log(/var/log/knet/deploy/knet_deploy.log) for more details.
```

#### 原因

需要安装jq依赖进行配置文件中telemetry配置项值的校验，未安装jq依赖便会报错。

#### 处理步骤

安装jq依赖后，再进行运维信息收集。

```
yum install -y jq
```

> **说明：** 
>如果yum安装jq回显如下：
>```
>openEuler                                                                       0.0  B/s |   0  B     00:00    
>Errors during downloading metadata for repository 'openEuler':
>  - Curl error (37): Couldn't read a file:// file for file:///path/repodata/repomd.xml [Couldn't open file /path/repodata/repomd.xml]
>Error: Failed to download metadata for repo 'openEuler': Cannot download repomd.xml: Cannot download repodata/repomd.xml: All mirrors were tried
>```
>请用户配置yum源后再安装jq依赖。

