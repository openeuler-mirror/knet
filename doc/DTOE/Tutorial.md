# 安装K-NET
## 安装DTOE依赖
根据网卡DTOE驱动固件安装指导安装

## 安装开源依赖
`K-NET`在代码仓中提供了统一的编译构建脚本build.py，可以直接执行该脚本进行编译构建rpm包，这一步会自动拉取开源securec, cJSON
```shell
$ python build.py Release dtoe rpm 生成Release版本
# 若需要调试版本，执行以下命令
$ python build.py debug dtoe rpm
```
执行以下操作进入构建目录找到rpm包并安装，这里以ARM环境为例
```shell
$ cd ./build/rpmbuild/RPMS
$ rpm -Uvh ./ubs-knet-1.0.0.aarch64.rpm --force --nodeps
```

# 运行环境配置

## 配置动态库查找路径环境变量

```shell
$ echo "/usr/lib64" >> /etc/ld.so.conf
$ ldconfig
```

# KNET接口适配
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