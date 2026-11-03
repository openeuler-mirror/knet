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

#include <stdarg.h>
#include <unistd.h>
#include <sys/queue.h>

#include "flexda_dtoe_interface.h"
#include "knet_log.h"
#include "knet_lock.h"
#include "knet_dtoe_fd.h"
#include "knet_dtoe_tcp.h"
#include "knet_dtoe_events.h"
#include "knet_dtoe_config.h"
#include "knet_dtoe_epoll.h"
#include "knet_dtoe_api.h"

_Static_assert(sizeof(struct knet_mr) == sizeof(flexda_dtoe_mr_s), \
    "struct knet_mr must same as dtoe");
_Static_assert(offsetof(struct knet_mr, addr) == offsetof(flexda_dtoe_mr_s, addr), \
    "struct knet_mr member offset must same as dtoe");
_Static_assert(offsetof(struct knet_mr, length) == offsetof(flexda_dtoe_mr_s, length), \
    "struct knet_mr member offset must same as dtoe");
_Static_assert(offsetof(struct knet_mr, lkey) == offsetof(flexda_dtoe_mr_s, lkey), \
    "struct knet_mr member offset must same as dtoe");

_Static_assert(sizeof(struct knet_send_channel) == sizeof(flexda_send_channel_s), \
    "struct knet_recv_channel must same as dtoe");
_Static_assert(offsetof(struct knet_send_channel, epoll_fd) == offsetof(flexda_send_channel_s, epoll_fd), \
    "struct knet_recv_channel member offset must same as dtoe");
_Static_assert(offsetof(struct knet_send_channel, tx_queue) == offsetof(flexda_send_channel_s, tx_queue), \
    "struct knet_recv_channel member offset must same as dtoe");

_Static_assert(sizeof(struct knet_recv_channel) == sizeof(flexda_recv_channel_s), \
    "struct knet_recv_channel must same as dtoe");
_Static_assert(offsetof(struct knet_recv_channel, epoll_fd) == offsetof(flexda_recv_channel_s, epoll_fd), \
    "struct knet_recv_channel member offset must same as dtoe");
_Static_assert(offsetof(struct knet_recv_channel, rx_queue) == offsetof(flexda_recv_channel_s, rx_queue), \
    "struct knet_recv_channel member offset must same as dtoe");

#define IP_STR_MAX_LEN 64
#define KNET_DTOE_MAX_CHANL_CONFIG 16

struct KnetDtoeDev {
    uint64_t devSn;
    uint32_t numaId;
    char ip[IP_STR_MAX_LEN];
};

struct KnetDtoeRes {
    struct KnetDtoeDev dev;
};

struct KnetDtoeRes g_dtoeRes = {0};
static struct knet_ulp_ops g_knetDtoeOps = {0};
static flexda_dtoe_ulp_ops_s g_dtoeOps = {
    .send_complete = NULL,
    .recv_notify = NULL,
    .close_done = NULL,
    .prepare_close_done = NULL,
    .conn_async_offload_done = NULL,
};

static int getFdFromDtoeConn(void *dtoe_conn)
{
    struct KnetTcp *knetUserData = flexda_dtoe_get_ulp_user_data(dtoe_conn);
    if (knetUserData == NULL) {
        KNET_ERR("Get ulp user data from dtoe failed, dtoe_conn %p", dtoe_conn);
        return KNET_INVALID_FD;
    }

    int fd = knetUserData->sk.fd;
    if (!KNET_IsOsFdTcp(fd)) {
        KNET_ERR("Sockfd %d is invalid", fd);
        return KNET_INVALID_FD;
    }
    return fd;
}

static void KnetRegCloseDone(void *dtoe_conn)
{
    if (g_knetDtoeOps.close_done == NULL) {
        return;
    }

    if (dtoe_conn == NULL) {
        KNET_ERR("K-NET dtoe_conn is null in close_done");
        return;
    }

    int fd = getFdFromDtoeConn(dtoe_conn);
    if (fd == KNET_INVALID_FD) {
        KNET_ERR("K-NET close_done fd is invalid");
        return;
    }

    KNET_ResetFdState(fd);
    // 置0后释放req队列
    KNET_UninitFreeReq(fd);

    KNET_DEBUG("Close down fd %d", fd);
    g_knetDtoeOps.close_done(fd);
}

