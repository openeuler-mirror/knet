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

#include <stdint.h>

/* conn_async_offload_done钩子函数入参 */
enum knet_async_rsp_type {
    KNET_ASYNC_OFFLOAD_SUCCESS = 0,
    KNET_ASYNC_OFFLOAD_FAIL,
};

struct knet_ulp_ops {
        void (*close_done)(int sockfd);
        /* 通知ulp预断链已完成 */
        void (*prepare_close_done)(int sockfd);
        void (*conn_async_offload_done)(int sockfd, uint8_t rsp_status);
};

struct knet_mr {
    void *addr;             /* 用户态传入需要注册的虚拟地址 */
    size_t length;          /* 用户态传入注册内存对应的长度 */
    uint32_t lkey;          /* 对应芯片MPT表项的索引 */
    void *kernel_mr_addr;   /* 内核态MR结构地址，解注册时使用 */
    uint32_t rsv[4];
};

enum knet_schd_type {
    KNET_EPOLL_SCHD = 0,
    KNET_POLL_SCHD,
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
    uint32_t desc_cnt; // 表示dtoe_recv上传的desc个数，建议dtoe_recv入参desc_num赋值到这里。 个数为0时表示连接已断开
    enum knet_recv_event_type type;
};

struct knet_recv_channel {
    int epoll_fd;   /* epoll 句柄, poll 模式无效 */
    void* rx_queue; /* rx channel 句柄, 后续 poll 使用 */
    uint32_t oqid;  /*  VBS场景,用于 IO abort保序 */
    uint32_t rsv[4];
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

struct knet_tx_desc {
    struct knet_iovec iov;
    uint32_t lkey;
};

struct knet_tx_req {
    struct knet_tx_desc* descs;
    uint16_t descs_num;
    uint64_t wr_id;
};

enum knet_recv_type {
    KNET_RX_NORM_TYPE = 0,
    KNET_RX_IO_FINISH,
    KNET_RX_IO_ABORT,
    KNET_RX_IO_EXCEPTION,
    KNET_RX_TYPE_MAX,
};

struct knet_rx_desc {
    enum knet_recv_type type;
    struct knet_iovec iov;
};

/**
 * @brief K-NET向DTOE驱动注册ulp回调
 *
 * @param ops ulp回调操作集
 * @return None
 */
void knet_ulp_ops_register(struct knet_ulp_ops *ops);

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
void knet_recv_mem_loopback(struct knet_iovec *iov, int iov_cnt);

/**
 * @brief 启动预断链
 * @param sockfd [IN] 标准socket的fd句柄
 */
void knet_prepare_close(int sockfd);

/**
 * @brief 连接上载
 * @param sockfd [IN] 标准socket的fd句柄
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
 * @param events [IN/OUT] 关注事件
 * @param maxevents [IN] 最大事件数
 * @return 完成事件数
 */
int knet_poll_send_channel(struct knet_send_channel* send_channel, struct knet_send_events* events, uint32_t maxevents);

/**
 * @brief knet send
 * @param sockfd [IN] knet返回的sockfd
 * @param tx_req [IN] 构造卸载发包的tx_req请求
 * @return 成功返回发包字节数，失败返回负数
 */
int knet_send(int sockfd, struct knet_tx_req* tx_req);

/**
 * @brief 检查 rx channel 完成事件，执行对应处理
 * @param receive_channel [IN/OUT] knet 对外rx channel
 * @param events [IN/OUT] 关注事件
 * @param maxevents [IN] 最大事件数
 * @return 完成事件数
 */
int knet_poll_recv_channel(struct knet_recv_channel* receive_channel, struct knet_recv_events* events, uint32_t maxevents);

/**
 * @brief knet接收接口
 * @param sockfd [IN] 标准socket的fd句柄
 * @param desc [IN] 接收buf信息
 * @param desc_num [IN] desc个数
 * @param flags [IN] 预留（与标准socket保持一致）。暂不支持，填全0
 * @return 非负数-成功接收的长度，负数-接收失败
 * @attention 建议desc_num赋值为struct knet_recv_event的iovcnt
 */
int knet_recv(int sockfd, struct knet_rx_desc *desc, int desc_num, int flags);

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
 * @brief 日志输出函数
 * @param function 调用处函数名
 * @param line 调用处行数
 * @param level 日志级别
 * @param format 日志信息
 */
void knet_log(const char *function, int line, int level, const char *format, ...);

#endif