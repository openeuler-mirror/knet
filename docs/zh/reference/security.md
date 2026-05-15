# 安全管理

## 防病毒软件例行检查

定期进行对集群的防病毒扫描，防病对软件例行检查会帮助集群免受病毒、恶意代码、间谍软件以及恶意程序侵害，减少系统瘫痪、信息泄露等安全风险。可以使用业界主流的防病毒软件进行防病毒检查。

## 漏洞修复

为了保证生产环境的安全，降低被供给的风险，请定期修复以下漏洞：

- 操作系统漏洞
- DPDK漏洞

## 安全配置基线

请参见[安全检查](../installation/installation.md#安全检查)。

## 日志管理

### 日志使用

安装K-NET RPM包后，K-NET日志是通过rsyslog重定向输出到/var/log/knet/knet_comm.log中，由操作系统的rsyslog服务管理。其中K-NET日志重定向配置文件默认在/etc/rsyslog.d/knet_rsyslog/conf。

日志查看方式如下：

- 转储后日志解压方式，在root用户下执行,以日志文件“knet_comm.log.1.gz”为例。

  ```bash
  gzip -dv knet_comm.log.1.gz
  ```

  回显如下：

  ```CodeFusion
  knet_comm.log.1.gz:         98.6%  --  replaced with knet_comm.log.1
  ```

  解压后文件名：

  ```CodeFusion
  -r------- 1 root root 2097381 Mar 14 09:06 knet_comm.log.1
  ```

- 日志查看命令：

  ```bash
  vim /var/log/knet/knet-comm.log
  ```

  或者

  ```bash
  tail -f  /var/log/knet/knet-comm.log
  ```

### 日志约束

#### 日志管理

在日志管理中请关注以下两点：

- 检查系统是否可以限制单个日志文件的大小。
- 检查日志空间占满后，需确保由机制进行清理。

#### 日志规格

在/etc/logrotate.d/knet中约束K-NET日志文件达到2MB就会触发knet_rsyslog.conf，进而触发Lograte（转储），转储后的日志为"knet_comm.log-序号.gz"。达到10个日志文件或者365天后，就会删除最旧的日志。

|  项目   |  限制 |
|------------|-------------|
| 单个日志文件大小 | 2MB    |
| 保留日志个数 | 10个    |
| 保留最长时间 | 365天    |

#### 日志转储

启用日志转储时，部分设备需检查SELinux。部分环境开启SELinux，rsyslog服务没有足够权限访问logrotate，从而导致日志无法继续转储。

1. 请参见[SELinux配置](#selinux配置)。

2. 重启rsyslog。
  
    ```bash
    systemctl restart rsyslog
    ```

## Capability

为消减安全风险，根据最小权限原则，K-NET业务进程使用非root用户运行。但部分需要root用户权限的操作使用了Capability机制，具体使用的Capability如下：

- CAP_DAC_OVERRIDE：忽略文件的DAC访问控制。初始化DPDK、DPDK大页内存申请和释放操作。
- CAP_DAC_READ_SEARCH：忽略文件读权限检查，忽略目录搜索权限，用于绕过文件访问控制权限，访问/proc/self/pagemaps。
- CAP_IPC_LOCK：允许锁定共享内存片段，用于DPDK初始化的内存大页锁定操作。
- CAP_SYS_ADMIN：允许执行系统管理任务，加载或卸载文件系统，设置磁盘等，用于DPDK初始化时读取/proc/self/pagemaps。
- CAP_NET_ADMIN：允许执行网络管理任务，用于创建和配置tap口。
- CAP_SYS_RAWIO：允许进程直接访问硬件端口和内存，DPDK初始化时访问/dev/vfio/noiommu-0所需，VFIO的No-IOMMU模式所需。
- CAP_NET-RAW：允许硬件进行数据通信、通道初始化时，需要CAP_NET_RAW权限。

安装时仅配置了“Permitted”，几这些Capability不会随进程启动生效。仅在初始化和管理面操作时，业务进程会自动配置“Effective”，使用完成后会立即删除Effective配置。
K-NET业务进程使用非root用户运行，由于redis-server设置了Capability，需要给libknet_frame.so设置set-user-id权限才能够使LD_PRELOAD生效。

- set-user-id：ld.so要确保只有被系统明确允许、能够提升权限的动态库才能被preload。

当可执行程序被设置了Capability，Linux的安全特性要求，LD_PRELOAD环境变量使用需满足以下三个条件：

- LD_PERLOAD环境变量指定的库文件不能包含斜线“/”。
- 库文件只会从标准路径下加载。
- 库文件必须使能set-user-id位。

## SELinux配置

> **说明** 
>
>- Linux系统默认开启的SELinux安全机制会显示K-NET部分功能，导致无法正常使用K-NET业务。这是Linux OS本身的行为。如果用户需在自己系统中使用SELinux，则需自行寻找解决方法。
>- 针对此限制，提供快速禁用SELinux的方法。
>- 禁用SELinux可能会导致安全问题，若用户最终解决方案本身没有规划启用SELinux，建议通过端到端的整体方案弥补SELinux关闭带来的风险，且需自行承担安全风险。若用户有SELinux的需求，建议根据实际的SELinux问题进行细粒度的规则配置，并保证整个系统的安全。

以下操作示例是在虚拟机侧使用root用户执行。

### 临时关闭SELinux
  
1. 关闭SELinux。

    ```bash
    setenforce 0
    ```

2. 查看SELinux状态。

    ```bash
    sestatus
    ```

    回显中的“SELinux Status”为“enabled”,"Current mode"变为“permissive”。

    ```CodeFusion
    SELinux Status:       enabled  
    Current mode:         permissive
    ```

### 永久关闭SELinux

永久关闭需重启设置的服务器才能生效。

```bash
sed -i "s/SELINUX=enforcing/SELINUX=disabled" /etc/selinux/config
```