static void KnetRegPrepareCloseDown(void *dtoe_conn)
{
    if (g_knetDtoeOps.prepare_close_done == NULL) {
        return;
    }

    if (dtoe_conn == NULL) {
        KNET_ERR("K-NET dtoe_conn is null in prepare_close_done");
        return;
    }

    int fd = getFdFromDtoeConn(dtoe_conn);
    if (fd == KNET_INVALID_FD) {
        KNET_ERR("K-NET prepare_close_done fd is invalid");
        return;
    }
    KNET_DEBUG("Prepare close down fd %d", fd);
    
    KNET_GetFdConnUserData(fd)->olStatus = KNET_OFFLOAD_PREPARE_CLOSED_DONE;
    g_knetDtoeOps.prepare_close_done(fd);
}

static void KnetRegConnAsyncOffloadDone(void *dtoe_conn, uint8_t rsp_status)
{
    if (g_knetDtoeOps.conn_async_offload_done == NULL) {
        return;
    }

    if (unlikely(dtoe_conn == NULL)) {
        KNET_ERR("K-NET dtoe_conn is null in conn_async_offload_done");
        return;
    }

    int fd = getFdFromDtoeConn(dtoe_conn);
    if (unlikely(fd == KNET_INVALID_FD)) {
        KNET_ERR("K-NET conn_async_offload_done fd is invalid");
        return;
    }

    if (unlikely(rsp_status == KNET_ASYNC_OFFLOAD_FAIL)) {
        KNET_ERR("K-NET sockfd %d async offload failed", fd);
        goto offloadFailed;
    }

    KNET_INFO("K-NET conn async offload done fd %d", fd);

    int ret = KNET_SockLeakResInit(KNET_GetFdConnUserData(fd));
    if (ret != 0) {
        KNET_ERR("K-NET sockfd %d leak res init failed, ret %d", fd, ret);
        goto leakResInitFailed;
    }

    KNET_GetFdConnUserData(fd)->olStatus = KNET_OFFLOAD_ASYNC_DONE;
    g_knetDtoeOps.conn_async_offload_done(fd, rsp_status);
    return;

leakResInitFailed:
offloadFailed:
    g_knetDtoeOps.conn_async_offload_done(fd, KNET_ASYNC_OFFLOAD_FAIL);
}

int knet_thrd_cpu_set(struct knet_thrd_cpu_cfg *cfg)
{
    flexda_dtoe_thrd_cpu_cfg_s dtoeCfg = {0};
    dtoeCfg.cpu_affinity_en = cfg->enable;
    dtoeCfg.cpu_mask_affinity = cfg->cpu_mask_affinity;
    int ret = flexda_dtoe_thrd_cpu_set(&dtoeCfg);
    if (ret != 0) {
        KNET_ERR("Dtoe thrd cpu set failed, ret %d, enable %u", ret, dtoeCfg.cpu_affinity_en);
        return ret;
    }

    return ret;
}

KNET_API int knet_init(const char * local_ip)
{
    KNET_LogInit();

    int ret = KNET_InitCfg();
    if (ret != 0) {
        KNET_ERR("K-NET init cfg failed, ret %d", ret);
        goto init_cfg_failed;
    }

    KNET_LogLevelSetByStr(KNET_GetCfg(CONF_DTOE_LOG_LEVEL)->strValue);

    ret = KNET_FdInit();
    if (ret != 0) {
        KNET_ERR("K-NET fd init failed, ret %d", ret);
        goto fd_init_failed;
    }

    ret = flexda_dtoe_ulp_config_set(KNET_GetCfg(CONF_DTOE_CHANNEL_NUM)->intValue);
    if (ret != 0) {
        KNET_ERR("Dtoe init failed, ret %d", ret);
        goto free;
    }
    KNET_INFO("Dtoe ulp config set success");

    ret = flexda_dtoe_init();
    if (ret != 0) {
        KNET_ERR("Dtoe init failed, ret %d", ret);
        goto dtoe_free; // flexda_dtoe_init已部分执行，需经uninit清理半初始化的驱动状态
    }
    KNET_INFO("Dtoe init success");

#define BIND_INTERVAL 1
#define BIND_TIMES 10
    uint8_t times = 0;
    do {
        ret = flexda_dtoe_bind_addr(local_ip, &g_dtoeRes.dev.devSn, &g_dtoeRes.dev.numaId);
        if (ret != 0) {
            KNET_INFO("Dtoe bind addr failed, local_ip %s, ret %d, times %u, retrying",
                      (local_ip == NULL) ? "null" : local_ip, ret, times);
            sleep(BIND_INTERVAL); // 仅失败重试前等待，成功路径不等待
        }
    } while (++times <= BIND_TIMES && ret != 0);
    if (ret != 0) {
        KNET_ERR("Dtoe bind addr failed, local_ip %s, ret %d", (local_ip == NULL) ? "null" : local_ip, ret);
        goto dtoe_free;
    }

    ret = strncpy_s(g_dtoeRes.dev.ip, IP_STR_MAX_LEN, local_ip, IP_STR_MAX_LEN - 1);
    if (ret != 0) {
        KNET_ERR("Strncpy dev ip failed, local_ip %s, ret %d", local_ip, ret);
        goto dtoe_free;
    }

    KNET_INFO("K-NET init success, ip %s, devSn %lu, numaId %u", g_dtoeRes.dev.ip, g_dtoeRes.dev.devSn,
              g_dtoeRes.dev.numaId);
    return ret;

dtoe_free:
    flexda_dtoe_uninit();
free:
    KNET_FdDeinit();
fd_init_failed:
init_cfg_failed:
    KNET_LogUninit();
    return ret;
}

