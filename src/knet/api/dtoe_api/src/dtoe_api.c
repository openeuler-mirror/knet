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

#include <stdarg.h>
#include <unistd.h>
#include <sys/queue.h>

#include "flexda_dtoe_interface.h"
#include "knet_log.h"
#include "knet_lock.h"
#include "knet_dtoe_fd.h"
#include "knet_dtoe_events.h"
#include "knet_dtoe_config.h"

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
    struct KNET_Fd *knetUserData = flexda_dtoe_get_ulp_user_data(dtoe_conn);
    if (knetUserData == NULL) {
        KNET_ERR("The dtoe_get_ulp_user_data return user data is null");
        return KNET_INVALID_FD;
    }

    int fd = knetUserData->sockfd;
    if (!KNET_IsOsFdValid(fd)) {
        KNET_ERR("Conn fd %d is invalid", fd);
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
        KNET_ERR("dtoe_conn is null in close_done");
        return;
    }

    int fd = getFdFromDtoeConn(dtoe_conn);
    if (fd == KNET_INVALID_FD) {
        KNET_ERR("Close down fd is invalid");
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
        KNET_ERR("dtoe_conn is null in prepare_close_done");
        return;
    }

    int fd = getFdFromDtoeConn(dtoe_conn);
    if (fd == KNET_INVALID_FD) {
        KNET_ERR("Close down fd is invalid");
        return;
    }
    KNET_DEBUG("Prepare close down fd %d", fd);
    g_knetDtoeOps.prepare_close_done(fd);
}

static void KnetRegConnAsyncOffloadDone(void *dtoe_conn, uint8_t rsp_status)
{
    if (g_knetDtoeOps.conn_async_offload_done == NULL) {
        return;
    }

    if (unlikely(dtoe_conn == NULL)) {
        KNET_ERR("dtoe_conn is null in conn_async_offload_done");
        return;
    }

    int fd = getFdFromDtoeConn(dtoe_conn);
    if (unlikely(fd == KNET_INVALID_FD)) {
        KNET_ERR("Close down fd is invalid");
        return;
    }

    if (unlikely(rsp_status == KNET_ASYNC_OFFLOAD_FAIL)) {
        KNET_ERR("sockfd %d async offload failed", fd);
        goto offloadFailed;
    }

    KNET_INFO("Conn async offload done fd %d", fd);

    int ret = KNET_SockLeakResInit(KNET_GetFdConnUserData(fd));
    if (ret != 0) {
        KNET_ERR("sockfd %d leak res init failed, ret %d", fd, ret);
        goto leakResInitFailed;
    }

    g_knetDtoeOps.conn_async_offload_done(fd, rsp_status);
    return;

leakResInitFailed:
offloadFailed:
    g_knetDtoeOps.conn_async_offload_done(fd, KNET_ASYNC_OFFLOAD_FAIL);
}

