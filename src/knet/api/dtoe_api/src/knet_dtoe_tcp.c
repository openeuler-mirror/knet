/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "securec.h"

#include "knet_log.h"
#include "knet_dtoe_fd.h"
#include "knet_dtoe_tcp.h"

#define KNET_INVALID_SN (0)
#define KNET_INITIAL_FREEREQ_SIZE 128

struct KnetTcp *KNET_GetFdConnUserData(int sockfd)
{
    return KnetGetTcpFd(sockfd);
}

void KNET_SetFdState(int sockfd, struct knet_offload_in *in, flexda_dtoe_offload_out_s *out)
{
    KNET_FdTypeSet(sockfd, KNET_FD_TYPE_TCP);

    struct KnetTcp *tcp =  KnetGetTcpFd(sockfd);
    tcp->sk.fd = sockfd;
    KNET_SockSetState(&tcp->sk, KNET_SOCK_STATE_WRITE); // 默认可写

    // 此处直接强转为struct KnetSendChannel *是因为knet_dtoe_create_send_channel返回的也是struct knet_send_channel地址，recv同理
    tcp->send_channel = (struct KnetSendChannel *)(in->send_channel);
    tcp->recv_channel = (struct KnetRecvChannel *)(in->recv_channel);
    tcp->recvEventIndex = KNET_INVALID_EVENT_INDEX;
    tcp->user_data = in->user_data;
    tcp->dtoe_conn = out->dtoe_conn;
    tcp->recv_sn = out->recv_sn;
    /* send_sn - 1是需要设置comp_sn和last_sn为上一次处理的值，要比send_sn小，所以-1，否则后续complete判断无法通过 */
    tcp->send.comp_sn = out->send_sn - 1;
    tcp->send.last_sn = out->send_sn - 1;
    tcp->olStatus = KNET_OFFLOAD_STARTED;
}

int KNET_InitFreeReq(int sockfd)
{
    struct KnetTcp *tcp =  KnetGetTcpFd(sockfd);

    TAILQ_INIT(&tcp->send.free_req);
    TAILQ_INIT(&tcp->send.unack_req);

    // 初始化锁
    memset_s(&tcp->send_lock, sizeof(KNET_SpinLock), 0, sizeof(KNET_SpinLock));
    for (int i = 0; i < KNET_INITIAL_FREEREQ_SIZE; i++) {
        KnetReqNode* node = (KnetReqNode*) calloc(1, sizeof(KnetReqNode));
        if (node == NULL) {
            KNET_ERR("Failed to allocate KnetReqNode %d", i);
            // 释放已分配的节点
            while (!TAILQ_EMPTY(&tcp->send.free_req)) {
                KnetReqNode* n = TAILQ_FIRST(&tcp->send.free_req);
                TAILQ_REMOVE(&tcp->send.free_req, n, node);
                free(n);
            }
            return -1;
        }
        node->wr_id = 0;
        node->send_sn = 0;

        // 添加到空闲队列
        TAILQ_INSERT_TAIL(&tcp->send.free_req, node, node);
    }
    KNET_DEBUG("InitFreeReq success, sockfd %d, %d free request nodes", sockfd, KNET_INITIAL_FREEREQ_SIZE);
    return 0;
}

void KNET_UninitFreeReq(int sockfd)
{
    struct KnetTcp *tcp =  KnetGetTcpFd(sockfd);

    KNET_SpinlockLock(&tcp->send_lock);
    // 释放freereq已分配的节点
    while (!TAILQ_EMPTY(&tcp->send.free_req)) {
        KnetReqNode* n = TAILQ_FIRST(&tcp->send.free_req);
        TAILQ_REMOVE(&tcp->send.free_req, n, node);
        free(n);
    }

    // 释放unackreq已分配的节点，防止未收到ack直接关闭泄露
    while (!TAILQ_EMPTY(&tcp->send.unack_req)) {
        KnetReqNode* n = TAILQ_FIRST(&tcp->send.unack_req);
        TAILQ_REMOVE(&tcp->send.unack_req, n, node);
        if (n->freeCb != NULL) {
            n->freeCb(n->sockfd, n->wr_id);
        }
        free(n);
    }
    KNET_SpinlockUnlock(&tcp->send_lock);
    KNET_DEBUG("UninitFreeReq done, sockfd %d", sockfd);
}

void *KNET_GetConnBySock(int sockfd)
{
    struct KnetTcp *tcp =  KnetGetTcpFd(sockfd);
    return tcp->dtoe_conn;
}

