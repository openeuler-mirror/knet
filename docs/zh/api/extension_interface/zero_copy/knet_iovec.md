# knet\_iovec

零拷贝读写接口需要用到的特殊的iovec结构体。

## 结构体定义

```
typedef void (*knet_iov_free_cb_t)(void *addr, void *opaque);

struct knet_iovec {
    void *iov_base;
    size_t iov_len;
    void *opaque;
    knet_iov_free_cb_t free_cb;
};
```

## 结构体描述

|字段|说明|备注|
|--|--|--|
|void *iov_base|用户读写缓冲区起始地址。|用作读缓冲区时，iov_base必须由knet_mp_alloc接口申请而来；用作写缓冲区时用户不填该字段。|
|size_t iov_len|用户读写缓冲区长度。|用作读缓冲区时，iov_len必须在knet_mp_alloc申请的内存长度范围内；用作写缓冲区时用户无需填写该字段。|
|void *opaque|读写缓冲区的释放回调函数的自定义输入参数。|用作读缓冲区时，用户负责填写该字段，可以为空；用作写缓冲区时用户不填该字段。|
|knet_iov_free_cb_t free_cb|读写缓冲区的自定义释放回调函数。|用作读缓冲区时，用户负责填写该字段，不能为空；用作写缓冲区时用户不填该字段。|