KNET_API int knet_init(const char * local_ip)
{
    KNET_LogInit();

    int ret = KNET_InitCfg();
    if (ret != 0) {
        KNET_ERR("Knet init cfg failed, ret %d", ret);
        goto init_cfg_failed;
    }

    KNET_LogLevelSetByStr(KNET_GetCfg(CONF_DTOE_LOG_LEVEL)->strValue);

    ret = KNET_FdInit();
    if (ret != 0) {
        KNET_ERR("Knet fd init failed, ret %d", ret);
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
        goto free;
    }
    KNET_INFO("Dtoe init success");

#define BIND_INTERVAL 1
#define BIND_TIMES 10
    uint8_t times = 0;
    do {
        ret = flexda_dtoe_bind_addr(local_ip, &g_dtoeRes.dev.devSn, &g_dtoeRes.dev.numaId);
        ++times;
        sleep(BIND_INTERVAL);
        KNET_WARN("Dtoe bind addr failed, local_ip %s, ret %d, times %u",
            (local_ip == NULL) ? "null" : local_ip, ret, times);
    } while (times <= BIND_TIMES && ret != 0);
    if (ret != 0) {
        KNET_ERR("Dtoe bind addr failed, local_ip %s, ret %d", (local_ip == NULL) ? "null" : local_ip, ret);
        goto dtoe_free;
    }

    ret = strncpy_s(g_dtoeRes.dev.ip, IP_STR_MAX_LEN, local_ip, IP_STR_MAX_LEN - 1);
    if (ret != 0) {
        KNET_ERR("Strncpy dev ip failed, local_ip %s, ret %d", local_ip, ret);
        goto dtoe_free;
    }

    KNET_INFO("Knet int success, ip %s, devSn %lu, numaId %u", g_dtoeRes.dev.ip, g_dtoeRes.dev.devSn, g_dtoeRes.dev.numaId);
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
        KNET_ERR("Knet malloc mr failed");
        return NULL;
    }

    int ret = flexda_dtoe_reg_mr(g_dtoeRes.dev.devSn, addr, length, (flexda_dtoe_mr_s *)dmr);
    if (ret != 0) {
        KNET_ERR("Knet reg mr failed, devSn %lu, length %zu, ret %d", g_dtoeRes.dev.devSn, length, ret);
        free(dmr);
        return NULL;
    }
    KNET_INFO("Knet reg mr success, devSn %lu, length %zu, ret %d", g_dtoeRes.dev.devSn, length, ret);
    return dmr;
}

int knet_unreg_mr(struct knet_mr *dmr)
{
    int ret = flexda_dtoe_unreg_mr(g_dtoeRes.dev.devSn, (flexda_dtoe_mr_s *)dmr);
    if (ret != 0) {
        KNET_ERR("Knet unreg mr failed, devSn %lu, ret %d", g_dtoeRes.dev.devSn, ret);
        return ret;
    }
    free(dmr);
    KNET_INFO("Knet unreg mr success, devSn %lu, ret %d", g_dtoeRes.dev.devSn, ret);
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
        KNET_ERR("Knet malloc send channel failed");
        return -1;
    }
    int ret = flexda_dtoe_create_send_channel(g_dtoeRes.dev.devSn, schd_mod, (flexda_send_channel_s *)(&knetSendChannel->channel));
    if (ret != 0) {
        free(knetSendChannel);
        KNET_ERR("Knet create send channel failed, devSn %lu, schd_mod %d, ret %d", g_dtoeRes.dev.devSn, schd_mod, ret);
        return ret;
    }
    *channel = &knetSendChannel->channel;
    KNET_INFO("Knet create send channel success, devSn %lu, schd_mod %d, epoll_fd %d",
        g_dtoeRes.dev.devSn, schd_mod, knetSendChannel->channel.epoll_fd);
    return ret;
}

int knet_destroy_send_channel(struct knet_send_channel *channel)
{
    int ret = flexda_dtoe_destroy_send_channel((flexda_send_channel_s *)channel);
    if (ret != 0) {
        KNET_ERR("Dtoe destroy send channel failed, ret %d", ret);
        return ret;
    }

    free((struct KnetSendChannel *)channel); // channel的地址即为knetSendChannel的地址
    KNET_INFO("Knet destroy send channel success");
    return ret;
}

int knet_create_recv_channel(enum knet_schd_type schd_mod, uint32_t depth, struct knet_recv_channel **channel)
{
    (void)depth;

    if (channel == NULL) {
        KNET_ERR("Channel is null in knet_create_recv_channel");
        return -1;
    }
    struct KnetRecvChannel *knetRecvChannel = (struct KnetRecvChannel *)malloc(sizeof(struct KnetRecvChannel));
    if (knetRecvChannel == NULL) {
        KNET_ERR("Knet malloc recv channel failed");
        return -1;
    }
    int ret = flexda_dtoe_create_receive_channel(g_dtoeRes.dev.devSn, schd_mod, (flexda_recv_channel_s *)(&knetRecvChannel->channel));
    if (ret != 0) {
        free(knetRecvChannel);
        KNET_ERR("Knet create recv channel failed, devSn %lu, schd_mod %d, ret %d", g_dtoeRes.dev.devSn, schd_mod, ret);
        return ret;
    }
    *channel = &knetRecvChannel->channel;

    TAILQ_INIT(&knetRecvChannel->leakList);

    KNET_INFO("Knet create recv channel success, devSn %lu, schd_mod %d", g_dtoeRes.dev.devSn, schd_mod);
    return ret;
}

