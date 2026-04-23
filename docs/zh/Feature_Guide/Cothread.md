# 共线程功能

>**说明：** 
>共线程功能指K-NET用户态协议栈与业务在同一个线程中运行，并在此业务线程中进行数据包的收发、事件处理和数据读写。
>具有如下约束：
>
>- 业务需适配非阻塞socket接口，带有超时时间的接口（例如epoll\_wait）必须使接口立即返回，如果违反约束可能导致线程阻塞。
>- 业务编译的时候，建议优先链接-lknet\_frame，再链接其他库，如-lpthread、-lc。
>- 业务线程的绑核由业务控制，建议K-NET worker业务绑核要在调用knet\_worker\_init\(\)之前执行，且不与“ctrl\_vcpu\_ids”共核；共线程场景下，“core\_list\_global”配置不生效。
>- K-NET worker业务线程之间不得共享与跨线程操作socket fd、epoll fd。
>- 主动建链时，非K-NET的进程不得使用分配给K-NET使用的随机端口，即与K-NET端口区间不要交叉，配置方式见步骤4。
>- K-NET worker业务线程个数与配置项中的“max\_worker\_num”一致，超过“max\_worker\_num”部分线程执行knet\_worker\_init\(\)会失败。
>- 共线程场景下，如需使用非K-NET worker线程或进程，需要开启"bifur\_enable"并设置为2使能内核流量转发，约束用户必须创建并初始化所有K-NET worker线程，并保证常驻运行，否则可能导致非K-NET worker线程或进程无法成功建链、打流。
>- 共线程场景下，开启流分叉"bifur\_enable"设置为1，或者max\_worker\_num”大于1时，启动业务时会下流表，此时bind\(\)，需要保证输入ip非0，为业务ip。

1. 业务适配<term>K-NET</term>共线程模式。
    - 以下为业务服务端使用共线程模式时的伪代码：

        ```c
        #include "knet_socket_api.h"
        
        int ret = knet_init();
        
        for (i = 0; i < thread_num; i++) {
            # 设置cpu亲和性，须与knet_comm.conf中"ctrl_vcpu_ids"不同
            pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);
                
            # 创建业务线程
            pthread_create(&tid, &attr, server_loop, arg);
        }
        # 业务线程运行函数
        void *server_loop(void *arg) {
            ret = knet_worker_init(); // 初始化此线程的K-NET worker
            ret = knet_is_worker_thread(); // 判断此线程是否在K-NET中，ret为0表示在K-NET用户态协议栈线程中；
            //为-1则不在K-NET用户态协议栈中，此线程创建的socket fd均为内核协议栈fd，开启"kernel_fdir"后，支持进行内核流量转发
            
            listen_fd = socket(AF_INET, SOCK_STREAM, 0);
            // 将listen_fd设置为非阻塞
            int flags = fcntl(listen_fd, F_GETFL, 0);
            fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK);
        
            bind(listen_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr);
            listen(listen_fd, 20);
            epfd = epoll_create1(0);
            struct epoll_event ev, events[MAX_EVENTS];
            ev.events = EPOLLIN;
            ev.data.fd = listen_fd;
            epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);
            while (1) {    
                knet_worker_run();
                int nfds = epoll_wait(epfd, events, MAX_EVENTS, 0);
                for (int i = 0; i < nfds; ++i) {
                    int fd = events[i].data.fd;
         
                    if (fd == listen_fd) {
                        conn_fd = accept(listen_fd, (struct sockaddr*)&cli_addr, &cli_len);
                        // 将conn_fd设置为非阻塞
                        flags = fcntl(conn_fd, F_GETFL, 0);
                        fcntl(conn_fd, F_SETFL, flags | O_NONBLOCK);
        
                        ev.events = EPOLLIN | EPOLLOUT;
                        ev.data.fd = conn_fd;
                        epoll_ctl(epfd, EPOLL_CTL_ADD, conn_fd, &ev);
                     } else {
                         // 处理 EPOLLIN 事件 - 客户端发送数据
                         if (events[i].events & EPOLLIN) {
                             int n = read(fd, buffer, sizeof(buffer));                    
                          }
          
                        // 处理 EPOLLOUT 事件 - 客户端可以接受数据
                        if (events[i].events & EPOLLOUT) {
                            const char *reply = "ok";
                            int sent = send(fd, reply, strlen(reply), 0);
                        }
                    }
                }
            }
        
            close(listen_fd);
            close(epfd);
            knet_worker_run(); // 再次进行发包，通知对端断链
        }
        ```

    - 业务客户端使用共线程模式的伪代码：

        ```C
        #include "knet_socket_api.h"
        
        int ret = knet_init();
        
        for (i = 0; i < thread_num; i++) {
            # 设置cpu亲和性，须与knet_comm.conf中"ctrl_vcpu_ids"不同
            pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);
                
            # 创建业务线程
            pthread_create(&tid, &attr, client_loop, arg);
        }
        # 业务线程运行函数
        void *client_loop(void *arg) {
            ret = knet_worker_init(); // 初始化此线程的K-NET worker
            ret = knet_is_worker_thread(); 
            
            sockfd = socket(AF_INET, SOCK_STREAM, 0);
            // 将sockfd 设置为非阻塞
            int flags = fcntl(sockfd , F_GETFL, 0);
            fcntl(sockfd , F_SETFL, flags | O_NONBLOCK);
        
            connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
        
            epfd = epoll_create1(0);
            struct epoll_event ev, events[MAX_EVENTS];
            ev.events = EPOLLIN | EPOLLOUT;
            ev.data.fd = sockfd;
            epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);
            while (1) {    
                knet_worker_run();
                int nfds = epoll_wait(epfd, events, MAX_EVENTS, 0);
                for (int i = 0; i < nfds; ++i) {
                    int fd = events[i].data.fd;
        
                     // 处理 EPOLLIN 事件 - 客户端发送数据
                     if (events[i].events & EPOLLIN) {
                         int n = read(fd, buffer, sizeof(buffer));
                      }
          
                    // 处理 EPOLLOUT 事件 - 客户端可以接受数据
                    if (events[i].events & EPOLLOUT) {
                        const char *reply = "ok";
                        int sent = send(fd, reply, strlen(reply), 0);
                    }
                }
            }
        
            close(sockfd);
            close(epfd);
            knet_worker_run(); // 再次进行发包，通知对端断链
        }
        ```

    - 总结

        上述伪代码的核心在于：引入knet\_socket\_api.h头文件，在业务进程初始化中进行knet\_init\(\)，设置线程CPU亲和性，注意需与"ctrl\_vcpu\_ids"不同；在业务线程运行时，首先进行knet\_worker\_init\(\)初始化worker，并可以通过knet\_is\_worker\_thread\(\)判断当前线程是否在用户态协议栈线程中；其次需要保证创建的socket fd均为非阻塞状态；再次需要保证knet\_worker\_run\(\)一直被调用。