int KNET_SockLeakResInit(struct KnetTcp *sock)
{
    int32_t leakSize = flexda_dtoe_get_leaked_packet_size(sock->dtoe_conn);
    if (leakSize > 0) {
        sock->leakBuf = malloc(leakSize);
        if (sock->leakBuf == NULL) {
            KNET_ERR("Leak buf malloc failed, sockfd %d, leakSize %d", sock->sk.fd, leakSize);
            return -1;
        }
        sock->leakSize = flexda_dtoe_recv_leaked_packet(sock->dtoe_conn, sock->leakBuf, leakSize);
        if (sock->leakSize < 0) {
            KNET_ERR("Recv leaked packet failed, sockfd %d, ret %d, leakSize %d", sock->sk.fd, sock->leakSize,
                     leakSize);
            goto recvLeakedPktFailed;
        }

        KNET_SpinlockLock(&sock->recv_channel->leakLock); // 摘除侧持锁，插入侧必须同锁，防止并发撕裂链表
        TAILQ_INSERT_TAIL(&sock->recv_channel->leakList, sock, leakNode);
        KNET_SpinlockUnlock(&sock->recv_channel->leakLock);
        KNET_SockSetState(&sock->sk, KNET_SOCK_STATE_READ);
        KNET_DEBUG("Leak resource init, sockfd %d, leakSize %d", sock->sk.fd, sock->leakSize);
    }

    return 0;

recvLeakedPktFailed:
    free(sock->leakBuf);
    sock->leakBuf = NULL;
    sock->leakSize = 0;
    return -1;
}

void KNET_SockLeakedResUninit(struct KnetTcp *sock)
{
    // 仅回收框架持有(未移交)的漏包缓冲；已移交(leakSize==0)的缓冲由用户经knet_recv_mem_loopback
    // 归还时释放(见KNET_SockLeakResRelease)，此处释放会造成用户侧悬挂指针
    if (sock->leakBuf == NULL || sock->leakSize <= 0) {
        return;
    }

    KNET_SpinlockLock(&sock->recv_channel->leakLock);
    TAILQ_REMOVE(&sock->recv_channel->leakList, sock, leakNode);
    KNET_SpinlockUnlock(&sock->recv_channel->leakLock);

    free(sock->leakBuf);
    sock->leakBuf = NULL;
    sock->leakSize = 0;
}

// 用户经knet_recv_mem_loopback归还已移交的漏包缓冲：移交后节点仍挂在leakList上，摘链并释放
void KNET_SockLeakResRelease(struct KnetTcp *sock)
{
    if (unlikely(sock->leakBuf == NULL)) {
        return;
    }

    KNET_SpinlockLock(&sock->recv_channel->leakLock);
    TAILQ_REMOVE(&sock->recv_channel->leakList, sock, leakNode);
    KNET_SpinlockUnlock(&sock->recv_channel->leakLock);

    free(sock->leakBuf);
    sock->leakBuf = NULL;
    sock->leakSize = 0;
}

/**
 * @note 不可将user_data置NULL
 * 原因: 目前Reset操作在用户close_done回调之前
 *       如果这里置NULL，会导致用户无法使用knet_get_ulp_user_data获取sockfd对应的user_data指针
 *       所以这里不置NULL，等到用户下一次相同的sockfd号进行tcp卸载时，再覆盖原先的值 
 * 其他方案：如果将KNET_ResetFdState放置于用户close_done之后，会导致用户close(fd)之后，多线程场景下其他线程申请相同fd tcp卸载，
 *          会导致KNET_ResetFdState覆盖了KNET_SetFdState的值。
 *          可行的方案是劫持close函数或封装close函数，在用户close之前执行user_data置空
 */
void KNET_ResetFdState(int sockfd)
{
    struct KnetTcp *tcp =  KnetGetTcpFd(sockfd);
    KNET_SockLeakedResUninit(tcp);
    tcp->dtoe_conn = NULL;
    tcp->recv_sn = KNET_INVALID_SN;
    tcp->send_channel = NULL;
    tcp->recv_channel = NULL;
    tcp->recvEventIndex = KNET_INVALID_EVENT_INDEX;
    tcp->send.comp_sn = KNET_INVALID_SN;
    tcp->send.last_sn = KNET_INVALID_SN;
    tcp->olStatus = KNET_OFFLOAD_INVALID;

    tcp->sk.fd = KNET_INVALID_FD;
    tcp->sk.associateFd = KNET_INVALID_FD;
    tcp->sk.iovCnt = 0;
    tcp->sk.state = 0;
    // 安全回收仍挂在epoll实例链表上的item（被动关闭等未经knet_prepare_close的路径），
    // 替代原裸置notifyCtx=NULL，避免item悬挂在readyList/idleList且无人释放
    KNET_EpollRemoveItem(&tcp->sk);

    KNET_FdTypeSet(sockfd, KNET_FD_TYPE_INVALID);
}