int knet_destroy_recv_channel(struct knet_recv_channel *channel)
{
    int ret = flexda_dtoe_destroy_receive_channel((flexda_recv_channel_s *)channel);
    if (ret != 0) {
        KNET_ERR("Dtoe destroy recv channel failed, ret %d", ret);
        return ret;
    }

    free((struct KnetRecvChannel *)channel); // channel的地址即为knet_recv_channel_t的地址
    KNET_INFO("Knet destroy recv channel success");
    return ret;
}

int knet_start_chimney_general(int sockfd, struct knet_offload_in *in)
{
    if (!KNET_IsOsFdValid(sockfd)) {
        KNET_ERR("Knet start chimney general offload sockfd %d is invalid", sockfd);
        return -1;
    }

    if (in == NULL || in->recv_channel == NULL || in->send_channel == NULL) {
        KNET_ERR("Knet start chimney general offload in or recv_channel or send_channel is null");
        return -1;
    }

    flexda_dtoe_offload_in_s input = {0};
    input.user_data = (void *)KNET_GetFdConnUserData(sockfd);
    input.rev_channel = (flexda_recv_channel_s *)in->recv_channel;
    input.send_channel = (flexda_send_channel_s *)in->send_channel;

    flexda_dtoe_offload_out_s out = {0};
    int ret = flexda_dtoe_start_conn_offload(sockfd, &input, &out);
    if (ret != 0) {
        KNET_ERR("Dtoe start chimney general failed, sockfd %d, ret %d", sockfd, ret);
        return ret;
    }

    KNET_SetFdState(sockfd, in, &out);

    ret = KNET_InitFreeReq(sockfd);
    if (ret != 0) {
        KNET_ERR("Knet start chimney general failed in init free request nodes, ret %d", ret);
        return ret;
    }
    KNET_INFO("Knet start chimney general success, sockfd %d", sockfd);
    return ret;
}

void knet_recv_mem_loopback(struct knet_iovec *iov, int iov_cnt)
{
    flexda_dtoe_recv_mem_loopback((struct iovec *)iov, iov_cnt);
}

void knet_prepare_close(int sockfd)
{
    if (!KNET_IsOsFdValid(sockfd)) {
        KNET_ERR("Knet prepare close sockfd %d is invalid", sockfd);
        return;
    }
    flexda_dtoe_prepare_close(KNET_GetConnBySock(sockfd));

    KNET_DEBUG("sockfd %d prepare close", sockfd);
}

void knet_close(int sockfd)
{
    if (!KNET_IsOsFdValid(sockfd)) {
        KNET_ERR("Knet close sockfd %d is invalid", sockfd);
        return;
    }

    flexda_dtoe_close(sockfd, KNET_GetConnBySock(sockfd));
    
    KNET_DEBUG("sockfd %d knet close", sockfd);
}

