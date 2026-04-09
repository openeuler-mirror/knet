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
DTEST_CASE_F(DPDK_TELEMETRY, TEST_INIT_UNINIT_DPDK_TELEMETRY_SUCCESS, NULL, NULL)
{
    unlink(KNET_DPDK_KNET_TELEMETRY);
    int32_t ret;
    int pipefd[2];
    int pipefdClose[2];
    char buffer[BUFFER_SIZE];
    g_pid = getpid();

    ret = CreateDirectory(KNET_DPDK_DIR);
    DT_ASSERT_EQUAL(ret, 0);
    ret = CreateDirectory(KNET_DPDK_KNET_DIR);
    DT_ASSERT_EQUAL(ret, 0);
    ret = pipe(pipefd);
    DT_ASSERT_EQUAL(ret, 0);
    ret = pipe(pipefdClose);
    DT_ASSERT_EQUAL(ret, 0);

    if (fork() == 0) {
        close(pipefd[PIPE_READ]);
        close(pipefdClose[PIPE_WRITE]);
        CreateSocket(KNET_DPDK_KNET_TELEMETRY, pipefd[PIPE_WRITE], pipefdClose[PIPE_READ]);
        exit(0);
    }
    close(pipefd[PIPE_WRITE]);
    close(pipefdClose[PIPE_READ]);

    char pipeBuffer[10];
    ret = read(pipefd[PIPE_READ], pipeBuffer, sizeof(pipeBuffer));
    DT_ASSERT_NOT_EQUAL(ret, KNET_OK);
    ret = strcmp(pipeBuffer, "READY");
    DT_ASSERT_EQUAL(ret, KNET_OK);
    KNET_INFO("socket is ready");

    ret = KNET_InitDpdkTelemetry();
    DT_ASSERT_EQUAL(ret, KNET_OK);
    KNET_INFO("knet dpdk telemetry init success");

    ret = KNET_UninitDpdkTelemetry();
    DT_ASSERT_EQUAL(ret, KNET_OK);
    KNET_INFO("knet dpdk telemetry uninit success");

    KTestMock *Mock = CreateMock();
    DT_ASSERT_NOT_EQUAL(Mock, NULL);
    Mock->Create(KNET_GetCfg, MockKnetGetCfg);
    Mock->Create(rte_memzone_reserve, MockRteMemzoneReserve);
    ret = KNET_InitDpdkTelemetry();
    DT_ASSERT_EQUAL(ret, KNET_OK);
    Mock->Delete(rte_memzone_reserve);

    ret = KNET_UninitDpdkTelemetry();
    DT_ASSERT_EQUAL(ret, KNET_OK);

    close(pipefd[PIPE_READ]);
    unlink(KNET_DPDK_KNET_TELEMETRY);
    Mock->Delete(KNET_GetCfg);
    DeleteMock(Mock);

    write(pipefdClose[PIPE_WRITE], "READY", READY_MSG_LENGTH);
    close(pipefdClose[PIPE_WRITE]);
}

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