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
#define _GNU_SOURCE
#include <dlfcn.h>
#include "knet_lock.h"
#include "knet_osapi.h"

#define SYS_LIBC_NAME "/usr/lib64/libc.so.6"

struct OsApi g_origOsApi = {0};

static void* g_sysLibcHandle = {0};

static int AssignDlopen(void)
{
    if (g_sysLibcHandle != NULL) {
        return 0;
    }
    g_sysLibcHandle = dlopen(SYS_LIBC_NAME, RTLD_NOW | RTLD_GLOBAL);
    if (g_sysLibcHandle == NULL) {
        KNET_ERR("Load So file failed, %s.", dlerror());
        return -1;
    }
    return 0;
}

void AssignDlsym(void **ptr, const char *name)
{
#ifndef KNET_TEST
    if (AssignDlopen() != 0) {
        *ptr = NULL;
        return;
    }
    *ptr = dlsym(g_sysLibcHandle, name);
#else
    *ptr = dlsym(RTLD_NEXT, name);
#endif
}

void OsGetOrigFunc(struct OsApi *osapi)
{
    GET_ORIG_FUNC(socket, osapi);
    GET_ORIG_FUNC(listen, osapi);
    GET_ORIG_FUNC(bind, osapi);
    GET_ORIG_FUNC(connect, osapi);
    GET_ORIG_FUNC(getpeername, osapi);
    GET_ORIG_FUNC(getsockname, osapi);
    GET_ORIG_FUNC(send, osapi);
    GET_ORIG_FUNC(sendto, osapi);
    GET_ORIG_FUNC(writev, osapi);
    GET_ORIG_FUNC(sendmsg, osapi);
    GET_ORIG_FUNC(recv, osapi);
    GET_ORIG_FUNC(recvfrom, osapi);
    GET_ORIG_FUNC(recvmsg, osapi);
    GET_ORIG_FUNC(readv, osapi);
    GET_ORIG_FUNC(getsockopt, osapi);
    GET_ORIG_FUNC(setsockopt, osapi);
    GET_ORIG_FUNC(accept, osapi);
    GET_ORIG_FUNC(accept4, osapi);
    GET_ORIG_FUNC(close, osapi);
    GET_ORIG_FUNC(shutdown, osapi);
    GET_ORIG_FUNC(read, osapi);
    GET_ORIG_FUNC(write, osapi);
    GET_ORIG_FUNC(epoll_create, osapi);
    GET_ORIG_FUNC(epoll_create1, osapi);
    GET_ORIG_FUNC(epoll_ctl, osapi);
    GET_ORIG_FUNC(epoll_wait, osapi);
    GET_ORIG_FUNC(epoll_pwait, osapi);
    GET_ORIG_FUNC(fcntl, osapi);
    GET_ORIG_FUNC(fcntl64, osapi);
    GET_ORIG_FUNC(poll, osapi);
    GET_ORIG_FUNC(ppoll, osapi);
    GET_ORIG_FUNC(select, osapi);
    GET_ORIG_FUNC(pselect, osapi);
    GET_ORIG_FUNC(ioctl, osapi);
    GET_ORIG_FUNC(fork, osapi);
    GET_ORIG_FUNC(sigaction, osapi);
    GET_ORIG_FUNC(signal, osapi);
}

void GetOrigFunc(void)
{
    static KNET_SpinLock lock = {
        .value = KNET_SPIN_UNLOCKED_VALUE,
    };

    KNET_SpinlockLock(&lock);
    OsGetOrigFunc(&g_origOsApi); // 加入函数
    KNET_SpinlockUnlock(&lock);
}