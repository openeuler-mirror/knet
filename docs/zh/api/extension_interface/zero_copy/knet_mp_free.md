# knet\_mp\_free

## 接口名称

**knet\_mp\_free(void \*addr, void \*opaque\)**

## 接口描述

写缓冲区释放。

## 参数说明

|参数|说明|备注|
|--|--|--|
|*addr|写缓冲区起始地址|输入的写缓冲区必须是由knet_mp_alloc申请而来的。|
|*opaque|未使用参数|用户不需要使用的入参，建议输入NULL，该入参的目的是使释放函数的函数签名符合knet_iov_free_cb_t。|


## 返回值

无

