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

#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>

#include "rte_ethdev.h"
#include "knet_mock.h"
#include "knet_types.h"
#include "knet_log.h"
#include "knet_dpdk_init.h"
#include "securec.h"
#include "common.h"
#include "mock.h"

extern "C" {
#include "knet_telemetry.h"
#define KNET_DPDK_DIR "/var/run/dpdk/"
#define KNET_DPDK_KNET_DIR "/var/run/dpdk/knet/"
#define KNET_DPDK_KNET_TELEMETRY "/var/run/dpdk/knet/dpdk_telemetry.v2"
#define BUFFER_SIZE 1024
#define PIPE_READ 0
#define PIPE_WRITE 1
#define MAX_PENDING_CONNECTIONS 2
#define READY_MSG_LENGTH 6
#define RESPONSE_SIZE 256
#define KNET_DPDK_DIRECTORY_PERMISSIONS 0755

pid_t g_pid;

struct TestPrivateData {
    int fd;
    int pipefdClose;
};

int32_t RegTelemetryCmd(void);
extern int32_t RegDpShowStatisticCmd(void);
extern int32_t DpdkRuntimeDirInit(void);
extern bool DpdkTelemetryJsonPidCheck(const char *buffer);
extern bool DpdkTelemetrySocketPidCheck(void);
extern int32_t DpdkTelemetryLinkCreate(void);
extern int32_t DpdkTelemetryFindSocket(void);
extern char g_knetTelemetrySocket[];
extern char g_knetTelemetrySocketNew[];

static union KNET_CfgValue g_cfg = {.intValue = 1};
static union KNET_CfgValue *MockKnetGetCfg(enum KNET_ConfKey key)
{
    (void)memset_s(&g_cfg, sizeof(g_cfg), 0, sizeof(g_cfg));
    g_cfg.intValue = 1;
    return &g_cfg;
}

static struct rte_memzone *MockRteMemzoneReserve(const char *name, size_t len, int socketId, unsigned flags)
{
    static struct rte_memzone mz = {0};
    static KNET_TelemetryInfo telemetryInfo = {0};
    mz.addr = (void *)&telemetryInfo;
    return &mz;
}

static int32_t CreateDirectory(const char *dir)
{
    if (access(dir, F_OK) == 0) {
        return 0;
    }

    if (mkdir(dir, KNET_DPDK_DIRECTORY_PERMISSIONS) == 0) {
        return 0;
    }

    return -1;
}

static void *CheckMainProcessQuit(void *arg)
{
    int ret = 0;
    struct TestPrivateData pData = *(struct TestPrivateData *)arg;
    int fd = pData.fd;
    int pipefdClose = pData.pipefdClose;

    char pipeBuffer[10];
    (void)read(pipefdClose, pipeBuffer, sizeof(pipeBuffer));
    printf("Received exit msg, child process exiting...\n");
    // shutdown主线程监听的fd后会触发accept返回
    shutdown(fd, SHUT_RDWR);
    close(fd);
    close(pipefdClose);
    return NULL;
}

static int32_t CreateSocket(const char *path, int pipeFd, int pipeFdClose)
{
    int32_t ret;
    int sockfd, clientSockfd;
    struct sockaddr_un addr;
    char response[RESPONSE_SIZE];
    pthread_t thread;
    struct TestPrivateData pData = { sockfd, pipeFdClose };

    sockfd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (sockfd == -1) {
        KNET_ERR("socket get sockfd failed");
        goto cleanup;
    }

    (void)memset_s(&addr, sizeof(addr), 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    ret = strncpy_s(addr.sun_path, sizeof(addr.sun_path), path, strlen(path));
    if (ret != 0) {
        KNET_ERR("socket strncpy_s failed, ret %d", ret);
        goto cleanup;
    }

    unlink(path);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) == -1) {
        KNET_ERR("socket bind failed, ret %d", ret);
        goto cleanup;
    }

    if (listen(sockfd, MAX_PENDING_CONNECTIONS) == -1) {
        KNET_ERR("socket listen failed, ret %d", ret);
        goto cleanup;
    }

    write(pipeFd, "READY", READY_MSG_LENGTH);

    pthread_create(&thread, NULL, CheckMainProcessQuit, &pData);

    while (1) {
        clientSockfd = accept(sockfd, NULL, NULL);
        if (clientSockfd == -1) {
            KNET_ERR("socket accept failed, ret %d", ret);
            goto cleanup;
        }

        ret = snprintf_s(response, RESPONSE_SIZE, RESPONSE_SIZE - 1, "{\"pid\": %d}", g_pid);
        if (ret < 0 || ret >= RESPONSE_SIZE) {
            KNET_ERR("failed to format response string, ret %d", ret);
            goto cleanup;
        }

        if (send(clientSockfd, response, strlen(response), 0) == -1) {
            KNET_ERR("socket send failed, ret %d", ret);
            close(clientSockfd);
            goto cleanup;
        }

        close(clientSockfd);
    }

cleanup:
    if (sockfd != -1) {
        close(sockfd);
    }
    return -1;
}
}
/* TEST_INIT_UNINIT_DPDK_TELEMETRY_SUCCESS removed: requires root to mkdir /var/run/dpdk */