KNET_API void knet_uninit(void)
{
    flexda_dtoe_uninit();
    KNET_FdDeinit();
    KNET_LogUninit();
}

struct knet_mr *knet_reg_mr(void *addr, size_t length)
{
    struct knet_mr *dmr = (struct knet_mr *)malloc(sizeof(struct knet_mr));
    if (dmr == NULL) {
        KNET_ERR("K-NET malloc mr failed, size %zu", sizeof(struct knet_mr));
        return NULL;
    }

    int ret = flexda_dtoe_reg_mr(g_dtoeRes.dev.devSn, addr, length, (flexda_dtoe_mr_s *)dmr);
    if (ret != 0) {
        KNET_ERR("K-NET reg mr failed, addr %p, devSn %lu, length %zu, ret %d", addr, g_dtoeRes.dev.devSn, length, ret);
        free(dmr);
        return NULL;
    }
    KNET_INFO("K-NET reg mr success, addr %p, devSn %lu, length %zu, ret %d", addr, g_dtoeRes.dev.devSn, length, ret);
    return dmr;
}

int knet_unreg_mr(struct knet_mr *dmr)
{
    int ret = flexda_dtoe_unreg_mr(g_dtoeRes.dev.devSn, (flexda_dtoe_mr_s *)dmr);
    if (ret != 0) {
        KNET_ERR("K-NET unreg mr failed, devSn %lu, ret %d", g_dtoeRes.dev.devSn, ret);
        return ret;
    }
    free(dmr);
    KNET_INFO("K-NET unreg mr success, devSn %lu, ret %d", g_dtoeRes.dev.devSn, ret);
    return ret;
}

int knet_create_send_channel(enum knet_schd_type schd_mod, uint32_t depth, struct knet_send_channel **channel)
{
    (void)depth;

    if (channel == NULL) {
        KNET_ERR("Channel is null in knet_create_send_channel");
        return -1;
    }
    struct KnetSendChannel *knetSendChannel =
        (struct KnetSendChannel *)malloc(sizeof(struct KnetSendChannel));
    if (knetSendChannel == NULL) {
        KNET_ERR("K-NET malloc send channel failed");
        return -1;
    }
    int ret = flexda_dtoe_create_send_channel(g_dtoeRes.dev.devSn, schd_mod, (flexda_send_channel_s *)(&knetSendChannel->channel));
    if (ret != 0) {
        free(knetSendChannel);
        KNET_ERR("K-NET create send channel failed, devSn %lu, schd_mod %d, ret %d", g_dtoeRes.dev.devSn, schd_mod, ret);
        return ret;
    }
    knetSendChannel->schdMode = schd_mod;
    *channel = &knetSendChannel->channel;
    KNET_INFO("K-NET create send channel success, devSn %lu, schd_mod %d, epoll_fd %d", g_dtoeRes.dev.devSn, schd_mod,
              knetSendChannel->channel.epoll_fd);
    return ret;
}

int knet_destroy_send_channel(struct knet_send_channel *channel)
{
    int ret = flexda_dtoe_destroy_send_channel((flexda_send_channel_s *)channel);
    if (ret != 0) {
        KNET_ERR("Dtoe destroy send channel failed, channel %p, ret %d", channel, ret);
        return ret;
    }

    free((struct KnetSendChannel *)channel); // channel的地址即为knetSendChannel的地址
    KNET_INFO("K-NET destroy send channel success");
    return ret;
}

