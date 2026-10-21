# 配置文件合并脚本merge_conf.sh

介绍K-NET配置文件合并脚本的功能与使用方法，涵盖命令功能、命令格式及使用示例。该脚本用于升级或重复安装K-NET后，将自动备份的历史配置更新回被覆盖的配置文件，并说明执行期间的注意事项。

## 命令功能

用户在升级安装或者重复安装K-NET之后，配置文件/etc/knet/knet_comm.conf会被软件包内默认配置文件覆盖，用户原先修改过的配置文件会被备份命名为knet_comm.conf.bak，执行该脚本可以将knet_comm.conf.bak的历史配置更新到/etc/knet/knet_comm.conf。

## 命令格式

**sh /etc/knet/tools/merge_conf.sh**

## 使用示例

更新配置文件。

```bash
sh /etc/knet/tools/merge_conf.sh
```

> [!NOTICE]须知 
>该过程涉及到文件修改，为防止意外操作导致异常，在执行过程中会屏蔽退出信号（比如“Ctrl+C”），执行完毕之后才会恢复。
