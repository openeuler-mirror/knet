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
#include "dp_zcopy_api.h"

#include "dp_socket.h"
#include "dp_errno.h"
#include "utils_log.h"
#include "pbuf.h"
#include "sock.h"

ssize_t DP_ZWritev(int sockfd, const struct DP_ZIovec* iov, int iovcnt)
{
    struct DP_ZMsghdr msg;

    if (UTILS_UNLIKELY(iovcnt < 0 || iovcnt > MAX_IOV_CNT)) {
        DP_SET_ERRNO(EINVAL);
        DP_LOG_DBG("Sk zero copy writev failed, iov cnt is invalid, iovcnt = %d.", iovcnt);
        return -1;
    }

    if (UTILS_UNLIKELY(iovcnt == 0)) {
        return 0;
    }

    if (UTILS_UNLIKELY(iov == NULL)) {
        DP_SET_ERRNO(EFAULT);
        DP_LOG_DBG("Sk zero copy writev failed, iov invalid.");
        return -1;
    }

    msg.msg_name       = NULL;
    msg.msg_namelen    = 0;
    msg.msg_control    = NULL;
    msg.msg_controllen = 0;
    msg.msg_iov        = (struct DP_ZIovec*)iov;
    msg.msg_iovlen     = (size_t)iovcnt;

    return DP_ZSendmsg(sockfd, &msg, 0);
}

ssize_t DP_ZReadv(int sockfd, struct DP_ZIovec* iov, int iovcnt)
{
    struct DP_ZMsghdr msg;
    if (UTILS_UNLIKELY(iovcnt == 0)) {
        return 0;
    }

    if (UTILS_UNLIKELY(iov == NULL)) {
        DP_SET_ERRNO(EFAULT);
        DP_LOG_DBG("Sk zero copy readv failed, iov invalid.");
        return -1;
    } else if (UTILS_UNLIKELY(iovcnt > MAX_IOV_CNT || iovcnt < 0)) {
        DP_SET_ERRNO(EINVAL);
        DP_LOG_DBG("Sk zero copy readv failed, iov cnt too big, iovcnt = %d.", iovcnt);
        return -1;
    }

    msg.msg_name       = NULL;
    msg.msg_namelen    = 0;
    msg.msg_control    = NULL;
    msg.msg_controllen = 0;
    msg.msg_iov        = (struct DP_ZIovec*)iov;
    msg.msg_iovlen     = (size_t)iovcnt;

    return DP_ZRecvmsg(sockfd, &msg, 0);
}

void* DP_ZcopyAlloc(size_t size)
{
    if (CFG_GET_VAL(DP_CFG_ZERO_COPY) == 0) {
        DP_LOG_DBG("Zero copy iovec alloc failed, zero copy not enable.");
        return NULL;
    }

    if (size > (size_t)CFG_GET_VAL(DP_CFG_ZBUF_LEN_MAX)) {
        DP_SET_ERRNO(EINVAL);
        DP_LOG_DBG("Extern buffer alloc failed, alloc size exceeds limit, iovlen = %zu.", size);
        return NULL;
    }

    void* ebuf = PBUF_ExtBufAlloc();
    if (ebuf == NULL) {
        DP_SET_ERRNO(ENOMEM);
        DP_LOG_ERR("Extern buffer alloc failed, reg hook fail or no memory.");
        return NULL;
    }

    return ebuf;
}

void DP_ZcopyFree(void* addr)
{
    if (CFG_GET_VAL(DP_CFG_ZERO_COPY) == 0) {
        DP_LOG_DBG("Zero copy iovec free failed, zero copy not enable.");
        return;
    }

    PBUF_ExtBufFree(addr);
    return;
}