int knet_create_recv_channel(enum knet_schd_type schd_mod, uint32_t depth, struct knet_recv_channel **channel)
{
    (void)depth;

    if (channel == NULL) {
        KNET_ERR("Channel is null in knet_create_recv_channel");
        return -1;
    }
    // calloc保证events/maxevents/nextEventIdx等框架侧字段清零，避免野指针/垃圾计数
    struct KnetRecvChannel *knetRecvChannel = (struct KnetRecvChannel *)calloc(1, sizeof(struct KnetRecvChannel));
    if (knetRecvChannel == NULL) {
        KNET_ERR("K-NET calloc recv channel failed, size %zu", sizeof(struct KnetRecvChannel));
        return -1;
    }
    knetRecvChannel->leakLock.value = KNET_SPIN_UNLOCKED_VALUE;
    int ret = flexda_dtoe_create_receive_channel(g_dtoeRes.dev.devSn, schd_mod, (flexda_recv_channel_s *)(&knetRecvChannel->channel));
    if (ret != 0) {
        free(knetRecvChannel);
        KNET_ERR("K-NET create recv channel failed, devSn %lu, schd_mod %d, ret %d", g_dtoeRes.dev.devSn, schd_mod, ret);
        return ret;
    }
    knetRecvChannel->schdMode = schd_mod;
    *channel = &knetRecvChannel->channel;

    TAILQ_INIT(&knetRecvChannel->leakList);

    KNET_INFO("K-NET create recv channel success, devSn %lu, schd_mod %d, epoll_fd %d", g_dtoeRes.dev.devSn, schd_mod,
              knetRecvChannel->channel.epoll_fd);
    return ret;
}

int knet_destroy_recv_channel(struct knet_recv_channel *channel)
{
    int ret = flexda_dtoe_destroy_receive_channel((flexda_recv_channel_s *)channel);
    if (ret != 0) {
        KNET_ERR("Dtoe destroy recv channel failed, channel %p, ret %d", channel, ret);
        return ret;
    }

    free((struct KnetRecvChannel *)channel); // channel的地址即为knet_recv_channel_t的地址
    KNET_INFO("K-NET destroy recv channel success");
    return ret;
}

int knet_start_chimney_general(int sockfd, struct knet_offload_in *in)
{
    if (!KNET_IsOsFdValid(sockfd)) {
        KNET_ERR("K-NET start chimney general offload sockfd %d is invalid", sockfd);
        return -1;
    }

    if (in == NULL || in->recv_channel == NULL || in->send_channel == NULL) {
        KNET_ERR("K-NET start chimney general offload in or recv_channel or send_channel is null, sockfd %d", sockfd);
        return -1;
    }

    // 提前初始化free_req：全部框架资源就绪后再发起驱动offload，offload失败只需回收框架内存，无需驱动侧撤销
    int ret = KNET_InitFreeReq(sockfd);
    if (ret != 0) {
        KNET_ERR("K-NET start chimney general failed in init free request nodes, sockfd %d, ret %d", sockfd, ret);
        return ret;
    }

    flexda_dtoe_offload_in_s input = {0};
    input.user_data = (void *)KNET_GetFdConnUserData(sockfd);
    input.recv_channel = (flexda_recv_channel_s *)in->recv_channel;
    input.send_channel = (flexda_send_channel_s *)in->send_channel;

    flexda_dtoe_offload_out_s out = {0};
    ret = flexda_dtoe_start_conn_offload(sockfd, &input, &out);
    if (ret != 0) {
        KNET_ERR("Dtoe start chimney general failed, sockfd %d, ret %d", sockfd, ret);
        KNET_UninitFreeReq(sockfd); // 回滚已初始化的框架资源，避免半初始化连接残留
        return ret;
    }

    KNET_SetFdState(sockfd, in, &out);

    KNET_INFO("K-NET start chimney general success, sockfd %d", sockfd);
    return 0;
}

void knet_recv_mem_loopback(int sockfd, struct knet_iovec *iov, int iov_cnt)
{
    if (unlikely(!KNET_IsOsFdTcp(sockfd))) {
        KNET_ERR("Sockfd %d is invalid", sockfd);
        return;
    }

    if (unlikely(iov == NULL || iov_cnt <= 0)) {
        KNET_ERR("Sockfd %d iov is NULL or iov_cnt <= 0, iov_cnt %d", sockfd, iov_cnt);
        return;
    }
    struct KnetTcp *tcp = KNET_GetFdConnUserData(sockfd);
    int loopbackCnt = 0;
    for (int i = 0; i < iov_cnt; i++) {
        // 漏包是框架 malloc 的拷贝内存，不归驱动 BD，这里 free 而非走驱动 loopback
        if (tcp->leakBuf != NULL && iov[i].iov_base == tcp->leakBuf) {
            KNET_SockLeakResRelease(tcp); // 已移交缓冲的用户归还：摘链并释放
            continue;
        }
        iov[loopbackCnt++] = iov[i];
    }

    if (loopbackCnt > 0) {
        flexda_dtoe_recv_mem_loopback((struct iovec *)iov, loopbackCnt);
    }
}

