/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/epoll.h>
#include <poll.h>
#include <dlfcn.h>

#include "common.h"
#include "mock.h"
#include "knet_tcp_symbols.h"
#include "dp_zcopy_api.h"
#include "knet_log.h"

/* DP_Deinit declared in inner header, forward-declare here */
extern "C" void DP_Deinit(int slave);

/* 不依赖落地的/usr/lib64/libdpstack.so:对realpath/dlopen/dlsym/dlclose打桩,
 * KnetInitDpSymbols()在伪handle上成功,g_dP*指针指向占位桩DpStubFn,
 * 调用各DP_* wrapper函数转发到占位桩安全返回,覆盖knet_tcp_symbols.c中61个转发函数体 */
static void DpStubFn(void) { }

static char *MockDpstackRealpath(const char *path, char *resolved)
{
    (void)path; (void)resolved;
    /* malloc'd,KnetInitSymbols内会free() */
    char *p = (char *)malloc(32);
    if (p != NULL) {
        (void)strcpy(p, "/tmp/__dpstack_fake__.so");
    }
    return p;
}

static void *MockDpstackDlopen(const char *file, int flag)
{
    (void)file; (void)flag;
    return (void *)0x1;              /* 伪handle,非NULL即视为dlopen成功 */
}

static void *MockDpstackDlsym(void *handle, const char *sym)
{
    (void)handle; (void)sym;
    return (void *)DpStubFn;         /* 61个符号统一解析到占位桩 */
}

static int MockDpstackDlclose(void *handle)
{
    (void)handle;
    return 0;                        /* 伪handle无需dlclose */
}

/* 仅作用于SYMBOLS类用例的fixture:在SetUp/TearDown中安装/卸载4个libc桩,
 * 避免污染其它用例;打桩是入口点替换,Delete后即恢复 */
class SymbolsMockTest : public TestBase {
public:
    virtual void SetUp(void);
    virtual void TearDown(void);
private:
    KTestMock *m_mock = NULL;
};

void SymbolsMockTest::SetUp(void)
{
    TestBase::SetUp();
    m_mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(m_mock, NULL);
    m_mock->Create(realpath, MockDpstackRealpath);
    m_mock->Create(dlopen, MockDpstackDlopen);
    m_mock->Create(dlsym, MockDpstackDlsym);
    m_mock->Create(dlclose, MockDpstackDlclose);
}

void SymbolsMockTest::TearDown(void)
{
    if (m_mock != NULL) {
        m_mock->Delete(realpath);
        m_mock->Delete(dlopen);
        m_mock->Delete(dlsym);
        m_mock->Delete(dlclose);
        DeleteMock(m_mock);
        m_mock = NULL;
    }
    TestBase::TearDown();
}

/**
 * @brief 调用所有DP_Posix* socket wrapper函数
 */
TEST_F(SymbolsMockTest, SYMBOLS_TEST_DP_POSIX_SOCKET_WRAPPERS)
{
    KnetInitDpSymbols();

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    socklen_t addrlen = sizeof(addr);
    char buf[64] = {0};
    struct iovec iov = {0};
    struct msghdr msg = {0};

    /* socket创建/关闭 */
    (void)DP_PosixSocket(AF_INET, SOCK_STREAM, 0);
    (void)DP_PosixClose(-1);

    /* listen/bind/connect */
    (void)DP_PosixListen(-1, 5);
    (void)DP_PosixBind(-1, (const struct sockaddr *)&addr, addrlen);
    (void)DP_PosixConnect(-1, (const struct sockaddr *)&addr, addrlen);

    /* getpeername/getsockname */
    (void)DP_PosixGetpeername(-1, (struct sockaddr *)&addr, &addrlen);
    (void)DP_PosixGetsockname(-1, (struct sockaddr *)&addr, &addrlen);

    /* 数据收发 */
    (void)DP_PosixSend(-1, buf, sizeof(buf), 0);
    (void)DP_PosixSendto(-1, buf, sizeof(buf), 0, (const struct sockaddr *)&addr, addrlen);
    (void)DP_PosixWritev(-1, &iov, 1);
    (void)DP_PosixSendmsg(-1, &msg, 0);
    (void)DP_PosixRecv(-1, buf, sizeof(buf), 0);
    (void)DP_PosixRecvfrom(-1, buf, sizeof(buf), 0, (struct sockaddr *)&addr, &addrlen);
    (void)DP_PosixRecvmsg(-1, &msg, 0);
    (void)DP_PosixReadv(-1, &iov, 1);
    (void)DP_PosixRead(-1, buf, sizeof(buf));
    (void)DP_PosixWrite(-1, buf, sizeof(buf));

    /* sockopt */
    (void)DP_PosixGetsockopt(-1, SOL_SOCKET, SO_REUSEADDR, buf, &addrlen);
    (void)DP_PosixSetsockopt(-1, SOL_SOCKET, SO_REUSEADDR, buf, sizeof(buf));

    /* accept/shutdown */
    (void)DP_PosixAccept(-1, (struct sockaddr *)&addr, &addrlen);
    (void)DP_PosixShutdown(-1, SHUT_RD);

    /* fcntl/ioctl */
    (void)DP_PosixFcntl(-1, F_GETFL, 0);
    (void)DP_PosixIoctl(-1, 0, buf);

    /* poll/epoll */
    struct pollfd pfd = {0};
    (void)DP_PosixPoll(&pfd, 1, 0);
    struct epoll_event ev = {0};
    (void)DP_PosixEpollCtl(-1, EPOLL_CTL_ADD, -1, &ev);
    (void)DP_PosixEpollWait(-1, &ev, 1, 0);
}

