/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef __KNET_DTOE_API_H__
#define __KNET_DTOE_API_H__

#define _GNU_SOURCE

#include <stdint.h>
#include <sched.h>
#include <sys/epoll.h>

struct knet_thrd_cpu_cfg {
    uint32_t enable; // 是否开启亲和性设置：1-开启，0-关闭
    cpu_set_t cpu_mask_affinity; // CPU集合
};

/* conn_async_offload_done钩子函数入参 */
enum knet_async_rsp_type {
    KNET_ASYNC_OFFLOAD_SUCCESS = 0,
    KNET_ASYNC_OFFLOAD_FAIL,
};

struct knet_ulp_ops {
        void (*close_done)(int sockfd); // 约束：close_done回调中会执行close(fd)操作, 用户不能再执行close(fd)
        /* 通知ulp预断链已完成 */
        void (*prepare_close_done)(int sockfd);
        void (*conn_async_offload_done)(int sockfd, uint8_t rsp_status);
};

struct knet_mr {
    void *addr;             /* 用户态传入需要注册的虚拟地址 */
    size_t length;          /* 用户态传入注册内存对应的长度 */
    uint32_t lkey;          /* 对应芯片MPT表项的索引 */
};

enum knet_schd_type {
    KNET_EPOLL_SCHD = 0,
    KNET_POLL_SCHD,
    KNET_SWITCHABLE_SCHD,
    KNET_SCHD_MOD_BUTT
};

enum knet_recv_event_type {
    KNET_RX_EVENT_NORMAL,
    KNET_RX_EVENT_LEAK, // 有漏包数据，需调用漏包接收接口将漏包数据先接收完
};

struct knet_send_events {
    uint64_t wr_id;
    int sockfd;
};

struct knet_send_channel {
    int epoll_fd;   /* epoll 句柄, poll 模式无效 */
    void* tx_queue; /* tx channel 句柄, 后续 poll 使用 */
};

struct knet_recv_events {
    int sockfd;
    uint32_t iov_cnt; // 表示dtoe_recv上传的iov个数，建议dtoe_recv入参iov_cnt赋值到这里。 个数为0时表示连接已断开
    enum knet_recv_event_type type;
};

struct knet_recv_channel {
    int epoll_fd;   /* epoll 句柄, poll 模式无效 */
    void* rx_queue; /* rx channel 句柄, 后续 poll 使用 */
};

struct knet_offload_in {
    void *user_data;    /* 标识上层业务的句柄，多用于ulp回调 */
    struct knet_recv_channel *recv_channel;
    struct knet_send_channel *send_channel;
};

struct knet_iovec {
    void* iov_base;
    size_t iov_len;
};

typedef void (*knet_tx_req_free_cb_t)(int sockfd, uint64_t wr_id);

struct knet_tx_req {
    struct knet_iovec *iov;
    uint32_t iov_cnt;
    uint32_t lkey;
    uint64_t wr_id;
    knet_tx_req_free_cb_t free_cb; // 发送请求完成后执行的回调函数
};

/**
 * @brief K-NET向DTOE驱动注册ulp回调
 *
 * @param ops ulp回调操作集
 * @return None
 */
void knet_ulp_ops_register(struct knet_ulp_ops *ops);

/**
 * @brief 设置控制面线程绑核
 * @param cfg 绑核配置 
 * @return 0-success, others-failed
 */
int knet_thrd_cpu_set(struct knet_thrd_cpu_cfg *cfg);

/**
 * @brief K-NET初始化DTOE
 *
 * @param local_ip 绑定的业务ip
 * @return int 0 成功，-1 失败
 */
int knet_init(const char *local_ip);

/**
 * @brief K-NET DTOE解初始化
 */
void knet_uninit(void);

/**
* @param addr [IN] 目标buf基址，虚拟地址
* @param addr [IN] 目标buf长度
* @retval knet_mr *dmr: 注册成功后返回的MR信息, NULL 失败, 其他成功
*/
struct knet_mr *knet_reg_mr(void *addr, size_t length);

/**
* @param knet_mr *dmr [IN] 注册成功后返回的MR信息
* @retval 0-success, others-failed
*/
int knet_unreg_mr(struct knet_mr *dmr);