DTEST_CASE_F(DPDK_TELEMETRY, TEST_TELEMETRY_REGCMD, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(rte_memzone_reserve, MockRteMemzoneReserve);
    Mock->Create(rte_telemetry_register_cmd, TEST_GetFuncRetPositive(0));
    int ret = RegTelemetryCmd();
    DT_ASSERT_EQUAL(ret, (int)KNET_OK);
    Mock->Delete(rte_telemetry_register_cmd);

    Mock->Create(rte_telemetry_register_cmd, TEST_GetFuncRetNegative(1));
    ret = RegTelemetryCmd();
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);
    Mock->Delete(rte_memzone_reserve);
    Mock->Delete(rte_telemetry_register_cmd);
    DeleteMock(Mock);
}

/* ===== knet_telemetry.c 错误路径覆盖测试 ===== */

static char *MockGetenvNull(const char *name)
{
    (void)name;
    return NULL;
}

static int MockLstatFail(const char *path, struct stat *buf)
{
    (void)path;
    (void)buf;
    return -1;
}

static int MockLstatSuccessSock(const char *path, struct stat *buf)
{
    (void)path;
    (void)memset(buf, 0, sizeof(*buf));
    buf->st_mode = S_IFSOCK;
    return 0;
}

static char *MockRealpathNull(const char *path, char *resolved)
{
    (void)path;
    (void)resolved;
    return NULL;
}

static union KNET_CfgValue g_cfgTele0 = {0};
static union KNET_CfgValue *MockKnetGetCfgTele0(enum KNET_ConfKey key)
{
    (void)key;
    g_cfgTele0.intValue = 0;
    return &g_cfgTele0;
}

static union KNET_CfgValue g_cfgTele1 = {.intValue = 1};
static union KNET_CfgValue *MockKnetGetCfgTele1(enum KNET_ConfKey key)
{
    (void)key;
    g_cfgTele1.intValue = 1;
    return &g_cfgTele1;
}

static struct rte_memzone *MockRteMemzoneReserveNull(const char *name, size_t len, int socketId, unsigned flags)
{
    (void)name;
    (void)len;
    (void)socketId;
    (void)flags;
    return NULL;
}

