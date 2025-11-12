# K-NET
传统的内核协议栈对网络数据包的处理涉及CPU中断、TCP/IP协议栈处理以及跨态拷贝，使得业务收发包的性能低，不足以满足业务的高性能转发需求。DPDK（Data Plane Development Kit）的出现解决了上述问题，可以提供高速转发能力，实现2层协议转发，但是缺少TCP/IP协议栈解析能力。应用想要利用DPDK进行业务加速，需要花费大量的工作自己实现2~4层协议、socket接口、事件处理才能进行业务对接。
为了解决这个问题，`K-NET`（K-Network，网络加速套件）框架应运而生。`K-NET`基于鲲鹏网卡，实现应用无感知迁移。`K-NET`提供高性能用户态协议栈，标准socket接口，业务无感知适配；业务可以使用`K-NET`高性能协议栈快速适配进行性能加速。
`K-NET`的目标是面向网络场景，打造的一套柔性的高性能协议框架，兼顾性能以及易用性，并在演进上能满足未来网络场景的需求。以下是Redis场景下K-NET加速效果。
| 场景       | 并发  | 包长 | 内核值    | K-NET值   |
|------------|-------|------|-----------|-----------|
| redis get  | 100   | redis-benchmark默认大小   | 194886.19 | 540511.31 |
| redis set  | 100   | redis-benchmark默认大小    | 169064.58 | 434008.94 |
| redis get  | 1000  | redis-benchmark默认大小 | 161480.45 | 505816.66 |
| redis set  | 1000  | redis-benchmark默认大小    | 141693.23 | 376067.09 |

## 1 源码下载
可以使用如下方式下载K-NET源码。
```shell
$ git clone <knet-repo-url>
```

## 2 源码目录结构
```shell
.
├── cmake      // 存放构建依赖
├── conf       // 存放初始配置项
├── doc        // 文档说明
├── opensource // 存放项目依赖
├── package    // 存放rpm包构建配置
├── src        // 存放项目的功能实现源码，仅该目录参与构建出包
├── test       // 存放项目的ut和sdv测试
└── build.py   // 统一的构建入口
```

## 3 用户指南
`K-NET`提供给开发者的的资料主要有用户指南。
- 《K-NET 通信协议加速 用户指南》
另外K-NET支持Huawei SP670网卡，网卡驱动和固件可通过support官网获取

## 4 编译与安装
编译前请确保Glibc版本及ASLR是否开启

#### 检查Glibc版本
```shell
$ ldd --version
```
Glibc 2.10及以上版本会开启堆栈保护，若查询出来的版本低于2.10，建议升级至2.10以上。这里以2.28版本为例
```shell
$ yum update glibc-2.28
```

#### 检查ASLR是否开启
ASLR是一种针对缓冲区溢出的安全保护技术，通过地址布局的随机化，增加攻击者预测目的地址的难度
```shell
$ cat /proc/sys/kernel/randomize_va_space
```
若结果不为2，请执行以下命令开启ASLR
```shell
$ bash -c 'echo 2 >/proc/sys/kernel/randomize_va_space'
```

## 5 编译、安装、执行K-NET示例
参考[Tutorial教程](./doc/Tutorial.md)


## 6 执行UT用例
可以按照如下方式，手动编译和执行UT用例。
```shell
$ python build.py
$ cd test/sdv_new/scripts
# 执行UT构建脚本并下载依赖
$ sh unit_test.sh t
# 设置系统资源上限
$ ulimit -n 65535
# 运行UT测试用例
$ sh unit_test.sh r
# 生成测试报告
$ sh unit_test.sh g
# UT覆盖率测试结果存放在build目录中，可解压查看
$ cd ../build ; tar -zxf lcov_result.tgz
$ start ./lcov_result/index.html
```

## License
`K-NET`采用木兰宽松许可证, 第2版 MulanPSLv2。

## 贡献指南
请阅读 贡献指南 `CONTRIBUTING.md` 以了解如何贡献项目。