void knet_prepare_close(int sockfd)
{
    if (!KNET_IsOsFdTcp(sockfd)) {
        KNET_ERR("Sockfd %d is invalid", sockfd);
        return;
    }

    struct KnetTcp *tcp = KnetGetTcpFd(sockfd);
    // 释放通知机制相关资源：SetState(CLOSE)与DisableNotify必须在同一临界区内原子完成，
    // 否则两步之间并发close_done回调可能穿插触发通知，产生竞态
    KNET_SockLock(&tcp->sk);
    KNET_SockSetStateLocked(&tcp->sk, KNET_SOCK_STATE_CLOSE); // 上报CLOSE，EpollNotify内释放item
    KNET_SockDisableNotify(&tcp->sk);
    KNET_SockUnlock(&tcp->sk);

    flexda_dtoe_prepare_close(KNET_GetConnBySock(sockfd));
    
    KNET_GetFdConnUserData(sockfd)->olStatus = KNET_OFFLOAD_PREPARE_CLOSING;

    KNET_DEBUG("Prepare close, sockfd %d", sockfd);
}

static void KNET_TcpClose(int sockfd)
{
    flexda_dtoe_close(sockfd, KNET_GetConnBySock(sockfd));
    KNET_GetFdConnUserData(sockfd)->olStatus = KNET_OFFLOAD_CLOSING;
}

static void KNET_EpollClose(int epfd)
{
    KNET_ResetEpfd(epfd);
    close(epfd); // 回收OS层epoll fd，与TCP路径对称，否则epoll fd泄漏
}

void knet_close(int fd)
{
    if (!KNET_IsOsFdValid(fd)) {
        KNET_ERR("K-NET close fd error, fd %d is invalid", fd);
        return;
    }

    switch (KNET_FdTypeGet(fd))
    {
    case KNET_FD_TYPE_TCP:
        KNET_TcpClose(fd);
        break;
    case KNET_FD_TYPE_EPOLL:
        KNET_EpollClose(fd);
        break;
    default:
        KNET_WARN("K-NET close fd skip, fd %d type %u is not target type", fd, KNET_FdTypeGet(fd));
        break;
    }

    KNET_DEBUG("K-NET close fd %d success", fd);
}

int knet_fd_is_offloaded(int sockfd)
{
    if (!KNET_IsOsFdTcp(sockfd)) {
        KNET_ERR("K-NET fd is offloaded check failed, sockfd %d is invalid", sockfd);
        return 0;
    }

    if (KNET_GetConnBySock(sockfd) != NULL) {
        return 1;
    }
    return 0;
}

void *knet_get_ulp_user_data(int sockfd)
{
    if (unlikely(!KNET_IsOsFdValid(sockfd))) {
        KNET_ERR("K-NET get ulp user data sockfd %d is invalid", sockfd);
        return NULL;
    }
    return KNET_GetFdConnUserData(sockfd)->user_data;
}

/**
 * @note req发送完成需要满足如下条件
 * 都不翻转               last_sn < send_sn <= comp_sn
 * comp_sn翻转            comp_sn < last_sn < send_sn
 * comp_sn, send_sn都翻转 send_sn <= comp_sn < last_sn
*/
static inline bool is_send_complete(struct KnetTcp* conn, KnetReqNode* rnode)
{
    bool complete = false;
    if (likely(conn->send.last_sn < conn->send.comp_sn)) {
        if (rnode->send_sn > conn->send.last_sn && rnode->send_sn <= conn->send.comp_sn) {
            complete = true;
        }
    } else if (conn->send.last_sn > conn->send.comp_sn) {
        if (rnode->send_sn <= conn->send.comp_sn || rnode->send_sn > conn->send.last_sn) {
            complete = true;
        }
    }
    
    return complete;
}

