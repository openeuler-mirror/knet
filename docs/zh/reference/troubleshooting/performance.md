# 性能波动故障

## 现象描述

K-NET加速应用，TCP单连接性能波动、不稳定。

## 原因

网络性能波动原因较多，仅给出可能原因，供参考：TCP连接流量峰值过高，导致交换机缓冲区溢出，出现交换机丢包，引起TCP单连接性能波动。

以华为交换机为例，其他交换机命令需根据手册获取对应查询命令：

1.  进入交换机后，查看接口上基于队列的流量统计信息。

    ```
    display qos queue statistics interface interface-type interface-number
    ```

    >![](public_sys-resources/icon-note.gif) **说明：** 
    >-   interface-type表示接口的类型。
    >-   interface-number表示接口的编号。

    使用示例：

    ```
    display qos queue statistics interface 100GE 1/0/1
    ```

    输出信息描述：

    
    <table><tbody><tr id="row3484194614530"><td class="cellrowborder" valign="top" width="32.53%"><p id="p648444620539">Passed(Packet/Byte)</p>
    </td>
    <td class="cellrowborder" valign="top" width="67.47%"><p id="p8484104615314">通过的包数和字节数。</p>
    </td>
    </tr>
    <tr id="row174841146115319"><td class="cellrowborder" valign="top" width="32.53%"><p id="p154842464530">Pass Rate(pps/bps)</p>
    </td>
    <td class="cellrowborder" valign="top" width="67.47%"><p id="p848404614533">通过的报文速率信息（包每秒/比特每秒）。“-”表示不支持该项统计。</p>
    </td>
    </tr>
    <tr id="row18484194610532"><td class="cellrowborder" valign="top" width="32.53%"><p id="p1948434619539">Dropped(Packet/Byte)</p>
    </td>
    <td class="cellrowborder" valign="top" width="67.47%"><p id="p1148413464537">丢弃的包数和字节数。</p>
    </td>
    </tr>
    <tr id="row1548474675315"><td class="cellrowborder" valign="top" width="32.53%"><p id="p13485194675312">Drop Rate(pps/bps)</p>
    </td>
    <td class="cellrowborder" valign="top" width="67.47%"><p id="p124851346165311">丢弃的报文速率信息（包每秒/比特每秒）。“-”表示不支持该项统计。</p>
    </td>
    </tr>
    </tbody>
    </table>

    回显示例如下：

    ![](figures/zh-cn_image_0000002486509776.png)

    Dropped(Packet/Byte)与Drop Rate(pps/bps)不为0，表示交换机已经丢包。

## 处理步骤

建议查看交换机规格，交换机队列缓存越大，交换机丢包概率越小，TCP单连接性能越稳定。

1.  查看接口的缓存使用情况。

    以华为交换机为例，其他交换机命令需根据手册获取对应查询命令：

    ```
    display qos buffer-usage interface 100GE interface-number
    ```

    interface-number表示接口的编号。命令以及回显示例如下：

    ```
    display qos buffer-usage interface 100GE 1/0/1
    ```

    ```
    Total         : 105102 cells (26275 KBytes)
    Current used  : 0 cells (0 KBytes)
    Remained      : 105102 cells (26275 KBytes)
    Peak used     : 28979 cells (7244 KBytes)
    Average used  : 0 cells (0 KBytes)
    
    Buffer Usage on each Queue: (cells/KBytes)
    -----------------------------------------------------------------------------
    QueueIndex           Current               Peak                Average
    -----------------------------------------------------------------------------
    0                      0/0              28979/7244               0/0         
    1                      0/0                  0/0                  0/0         
    2                      0/0                  0/0                  0/0         
    3                      0/0                  0/0                  0/0         
    4                      0/0                  0/0                  0/0         
    5                      0/0                  0/0                  0/0         
    6                      0/0                  0/0                  0/0         
    7                      0/0                  0/0                  0/0         
    -----------------------------------------------------------------------------
    ```

    其中，Total (KBytes\)表示接口的总缓存能力。

2.  尝试调大交换机步骤1的接口的缓存Total (KBytes\)，或者换缓存容量更大的交换机。

