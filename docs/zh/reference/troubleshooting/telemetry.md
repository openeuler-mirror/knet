# telemetry故障

## 服务端启动失败

### 现象描述

配置文件中开启telemetry后，服务端启动失败，日志出现报错：

```
K-NET dpdk telemetry mkdir rte dir failed, errno 13
```

### 原因

服务端未配置环境变量XDG\_RUNTIME\_DIR。

### 操作步骤

1.  设置“XDG\_RUNTIME\_DIR”启动环境变量，普通用户未设置该变量会产生错误。

    >**说明：** 
    >用户名使用KNET\_USER作为通配符进行示例，运行时请将其替换为实际用户名。环境变量路径涉及的权限及安全需用户保证。

    用户可以根据需要选择永久或者临时配置环境变量。如果用户选择临时配置环境变量，需要在每个终端会话执行相关命令。

    -   永久配置环境变量。

        > **说明：** 
        >配置完成之后重新切换到该用户时无需重新配置环境变量。

        1.  创建环境变量路径。

            ```
            cd /home/KNET_USER/
            mkdir knet
            ```

        2.  编辑环境变量相关文件。

            ```
            vi ~/.bashrc
            ```

        3.  按“i”进入编辑模式，在末尾加上：

            ```
            export XDG_RUNTIME_DIR=/home/KNET_USER/knet
            ```

        4.  按“Esc”键退出编辑模式，输入 **:wq!**，按“Enter”键保存并退出文件。

            ```
            source ~/.bashrc
            echo $XDG_RUNTIME_DIR #确认是否配置环境变量，如果已配置会显示配置的路径
            ```

    -   临时配置环境变量。

        >**说明：** 
        >-   服务端环境关闭或重启后，或者退出普通用户再重新切换到该用户，均需要重新执行步骤。
        >-   通过设置环境变量指定运行时目录，且该路径会根据不同的用户名而变化。

        1.  创建环境变量路径。

            ```
            cd /home/KNET_USER/
            mkdir knet
            ```

        2.  配置环境变量。

            ```
            export XDG_RUNTIME_DIR=/home/KNET_USER/knet
            echo $XDG_RUNTIME_DIR #确认是否配置环境变量，如果已配置会显示配置的路径
            ```

## telemetry连接失败
### 现象描述

执行`dpdk-telemetry.py -f knet -i 1`后，出现telemetry连接失败的报错：

```
Connecting to /var/run/dpdk/rte/dpdk_telemetry.v2
Error connecting to /var/run/dpdk/rte/dpdk_telemetry.v2
No DPDK apps with telemetry enabled available
```

或者

```
Connecting to /tmp/dpdk/rte/dpdk_telemetry.v2
Error connecting to /tmp/dpdk/rte/dpdk_telemetry.v2
No DPDK apps with telemetry enabled available
```

### 原因

dpdk-telemetry.py脚本没有找到可用的socket文件。

### 操作步骤

1.  配置文件knet\_comm.conf中开启telemetry选项，将“telemetry”改为“1”。

    ```
    vi /etc/knet/knet_comm.conf
    ```

    按“i”进入编辑模式，修改如下配置项：

    ```
    {
        "dpdk": {
            "telemetry": 1
        }
    }
    ```

    按“Esc”键退出编辑模式，输入 **:wq!**，按“Enter”键保存并退出文件。

2.  设置“XDG\_RUNTIME\_DIR”启动环境变量，普通用户未设置该变量会产生错误。

    >**说明：** 
    >用户名使用KNET\_USER作为通配符进行示例，运行时请将其替换为实际用户名。环境变量路径涉及的权限及安全需要用户保证。

    用户可以根据需要选择永久或者临时配置环境变量。如果用户选择临时配置环境变量，需要在每个终端页面执行相关命令。

    -   永久配置环境变量。

        >**说明：** 
        >配置完成之后重新切换到该用户时无需重新配置环境变量。

        1.  创建环境变量路径。

            ```
            cd /home/KNET_USER/
            mkdir knet
            ```

        2.  编辑环境变量相关文件。

            ```
            vi ~/.bashrc
            ```

        3.  按“i“进入编辑模式，在末尾加上：

            ```
            export XDG_RUNTIME_DIR=/home/KNET_USER/knet
            ```

        4.  按“Esc”键退出编辑模式，输入 **:wq!**，按“Enter”键保存并退出文件。

            ```
            source ~/.bashrc
            echo $XDG_RUNTIME_DIR #确认是否配置环境变量，如果已配置会显示配置的路径
            ```

    -   临时配置环境变量。

        >**说明：** 
        >-   服务端环境关闭或重启后，或者退出普通用户再重新切换到该用户，均需要重新执行步骤。
        >-   通过设置环境变量指定运行时目录，路径依据不同用户名会有差异。

        1.  创建环境变量路径。

            ```
            cd /home/KNET_USER/
            mkdir knet
            ```

        2.  配置环境变量。

            ```
            export XDG_RUNTIME_DIR=/home/KNET_USER/knet
            echo $XDG_RUNTIME_DIR #确认是否配置环境变量，如果已配置会显示配置的路径
            ```

3.  启动服务端进程。

    请根据实际业务进行启动。

4.  检查是否存在socket文件。

    ```
    ls $XDG_RUNTIME_DIR/dpdk/rte/dpdk*
    ```

    回显示例：

    ```
    /home/KNET_USER/knet/dpdk/rte/dpdk_telemetry.v2 # KNET_USER为用户名占位符，/dpdk/rte路径会自动创建，/home/KNET_USER/knet为[2]配置的环境变量
    ```

