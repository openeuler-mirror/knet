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

#ifndef __KNET_SOCKET_API_H__
#define __KNET_SOCKET_API_H__

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 零拷贝对外接口 */

typedef void (*knet_iov_free_cb_t)(void *addr, void *opaque);

struct knet_iovec {
    void *iov_base;
    size_t iov_len;
    void *opaque;
    knet_iov_free_cb_t free_cb;
};

/**
 * @brief 写缓冲区申请，底层实现为分配定长内存单元
 *
 * @param iov 写缓冲区iov
 * @param size 写缓冲区大小，大小不得超过定长内存单元的长度，其长度由配置项 zcopy_sge_len 决定
 * @return int 0 成功，-1 失败
 */
void *knet_mp_alloc(size_t size);

/**
 * @brief 写缓冲区释放
 *
 * @param iov 写缓冲区iov
 * @param opaque 未使用参数，使释放函数的函数签名符合 knet_iov_free_cb_t
 */
void knet_mp_free(void *addr, void *opaque);

/**
 * @brief 零拷贝写，入参 iov 中的 free_cb 字段不得为空
 *
 * @param sockfd 套接字文件描述符
 * @param iov 写缓冲区iov数组
 * @param iovcnt 写缓冲区iov数量
 * @return ssize_t 成功返回写入的字节数，小于等于0表示失败
 */
ssize_t knet_zwritev(int sockfd, const struct knet_iovec *iov, int iovcnt);

/**
 * @brief 零拷贝读
 *
 * @param sockfd 套接字文件描述符
 * @param iov 读缓冲区iov数组
 * @param iovcnt 读缓冲区iov数量
 * @return ssize_t 成功返回读取的字节数，小于等于0表示失败
 */
ssize_t knet_zreadv(int sockfd, struct knet_iovec *iov, int iovcnt);


/* 共线程对外接口 */

/**
 * @brief 共线程初始化
 *
 * @return int 0: 成功; -1: 失败
 */
int knet_init(void);

/**
 * @brief 共线程worker初始化
 *
 * @return int 0: 成功; -1: 失败
 */
int knet_worker_init(void);

/**
 * @brief 共线程worker运行
 */
void knet_worker_run(void);

/**
 * @brief 共线程模式下，确定当前线程是否为worker线程
 *
 * @return 0：是worker线程；-1：不是worker线程
 */
int knet_is_worker_thread(void);

#ifdef __cplusplus
}
#endif

#endif // __KNET_SOCKET_API_H__