static void knet_dtoe_send_complete(void* dtoe_conn, flexda_dtoe_tx_event_s* event)
{
    struct KnetTcp* sock = (struct KnetTcp*)flexda_dtoe_get_ulp_user_data(dtoe_conn);
    if (unlikely(sock == NULL || event == NULL)) {
        KNET_ERR("Sock: %s, event: %s, null is illegal", sock == NULL ? "Null" : "not Null",
                 event == NULL ? "Null" : "not Null");
        return;
    }

    sock->send.comp_sn = event->finish_msn;

#ifndef KNET_REQ_NODE_ATOMIC
    KNET_SpinlockLock(&sock->send_lock);
#endif

    KnetReqNode *req = TAILQ_FIRST(&sock->send.unack_req);
    while (req != NULL) {
        if (!is_send_complete(sock, req)) {
            break;
        }

        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Send complete, sockfd %d, wr_id %llu, compSn %u, lastSn %u, sendSn %u",
                            sock->sk.fd, req->wr_id, sock->send.comp_sn, sock->send.last_sn, req->send_sn);
        if (likely(req->freeCb != NULL)) {
            req->freeCb(req->sockfd, req->wr_id);
        }

        // last_sn 在收到ack时更新
        sock->send.last_sn = req->send_sn;

        TAILQ_REMOVE(&sock->send.unack_req, req, node);
        TAILQ_INSERT_TAIL(&sock->send.free_req, req, node);

        req = TAILQ_FIRST(&sock->send.unack_req);
    }

#ifndef KNET_REQ_NODE_ATOMIC
    KNET_SpinlockUnlock(&sock->send_lock);
#endif
}

/**
 * @brief 引擎收到包后调用该回调，记录事件
 * @param [IN] dtoe_conn
 * @param [IN] iov_cnt
 * @return void
 */
static void KnetDtoeRecvNotify(void *dtoe_conn, int iov_cnt)
{
    struct KnetTcp *tcp = (struct KnetTcp *)flexda_dtoe_get_ulp_user_data(dtoe_conn);
    if (unlikely(tcp == NULL)) {
        KNET_ERR("Recv notify tcp is null, dtoe_conn %p", dtoe_conn);
        return;
    }

    if (iov_cnt > 0) {
        // 驱动回调与knet_recv消费在同一线程内顺序执行，iovCnt无需原子操作
        tcp->sk.iovCnt += iov_cnt;
        KNET_SockSetState(&tcp->sk, KNET_SOCK_STATE_READ);
    } else if (iov_cnt == 0) {
        KNET_SockSetState(&tcp->sk, KNET_SOCK_STATE_CANTRCVMORE);
    } else {
        KNET_ERR("Sockfd %d, iov_cnt: %d, unknown reason", tcp->sk.fd, iov_cnt);
        KNET_SockSetState(&tcp->sk, KNET_SOCK_STATE_EXCEPTION);
    }

    KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Recv notify, sockfd %d, iov_cnt %d", tcp->sk.fd, iov_cnt);
}

void knet_ulp_ops_register(struct knet_ulp_ops *ops)
{
    if (ops == NULL) {
        KNET_ERR("K-NET ulp ops is NULL");
        return;
    }

    g_knetDtoeOps.close_done = ops->close_done;
    g_knetDtoeOps.prepare_close_done = ops->prepare_close_done;
    g_knetDtoeOps.conn_async_offload_done = ops->conn_async_offload_done;

    g_dtoeOps.close_done = KnetRegCloseDone;
    g_dtoeOps.prepare_close_done = KnetRegPrepareCloseDown;
    g_dtoeOps.conn_async_offload_done = KnetRegConnAsyncOffloadDone;
    g_dtoeOps.send_complete = knet_dtoe_send_complete;
    g_dtoeOps.recv_notify = KnetDtoeRecvNotify;

    flexda_dtoe_ulp_ops_register(&g_dtoeOps);
}

/**
 * @brief 检查 tx channel 完成事件，执行对应处理
 * @param send_channel [IN/OUT] knet 对外tx channel
 * @param events [IN/OUT] 关注事件
 * @param maxevents [IN] 最大事件数
 * @return 完成事件数
 */
int knet_poll_send_channel(struct knet_send_channel* send_channel, uint32_t maxevents)
{
    if (unlikely(send_channel == NULL)) {
        KNET_ERR("Send_channel is null, illegal");
        return -EINVAL;
    }

    return flexda_dtoe_poll_send_channel((flexda_send_channel_s*)send_channel, maxevents);   // send_channel地址即是send_channel_events地址
}

/**
 * @brief knet for dtoe send 
 * @param sockfd [IN] knet返回的sockfd
 * @param tx_req [IN] 构造卸载发包的tx_req请求
 * @return 成功返回发包字节数，失败返回负数
 */
