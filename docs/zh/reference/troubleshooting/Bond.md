# Bond故障
## Bond场景下启动业务失败，日志报错“ BondPort 2 wait slaves activate failed”

#### 现象描述

Bond场景下启动业务失败，日志可以看到“ BondPort 2 wait slaves activate failed”。

![](/figures/zh-cn_image_0000002535828403.png)

#### 原因

被接管的网卡至少有一张链路不通，导致Bond等待slave端口唤醒过程超时，程序退出。

#### 处理步骤

参考[相关业务配置中的步骤2](../../feature/preparations.md#相关业务配置)中提到的取消接管网卡步骤还原网卡，检测网卡状态。

```
ethtool enp125s0f0  # 网卡名根据用户具体使用的网卡进行修改
```

![](/figures/zh-cn_image_0000002504028422.png)

以上回显说明网卡链路不通，需要先解决组网问题。
