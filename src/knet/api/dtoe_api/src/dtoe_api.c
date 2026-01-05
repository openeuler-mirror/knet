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

#include "dtoe_interface.h"

#include "knet_log.h"
#include "knet_dtoe_fd.h"

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
static struct knet_ulp_ops g_knet_dtoe_ops = {0};
static dtoe_ulp_ops_s g_dtoe_ops = {
    .sqe_init = NULL,
    .send_process = NULL, // TODO: 暂时还需要，平台更新版本后不用
    .wind_expect = NULL,
    .recv_process = NULL,
    .send_complete = NULL, // TODO: 实现msn隐藏 + 数组返回
    .recv_notify = NULL, // TODO: 实现数组返回
    .close_done = NULL,
    .try_close_done = NULL,
    .prepare_close_done = NULL,
    .conn_async_offload_done = NULL,
};

int get_fd_from_dtoe_conn(void *dtoe_conn)
{
    struct KNET_Fd *knet_user_data = dtoe_get_ulp_user_data(dtoe_conn);
    if (knet_user_data == NULL) {
        KNET_ERR("The dtoe_get_ulp_user_data return user data is null");
        return KNET_INVALID_FD;
    }

    int fd = knet_user_data->sockfd;
    if (!KNET_IsOsFdValid(fd)) {
        KNET_ERR("Conn fd is invalid");
        return KNET_INVALID_FD;
    }
    return fd;
}

void knet_reg_close_done(void *dtoe_conn)
{
    if (g_knet_dtoe_ops.close_done == NULL) {
        return;
    }

    if (dtoe_conn == NULL) {
        KNET_ERR("dtoe_conn is null in close_done");
        return;
    }

    int fd = get_fd_from_dtoe_conn(dtoe_conn);
    if (fd == KNET_INVALID_FD) {
        KNET_ERR("Close down fd is invalid");
        return;
    }
    KNET_INFO("Close down fd %d", fd);
    g_knet_dtoe_ops.close_done(fd);
}

void knet_reg_prepare_close_down(void *dtoe_conn)
{
    if (g_knet_dtoe_ops.prepare_close_done == NULL) {
        return;
    }

    if (dtoe_conn == NULL) {
        KNET_ERR("dtoe_conn is null in prepare_close_done");
        return;
    }

    int fd = get_fd_from_dtoe_conn(dtoe_conn);
    if (fd == KNET_INVALID_FD) {
        KNET_ERR("Close down fd is invalid");
        return;
    }
    KNET_INFO("Prepare close down fd %d", fd);
    g_knet_dtoe_ops.prepare_close_done(fd);
}

void knet_reg_conn_async_offload_done(void *dtoe_conn, uint8_t rsp_status)
{
    if (g_knet_dtoe_ops.conn_async_offload_done == NULL) {
        return;
    }

    if (dtoe_conn == NULL) {
        KNET_ERR("dtoe_conn is null in conn_async_offload_done");
        return;
    }

    int fd = get_fd_from_dtoe_conn(dtoe_conn);
    if (fd == KNET_INVALID_FD) {
        KNET_ERR("Close down fd is invalid");
        return;
    }
    KNET_INFO("Conn async offload done fd %d", fd);
    g_knet_dtoe_ops.conn_async_offload_done(fd, rsp_status);
}

void knet_ulp_ops_register(struct knet_ulp_ops *ops)
{
    if (ops == NULL) {
        KNET_ERR("KNET ulp ops is NULL");
        return;
    }

    g_knet_dtoe_ops.close_done = ops->close_done;
    g_knet_dtoe_ops.prepare_close_done = ops->prepare_close_done;
    g_knet_dtoe_ops.conn_async_offload_done = ops->conn_async_offload_done;

    g_dtoe_ops.close_done = knet_reg_close_done,
    g_dtoe_ops.prepare_close_done = knet_reg_prepare_close_down,
    g_dtoe_ops.conn_async_offload_done = knet_reg_conn_async_offload_done,

    dtoe_ulp_ops_register(DTOE_ULP_DTOE_TCP, &g_dtoe_ops);
}

KNET_API int knet_init(const char * local_ip)
{
    KNET_LogInit();
    KNET_FdInit();

    int ret = dtoe_ulp_config_set(KNET_DTOE_MAX_CHANL_CONFIG, 0);
    if (ret != 0) {
        KNET_ERR("Dtoe init failed, ret %d", ret);
        goto free;
    }
    KNET_INFO("Dtoe ulp config set success");
    ret = dtoe_init();
    if (ret != 0) {
        KNET_ERR("Dtoe init failed, ret %d", ret);
        goto free;
    }
    KNET_INFO("Dtoe int success");
    ret = dtoe_bind_addr(local_ip, &g_dtoeRes.dev.devSn, &g_dtoeRes.dev.numaId);
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
    dtoe_uninit();
free:
    KNET_FdDeinit();
    KNET_LogUninit();
    return ret;
}

