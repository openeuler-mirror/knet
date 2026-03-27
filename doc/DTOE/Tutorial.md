# KNET-DTOE简介
## DTOE简介
TCP协议卸载加速引擎(DTOE, Direct TCP Offloading Engine)，将TCP数据传输过程卸载到网卡硬件中进行，降低系统CPU的消耗，提高服务器处理性能；连接建立和删除以及连接维护等功能继续由软件内核处理。开发者可基于DTOE API接口对既有业务的TCP网络通信代码编程改造，实现DTOE卸载加速能力。具体介绍可见DTOE资料说明 xx support链接。
## K-NET简介
K-NET通过封装DTOE接口，提供更接近socket语义的KNET-DTOE接口，提升易用性。

**图 1** KNET-DTOE架构图

![](../figures/KNET-DTOE.png)

# 源码下载
可以使用如下方式下载KNET-DTOE源码
```shell
git clone https://gitcode.com/openeuler/knet.git
git checkout dtoe
```
# 源码目录结构
```shell
.
├── cmake      // 存放构建依赖
├── conf       // 存放初始配置项
├── demo       // 存放示例demo
├── docs       // 文档说明
├── opensource // 存放项目依赖
├── package    // 存放rpm包构建配置
├── src        // 存放项目的功能实现源码，仅该目录参与构建出包
├── test       // 存放项目的ut和sdv测试
└── build.py   // 统一的构建入口
```

# 安装K-NET
## 安装DTOE依赖
根据网卡DTOE驱动固件安装指导安装，xx suport资料链接

## 安装开源依赖
1. 安装libboundscheck依赖。
    - openEuler操作系统下：

    ```bash
    yum install -y libboundscheck
    ```
## K-NET
1. 下载K-NET源码并编译。
    ```bash
    git clone https://atomgit.com/openeuler/knet.git
    cd knet
    python build.py Release dtoe rpm
    ```
2. 安装K-NET。

    若首次安装，执行以下命令：
    - 鲲鹏架构：

        ```bash
        rpm -ivh build/rpmbuild/RPMS/knet-1.0.0.aarch64.rpm
        ```

    - x86架构：

        ```bash
        rpm -ivh build/rpmbuild/RPMS/knet-1.0.0.x86_64.rpm
        ```
    
    若安装过K-NET，执行以下命令直接升级：
    - 鲲鹏架构：

        ```bash
        rpm -Uvh build/rpmbuild/RPMS/knet-1.0.0.aarch64.rpm --force --nodeps
        ```

    - x86架构：

        ```bash
        rpm -Uvh build/rpmbuild/RPMS/knet-1.0.0.x86_64.rpm --force --nodeps
        ```

# 运行环境配置

## 配置动态库查找路径环境变量

```shell
$ echo "/usr/lib64" >> /etc/ld.so.conf
$ ldconfig
```

# API接口
具体接口描述见[对外接口](../../src/knet/api/dtoe_api/include/knet_dtoe_api.h)

# 修改KNET配置文件

```shell
$ vim /etc/knet/knet_comm.conf
```
配置参考如下：
| 配置项 | 说明 | 默认值 | 取值范围 | 约束说明 |
|--------|------|--------|----------|----------|
| **log_level** | 日志级别 | "WARNING" | "ERROR", "WARNING", "INFO", "DEBUG" | 支持大小写混写 |
| **channel_num** | 通道个数 | 1 | 64 | tx和rx通道个数各有channel_num个 |

# KNET日志
KNET运行过程中打印的日志在目录`/var/log/knet/knet_comm.log`