int knet_fd_is_offloaded(int sockfd)
{
    if (!KNET_IsOsFdValid(sockfd)) {
        KNET_ERR("Knet offload sockfd %d is invalid", sockfd);
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
        KNET_ERR("Knet get ulp user data sockfd %d is invalid", sockfd);
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
static inline bool is_send_complete(struct KNET_Fd* conn, KnetReqNode* rnode)
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
    struct KNET_Fd* sock = (struct KNET_Fd*)flexda_dtoe_get_ulp_user_data(dtoe_conn);
    if (unlikely(sock == NULL || event == NULL)) {
        KNET_ERR("sock: %s, event: %s, null is illeagal",
            sock == NULL ? "Null" : "not Null", event == NULL ? "Null" : "not Null");
        return;
    }
    
    KnetReqNode* req = (KnetReqNode*)TAILQ_FIRST(&sock->send.unack_req);
    while (req != NULL) {
        sock->send.comp_sn = event->finish_msn;
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG,
            "send complete, sockfd %d, wr_id %llu, nextEventIdx %u, compSn %u, lastSn %u, sendSn %u",
            sock->sockfd, req->wr_id, sock->send_channel->nextEventIdx,
            sock->send.comp_sn, sock->send.last_sn, req->send_sn);
        if (!is_send_complete(sock, req)) {
            break;
        }
        // req 序号满足send complete时记录事件
        sock->send_channel->events[sock->send_channel->nextEventIdx].wr_id = req->wr_id;
        sock->send_channel->events[sock->send_channel->nextEventIdx].sockfd = sock->sockfd;
        ++sock->send_channel->nextEventIdx;
        
        // last_sn 在收到ack时更新
        sock->send.last_sn = req->send_sn;

#ifndef KNET_REQ_NODE_ATOMIC
        KNET_SpinlockLock(&sock->send_lock);
#endif
        TAILQ_REMOVE(&sock->send.unack_req, req, node);
        TAILQ_INSERT_TAIL(&sock->send.free_req, req, node);
#ifndef KNET_REQ_NODE_ATOMIC
        KNET_SpinlockUnlock(&sock->send_lock);
#endif

        // 下一次req处理，直到没有req或者req非complete
        req = (KnetReqNode*)TAILQ_FIRST(&sock->send.unack_req);
    }
}

/**
 * @brief 引擎收到包后调用该回调，记录事件
 * @param [IN] dtoe_conn
 * @param [IN] iov_cnt
 * @return void
 */
static void knet_dtoe_receive_notify(void* dtoe_conn, int iov_cnt)
{
    struct KNET_Fd* sock = (struct KNET_Fd*)flexda_dtoe_get_ulp_user_data(dtoe_conn);
    if (unlikely(sock == NULL)) {
        KNET_ERR("sock null is illeagal");
        return;
    }

    struct KnetRecvChannel *recvChannel = sock->recv_channel;
    if (likely(iov_cnt > 0)) {
        if (sock->recvEventIndex == KNET_INVALID_EVENT_INDEX) { // sockfd第一次触发recv notify，记录event index位置
            recvChannel->events[recvChannel->nextEventIdx].sockfd = sock->sockfd;
            recvChannel->events[recvChannel->nextEventIdx].iov_cnt = iov_cnt;
            recvChannel->events[recvChannel->nextEventIdx].type = KNET_RX_EVENT_NORMAL;
            sock->recvEventIndex = recvChannel->nextEventIdx;
            ++recvChannel->nextEventIdx;
        } else { // 后续第2 3 ... N 次触发recv_notify，聚合iov_cnt
            recvChannel->events[sock->recvEventIndex].iov_cnt += iov_cnt;
        }
    } else if (iov_cnt == 0) { // iov_cnt为0的事件不能聚合，要返回给用户，告知对端连接已断开
        recvChannel->events[recvChannel->nextEventIdx].sockfd = sock->sockfd;
        recvChannel->events[recvChannel->nextEventIdx].iov_cnt = iov_cnt;
        recvChannel->events[recvChannel->nextEventIdx].type = KNET_RX_EVENT_NORMAL;
        ++recvChannel->nextEventIdx;
    } else {
        KNET_ERR("sockfd %d, iov_cnt: %d, unknown reason", sock->sockfd, iov_cnt);
    }
    KNET_DEBUG("recv notify, sockfd %d, iov_cnt %d, nextEventIdx %d, recvEventIndex %d",
                    sock->sockfd, iov_cnt, recvChannel->nextEventIdx, sock->recvEventIndex);
}

void knet_ulp_ops_register(struct knet_ulp_ops *ops)
{
    if (ops == NULL) {
        KNET_ERR("KNET ulp ops is NULL");
        return;
    }

    g_knetDtoeOps.close_done = ops->close_done;
    g_knetDtoeOps.prepare_close_done = ops->prepare_close_done;
    g_knetDtoeOps.conn_async_offload_done = ops->conn_async_offload_done;

    g_dtoeOps.close_done = KnetRegCloseDone;
    g_dtoeOps.prepare_close_done = KnetRegPrepareCloseDown;
    g_dtoeOps.conn_async_offload_done = KnetRegConnAsyncOffloadDone;
    g_dtoeOps.send_complete = knet_dtoe_send_complete;
    g_dtoeOps.recv_notify = knet_dtoe_receive_notify;

    flexda_dtoe_ulp_ops_register(&g_dtoeOps);
}