KNET_API void knet_uninit(void)
{
    dtoe_uninit();
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

    int ret = dtoe_reg_mr(g_dtoeRes.dev.devSn, addr, length, (struct dtoe_mr_s *)dmr);
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
    int ret = dtoe_unreg_mr(g_dtoeRes.dev.devSn, (struct dtoe_mr_s *)dmr);
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
    if (channel == NULL) {
        KNET_ERR("Channel is null in knet_create_send_channel");
        return -1;
    }
    struct KnetSendChannel *knetSendChannel = (struct KnetSendChannel *)malloc(sizeof(struct KnetSendChannel));
    if (knetSendChannel == NULL) {
        KNET_ERR("Knet malloc send channel failed");
        return -1;
    }
    int ret = dtoe_create_send_channel(g_dtoeRes.dev.devSn, schd_mod, DTOE_ULP_DTOE_TCP, depth, (send_channel_s *)(&knetSendChannel->channel));
    if (ret != 0) {
        free(knetSendChannel);
        KNET_ERR("Knet create send channel failed, devSn %lu, schd_mod %d, depth %u, ret %d", g_dtoeRes.dev.devSn, schd_mod, depth, ret);
        return ret;
    }
    *channel = &knetSendChannel->channel;
    KNET_INFO("Knet create send channel success, devSn %lu, schd_mod %d, depth %u, ret %d", g_dtoeRes.dev.devSn, schd_mod, depth, ret);
    return ret;
}

int knet_destroy_send_channel(struct knet_send_channel *channel)
{
    int ret = dtoe_destroy_send_channel((send_channel_s *)channel);
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
    if (channel == NULL) {
        KNET_ERR("Channel is null in knet_create_recv_channel");
        return -1;
    }
    struct KnetRecvChannel *knetRecvChannel = (struct KnetRecvChannel *)malloc(sizeof(struct KnetRecvChannel));
    if (knetRecvChannel == NULL) {
        KNET_ERR("Knet malloc recv channel failed");
        return -1;
    }
    int ret = dtoe_create_receive_channel(g_dtoeRes.dev.devSn, schd_mod, DTOE_ULP_DTOE_TCP, depth, (recv_channel_s *)(&knetRecvChannel->channel));
    if (ret != 0) {
        free(knetRecvChannel);
        KNET_ERR("Knet create recv channel failed, devSn %lu, schd_mod %d, depth %u, ret %d", g_dtoeRes.dev.devSn, schd_mod, depth, ret);
        return ret;
    }
    *channel = &knetRecvChannel->channel;
    KNET_INFO("Knet create recv channel success, devSn %lu, schd_mod %d, depth %u, ret %d", g_dtoeRes.dev.devSn, schd_mod, depth, ret);
    return ret;
}

int knet_destroy_recv_channel(struct knet_recv_channel *channel)
{
    int ret = dtoe_destroy_receive_channel((recv_channel_s *)channel);
    if (ret != 0) {
        KNET_ERR("Dtoe destroy recv channel failed, ret %d", ret);
        return ret;
    }

    free((struct KnetRecvChannel *)channel); // channel的地址即为knetRecvChannel的地址
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

    dtoe_offload_in_s input = {0};
    input.ulp_type = DTOE_ULP_DTOE_TCP;
    input.version = 0;
    input.user_data = (void*)KNET_GetFdConnUserData(sockfd);
    input.conn_keep = 0;
    input.rq = in->recv_channel->rx_queue;
    input.sq = in->send_channel->tx_queue;

    dtoe_offload_out_s out = {0};
    int ret = dtoe_start_chimney_general(sockfd, &input, &out);
    if (ret != 0) {
        KNET_ERR("Dtoe start chimney general failed, sockfd %d, ret %d", sockfd, ret);
        return ret;
    }

    KNET_SetFdState(sockfd, in, &out);
    KNET_INFO("Knet start chimney general success, sockfd %d", sockfd);
    return ret;
}

void knet_recv_mem_loopback(struct knet_dtoe_iovec *iov, int iov_cnt)
{
    dtoe_recv_mem_loopback((struct dtoe_iovec *)iov, iov_cnt);
}

void knet_prepare_close(int sockfd)
{
    if (!KNET_IsOsFdValid(sockfd)) {
        KNET_ERR("Knet prepare close sockfd %d is invalid", sockfd);
        return;
    }
    dtoe_prepare_close(KNET_GetConnBySock(sockfd));
}

void knet_close(int sockfd)
{
    if (!KNET_IsOsFdValid(sockfd)) {
        KNET_ERR("Knet close sockfd %d is invalid", sockfd);
        return;
    }
    KNET_ResetFdState(sockfd);
    dtoe_close(sockfd, KNET_GetConnBySock(sockfd));
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
    if (!KNET_IsOsFdValid(sockfd)) {
        KNET_ERR("Knet get ulp user data sockfd %d is invalid", sockfd);
        return NULL;
    }
    return KNET_GetFdConnUserData(sockfd)->user_data;
}
