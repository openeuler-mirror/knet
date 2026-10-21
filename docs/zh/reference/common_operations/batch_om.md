# 批量运维

介绍采用Computing ToolKit对K-NET业务环境进行批量运维的方法，涵盖工具安装、服务器信息配置、批量分发任务的配置与执行，以及执行结果报告导出。阅读本指南后，您将能在同一时刻批量操作多个业务环境。

如果需要同一时刻批量操作多个业务环境，可以采用Computing ToolKit进行批量运维，具体步骤如下：

1. 安装Computing ToolKit工具。

    下载安装方式请参见[《Computing ToolKit 25.2.0 用户指南》](https://support.huawei.com/enterprise/zh/management-software/computing-toolkit-pid-264314551)中“安装Computing ToolKit”来操作，使用25.2.0版本的软件安装包。

2. 配置服务器信息。
    打开Computing ToolKit，进入首页“批量分发”，参见《Computing ToolKit 25.2.0 用户指南》中“5.7.3.3 添加设备”，设备类型选择“OS”，添加目标服务器的IP地址、用户名和密码，将需要批量操作的服务器信息全部添加到此列表中。

    ![Computing ToolKit添加目标服务器设备的界面，展示设备类型选择OS及填写IP地址、用户名、密码的配置项](../../figures/batch_om_1.png)

    ![Computing ToolKit批量分发首页服务器列表界面，展示已添加的目标服务器信息列表用于后续批量操作选择](../../figures/batch_om_2.png)

    ![Computing ToolKit批量分发操作界面，展示已配置服务器列表并准备进入批量分发命令配置流程](../../figures/batch_om_3.png)

3. 配置批量分发命令。
    1. 单击“批量分发”，选中要批量操作的DPU（Data Processing Unit，数据处理器）OS，单击“下一步”。

    ![Computing ToolKit批量分发任务配置界面，展示选中目标DPU OS后进入任务添加流程的操作位置](../../figures/batch_om_4.png)
    2. 单击“添加”，选择“文件传输”，填写“任务名称”、“本地路径”和“远端路径”，然后单击“确定”（该步骤用于将本地的K-NET传输到目标服务器）。

    ![Computing ToolKit文件传输任务配置对话框，展示填写任务名称、本地路径、远端路径等配置项以将K-NET传输到目标服务器](../../figures/batch_om_5.png)

    ![Computing ToolKit批量分发任务列表界面，展示已添加的文件传输任务，准备继续添加命令执行任务](../../figures/batch_om_6.png)
    3. 单击“添加”，选择“命令执行”，输入要批量执行的命令，然后单击“确定”。
    ![Computing ToolKit命令执行任务配置对话框，展示选择命令执行类型并输入批量执行命令的操作界面](../../figures/batch_om_7.png)

    ![Computing ToolKit命令执行任务中批量命令输入示例，展示鲲鹏与x86架构对应的命令内容填写](../../figures/batch_om_8.png)

    命令示例如下：
    - 鲲鹏架构：

        ```bash
        cd /home
        rpm -Uvh knet-1.0.0.aarch64.rpm
        ```

    - x86架构：

        ```bash
        cd /home
        rpm -Uvh knet-1.0.0.x86_64.rpm
        ```

4. 批量执行命令。
    1. 单击“下一步”，选择“执行业务流”，即可实现批量执行命令，即批量运维。
    ![Computing ToolKit批量执行命令操作界面，展示单击下一步选择执行业务流以启动批量运维的操作位置](../../figures/batch_om_9.png)

    ![Computing ToolKit批量执行完成后的结果界面，展示执行业务流运行状态并准备导出报告的操作位置](../../figures/batch_om_10.png)
    2.  单击“导出报告”，查看批量执行结果。

    ![Computing ToolKit批量执行结果报告导出界面，展示批量运维任务的执行结果与报告导出操作](../../figures/batch_om_11.png)