/**
 * @brief KNET_InitDpdkTelemetry: DpdkRuntimeDirInit失败(非root且无XDG_RUNTIME_DIR)
 *        覆盖 DpdkRuntimeDirInit 行105-106, KNET_InitDpdkTelemetry 行338-339
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_INIT_TELEMETRY_RUNTIME_DIR_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(getuid, TEST_GetFuncRetPositive(1));
    Mock->Create(getenv, MockGetenvNull);
    int32_t ret = KNET_InitDpdkTelemetry();
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);
    Mock->Delete(getuid);
    Mock->Delete(getenv);
    DeleteMock(Mock);
}

/**
 * @brief KNET_InitDpdkTelemetry: DpdkTelemetryFindSocket lstat失败
 *        覆盖 DpdkTelemetryFindSocket 行257-259, KNET_InitDpdkTelemetry 行342-345
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_INIT_TELEMETRY_FIND_SOCKET_LSTAT_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(getuid, TEST_GetFuncRetPositive(0));
    Mock->Create(lstat, MockLstatFail);
    int32_t ret = KNET_InitDpdkTelemetry();
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);
    Mock->Delete(getuid);
    Mock->Delete(lstat);
    DeleteMock(Mock);
}

/**
 * @brief KNET_InitDpdkTelemetry: DpdkTelemetrySocketPidCheck socket失败
 *        覆盖 DpdkTelemetrySocketPidCheck 行165-166, DpdkTelemetryFindSocket 行266
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_INIT_TELEMETRY_PID_CHECK_SOCKET_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(getuid, TEST_GetFuncRetPositive(0));
    Mock->Create(lstat, MockLstatSuccessSock);
    Mock->Create(socket, TEST_GetFuncRetNegative(1));
    int32_t ret = KNET_InitDpdkTelemetry();
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);
    Mock->Delete(getuid);
    Mock->Delete(lstat);
    Mock->Delete(socket);
    DeleteMock(Mock);
}

/**
 * @brief KNET_UninitDpdkTelemetry: telemetry未启用(intValue=0), 直接返回KNET_OK
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_UNINIT_TELEMETRY_DISABLED, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfgTele0);
    int32_t ret = KNET_UninitDpdkTelemetry();
    DT_ASSERT_EQUAL(ret, (int)KNET_OK);
    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

/**
 * @brief KNET_UninitDpdkTelemetry: realpath失败(g_knetTelemetrySocketNew不存在)
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_UNINIT_TELEMETRY_REALPATH_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfgTele1);
    Mock->Create(realpath, MockRealpathNull);
    int32_t ret = KNET_UninitDpdkTelemetry();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(KNET_GetCfg);
    Mock->Delete(realpath);
    DeleteMock(Mock);
}

/**
 * @brief RegTelemetryCmd: 多进程模式, TelemetryMzInit失败(rte_memzone_reserve返回NULL)
 *        覆盖 TelemetryMzInit 行274-275, RegTelemetryCmd 行312
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_REG_TELEMETRY_MZ_INIT_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    Mock->Create(rte_memzone_reserve, MockRteMemzoneReserveNull);
    int ret = RegTelemetryCmd();
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);
    Mock->Delete(rte_memzone_reserve);
    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);
}

/* ===== knet_telemetry.c 新增覆盖率测试 ===== */

static ssize_t MockRecvFail(int fd, void *buf, size_t len, int flags)
{
    (void)fd; (void)buf; (void)len; (void)flags;
    return -1;
}

static ssize_t MockRecvOk(int fd, void *buf, size_t len, int flags)
{
    (void)fd; (void)len; (void)flags;
    const char *json = "{\"pid\": 99999}";
    (void)memcpy_s(buf, len, json, strlen(json) + 1);
    return (ssize_t)strlen(json);
}

static int MockConnectFail(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    (void)sockfd; (void)addr; (void)addrlen;
    return -1;
}

static int MockConnectSuccess(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    (void)sockfd; (void)addr; (void)addrlen;
    return 0;
}

static int MockStrncpySFail(char *dest, size_t destMax, const char *src, size_t maxLen)
{
    (void)dest; (void)destMax; (void)src; (void)maxLen;
    return -1;
}

