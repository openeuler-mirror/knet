# knet_mp_free

介绍K-NET的knet_mp_free接口，用于释放写缓冲区。涵盖参数说明、返回值与错误码，并说明待释放缓冲区须由配套的申请接口分配。适用于基于K-NET零拷贝特性进行写缓冲区管理的应用开发场景。

## 接口名称

**knet_mp_free(void \*addr, void \*opaque)**

## 接口描述

写缓冲区释放。

## 参数说明

|参数|说明|备注|
|--|--|--|
|*addr|写缓冲区起始地址|输入的写缓冲区必须是由knet_mp_alloc申请而来的。|
|*opaque|未使用参数|用户不需要使用的入参，建议输入NULL，该入参的目的是使释放函数的函数签名符合knet_iov_free_cb_t。|

## 返回值

无

## 错误码

|错误码|描述|
|--|--|
|EAFNOSUPPORT|K-NET TCP 模块未初始化。|