int knet_send(int sockfd, struct knet_tx_req* tx_req)
{
    if (unlikely(tx_req == NULL)) {
        KNET_ERR("Tx_req is null, illegal");
        return -EINVAL;
    }

    if (unlikely(!KNET_IsOsFdTcp(sockfd))) {
        KNET_ERR("Sockfd %d is invalid", sockfd);
        return -EINVAL;
    }

    flexda_dtoe_tx_info_s info = {
        .tx_in.op_code = 0,
        .tx_in.lkey = tx_req->lkey
    };

    struct KnetTcp *tcp = KNET_GetFdConnUserData(sockfd);

    KnetReqNode *req = (KnetReqNode *)TAILQ_FIRST(&tcp->send.free_req);
    if (unlikely(req == NULL)) {
        KNET_ERR("Free req list is empty, sockfd %d", sockfd);
        return -ENOMEM;
    }
    int ret = flexda_dtoe_send(tcp->dtoe_conn, (struct iovec *)tx_req->iov, tx_req->iov_cnt, &info);
    if (ret < 0) {
        if (ret != -EAGAIN) {
            KNET_ERR("Sockfd %d dtoe send failed, ret %d, iov_cnt %u, wr_id %llu", sockfd, ret, tx_req->iov_cnt, tx_req->wr_id);
        }

        return ret;
    }
    KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Dtoe send bytes: %d, dtoe fd: %d, send iov_cnt: %d, curr_msn %u", ret, sockfd,
                        tx_req->iov_cnt, info.tx_out.curr_msn);

    #ifndef KNET_REQ_NODE_ATOMIC
    KNET_SpinlockLock(&KNET_GetFdConnUserData(sockfd)->send_lock);
#endif
    TAILQ_REMOVE(&KNET_GetFdConnUserData(sockfd)->send.free_req, req, node);
    req->freeCb = tx_req->free_cb;
    req->wr_id = tx_req->wr_id;
    req->sockfd = sockfd;
    req->send_sn = info.tx_out.curr_msn;
    TAILQ_INSERT_TAIL(&KNET_GetFdConnUserData(sockfd)->send.unack_req, req, node);
#ifndef KNET_REQ_NODE_ATOMIC
    KNET_SpinlockUnlock(&KNET_GetFdConnUserData(sockfd)->send_lock);
#endif
    return ret;
}

static int Bytes2IovCnt(int bytes, struct knet_iovec *iov, int iov_cnt)
{
    int iovCnt = 0;
    while (bytes > 0 && iovCnt < iov_cnt) {
        if (iov[iovCnt].iov_len <= 0) {
            break; // 防止 iov_len 非法导致死循环/越界
        }
        bytes -= iov[iovCnt].iov_len;
        ++iovCnt;
    }
    return iovCnt;
}

int knet_recv(int sockfd, struct knet_iovec *iov, int iov_cnt)
{
    if (unlikely(!KNET_IsOsFdTcp(sockfd))) {
        KNET_ERR("Sockfd %d is invalid", sockfd);
        return -EINVAL;
    }

    if (unlikely(iov == NULL || iov_cnt <= 0)) {
        KNET_ERR("Sockfd %d iov is NULL or iov_cnt <= 0, iov_cnt %d", sockfd, iov_cnt);
        return -EINVAL;
    }

    struct KnetTcp *tcp = KNET_GetFdConnUserData(sockfd);

    // 先处理漏包：漏包在时序上先于驱动零拷贝数据，必须以零拷贝方式先返回。
    // leakBuf 生命周期与驱动 recv 内存一致：本次返回后，直到上层通过 knet_recv_mem_loopback 归还之前有效。
    if (tcp->leakBuf != NULL) {
        if (tcp->leakSize > 0) {
            // 漏包尚未移交：以零拷贝 iov 形式返回，标记已移交，等待上层通过 loopback 归还
            iov[0].iov_base = tcp->leakBuf;
            iov[0].iov_len = (size_t)tcp->leakSize;
            int ret = tcp->leakSize;
            tcp->leakSize = 0;
            // 漏包已交付；若驱动无更多可读数据则清除可读状态，避免 level-trigger 下忙等
            if (tcp->sk.iovCnt == 0) {
                KNET_SockUnsetState(&tcp->sk, KNET_SOCK_STATE_READ);
            }
            KNET_INFO("Sockfd %d, dtoe recv leaked packet %d bytes", sockfd, ret);
            return ret;
        }
        // leakSize==0 且 leakBuf 仍在：漏包已移交、等待上层通过 loopback 归还
    }

    // 对端已关闭(FIN)，且无未读数据 → 返回 0 表示 EOF
    if ((tcp->sk.state & KNET_SOCK_STATE_CANTRCVMORE) && tcp->sk.iovCnt == 0) {
        KNET_INFO("Sockfd %d, dtoe recv EOF", sockfd);
        return 0;
    }
    int ret = flexda_dtoe_recv(tcp->dtoe_conn, (struct iovec *)iov, iov_cnt);
    if (ret < 0) {
        KNET_ERR("Dtoe recv failed, sockfd %d, ret %d", sockfd, ret);
        return ret;
    }
    if (ret == 0) {
        if (tcp->sk.state & KNET_SOCK_STATE_CANTRCVMORE) {
            return 0; // 对端已关，0 = EOF
        }
        return -EAGAIN; // 对端没关，只是暂时无数据
    }
    int curIovCnt = Bytes2IovCnt(ret, iov, iov_cnt);
    tcp->sk.iovCnt -= curIovCnt;
    if (tcp->sk.iovCnt == 0) {
        KNET_SockUnsetState(&tcp->sk, KNET_SOCK_STATE_READ);
    }

    KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Dtoe recv bytes %d, dtoe fd %d, iov_cnt %d, curIovCnt %d", ret, sockfd, iov_cnt, curIovCnt);

    return ret;
}

