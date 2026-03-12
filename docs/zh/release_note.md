# Release Notes

## 产品版本信息

|  产品名称      |   软件名称   | 软件版本 |
|------------|-------------|-------------|
| Data Acceleration Kit | 网络加速套件（K-NET）   | 26.0.RC1 |

## 版本配套关系

### 鲲鹏环境

|  项目       |    配套关系1  |    配套关系2 |
|------------|-------------|-----------|
| 操作系统  | openEuler 22.03 LTS SP4(Host和Guest OS保持一致)   | CTyunos-2.0.1-220311-aarch64(4.19.90-2102.2.0.0062.ctl2.aarch64) |
| 服务器  |  TaiShan 200服务器（型号2280 VD）  | TaiShan 200服务器（型号2280 VD）    |
| 网卡  | SP670  | SP670 |
| CPU  | 鲲鹏920 7260处理器  | 鲲鹏920 7260处理器    |

### x86环境

| 项目       | 版本  |
|------------|-------|
| 操作系统  | openEuler 22.03 LTS SP1(Host和Guest OS保持一致)   |
| 服务器  |  FusionServer 2288H V6  |
| 网卡  | SP670  |
| CPU  | 1/2个第三代英特尔至强可扩展处理器（lce Lake）（8300/6300/5300/4300系列）  |

### 软件配套关系

| 项目       | 版本  |获取地址|
|------------|-------|-------|
| Redis  | 6.0.20   |[获取链接](https://github.com/redis/redis/tree/6.0.20)|
| DPDK  |  21.11.7 |[获取链接](https://fast.dpdk.org/rel/dpdk-21.11.7.tar.xz)|
| iPerf3  | 3.16  |[获取链接](https://github.com/esnet/iperf/releases/tag/3.16)|
| SockPerf  | 3.10  |[获取链接](https://github.com/Mellanox/sockperf/archive/3c65ad99cd385e18f8a2a655c19826e81a4d17e8.zip)|
| TPerf  | 1.0  |[获取链接](https://github.com/bytedance/libtpa/archive/3c9f05df7b7c8ebc46bfebc83c316ec50f149e1c.zip)|

## K-NET 1.0.0

### 更新说明

K-NET网络加速套件首次发布在开源社区。

K-NET作为网络协议加速框架，北向提供统一的Socket API，南向提供统一的数据IO抽象层，同时提供配置文件，允许用户通过配置进行协议路由。K-NET加速框架通过规范协议适配接口，同时内部集成了不同的协议栈，当前已经集成了基于以太的用户态TCP/IP协议，未来会继续集成基于RDMA的用户态协议和基于UB的协议栈，来满足和适配大数据、数据库、分布式存储等多个业务场景。

- K-NET框架Framework
    - Socket 透明替换：提供Socket透明替换接口支撑业务零侵入修改。
    - 提供插件化框架：插件化框架可以基于配置适配不同的协议，如用户态TCP协议栈，RDMA和UB协议等系统。
    - 提供协议编排能力：协议编排主要提供基础的协议进行编排，业务在启动时加载协议编排的能力使能业务处理。
    - 配置管理：加载协议栈和资源配置信息。

- 协议插件层
    - 提供一个用户态TCP/IP协议栈：用户态TCP/IP高性能协议栈，免除内核态和用户态的数据拷贝和系统调用，实现数据面高性能加速。
