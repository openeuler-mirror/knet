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
#include "knet_socket_api.h"
#include "knet_log.h"
#include "knet_signal_tcp.h"
#include "knet_tcp_api_init.h"
#include "knet_fmm.h"
#include "knet_types.h"
#include "tcp_fd.h"
#include "dp_zcopy_api.h"
#include "dp_pbuf_api.h"

#define ZCOPY_IOV_LEN_MAX (512 * 1024)
#define ZCOPY_IOV_CNT_MAX 1024

void *knet_mp_alloc(size_t size)
{
    if (!g_tcpInited) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "K-NET zero copy read buffer alloc failed, tcp is not initialized");
        return NULL;
    }

    BEFORE_DPFUNC();
    void *ret = DP_ZcopyAlloc(size);
    AFTER_DPFUNC();
    if (ret == NULL) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "K-NET zero copy read buffer alloc failed, DP zcopy buffer alloc failed, "
            "size %zu", size);
        return NULL;
    }

    return ret;
}

void knet_mp_free(void *addr, void *opaque)
{
    (void)opaque;

    if (!g_tcpInited) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "K-NET zero copy read buffer free failed, tcp is not initialized");
        return;
    }

    BEFORE_DPFUNC();
    DP_ZcopyFree(addr);
    AFTER_DPFUNC();

    return;
}

static ssize_t KnetZWritevNotHijackPath(int sockfd, const struct knet_iovec *iov, int iovcnt)
{
    if (iovcnt < 0 || iovcnt > ZCOPY_IOV_CNT_MAX) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "K-NET zero copy writev failed, iovcnt %d is invalid", iovcnt);
        return -1;
    }

    if (iov == NULL) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "K-NET zero copy writev failed, iov invalid");
        return -1;
    }

    struct iovec posixIov[iovcnt];
    for (int i = 0; i < iovcnt; ++i) {
        posixIov[i].iov_base = iov[i].iov_base;
        posixIov[i].iov_len = iov[i].iov_len;
    }
    KNET_CHECK_AND_GET_OS_API(g_origOsApi.writev, KNET_INVALID_FD);
    ssize_t ret = g_origOsApi.writev(sockfd, posixIov, iovcnt);
    if (ret <= 0) {
        return ret;
    }

    for (int i = 0; i < iovcnt; ++i) {
        if (iov[i].free_cb != NULL) {
            iov[i].free_cb(iov[i].iov_base, iov[i].opaque);
        }
    }
    return ret;
}

ssize_t knet_zwritev(int sockfd, const struct knet_iovec *iov, int iovcnt)
{
    if (!g_tcpInited) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "K-NET zero copy writev failed, tcp is not initialized");
        return -1;
    }

    if (!KNET_IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d zwritev is not hijacked", sockfd);
        return KnetZWritevNotHijackPath(sockfd, iov, iovcnt);
    }

    ssize_t totalLen = 0;
    struct KNET_ExtBuf *ebuf = NULL;
    for (int i = 0; i < iovcnt; ++i) {
        if (iov[i].iov_base == NULL) {
            continue;
        }

        ebuf = KnetPtrSub(iov[i].iov_base, sizeof(struct KNET_ExtBuf));
        ebuf->addr = iov[i].iov_base;
        ebuf->freeCb = iov[i].free_cb;
        ebuf->opaque = iov[i].opaque;
        totalLen += iov[i].iov_len;
    }
    
    BEFORE_DPFUNC();
    ssize_t ret = DP_ZWritev(KNET_OsFdToDpFd(sockfd), (const struct DP_ZIovec *)iov, iovcnt, totalLen);
    AFTER_DPFUNC();
    if (ret < 0 && errno != EAGAIN) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "K-NET zero copy writev failed, osFd %d dpFd %d DP_ZWritev ret %zd,"
            "errno %d, %s, iovcnt %d", sockfd, KNET_OsFdToDpFd(sockfd), ret, errno, strerror(errno), iovcnt);
    }

    return ret;
}

static void IovFreeCb(void *addr, void *opaque)
{
    (void)addr;
    free(opaque);
}

ssize_t knet_zreadv(int sockfd, struct knet_iovec *iov, int iovcnt)
{
    ssize_t ret = 0;
    if (!g_tcpInited) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "K-NET zero copy readv failed, tcp is not initialized");
        return -1;
    }

    if (!KNET_IsFdHijack(sockfd)) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_DEBUG, "Fd %d zreadv is not hijacked", sockfd);

        if (iovcnt < 0 || iovcnt > ZCOPY_IOV_CNT_MAX) {
            KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "K-NET zero copy readv failed, iovcnt %d is invalid", iovcnt);
            return -1;
        }

        if (iov == NULL) {
            KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "K-NET zero copy readv failed, iov invalid");
            return -1;
        }

        struct iovec posixIov[iovcnt];
        int i;
        for (i = 0; i < iovcnt; ++i) {
            iov[i].iov_base = malloc(ZCOPY_IOV_LEN_MAX);
            if (iov[i].iov_base == NULL) {
                break;
            }
            iov[i].iov_len = ZCOPY_IOV_LEN_MAX;
            iov[i].opaque = iov[i].iov_base;
            iov[i].free_cb = IovFreeCb;

            posixIov[i].iov_base = iov[i].iov_base;
            posixIov[i].iov_len = iov[i].iov_len;
        }
        for (; i < iovcnt; ++i) {
            posixIov[i].iov_base = NULL;
            posixIov[i].iov_len = 0;
        }
        KNET_CHECK_AND_GET_OS_API(g_origOsApi.readv, KNET_INVALID_FD);
        return g_origOsApi.readv(sockfd, posixIov, i);
    }

    BEFORE_DPFUNC();
    ret = DP_ZReadv(KNET_OsFdToDpFd(sockfd), (struct DP_ZIovec *)iov, iovcnt);
    AFTER_DPFUNC();
    if (ret < 0 && errno != EAGAIN) {
        KNET_LOG_LINE_LIMIT(KNET_LOG_ERR, "K-NET zero copy readv failed, osFd %d dpFd %d DP_ZReadv ret %zd,"
            "errno %d,%s, iovcnt %d", sockfd, KNET_OsFdToDpFd(sockfd), ret, errno, strerror(errno), iovcnt);
    }

    return ret;
}