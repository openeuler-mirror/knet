# knet\_mp\_alloc

## 接口名称

**knet\_mp\_alloc(size\_t size\)**

## 接口描述

写缓冲区申请，底层实现为分配定长内存单元。

## 参数说明

|参数|说明|备注|
|--|--|--|
|size|写缓冲区大小|取值范围为[0, zcopy_sge_len]，"zcopy_sge_len"由配置项确定。|

## 返回值

类型：void \*

- 非 NULL：表示申请成功
- NULL：表示申请失败

## 错误码

|错误码|描述|
|--|--|
|EINVAL|入参size大于配置"zcopy_sge_len"的值。|