static int MockLinkFail(const char *oldpath, const char *newpath)
{
    (void)oldpath; (void)newpath;
    return -1;
}

static int MockUnlinkFail(const char *pathname)
{
    (void)pathname;
    return -1;
}

static int MockUnlinkSuccess(const char *pathname)
{
    (void)pathname;
    return 0;
}

static char *MockRealpathSuccess(const char *path, char *resolved)
{
    (void)path;
    return resolved;
}

static union KNET_CfgValue g_cfgMulti = {.intValue = 1};
static union KNET_CfgValue *MockKnetGetCfgMulti(enum KNET_ConfKey key)
{
    (void)key;
    g_cfgMulti.intValue = KNET_RUN_MODE_MULTIPLE;
    return &g_cfgMulti;
}

/**
 * @brief DpdkTelemetryJsonPidCheck: 各种JSON输入路径
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_TELEMETRY_JSON_PID_CHECK, NULL, NULL)
{
    /* 1. 无效JSON -> cJSON_Parse返回NULL */
    DT_ASSERT_EQUAL(DpdkTelemetryJsonPidCheck("not json"), false);
    DT_ASSERT_EQUAL(DpdkTelemetryJsonPidCheck(""), false);

    /* 2. 有效JSON但没有pid key */
    DT_ASSERT_EQUAL(DpdkTelemetryJsonPidCheck("{\"foo\":1}"), false);

    /* 3. 有效JSON但pid不是数字 */
    DT_ASSERT_EQUAL(DpdkTelemetryJsonPidCheck("{\"pid\":\"abc\"}"), false);

    /* 4. 有效JSON, pid不匹配当前进程 (用一个不可能的pid) */
    DT_ASSERT_EQUAL(DpdkTelemetryJsonPidCheck("{\"pid\":-1}"), false);

    /* 5. 有效JSON, pid匹配当前进程 */
    char jsonBuf[64];
    pid_t myPid = getpid();
    (void)snprintf(jsonBuf, sizeof(jsonBuf), "{\"pid\": %d}", (int)myPid);
    DT_ASSERT_EQUAL(DpdkTelemetryJsonPidCheck(jsonBuf), true);
}

/**
 * @brief DpdkTelemetrySocketPidCheck: socket失败路径
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_TELEMETRY_SOCKET_PID_CHECK_SOCKET_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(socket, TEST_GetFuncRetNegative(1));
    bool ret = DpdkTelemetrySocketPidCheck();
    DT_ASSERT_EQUAL(ret, false);
    Mock->Delete(socket);
    DeleteMock(Mock);
}

/**
 * @brief DpdkTelemetrySocketPidCheck: strncpy_s失败路径
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_TELEMETRY_SOCKET_PID_CHECK_STRNCPY_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(socket, TEST_GetFuncRetPositive(1));
    Mock->Create(strncpy_s, MockStrncpySFail);
    Mock->Create(close, TEST_GetFuncRetPositive(0));
    bool ret = DpdkTelemetrySocketPidCheck();
    DT_ASSERT_EQUAL(ret, false);
    Mock->Delete(socket);
    Mock->Delete(strncpy_s);
    Mock->Delete(close);
    DeleteMock(Mock);
}

/**
 * @brief DpdkTelemetrySocketPidCheck: connect失败路径
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_TELEMETRY_SOCKET_PID_CHECK_CONNECT_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(socket, TEST_GetFuncRetPositive(1));
    Mock->Create(connect, MockConnectFail);
    Mock->Create(close, TEST_GetFuncRetPositive(0));
    bool ret = DpdkTelemetrySocketPidCheck();
    DT_ASSERT_EQUAL(ret, false);
    Mock->Delete(socket);
    Mock->Delete(connect);
    Mock->Delete(close);
    DeleteMock(Mock);
}

/**
 * @brief DpdkTelemetrySocketPidCheck: recv失败路径
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_TELEMETRY_SOCKET_PID_CHECK_RECV_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(socket, TEST_GetFuncRetPositive(1));
    Mock->Create(connect, MockConnectSuccess);
    Mock->Create(recv, MockRecvFail);
    Mock->Create(close, TEST_GetFuncRetPositive(0));
    bool ret = DpdkTelemetrySocketPidCheck();
    DT_ASSERT_EQUAL(ret, false);
    Mock->Delete(socket);
    Mock->Delete(connect);
    Mock->Delete(recv);
    Mock->Delete(close);
    DeleteMock(Mock);
}

/**
 * @brief DpdkTelemetrySocketPidCheck: recv成功但pid不匹配
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_TELEMETRY_SOCKET_PID_CHECK_PID_MISMATCH, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(socket, TEST_GetFuncRetPositive(1));
    Mock->Create(connect, MockConnectSuccess);
    Mock->Create(recv, MockRecvOk);
    Mock->Create(close, TEST_GetFuncRetPositive(0));
    bool ret = DpdkTelemetrySocketPidCheck();
    DT_ASSERT_EQUAL(ret, false);  /* pid=99999 不匹配 */
    Mock->Delete(socket);
    Mock->Delete(connect);
    Mock->Delete(recv);
    Mock->Delete(close);
    DeleteMock(Mock);
}

