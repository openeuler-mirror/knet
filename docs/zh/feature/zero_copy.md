# 零拷贝功能

本节主要说明如何配置、使用[API参考](../api/api_readme.md)的零拷贝接口，提供零拷贝使用伪代码，指导用户使用。

>**须知：** 
>业务在使用零拷贝接口时，必须遵循以下约束，以保证接口使用的安全性。

- 业务在声明struct knet\_iovec类型的变量时应对其进行正确的初始化，确保knet\_iovec中的iov\_base字段由knet\_mp\_alloc申请而来，iov\_len字段不大于申请的写缓冲区的实际长度，否则会导致发送失败，或者段错误（SegFault）的发生。

    ```c
    // 错误场景一：调用knet_zwritev时，入参iov的iov_base指向非法地址
    struct knet_iovec iov = {0};
    ...
    
    iov.iov_base = 0x1;    // 填充非法的内存地址
    iov.iov_len = iovlen;
    iov.free_cb = knet_mp_free;
    iov.opaque = NULL;
    knet_zwritev(sockfd, &iov, 1);    // 出现非法地址访问的段错误！
    ...
    
    // 错误场景二：调用knet_zwritev时，入参iov的iov_base指向的地址不是由knet_mp_alloc申请而得来的
    struct knet_iovec iov = {0};
    iov.iov_base = malloc(iovlen);    // 未使用knet_mp_alloc返回的内存
    iov.iov_len = iovlen;
    iov.free_cb = knet_mp_free;
    iov.opaque = NULL;
    knet_zwritev(sockfd, &iov, 1);    // 接口不会返回错误，但对端不会接收到正确的数据报文
    
    // 正常场景
    struct knet_iovec iov = {0};
    iov.iov_base = knet_mp_alloc(iovlen);    // 使用knet_mp_alloc申请写缓冲区内存
    iov.iov_len = iovlen;
    iov.free_cb = knet_mp_free;
    iov.opaque = NULL;
    knet_zwritev(sockfd, &iov, 1);    // 发送成功，对端可以收到正确的数据报文
    ```

    业务应自定义合理的iov的释放回调，或使用knet\_mp\_free来作为iov的free\_cb，否则会导致内存泄漏，甚至是段错误。

    ```c
    // 错误场景一：调用knet_writev时，free_cb指向非法的地址
    struct knet_iovec iov = {0};
    
    iov.iov_base = knet_mp_alloc(iovlen);
    iov.iov_len = iovlen;
    iov.free_cb = 0x1;   // free_cb 指向的地址非法
    iov.opaque = NULL; 
    
    knet_zwritev(sockfd, &iov, 1);   // 不会立即返回错误，实际调用free_cb时发生段错误！
    
    
    // 错误场景二：调用knet_iov_free时，free_cb设置不合理
    void FreeCb(void *addr, void *opaque)     // 自定义的不合理的释放回调，实际上什么也没做
    {
        return;
    }
    iov.iov_base = knet_mp_alloc(iovlen);
    iov.iov_len = iovlen;
    iov.free_cb = FreeCb;   // 使用自定义的释放回调
    iov.opaque = NULL;
    knet_zwritev(sockfd, &iov, 1);   // 不会立即返回错误，会出现内存泄漏，写缓冲区不会被正常释放，内存泄漏！
    ```

- 业务访问knet\_mp\_alloc或knet\_zreadv两个函数填充的iov\_base时，需自行保证内存访问的安全性。对iov\_base进行访存时，不要超过iov\_len的限制范围。如果越界访存，可能出现内存破坏，或者段错误的问题。

    ```c
    // 错误场景一：使用knet_iovec时读内存越界
    struct knet_iovec iov = {0};
    knet_zreadv(sockfd, &iov, 1);   // 读取成功，knet_zreadv接口填充iov的各个字段
    uint8_t *data = (uint8_t *)iov.iov_base;
    for (size_t i = 0; i < iov.iov_len + 1000; i++) {     // 读越界，可能导致读取错误数据或者出现段错误！
        printf("%02X ", iov[i]);
        if ((i + 1) % 16 == 0) {
            printf("\n");
        }
    }
    
    // 错误场景二：使用knet_iovec时写内存越界
    struct knet_iovec iov = {0};
    iov.iov_base = knet_mp_alloc(iovlen);   // 使用knet_mp_alloc申请了写缓冲区
    memcpy(iov.iov_base, data, iovlen + 1000);   // 写越界！可能导致内存被破坏，或者出现段错误！
    ```