/**
 * @brief 检查 tx channel 完成事件，执行对应处理
 * @param send_channel [IN/OUT] knet 对外tx channel
 * @param events [IN/OUT] 关注事件
 * @param maxevents [IN] 最大事件数
 * @return 完成事件数
 */
int knet_poll_send_channel(struct knet_send_channel* send_channel, struct knet_send_events* events, uint32_t maxevents)
{
    if (unlikely(send_channel == NULL || events == NULL)) {
        KNET_ERR("send_channel: %s, events: %s, null is illeagal",
            send_channel == NULL ? "Null" : "not Null", events == NULL ? "Null" : "not Null");
        return -EINVAL;
    }
    //
    struct KnetSendChannel* send_channel_events = (struct KnetSendChannel*)send_channel;
    send_channel_events->events = events;
    send_channel_events->nextEventIdx = 0;
    send_channel_events->maxevents = maxevents;

    /* todo：maxevents / 2，除以2是因为dtoe_poll_send_channel的index与rnode存在一对多的情况，可能导致越界，除2先缓解 */
    flexda_dtoe_poll_send_channel((flexda_send_channel_s*)send_channel, maxevents / 2);   // send_channel地址即是send_channel_events地址
    return send_channel_events->nextEventIdx;
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
        KNET_ERR("tx_req null is illeagal");
        return -EINVAL;
    }

    if (unlikely(!KNET_IsOsFdValid(sockfd))) {
        KNET_ERR("sockfd %d is invalid", sockfd);
        return -EINVAL;
    }

    flexda_dtoe_tx_info_s info = {
        .tx_in.op_code = 0,
        .tx_in.lkey = tx_req->lkey
    };

    KnetReqNode* req = (KnetReqNode*)TAILQ_FIRST(&KNET_GetFdConnUserData(sockfd)->send.free_req);
    if (unlikely(req == NULL)) {
        KNET_ERR("req is Null, out of memory");
        return -ENOMEM;
    }
    int ret = flexda_dtoe_send(KNET_GetFdConnUserData(sockfd)->dtoe_conn, (struct iovec *)tx_req->iov, tx_req->iov_cnt, &info);
    if (ret < 0) {
        if (ret != -EAGAIN) {
            KNET_ERR("dtoe send failed, ret %d", ret);
        }
        return ret;
    }
    KNET_DEBUG("dtoe send bytes: %d, dtoe fd: %d, send iov_cnt: %d, curr_msn %u",
        ret, sockfd, tx_req->iov_cnt, info.tx_out.curr_msn);

#ifndef KNET_REQ_NODE_ATOMIC
    KNET_SpinlockLock(&KNET_GetFdConnUserData(sockfd)->send_lock);
#endif
    TAILQ_REMOVE(&KNET_GetFdConnUserData(sockfd)->send.free_req, req, node);
    req->wr_id = tx_req->wr_id;
    req->send_sn = info.tx_out.curr_msn;
    TAILQ_INSERT_TAIL(&KNET_GetFdConnUserData(sockfd)->send.unack_req, req, node);
#ifndef KNET_REQ_NODE_ATOMIC
    KNET_SpinlockUnlock(&KNET_GetFdConnUserData(sockfd)->send_lock);
#endif
    return ret;
}

/**
 * @brief 检查 rx channel 完成事件，执行对应处理
 * @param receive_channel [IN/OUT] knet 对外rx channel
 * @param events [IN/OUT] 关注事件
 * @param maxevents [IN] 最大事件数
 * @return 完成事件数
 */