/**
 * @brief DpdkTelemetryLinkCreate: socket失败
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_TELEMETRY_LINK_CREATE_SOCKET_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(socket, TEST_GetFuncRetNegative(1));
    int32_t ret = DpdkTelemetryLinkCreate();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(socket);
    DeleteMock(Mock);
}

/**
 * @brief DpdkTelemetryLinkCreate: strncpy_s失败
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_TELEMETRY_LINK_CREATE_STRNCPY_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(socket, TEST_GetFuncRetPositive(1));
    Mock->Create(strncpy_s, MockStrncpySFail);
    Mock->Create(close, TEST_GetFuncRetPositive(0));
    int32_t ret = DpdkTelemetryLinkCreate();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(socket);
    Mock->Delete(strncpy_s);
    Mock->Delete(close);
    DeleteMock(Mock);
}

/**
 * @brief DpdkTelemetryLinkCreate: connect成功(socket已激活) -> -EADDRINUSE
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_TELEMETRY_LINK_CREATE_IN_USE, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(socket, TEST_GetFuncRetPositive(1));
    Mock->Create(connect, MockConnectSuccess);
    Mock->Create(close, TEST_GetFuncRetPositive(0));
    int32_t ret = DpdkTelemetryLinkCreate();
    DT_ASSERT_EQUAL(ret, -EADDRINUSE);
    Mock->Delete(socket);
    Mock->Delete(connect);
    Mock->Delete(close);
    DeleteMock(Mock);
}

/**
 * @brief DpdkTelemetryLinkCreate: realpath(g_knetTelemetrySocket)失败
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_TELEMETRY_LINK_CREATE_REALPATH_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(socket, TEST_GetFuncRetPositive(1));
    Mock->Create(connect, MockConnectFail);  /* connect失败继续 */
    Mock->Create(realpath, MockRealpathNull);  /* 所有realpath返回NULL */
    Mock->Create(close, TEST_GetFuncRetPositive(0));
    int32_t ret = DpdkTelemetryLinkCreate();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(socket);
    Mock->Delete(connect);
    Mock->Delete(realpath);
    Mock->Delete(close);
    DeleteMock(Mock);
}

/**
 * @brief DpdkTelemetryLinkCreate: link失败
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_TELEMETRY_LINK_CREATE_LINK_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(socket, TEST_GetFuncRetPositive(1));
    Mock->Create(connect, MockConnectFail);
    /* realpath第一次返回NULL(跳过unlink), 第二次返回成功 */
    Mock->Create(realpath, MockRealpathSuccess);
    Mock->Create(link, MockLinkFail);
    Mock->Create(close, TEST_GetFuncRetPositive(0));
    int32_t ret = DpdkTelemetryLinkCreate();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(socket);
    Mock->Delete(connect);
    Mock->Delete(realpath);
    Mock->Delete(link);
    Mock->Delete(close);
    DeleteMock(Mock);
}

