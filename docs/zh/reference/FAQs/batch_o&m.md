# 批量运维

如果需要同一时刻批量操作多个业务环境，可以采用SmartKit进行批量运维，具体步骤如下：

1.  安装SmartKit工具。

    下载安装方式请参见[《SmartKit Computing 24.1.0 用户指南》](https://support.huawei.com/enterprise/zh/management-software/smartkit-computing-pid-253985677)中“安装SmartKit-\>安装SmartKit工具箱-\>操作步骤”来操作，使用24.0.0版本的软件安装包。

2.  配置服务器信息。
    1.  打开SmartKit，点击“服务器-\>批量分发-\>选择设备-\>OS-\>增加”，如下图所示：

        ![](/figures/zh-cn_image_0000002532211225.png)

        ![](/figures/zh-cn_image_0000002532371195.png)

        ![](/figures/zh-cn_image_0000002500411304.png)

    2.  添加目标服务器的IP地址、用户名和密码，将需要批量操作的服务器信息全部添加到此列表中。

        ![](/figures/zh-cn_image_0000002500531150.png)

        显示如下，表示添加成功。

        ![](/figures/zh-cn_image_0000002532211227.png)

3.  配置批量分发命令。
    1.  选择“批量分发”。

        ![](/figures/zh-cn_image_0000002532371199.png)

    2.  选中要批量操作的DPU OS，单击“配置业务流”。

        ![](/figures/zh-cn_image_0000002500411306.png)

    3.  选择配置方式，默认选择“自定义”，单击“下一步”。

        ![](/figures/zh-cn_image_0000002500531152.png)

    4.  进行“业务流配置”，选择“文件传输”，单击“添加”，选择“本地路径”，填写“远端路径”，然后单击“保存-\>完成”。

        ![](/figures/zh-cn_image_0000002532211229.png)

    5.  在“业务流配置”界面选择“命令执行”，单击“添加”，输入要批量执行的命令，然后单击“保存-\>完成”。

        ![](/figures/zh-cn_image_0000002532371201.png)

        命令示例如下：

        -   鲲鹏架构：

            ```
            cd /home; tar -xzvf Data-Acceleration-Kit-KNET_**.*.*_Arm.tar.gz; cd Data-Acceleration-Kit-KNET_**.*.*_Arm; sh knet_ctl.sh --upgrade comm all
            ```

        -   x86架构：

            ```
            cd /home; tar -xzvf Data-Acceleration-Kit-KNET_**.*.*_X86.tar.gz; cd Data-Acceleration-Kit-KNET_**.*.*_X86; sh knet_ctl.sh --upgrade comm all
            ```

4.  批量执行命令。
    1.  单击“执行业务流”，即可实现批量执行命令，即批量运维。

        ![](/figures/zh-cn_image_0000002500411308.png)

    2.  单击“导出报告“，查看批量执行结果。

        ![](/figures/zh-cn_image_0000002500531154.png)
