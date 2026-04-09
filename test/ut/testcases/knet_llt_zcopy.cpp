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


#include "securec.h"

#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <dlfcn.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <linux/if_arp.h>
#include <linux/if_tun.h>
#include <linux/if.h>
#include <sys/uio.h>

#include "knet_config.h"
#include "knet_log.h"
#include "knet_socket_api.h"

#include "tcp_fd.h"
#include "dp_zcopy_api.h"
#include "dp_pbuf_api.h"
#include "common.h"
#include "mock.h"

extern bool g_tcpInited;

DTEST_CASE_F(ZCOPY, TEST_IOV_ALLOC_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    void *ebuf = knet_mp_alloc(0);
    DT_ASSERT_EQUAL(ebuf, NULL);

    Mock->Create(DP_ZcopyAlloc, TEST_GetFuncRetPositive(1));
    g_tcpInited = true;
    ebuf = knet_mp_alloc(1);
    DT_ASSERT_NOT_EQUAL(ebuf, NULL);
    
    g_tcpInited = false;
    Mock->Delete(DP_ZcopyAlloc);
    DeleteMock(Mock);
}

static void FakeFreeCb(void *addr)
{
    (void)addr;
    return;
}

DTEST_CASE_F(ZCOPY, TEST_IOV_FREE_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    knet_mp_free(NULL, NULL);

    Mock->Create(DP_ZcopyFree, TEST_GetFuncRetPositive(0));
    g_tcpInited = true;
    knet_mp_free(NULL, NULL);
    
    g_tcpInited = false;
    Mock->Delete(DP_ZcopyFree);
    DeleteMock(Mock);
}

static bool KNET_IsFdHijackMock(int osFd)
{
    return true;
}

DTEST_CASE_F(ZCOPY, TEST_ZWRITEV_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    uint32_t ret;
    struct knet_iovec iov = {0};
    ret = knet_zwritev(0, NULL, 0);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);

    Mock->Create(KNET_IsFdHijack, KNET_IsFdHijackMock);
    Mock->Create(DP_ZWritev, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    g_tcpInited = true;
    ret = knet_zwritev(1, &iov, 1);
    DT_ASSERT_EQUAL(ret, KNET_OK);
    
    g_tcpInited = false;
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(DP_ZWritev);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}

DTEST_CASE_F(ZCOPY, TEST_ZREADV_NORMAL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    uint32_t ret;
    struct knet_iovec iov = {0};
    ret = knet_zreadv(0, NULL, 0);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);

    Mock->Create(KNET_IsFdHijack, KNET_IsFdHijackMock);
    Mock->Create(DP_ZReadv, TEST_GetFuncRetPositive(0));
    Mock->Create(KNET_OsFdToDpFd, TEST_GetFuncRetPositive(0));
    g_tcpInited = true;
    ret = knet_zreadv(1, &iov, 1);
    DT_ASSERT_EQUAL(ret, KNET_OK);

    if (iov.free_cb != NULL) {
        iov.free_cb(iov.iov_base, iov.opaque);
    }
    
    g_tcpInited = false;
    Mock->Delete(KNET_OsFdToDpFd);
    Mock->Delete(DP_ZReadv);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}

static bool KNET_IsFdHijackMockNegative(int osFd)
{
    return false;
}

DTEST_CASE_F(ZCOPY, TEST_ZWRITEV_NOT_HIJACK, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    uint32_t ret;
    struct knet_iovec iov = {0};
    ret = knet_zwritev(0, NULL, 0);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);

    ssize_t (*osWritev)(int fd, const struct iovec *iov, int iovcnt) = dlsym(g_sysLibcHandle, "writev");

    Mock->Create(KNET_IsFdHijack, KNET_IsFdHijackMockNegative);
    Mock->Create(osWritev, TEST_GetFuncRetPositive(0));
    g_tcpInited = true;
    ret = knet_zwritev(1, &iov, 1);
    DT_ASSERT_EQUAL(ret, KNET_OK);
    
    g_tcpInited = false;
    Mock->Delete(osWritev);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}

DTEST_CASE_F(ZCOPY, TEST_ZREADV_NOT_HIJACK, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);

    uint32_t ret;
    struct knet_iovec iov = {0};
    ret = knet_zreadv(0, NULL, 0);
    DT_ASSERT_EQUAL(ret, KNET_ERROR);

    void *g_sysLibcHandle = dlopen("/usr/lib64/libc.so.6", RTLD_NOW | RTLD_GLOBAL);
    ssize_t (*osReadv)(int fd, const struct iovec *iov, int iovcnt) = dlsym(g_sysLibcHandle, "readv");

    Mock->Create(KNET_IsFdHijack, KNET_IsFdHijackMockNegative);
    Mock->Create(osReadv, TEST_GetFuncRetPositive(0));
    g_tcpInited = true;
    ret = knet_zreadv(1, &iov, 1);
    DT_ASSERT_EQUAL(ret, KNET_OK);

    if (iov.free_cb != NULL) {
        iov.free_cb(iov.iov_base, iov.opaque);
    }
    
    g_tcpInited = false;
    Mock->Delete(osReadv);
    Mock->Delete(KNET_IsFdHijack);
    DeleteMock(Mock);
}