>**说明：** 
>零拷贝功能指K-NET提供特殊的读写接口，使用零拷贝的读写接口可以消除用户缓冲区到mbuf间的内存拷贝，以提高读写的性能。
>具有如下约束：
>
>- 业务使用K-NET零拷贝接口，需保证配置项“zcopy\_enable”为1，即已使能。
>- 业务使用零拷贝写接口时的iov\_len不得超过配置项“zcopy\_sge\_len”的值，该配置项建议设置为希望发送的最大iov\_len的值。
>- 业务单次调用零拷贝写接口写入的数据总长度不得大于“def\_sendbuf”的值。
>- “zcopy\_sge\_num”推荐按照[用户态TCP/IP协议栈配置项](../configuration_item_reference.md#用户态tcpip协议栈配置项)中的zcopy\_sge\_num的计算公式配置。
>- 业务使用零拷贝读接口读取数据并使用完毕后，需调用iov的free\_cb来释放读缓冲区。
>- 业务使用零拷贝写接口时，应该调用写缓冲区申请接口knet\_mp\_alloc来申请写缓冲区。
>- 零拷贝写接口的发送操作具有原子性，要么全部发送成功，要么全部发送失败。
>- 通过设置socket fd的属性可以设置零拷贝接口的阻塞模式和非阻塞模式。零拷贝读接口支持阻塞模式和非阻塞模式，零拷贝写接口仅支持非阻塞模式。
>- 为保证零拷贝接口有正常的性能，建议开启配置中的tso和lro选项。

1. 业务适配<term>K-NET</term>零拷贝接口。
    - 以下为业务使用K-NET零拷贝读接口的伪代码。

        ```c
        #include "knet_socket_api.h"
        
        // 零初始化 knet_iovec
        struct knet_iovec iov[iovcnt] = {0};
        
        // 调用零拷贝读接口读取数据
        ssize_t ret = knet_zreadv(sockfd, iov, iovcnt);
        if (ret <= 0) {
            // 读接口返回值小于等于零表示失败，进行错误处理
        }
        
        // 业务处理读取到的数据
        process_data(iov, iovcnt, ret);
         
        // 业务处理完数据后，调用iov的free_cb对读缓冲区进行释放
        for(int i = 0; i< iovcnt; ++i) {
            
            if (iov[i].free_cb != NULL) {
                iov[i].free_cb(iov[i].iov_base, iov[i].opaque);
            }
        }
        ```

    - 以下为业务使用K-NET零拷贝写接口的伪代码。

        ```c
        #include "knet_socket_api.h"
        
        // 零初始化 knet_iovec
        struct knet_iovec iov[iovcnt] = {0};
        
        // 调用knet_mp_alloc申请写缓冲区，释放回调设置为knet_mp_free，knet_mp_free不需要额外的自定义参数opaque，可以设置为NULL
        for(int i = 0; i < iovcnt; ++i) {
            
            iov[i].iov_base = knet_mp_alloc(iovlen);
            assert(iov[i].iov_base != NULL);
            iov[i].iov_len = iovlen;
            iov[i].free_cb = knet_mp_free;
            iov[i].opaque = NULL;
        }
        
        // 往写缓冲区中填充数据
        for (int i = 0; i < iovcnt; ++i) {
            fill_data(iov[i].iov_base, iov[i].iov_len);
        }
        
        // 调用零拷贝写接口发送数据
        ssize_t ret = knet_zwritev(sockfd, iov, iovcnt);
        if (ret <= 0) {
            // 写接口返回值小于等于零表示失败，进行错误处理，需手动调用iov的free_cb来保证写缓冲区的正常释放
            for(int i = 0; i < iovcnt; ++i) {
                if (iov[i].free_cb != NULL) {
                    iov[i].free_cb(iov[i].iov_base, iov[i].opaque);
                }
            }
        }
        
        // 发送成功后无需手动释放写缓冲区
        ```

    - 以下为业务使用K-NET零拷贝写接口，并使用自定义的释放回调的伪代码。

        ```c
        #include "knet_socket_api.h"
        
        // 业务调用knet_mp_alloc申请写缓冲区，并将其添加到业务自定义的内存池中
        void *buf = NULL;
        for (int i = 0; i < MP_MAX_NUM; ++i) {
            buf = knet_mp_alloc(MP_SIZE);
            assert(buf != NULL);
            MempoolPut(poolId, buf);   // 用户自定义内存池的put函数
        }
        
        void FreeCb(void *addr, void *poolId)   // 自定义释放回调，将写缓冲区放回用户自建的内存池中
        {
            MempoolPut(poolId, addr);    // 用户自定义内存池的put函数 
        }
        
        // 调用用户自建内存池的申请函数申请写缓冲区，free_cb和opaque设置为合理的内容，用于将写缓冲区释放回用户自定义的内存池中
        int cnt;
        for(cnt = 0; i < iovcnt; ++cnt) {
            iov[i].iov_base = MempoolGet(poolId, iovlen);    // 用户自定义内存池的get函数
            assert(iov[i].iov_base != NULL);
            iov[i].iov_len = iovlen;
            iov[i].free_cb = FreeCb;     // 用户自定义释放回调
            iov[i].opaque = poolId;      // 释放回调所需的自定义参数
        }
        
        // 往写缓冲区中填充数据
        for (int i = 0; i < cnt; ++i) {
            fill_data(iov[i].iov_base, iov[i].iov_len);
        }
        
        // 调用零拷贝写接口发送数据
        ssize_t ret = knet_zwritev(sockfd, iov, cnt);
        if (ret <= 0) {
            // 写接口返回值小于等于零表示失败，进行错误处理，需手动调用iov的free_cb来保证写缓冲区的正常释放
            for(int i = 0; i< iovcnt; ++i) {
                if (iov[i].free_cb != NULL) {
                    iov[i].free_cb(iov[i].iov_base, iov[i].opaque);
                }
            }
        }
        
        // 发送成功后无需手动释放写缓冲区
        ```

2. 业务编译。
    - 添加编译选项：指定头文件搜索路径与链接的库名称，以iPerf3为例。

        ```bash
        // Makefile.am
        libiperf_la_LIBADD = -lknet_frame
        AM_CPPFLAGS = -I/usr/include/knet
        ```

    - 编译构建业务，以iPerf3为例。

        ```bash
        // iperf3目录下
        sh bootstrap.sh
        ./configure; make
        ```

3. 修改K-NET配置文件。
    - 以iPerf3为例，假设发送包长为65535，每次发送的iovcnt为1，进行单条tcp链接的打流。

        ```bash
        vi /etc/knet/knet_comm.conf
        ```

    - 将“zcopy\_enable”设置为1，使能零拷贝；开启tso、lro选项，保证def\_sendbuf的值大于单次发送的数据总长度，在本示例中大于65535 \* 1即可；确保“zcopy\_seg\_len”为写缓冲区iov的iov\_len的最大值，本示例中配置为发送包长的大小，即65535；按照推荐公式计算 “zcopy\_seg\_num”的值，本示例中低于最小值8192，故配置为8192，当发送包长很小时，此配置项需要比较大的配置。

        ```json
        {
          "version": "2.0.0",
          "common": {
            ...
            "zcopy_enable": 1     // 使能零拷贝
          },
          ...
          "hw_offload": {         // 开启 tso，lro，tcp_checksum
            "tso": 1,
            "lro": 1,
            "tcp_checksum": 1,
            ...
          },
          "proto_stack": {
            ...
            "def_sendbuf": 1048576,    // 必须大于单次发送的数据总长度，即 iovlen * iovcnt
            ...
            "zcopy_sge_len": 65535,    // 设置为发送包长的大小
            "zcopy_sge_num": 8192      // 根据公式计算推荐值，最小值为8192
          },
          "dpdk": {
            ...
            "socket_mem": "--socket-mem=1024",       // 确保内存足够
            "socket_limit": "--socket-limit=1024",
            ...
          }
        }
        ```

4. 启动业务。

    以iperf3为例。

    ```bash
    iperf3 -s -4 -p 10001 --bind 192.168.*.*
    ```

    启动业务后可进行业务操作。