int knet_poll_recv_channel(struct knet_recv_channel* recv_channel, struct knet_recv_events* events, uint32_t maxevents)
{
    if (unlikely(recv_channel == NULL || events == NULL)) {
        KNET_ERR("recv_channel: %s, events: %s, null is illeagal", 
        recv_channel == NULL ? "Null" : "not Null", events == NULL ? "Null" : "not Null");
        return -1;
    }
    struct KnetRecvChannel* knetRecvChannel = (struct KnetRecvChannel*)recv_channel;

    knetRecvChannel->events = events;
    knetRecvChannel->maxevents = maxevents;
    knetRecvChannel->nextEventIdx = 0;

    /* 有泄漏数据未被接收，增加事件返回告知用户 */
    if (!TAILQ_EMPTY(&knetRecvChannel->leakList)) {
        struct KNET_Fd *sock;
#ifndef KNET_REQ_NODE_ATOMIC
        KNET_SpinlockLock(&knetRecvChannel->leakLock);
#endif
        TAILQ_FOREACH(sock, &knetRecvChannel->leakList, sock) {
            knetRecvChannel->events[knetRecvChannel->nextEventIdx].sockfd = sock->sockfd;
            knetRecvChannel->events[knetRecvChannel->nextEventIdx].iov_cnt = 0;
            knetRecvChannel->events[knetRecvChannel->nextEventIdx].type = KNET_RX_EVENT_LEAK;
            KNET_DEBUG("recv channel, leak sockfd %d, nextEventIdx %u", sock->sockfd, knetRecvChannel->nextEventIdx);
            ++knetRecvChannel->nextEventIdx;
        }
#ifndef KNET_REQ_NODE_ATOMIC
        KNET_SpinlockUnlock(&knetRecvChannel->leakLock);
#endif
    }

    /* recv_channel地址即是recv_channel_events地址 */
    flexda_dtoe_poll_receive_channel((flexda_recv_channel_s *)recv_channel, maxevents - knetRecvChannel->nextEventIdx);

    for (uint32_t i = 0; i < knetRecvChannel->nextEventIdx; ++i) {
        KNET_GetFdConnUserData(events[i].sockfd)->recvEventIndex = KNET_INVALID_EVENT_INDEX;
    }

    return knetRecvChannel->nextEventIdx;
}

int knet_recv(int sockfd, struct knet_iovec *iov, int iov_cnt)
{
    if (unlikely(!KNET_IsOsFdValid(sockfd))) {
        KNET_ERR("sockfd %d is invalid", sockfd);
        return -EINVAL;
    }

    int ret = flexda_dtoe_recv(KNET_GetFdConnUserData(sockfd)->dtoe_conn, (struct iovec *)iov, iov_cnt);
    if (ret < 0) {
        KNET_ERR("dtoe recv failed, ret %d", ret);
        return ret;
    }

    KNET_DEBUG("dtoe recv bytes: %d, dtoe fd: %d", ret, sockfd);

    return ret;
}

int32_t knet_get_leaked_packet_size(int sockfd)
{
    if (unlikely(!KNET_IsOsFdValid(sockfd))) {
        KNET_ERR("sockfd %d is invalid", sockfd);
        return -EINVAL;
    }

    return KNET_GetFdConnUserData(sockfd)->leakSize;
}

int32_t knet_recv_leaked_packet(int sockfd, void *buffer, int32_t recv_len)
{
    if (unlikely(!KNET_IsOsFdValid(sockfd))) {
        KNET_ERR("sockfd %d is invalid", sockfd);
        return -EINVAL;
    }

    struct KNET_Fd *sock = KNET_GetFdConnUserData(sockfd);
    int32_t leakSize = sock->leakSize;
    if (recv_len < leakSize) {
        KNET_ERR("recv_len %d must bigger than leakSize %d", recv_len, leakSize);
        return -EINVAL;
    }

    (void)memcpy_s(buffer, recv_len, sock->leakBuf, leakSize); // 长度已提前判断，不会异常

    KNET_SockLeakedResUninit(sock);

    return leakSize;
}

void knet_log(const char *function, int line, int level, const char *format, ...)
{
    KNET_Log(function, line, level, format);
}