/**
 * @brief KNET_UninitDpdkTelemetry: unlink成功 -> KNET_OK
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_UNINIT_TELEMETRY_UNLINK_SUCCESS, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfgTele1);
    Mock->Create(realpath, MockRealpathSuccess);
    Mock->Create(unlink, MockUnlinkSuccess);
    int32_t ret = KNET_UninitDpdkTelemetry();
    DT_ASSERT_EQUAL(ret, (int)KNET_OK);
    Mock->Delete(KNET_GetCfg);
    Mock->Delete(realpath);
    Mock->Delete(unlink);
    DeleteMock(Mock);
}

/**
 * @brief KNET_UninitDpdkTelemetry: unlink失败 -> KNET_ERROR
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_UNINIT_TELEMETRY_UNLINK_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfgTele1);
    Mock->Create(realpath, MockRealpathSuccess);
    Mock->Create(unlink, MockUnlinkFail);
    int32_t ret = KNET_UninitDpdkTelemetry();
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);
    Mock->Delete(KNET_GetCfg);
    Mock->Delete(realpath);
    Mock->Delete(unlink);
    DeleteMock(Mock);
}

/**
 * @brief RegDpShowStatisticCmd: 多进程模式, rte_telemetry_register_cmd失败
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_REG_DP_SHOW_STATISTIC_CMD_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfgMulti);
    Mock->Create(rte_telemetry_register_cmd, TEST_GetFuncRetNegative(1));
    int ret = RegDpShowStatisticCmd();
    DT_ASSERT_EQUAL(ret, (int)KNET_ERROR);
    Mock->Delete(KNET_GetCfg);
    Mock->Delete(rte_telemetry_register_cmd);
    DeleteMock(Mock);
}

/**
 * @brief DpdkRuntimeDirInit: snprintf_s第一次失败
 */
static int MockSnprintfFail(char *dest, size_t destMax, size_t count, const char *fmt, ...)
{
    (void)dest; (void)destMax; (void)count; (void)fmt;
    return -1;
}

DTEST_CASE_F(DPDK_TELEMETRY, TEST_RUNTIME_DIR_INIT_SNPRINTF_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(getuid, TEST_GetFuncRetPositive(0));  /* root */
    Mock->Create(snprintf_s, MockSnprintfFail);
    int32_t ret = DpdkRuntimeDirInit();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(getuid);
    Mock->Delete(snprintf_s);
    DeleteMock(Mock);
}

/**
 * @brief DpdkTelemetryFindSocket: lstat成功且是socket, 但pid check失败
 * @note 已有TEST_INIT_TELEMETRY_PID_CHECK_SOCKET_FAIL覆盖socket失败路径
 */
DTEST_CASE_F(DPDK_TELEMETRY, TEST_FIND_SOCKET_PID_CHECK_FAIL, NULL, NULL)
{
    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(getuid, TEST_GetFuncRetPositive(0));
    Mock->Create(lstat, MockLstatSuccessSock);
    /* socket成功, connect失败 => pid check返回false => find socket返回-1 */
    Mock->Create(socket, TEST_GetFuncRetPositive(1));
    Mock->Create(connect, MockConnectFail);
    Mock->Create(close, TEST_GetFuncRetPositive(0));
    int32_t ret = DpdkTelemetryFindSocket();
    DT_ASSERT_EQUAL(ret, -1);
    Mock->Delete(getuid);
    Mock->Delete(lstat);
    Mock->Delete(socket);
    Mock->Delete(connect);
    Mock->Delete(close);
    DeleteMock(Mock);
}