int32_t knet_get_leaked_packet_size(int sockfd)
{
    if (unlikely(!KNET_IsOsFdTcp(sockfd))) {
        KNET_ERR("Sockfd %d is invalid", sockfd);
        return -EINVAL;
    }

    return KNET_GetFdConnUserData(sockfd)->leakSize;
}

int32_t knet_recv_leaked_packet(int sockfd, void *buffer, int32_t recv_len)
{
    if (unlikely(!KNET_IsOsFdTcp(sockfd))) {
        KNET_ERR("Sockfd %d is invalid", sockfd);
        return -EINVAL;
    }

    struct KnetTcp *sock = KNET_GetFdConnUserData(sockfd);
    int32_t leakSize = sock->leakSize;
    if (recv_len < leakSize) {
        KNET_ERR("Recv_len %d must be greater than leakSize %d, sockfd %d", recv_len, leakSize, sockfd);
        return -EINVAL;
    }

    (void)memcpy_s(buffer, recv_len, sock->leakBuf, leakSize); // 长度已提前判断，不会异常

    KNET_SockLeakedResUninit(sock);

    return leakSize;
}

void knet_log(const char *function, int line, int level, const char *format, ...)
{
    va_list va;
    va_start(va, format);
    KNET_LogVa(function, line, level, format, va);
    va_end(va);
}

int32_t knet_flexda_dtoe_channel_epoll_set(struct knet_send_channel *send_channel, struct knet_recv_channel *recv_channel, uint32_t epoll_enable)
{
    return flexda_dtoe_channel_epoll_set(g_dtoeRes.dev.devSn, (flexda_send_channel_s*)send_channel, (flexda_recv_channel_s*)recv_channel, epoll_enable);
}

int32_t knet_flexda_dtoe_channel_qpc_create(struct knet_send_channel *send_channel, struct knet_recv_channel *recv_channel)
{
    return flexda_dtoe_channel_qpc_create(g_dtoeRes.dev.devSn, (flexda_send_channel_s*)send_channel, (flexda_recv_channel_s*)recv_channel);
}

int32_t knet_flexda_dtoe_channel_qpc_destroy(struct knet_send_channel *send_channel, struct knet_recv_channel *recv_channel)
{
    return flexda_dtoe_channel_qpc_destroy(g_dtoeRes.dev.devSn, (flexda_send_channel_s*)send_channel, (flexda_recv_channel_s*)recv_channel);
}

int32_t knet_flexda_dtoe_channel_qpc_rx_invalid_set(struct knet_recv_channel *recv_channel, uint32_t rx_invalid_enable)
{
    return flexda_dtoe_channel_qpc_rx_invalid_set(g_dtoeRes.dev.devSn, (flexda_recv_channel_s*)recv_channel, rx_invalid_enable);
}

int knet_flexda_dtoe_receive_channel_is_empty(struct knet_recv_channel *recv_channel)
{
    return flexda_dtoe_receive_channel_is_empty((flexda_recv_channel_s*)recv_channel);
}