/**
 * @brief 创建tx channel
 * @param schd_mod [IN] poll模式
 * @param depth [IN] 队列深度
 * @param channel [OUT] tx channel句柄，具体详见结构体定义
 * @retval 0-success, others-failed
 */
int knet_create_send_channel(enum knet_schd_type schd_mod, uint32_t depth, struct knet_send_channel **channel);

/**
 * @brief 销毁tx channel
 * @param channel [IN] tx channel句柄，具体详见结构体定义
 * @retval 0-success, others-failed
 */
int knet_destroy_send_channel(struct knet_send_channel *channel);

/**
 * @brief 创建rx channel
 * @param schd_mod [IN] poll模式
 * @param depth [IN] 队列深度
 * @param channel [OUT] rx channel句柄，具体详见结构体定义
 * @retval 0-success, others-failed
 */
int knet_create_recv_channel(enum knet_schd_type schd_mod, uint32_t depth, struct knet_recv_channel **channel);

/**
 * @brief 销毁rx channel
 * @param channel [IN] rx channel句柄，具体详见结构体定义
 * @retval 0-success, others-failed
 */
int knet_destroy_recv_channel(struct knet_recv_channel *channel);

/**
 * @brief tcp链接卸载。同步接口，会阻塞等待卸载完成，接口返回时连接完成卸载
 * @param sockfd [IN] 标准socket的fd句柄
 * @param in [IN] 连接卸载需要的信息集合
 * @retval 0-success, others-failed
 */
int knet_start_chimney_general(int sockfd, struct knet_offload_in *in);

/**
 * @brief 将knet接收方向的BD buf物归原主
 * @param sockfd [IN] 标准socket的fd句柄
 * @param iov [IN] 待归还的iov列表
 * @param iov_cnt [IN] iov个数
 */
void knet_recv_mem_loopback(int sockfd, struct knet_iovec *iov, int iov_cnt);

/**
 * @brief 启动预断链
 * @param sockfd [IN] 标准socket的fd句柄
 */
void knet_prepare_close(int sockfd);

/**
 * @brief 连接上载
 * @param sockfd [IN] 标准socket的fd句柄
 * @attention 约束：close_done回调中会执行close(fd)操作, 用户不能再执行close(fd)
 */
void knet_close(int sockfd);

/**
 * @brief fd对应的tcp连接是否卸载
 * @param sockfd [IN] 标准socket的fd句柄
 * @retval 0-未卸载，1-卸载
 */
int knet_fd_is_offloaded(int sockfd);

/**
 * @brief 获取sockfd对应的user_data
 * @param sockfd [IN] 标准socket的fd句柄
 * @retval void* sockfd对应的user_data
 */
void *knet_get_ulp_user_data(int sockfd);

/**
 * @brief 检查 tx channel 完成事件，执行对应处理
 * @param send_channel [IN/OUT] knet 对外tx channel
 * @param maxevents [IN] 最大事件数
 * @return 完成事件数
 */
int knet_poll_send_channel(struct knet_send_channel* send_channel, uint32_t maxevents);

/**
 * @brief knet send
 * @param sockfd [IN] knet返回的sockfd
 * @param tx_req [IN] 构造卸载发包的tx_req请求
 * @return 成功返回发包字节数，失败返回负数
 */
int knet_send(int sockfd, struct knet_tx_req* tx_req);

/**
 * @brief knet接收接口
 * @param sockfd [IN] 标准socket的fd句柄
 * @param iov [IN] 接收buf信息
 * @param iov_cnt [IN] iov个数
 * @return 正数-成功接收的长度，负数-接收失败，0-对端已关闭
 * @attention 建议iov_cnt赋值为struct knet_recv_event的iovcnt
 */
int knet_recv(int sockfd, struct knet_iovec *iov, int iov_cnt);

/**
 * @brief 获取漏包数据大小
 * @param sockfd [IN] 标准socket的fd句柄
 * @return 非负数-可接收的长度，负数-获取失败
 */
int32_t knet_get_leaked_packet_size(int sockfd);

/**
 * @brief 接收漏包数据
 * @param sockfd [IN] 标准socket的fd句柄
 * @param buffer [IN] 接收缓冲区
 * @param recv_len [IN] 接收缓冲区长度
 * @return 非负数-成功接收的长度，负数-接收失败
 * @attention 必须一次接收完所有漏包数据，即recv_len必须大于等于knet_get_leaked_packet_size获取到的长度
 */