2. 业务编译。
    - 添加编译选项：指定头文件搜索路径与链接的库名称：

        ```bash
        -I/usr/include/knet
        -lknet_frame
        ```

    - 编译构建业务、验证可执行文件依赖的共享库列表包含libknet\_frame.so，以test为例：

        ```bash
        ldd test
        ```

        ```ColdFusion
        linux-vdso.so.1 (0x0000ffff3ae80000)
        libknet_frame.so.2 => /lib64/libknet_frame.so.2 (0x0000ffff3ae10000)
        libpthread.so.0 => /lib64/libpthread.so.0 (0x0000ffff3add0000)
        libc.so.6 => /lib64/libc.so.6 (0x0000ffff3ac50000)
        libknet_core.so.2 => /lib64/libknet_core.so.2 (0x0000ffff3a920000)
        ```

3. 修改K-NET配置文件。

    ```bash
    vi /etc/knet/knet_comm.conf
    ```

    ```text
    #common配置项
        "common": {
            "cothread_enable": 1
        }
        "proto_stack": {
            "min_port": 49152,
            "max_port": 65535,
            ...
        }
    ```

    开启共线程模式，确定端口范围。

4. 保证内核协议栈与K-NET端口范围不交叉。<a id="step4"></a>
    - 查看内核协议栈使用的端口范围。

        ```bash
        cat /proc/sys/net/ipv4/ip_local_port_range
        ```

        回显示例如下：

        ```ColdFusion
        55535 65535
        ```

    - 内核协议栈与K-NET端口范围交叉，需修改内核协议栈或者K-NET端口范围
        - 方案1：修改内核协议栈端口范围，使其不与K-NET端口范围冲突。

            ```bash
            echo "1024 36180" > /proc/sys/net/ipv4/ip_local_port_range
            ```

        - 方案2：修改K-NET配置文件中"min\_port"与"max\_port"配置项，参考步骤3，使其不与内核协议栈端口范围冲突。

5. 启动业务。

    以Tperf为例，参考[特性支持](./Feature_Overview.md#特性支持)中的Tperf patch链接使用。
