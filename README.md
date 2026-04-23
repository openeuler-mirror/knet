# K-NET

## 最新消息

- [2026.03.30]：K-NET首次发布到openEuler社区。

## 简介
传统的内核协议栈对网络数据包的处理涉及CPU中断、TCP/IP协议栈处理以及跨态拷贝，使得业务收发包的性能低，不足以满足业务的高性能转发需求。DPDK（Data Plane Development Kit）的出现解决了上述问题，可以提供高速转发能力，实现2层协议转发，但是缺少TCP/IP协议栈解析能力。应用想要利用DPDK进行业务加速，需要花费大量的工作自己实现2~4层协议、socket接口、事件处理才能进行业务对接。
为了解决这个问题，`K-NET`（K-Network，网络加速套件）框架应运而生。`K-NET`基于鲲鹏网卡，实现应用无感知迁移。`K-NET`提供高性能用户态协议栈，标准socket接口，业务无感知适配；业务可以使用`K-NET`高性能协议栈快速适配进行性能加速。
`K-NET`的目标是面向网络场景，打造的一套柔性的高性能协议框架，兼顾性能以及易用性，并在演进上能满足未来网络场景的需求。以下是Redis场景下K-NET加速效果。
| 场景       | 并发  | 包长 | 内核值    | K-NET值   |
|------------|-------|------|-----------|-----------|
| redis get  | 100   | redis-benchmark默认大小   | 194886.19 | 540511.31 |
| redis set  | 100   | redis-benchmark默认大小    | 169064.58 | 434008.94 |
| redis get  | 1000  | redis-benchmark默认大小 | 161480.45 | 505816.66 |
| redis set  | 1000  | redis-benchmark默认大小    | 141693.23 | 376067.09 |

具体请参见[K-NET产品描述](./docs/zh/Product_Descritions/Descritions_Menu.md)。


## 版本说明

K-NET版本说明包含版本配套关系和每个版本的特性变更说明，具体请参见[Release Note](./docs/zh/Release_Note.md)。

## 源码下载

可以使用如下方式下载K-NET源码。

```shell
$ git clone https://atomgit.com/openeuler/knet.git
```

## 源码目录结构

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

## 快速入门

快速入门适用于K-NET初学者，通过快速跑通一个实例，来试用K-NET的加速能力。
请参见[快速入门](./docs/zh/Quick_Start.md)。

## 安装部署

K-NET的组网规划、详细环境搭建和安装部署等操作请参见[安装](./docs/zh/Installation/Installation_Menu.md)。


## 特性使用

K-NET加速特性的详细使用方式、注意事项等请参见[特性指南](./docs/zh/Feature_Guide/Feature_Menu.md)。

### 硬件支持

K-NET支持Huawei SP670网卡，网卡驱动和固件可通过support官网获取。

### 特性支持

- 单进程加速
- 多进程加速
- 流量分叉
- 用户态网卡bond
- 用户态转发内核协议栈流量
- 零拷贝接口
- 用户态协议栈和业务共线程部署

## API接口

API接口说明请参见[API接口](./docs/zh/API/API_Menu.md)。

## Reference

附录参考请参见[参考](./docs/zh/Reference/Reference_Menu.md)。

## License

`K-NET`采用木兰宽松许可证, 第2版 MulanPSLv2。

## 贡献指南

请阅读贡献指南[CONTRIBUTING.md](./CONTRIBUTING.md)以了解如何贡献项目。

## 建议与交流

欢迎大家为社区做贡献。如果有任何疑问或建议，请提交issues，我们会尽快回复，感谢您的支持。