int32_t knet_recv_leaked_packet(int sockfd, void *buffer, int32_t recv_len);

/**
 * @brief 将tx/rx channel绑定到epoll实例，供knet_epoll_wait收发事件唤醒使用
 * @param epfd [IN] knet_epoll_create返回的epoll fd
 * @param send_channel [IN] tx channel句柄，支持EPOLL/POLL两种调度模式：EPOLL模式send完成事件经事件fd触发收割，
 *                           POLL模式由knet_epoll_wait每轮主动轮询收割
 * @param recv_channel [IN] rx channel句柄，支持EPOLL/POLL两种调度模式：EPOLL模式经事件fd唤醒阻塞等待，
 *                           POLL模式无唤醒源，knet_epoll_wait阻塞等待退化为毫秒级轮询（收包上报延迟和CPU开销略高）；
 *                           send/recv channel调度模式必须一致（驱动约束）
 * @retval 0-success, others-failed
 */
int knet_channel_bind_epoll(int epfd, struct knet_send_channel *send_channel, struct knet_recv_channel *recv_channel);

/**
 * @brief 创建knet epoll实例，用于管理DTOE卸载连接的事件
 * @retval 非负-epoll fd，负数-失败
 */
int knet_epoll_create(void);

/**
 * @brief 对卸载连接fd注册/修改/删除epoll事件监听，语义对齐系统epoll_ctl
 * @param epfd [IN] knet_epoll_create返回的epoll fd
 * @param op [IN] 操作类型，支持EPOLL_CTL_ADD/EPOLL_CTL_MOD/EPOLL_CTL_DEL
 * @param fd [IN] 已卸载的tcp连接fd，其channel须与epfd绑定的channel一致
 * @param event [IN] 监听事件，支持EPOLLIN/EPOLLOUT/EPOLLRDHUP/EPOLLERR/EPOLLHUP，EPOLL_CTL_DEL时可为NULL；
 *                   对端半关闭（RDHUP）事件仅在订阅了EPOLLRDHUP时上报，订阅EPOLLIN只上报可读事件
 * @retval 0-success, others-failed
 */
int knet_epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);

/**
 * @brief 等待就绪事件，须先调用knet_channel_bind_epoll绑定channel
 * @param epfd [IN] knet_epoll_create返回的epoll fd
 * @param events [OUT] 返回的就绪事件数组
 * @param maxevents [IN] events数组的最大长度
 * @param timeout [IN] 超时时间（毫秒），-1-无限等待，0-立即返回；recv channel为POLL调度模式时不支持-1（无唤醒源，返回失败）
 * @return 正数-就绪事件数，0-超时无事件，负数-失败
 */
int knet_epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);

/**
 * @brief 日志输出函数
 * @param function 调用处函数名
 * @param line 调用处行数
 * @param level 日志级别
 * @param format 日志信息
 */
void knet_log(const char *function, int line, int level, const char *format, ...);

int32_t knet_flexda_dtoe_channel_epoll_set(struct knet_send_channel *send_channel, struct knet_recv_channel *recv_channel, uint32_t epoll_enable);

int32_t knet_flexda_dtoe_channel_qpc_create(struct knet_send_channel *send_channel, struct knet_recv_channel *recv_channel);

int32_t knet_flexda_dtoe_channel_qpc_destroy(struct knet_send_channel *send_channel, struct knet_recv_channel *recv_channel);

/**
 * @brief 动态使能或去使能通道的接收能力
 * @param recv_channel [IN] rx channel句柄，具体详见结构体定义
 * @param rx_invalid_enable [IN] 是否关闭通道能力。1:关闭 0:打开
 * @retval 0-success, others-failed
 */
int32_t knet_flexda_dtoe_channel_qpc_rx_invalid_set(struct knet_recv_channel *recv_channel, uint32_t rx_invalid_enable);

/**
 * @brief 获取当前通道是否还有有效的事件
 * @param recv_channel [IN] rx channel句柄，具体详见结构体定义
 * @retval 1-没有事件，0-有事件，others-failed
 */
int knet_flexda_dtoe_receive_channel_is_empty(struct knet_recv_channel *recv_channel);

#endif