/**
 * @brief 调用所有Hook注册类wrapper函数
 */
TEST_F(SymbolsMockTest, SYMBOLS_TEST_DP_HOOK_WRAPPERS)
{
    KnetInitDpSymbols();

    DP_MemHooks_S memHooks = {0};
    (void)DP_MemHookReg(&memHooks);

    DP_MempoolHooks_S mpHooks = {0};
    (void)DP_MempoolHookReg(&mpHooks);

    DP_RandomHooks_S randHook = {0};
    (void)DP_RandIntHookReg(&randHook);

    (void)DP_RegGetSelfWorkerIdHook(NULL);

    (void)DP_ClockReg(NULL);

    DP_HashTblHooks_t hashHooks = {0};
    (void)DP_HashTblHooksReg(&hashHooks);

    (void)DP_LogHookReg(NULL);
    DP_LogLevelSet((DP_LogLevel_E)0);

    DP_SemHooks_S semHooks = {0};
    (void)DP_SemHookReg(&semHooks);

    (void)DP_DebugShowHookReg(NULL);

    DP_AddrHooks_t addrHooks = {0};
    (void)DP_AddrHooksReg(&addrHooks);

    DP_AddrBindHooks_t bindHooks = {0};
    (void)DP_AddrBindHooksReg(&bindHooks);
}

/**
 * @brief 调用其余wrapper函数(Init/Deinit/Cfg/Show/Netdev/Zcopy等)
 */
TEST_F(SymbolsMockTest, SYMBOLS_TEST_DP_MISC_WRAPPERS)
{
    KnetInitDpSymbols();

    /* Show/Statistics */
    DP_ShowStatistics((DP_StatType_t)0, 0, 0);

    /* Socket info */
    DP_SocketState_t state = {0};
    (void)DP_GetSocketState(-1, &state);
    DP_SockDetails_t details = {0};
    (void)DP_GetSocketDetails(-1, &details);
    DP_EpollDetails_t epDetails = {0};
    int wid = 0;
    (void)DP_GetEpollDetails(-1, &epDetails, 1, &wid);
    (void)DP_SocketCountGet(0);

    /* Init/Deinit */
    (void)DP_Init(0);
    DP_Deinit(0);

    /* CPD */
    (void)DP_CpdInit();
    (void)DP_CpdRunOnce(0);
    DP_RunWorkerOnce(0);

    /* Netdev */
    DP_NetdevCfg_t netdevCfg = {0};
    (void)DP_CreateNetdev(&netdevCfg);
    (void)DP_ProcIfreq(NULL, 0, NULL);

    /* Route config */
    (void)DP_RtCfg((DP_RtOpt_t)0, NULL, NULL, 0);

    /* Epoll create notify */
    (void)DP_EpollCreateNotify(1, NULL);

    /* Poll create/destroy notify */
    struct pollfd pfd = {0};
    (void)DP_PollCreateNotify(&pfd, 1, NULL, NULL);
    DP_PollDestroyNotify(NULL);

    /* Zcopy */
    (void)DP_ZcopyAlloc(64);
    DP_ZcopyFree(NULL);
    (void)DP_ZWritev(-1, NULL, 1, 64);
    (void)DP_ZReadv(-1, NULL, 1);

    /* Cfg */
    DP_CfgKv_t kv;
    memset(&kv, 0, sizeof(kv));
    (void)DP_Cfg(&kv, 1);

    /* CpdQue */
    (void)DP_CpdQueHooksReg(NULL);

    /* NetdevQueMap */
    uint32_t queMap = 0;
    (void)DP_GetNetdevQueMap(0, 0, &queMap, 1);
}

/**
 * @brief KnetDeinitDpSymbols: handle非空时执行dlclose路径
 */
TEST_F(SymbolsMockTest, SYMBOLS_TEST_SYMBOLS_DEINIT_NONNULL)
{
    KnetInitDpSymbols();
    KnetDeinitDpSymbols();
}

/**
 * @brief KnetDeinitDpSymbols: handle为空时直接返回路径
 */
TEST_F(SymbolsMockTest, SYMBOLS_TEST_SYMBOLS_DEINIT_NULL)
{
    /* 先deinit确保handle为NULL */
    KnetDeinitDpSymbols();
    /* 再次deinit, handle已为NULL, 跳过dlclose直接返回 */
    KnetDeinitDpSymbols();
}
