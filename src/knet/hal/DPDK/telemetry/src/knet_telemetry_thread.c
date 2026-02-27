/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: telemetry 持久化线程相关操作
 */
#include <dirent.h>
#include <limits.h> /* PATH_MAX */
#include <time.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "cJSON.h"
#include "rte_ethdev.h"
#include "rte_memzone.h"

#include "knet_telemetry_format.h"
#include "knet_telemetry_debug.h"
#include "knet_telemetry.h"
#include "knet_log.h"
#include "knet_config.h"
#include "knet_lock.h"
#include "knet_rpc.h"
#include "knet_thread.h"
#include "knet_types.h"

#define TIME_FORMAT_LEN 32  // 时间格式化长度
/* 缓冲区大小，dp数据较小，对于xstats,不同网卡的以及不同队列长度会影响数据的长度
   目前支持最大大小32队列长度约54000+字节
*/
#define DECIMAL 10
#define SINGLE_BLOCK_SIZE 60000
#define SINGLE_BLOCK_RESERVE 100 // 单个块保留100字节
#define TEMP_BUFF_LEN 30         // 临时buffer长度
#define DUMP_FILE_BUFFER_SIZE 4096
#define CATALOGUE_AUTHORITY 0750 // stats目录权限, 目录访问需要执行权限
#define FILE_AUTHORITY 0640      // 文件权限
#define FILE_AUTHORITY_DUMP 0440 // 转储后的文件权限
#define MAX_DUMP_FILE_NUM 9 // 文件转储前保持9个，转储会新增1个
#define MAX_COLLECT_DUMP_FILES 50    /* 定义一个足够大的缓冲区来收集转储文件，最多支持50个转储文件 */
#define DUMP_FILE_PREFIX_LEN 13       // "knet_persist-" 前缀长度
#define DUMP_FILE_TIMESTAMP_LEN 14    // 时间戳长度 YYYYmmddHHMMSS
#define DUMP_FILE_EXT_LEN 5           // ".json" 扩展名长度
#define DUMP_FILE_MIN_NAME_LEN (DUMP_FILE_PREFIX_LEN + DUMP_FILE_TIMESTAMP_LEN + DUMP_FILE_EXT_LEN)  // 文件名最小长度
#define DUMP_FILE_TIME_STR_LEN (DUMP_FILE_TIMESTAMP_LEN + 1)  // 时间戳字符串数组长度（含'\0'）
#define ROLLBACK_STR_LEN 2

#define SAVE_FILE_PATH "/etc/knet/run/stats"
#define SAVE_FILE_NAME "/etc/knet/run/stats/knet-persist.json"
#define SAVE_FILE_DUMP_NAME "/etc/knet/run/stats/knet_persist-%s.json"
#define DUMP_FILE_PREFIX "knet_persist-"  // 转储文件前缀
#define DUMP_FILE_EXT ".json"             // 转储文件扩展名

#define BIT_TEST(value, pos) (((value) & (1 << (pos))) != 0)

KNET_STATIC bool g_persistThreadExit = false;

char g_knetTeleToFileFormatOutput[SINGLE_BLOCK_SIZE] = {0};  // 单个进程所有统计数据缓冲区

struct ProcessInfo {
    pid_t pid;
    bool alive;
    time_t exitTime; // 进程退出时间
    int offset;             // 表示该进程写到文件的数据长度,用于计算偏移量
    int clientID;           // 该进程对应的rpc fd
};

struct KnetProcessInfo {
    struct ProcessInfo processInfo[MAX_PROCESS_NUM];
    int curProcessNum;
    int totalProcessNum;
    uint64_t writeBitMap; // 每有一个新增的进程写数据,就将对应位置的bit置为1,最多32进程
    KNET_RWLock lock;
};

typedef int (*RefreshDataFunc)(FILE *, struct KnetProcessInfo *, uint64_t *);
RefreshDataFunc g_refreshDataFuncByRunMode = NULL;
struct KnetProcessInfo g_processInfo = {0};

void InsertNewProcess(int i, pid_t pid, int clientID)
{
    g_processInfo.processInfo[i].pid = pid;
    g_processInfo.processInfo[i].alive = true;
    g_processInfo.processInfo[i].offset = 0;
    g_processInfo.processInfo[i].clientID = clientID;
    g_processInfo.curProcessNum++;
    int totalNum = g_processInfo.totalProcessNum;
    g_processInfo.totalProcessNum = totalNum >= MAX_PROCESS_NUM ? MAX_PROCESS_NUM : totalNum + 1;
    return;
}

void TelemetrySetNewProcess(int clientID, pid_t pid)
{
    if (pid == 0) {
        return;
    }
    KNET_RwlockWriteLock(&g_processInfo.lock);
    int *curProcessNum = &g_processInfo.curProcessNum;
    if (*curProcessNum >= MAX_PROCESS_NUM) { // 正常情况不应走到这，超出32个进程的情况启动都启动不了
        KNET_ERR("K-NET Telemetry persist set new process %d failed, too many processes", pid);
        KNET_RwlockWriteUnlock(&g_processInfo.lock);
        return;
    }
    /* 如果进程信息已满，找到最早退出的进程位置占掉 */
    if (g_processInfo.totalProcessNum >= MAX_PROCESS_NUM) {
        int oldestExitProc = -1;
        time_t oldestExitTime = time(NULL);
        for (int i = 0; i < MAX_PROCESS_NUM; i++) {
            if (!g_processInfo.processInfo[i].alive && oldestExitTime > g_processInfo.processInfo[i].exitTime) {
                oldestExitTime = g_processInfo.processInfo[i].exitTime;
                oldestExitProc = i;
            }
        }
        if (oldestExitProc == -1) {
            KNET_ERR("K-NET Telemetry can't find oldest exit process to replace");
        } else {
            InsertNewProcess(oldestExitProc, pid, clientID);
            g_processInfo.writeBitMap &= ~(1 << oldestExitProc); // 替换时将对应位置的bit清零
        }
    } else {
        /* 如果进程信息没满，添加到没添加过的地方 */
        for (int i = 0; i < MAX_PROCESS_NUM; i++) {
            if (g_processInfo.processInfo[i].pid == pid) { // 重复的pid通知
                break;
            } else if (g_processInfo.processInfo[i].pid == 0) { // 数组保证中间没有空位
                InsertNewProcess(i, pid, clientID);
                break;
            }
        }
    }
    KNET_RwlockWriteUnlock(&g_processInfo.lock);
}

void TelemetryDelOldProcess(int clientID, pid_t pid)
{
    (void)clientID;
    if (pid == 0) {
        return;
    }
    KNET_RwlockWriteLock(&g_processInfo.lock);
    for (int i = 0; i < MAX_PROCESS_NUM; i++) {
        if (g_processInfo.processInfo[i].pid == pid) {
            g_processInfo.processInfo[i].alive = false;
            g_processInfo.processInfo[i].exitTime = time(NULL);
            g_processInfo.processInfo[i].clientID = -1;
            g_processInfo.curProcessNum--;
            break;
        }
    }
    KNET_RwlockWriteUnlock(&g_processInfo.lock);
}

int TelemetryDisconnectHandler(int clientID, struct KNET_RpcMessage *knetRpcRequest,
                               struct KNET_RpcMessage *knetRpcResponse)
{
    (void)knetRpcRequest;
    (void)knetRpcResponse;
    KNET_RwlockWriteLock(&g_processInfo.lock);
    for (int i = 0; i < MAX_PROCESS_NUM; i++) {
        if (g_processInfo.processInfo[i].clientID == clientID) {
            g_processInfo.processInfo[i].alive = false;
            g_processInfo.processInfo[i].exitTime = time(NULL);
            g_processInfo.processInfo[i].clientID = -1;
            g_processInfo.curProcessNum--;
            break;
        }
    }
    KNET_RwlockWriteUnlock(&g_processInfo.lock);
    return 0;
}

int RegEventNotifyToRpc(void)
{
    struct KnetRpcReqNotifyTelemetry notifyFunc = {0};
    notifyFunc.addNewProcess = TelemetrySetNewProcess;
    notifyFunc.delOldProcess = TelemetryDelOldProcess;
    int ret = KNET_RpcRegTelemetryNotifyFunc(&notifyFunc);
    if (ret != 0) {
        return ret;
    }
    /* 从进程异常退出时，无法通过主从发送request消息通知主进程，所以需要注册一个disconnect事件，用于通知主进程 */
    ret = KNET_RpcRegServer(KNET_RPC_EVENT_DISCONNECT, KNET_RPC_MOD_TELEMETRY, TelemetryDisconnectHandler);
    return ret;
}

KNET_STATIC int ProcessDumpFileEntry(const struct dirent *entry, char *filePath, int *fileCount)
{
    /* 检查文件名格式是否正确 */
    const char *dot = strrchr(entry->d_name, '.');
    if (dot != NULL && strcmp(dot, DUMP_FILE_EXT) == 0) {
        int ret = snprintf_s(filePath, PATH_MAX + 1, PATH_MAX + 1, "%s/%s", SAVE_FILE_PATH,
                             entry->d_name);
        if (ret < 0) {
            KNET_ERR("K-NET telemetry cleanup old dump files failed. Format file path too long");
            return -1;
        }
        (*fileCount)++;
    }
    return 0;
}

KNET_STATIC int CollectDumpFiles(char dumpFiles[][PATH_MAX + 1], int maxFiles)
{
    DIR *dir = opendir(SAVE_FILE_PATH);
    if (dir == NULL) {
        KNET_ERR("K-NET telemetry cleanup old dump files failed. Open dir failed with errno %d", errno);
        return -1;
    }
    struct dirent *entry;
    int fileCount = 0;
    /* 遍历目录，收集所有转储文件 */
    while ((entry = readdir(dir)) != NULL && fileCount < maxFiles) {
        if (strncmp(entry->d_name, DUMP_FILE_PREFIX, DUMP_FILE_PREFIX_LEN) == 0) {
            int ret = ProcessDumpFileEntry(entry, dumpFiles[fileCount], &fileCount);
            if (ret < 0) {
                (void)closedir(dir);
                return -1;
            }
        }
    }
    /* 检查是否还有未处理的文件 */
    if (entry != NULL) {
        KNET_WARN("K-NET telemetry cleanup old dump files warning. More than %d dump files found, only first %d will "
                  "be processed",
                  maxFiles, maxFiles);
    }
    (void)closedir(dir);
    return fileCount;
}

static int ExtractTimestampInt(const char *filePath, long long *timestamp)
{
    /* 从文件名中提取时间戳字符串 */
    const char *basename = strrchr(filePath, '/');
    if (basename == NULL) {
        basename = filePath;
    } else {
        basename++;
    }
    /* 文件名格式: knet_persist-YYYYmmddHHMMSS.json */
    if (strlen(basename) < DUMP_FILE_MIN_NAME_LEN) {
        return -1;
    }

    const char *timeStr = basename + DUMP_FILE_PREFIX_LEN;
    if (strlen(timeStr) < DUMP_FILE_MIN_NAME_LEN - DUMP_FILE_PREFIX_LEN) {
        return -1;
    }
    /* 使用 strtoll 转换时间戳部分，会自动在.json处停止 */
    char *endptr;
    errno = 0;
    long long ts = strtoll(timeStr, &endptr, DECIMAL);
    if (errno != 0 || endptr == timeStr ||
        (endptr - timeStr) < DUMP_FILE_TIMESTAMP_LEN ||
        *endptr != '.' || ts < 0) {     /* 成功转换了至少1个字符，且停在正确位置(.json) */
        KNET_ERR("K-NET telemetry trans timestamp to long long failed. Invalid timestamp");
        return -1;
    }
    *timestamp = ts;
    return 0;
}

/**
 * @brief 从文件列表中找出最旧的文件
 *
 * @param dumpFiles 文件路径数组
 * @param fileCount 文件数量
 * @param oldestIndex 输出参数，最旧文件的索引
 * @return int 成功返回0，失败返回-1
 * @note 跳过空字符串（已删除标记）
 */
static int FindOldestDumpFile(char dumpFiles[][PATH_MAX + 1], int fileCount, int *oldestIndex)
{
    long long oldestTime = LLONG_MAX;
    bool found = false;

    for (int i = 0; i < fileCount; i++) {
        /* 跳过已删除的文件（空字符串标记） */
        if (dumpFiles[i][0] == '\0') {
            continue;
        }

        long long fileTime;
        if (ExtractTimestampInt(dumpFiles[i], &fileTime) != 0) {
            continue;
        }

        if (fileTime < oldestTime || !found) {
            oldestTime = fileTime;
            *oldestIndex = i;
            found = true;
        }
    }

    return found ? 0 : -1;
}

/**
 * @brief 检查并清理转储目录中的旧文件
 *
 * @return int 成功返回0，失败返回-1
 * @note 如果转储文件数量超过MAX_DUMP_FILE_NUM(10)，则删除最旧的文件，直到文件数不超过限制
 */
KNET_STATIC int CleanupOldDumpFiles(void)
{
    size_t totalSize = MAX_COLLECT_DUMP_FILES * (PATH_MAX + 1);
    char (*dumpFiles)[PATH_MAX + 1] = (char (*)[PATH_MAX + 1]) malloc(totalSize);
    if (dumpFiles == NULL) {
        KNET_ERR("K-NET telemetry cleanup old dump files failed. Malloc memory failed");
        return -1;
    }
    (void)memset_s(dumpFiles, totalSize, 0, totalSize);

    /* 收集目录中的所有转储文件 */
    int fileCount = CollectDumpFiles(dumpFiles, MAX_COLLECT_DUMP_FILES);
    if (fileCount < 0) {
        free(dumpFiles);
        return -1;
    }

    /* 如果文件数未超过限制，直接返回 */
    if (fileCount <= MAX_DUMP_FILE_NUM) {
        free(dumpFiles);
        return 0;
    }

    /* 循环删除最旧的文件，直到文件数不超过限制 */
    int filesToDelete = fileCount - MAX_DUMP_FILE_NUM;
    int deletedCount = 0;  /* 实际删除的文件数 */

    for (int i = 0; i < fileCount && deletedCount < filesToDelete; i++) {
        int oldestIndex = -1;
        if (FindOldestDumpFile(dumpFiles, fileCount, &oldestIndex) != 0) {
            KNET_ERR("K-NET telemetry cleanup old dump files failed. Cannot find oldest file");
            free(dumpFiles);
            return -1;
        }

        /* 删除最旧的文件 */
        if (unlink(dumpFiles[oldestIndex]) != 0) {
            KNET_ERR("K-NET telemetry cleanup old dump files failed. Delete oldest file %s failed with errno %d",
                     dumpFiles[oldestIndex], errno);
            free(dumpFiles);
            return -1;
        }

        /* 用空字符串标记该位置已删除 */
        dumpFiles[oldestIndex][0] = '\0';
        deletedCount++;
    }

    free(dumpFiles);
    return 0;
}

/**
 * @brief 将旧文件内容转储到以当前时间命名的新文件中
 *
 * @param oldFile 指向已打开的旧文件的FILE指针
 * @return int 成功返回0，失败返回-1
 * @note 新文件名格式由SAVE_FILE_DUMP_NAME宏定义，包含当前时间戳
 * @note 转储成功后会设置文件权限为FILE_AUTHORITY_DUMP(0440)
 * @warning 调用此函数前必须确保oldFile已正确打开
 */
int StartDumpOldFile(FILE *oldFile)
{
    /* 清理旧文件，最多保留MAX_DUMP_FILE_NUM个 */
    if (CleanupOldDumpFiles() != 0) {
        KNET_ERR("K-NET telemetry statistic persist dump failed. Cleanup old files failed");
        return -1;
    }

    char curTime[TEMP_BUFF_LEN + 1] = {0};
    time_t now = time(NULL);
    struct tm *tmInfo = localtime(&now);
    size_t sRet = strftime(curTime, sizeof(curTime), "%Y%m%d%H%M%S", tmInfo);
    if (sRet == 0) {
        KNET_ERR("K-NET telemetry statistic persist dump failed. Get time failed with errno %d", errno);
        return -1;
    }

    char dumpName[PATH_MAX + 1] = {0};
    int ret = sprintf_s(dumpName, sizeof(dumpName), SAVE_FILE_DUMP_NAME, curTime);
    if (ret < 0) {
        KNET_ERR("K-NET telemetry statistic persist dump failed. Format dump file name failed with errno %d", errno);
        return -1;
    }
    char resolvedPath[PATH_MAX + 1] = {0};
    if (realpath(SAVE_FILE_PATH, resolvedPath) == NULL) {
        KNET_ERR("K-NET telemetry statistic persist dump failed. Realpath check failed with errno %d", errno);
        return -1;
    }
    FILE *newFile = fopen(dumpName, "wb");
    if (newFile == NULL) {
        KNET_ERR("K-NET telemetry statistic persist dump failed, Open dump file failed with errno %d", errno);
        return -1;
    }
    char buffer[DUMP_FILE_BUFFER_SIZE];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), oldFile)) > 0) {
        if (fwrite(buffer, 1, bytes, newFile) != bytes) {
            KNET_ERR("K-NET telemetry statistic persist dump failed, Write file failed with errno %d", errno);
            (void)fclose(newFile);
            return -1;
        }
    }
    (void)fclose(newFile);
    /* 设置转储后的文件权限0440 */
    if (chmod(dumpName, FILE_AUTHORITY_DUMP) == -1) {
        KNET_ERR("K-NET telemetry statistic persist dump failed. Set dump file authority failed with errno %d", errno);
        return -1;
    }
    return 0;
}

/**
 * @brief 转储旧文件并清空当前统计文件
 * @return int 执行结果
 *   @retval 0 成功
    *   @retval -1 失败（路径创建失败/转储失败/权限设置失败）
 *
 * @note 文件路径由SAVE_FILE_PATH定义，文件名由SAVE_FILE_NAME定义
 */
int DumpOldFile(void)
{
    /* 确保文件路径存在 */
    char absPath[PATH_MAX + 1] = {0};
    char *retPath = realpath(SAVE_FILE_PATH, absPath);
    if (retPath == NULL) {
        if (errno != ENOENT) {
            KNET_ERR("K-NET telemetry statistic Persist realpath failed with errno %d", errno);
            return -1;
        }
        /* 路径不存在，则创建路径 */
        if (mkdir(SAVE_FILE_PATH, CATALOGUE_AUTHORITY) != 0) { // 0750 权限 rwxr-x---
            KNET_ERR("K-NET telemetry statistic persist mkdir failed with errno %d", errno);
            return -1;
        }
    }
    /* 旧文件不存在，无需转储 */
    FILE *oldFile = fopen(SAVE_FILE_NAME, "rb");
    if (oldFile == NULL) {
        return 0;
    }
    /* 转储文件 */
    int ret = StartDumpOldFile(oldFile);
    if (ret != 0) {
        (void)fclose(oldFile);
        KNET_ERR("K-NET telemetry statistic persist dump old file failed");
        return -1;
    }
    /* 写完后清空原文件内容,这里打开失败也没关系 */
    (void)fclose(oldFile);
    FILE *file = fopen(SAVE_FILE_NAME, "wb");
    if (file != NULL) {
        (void)fclose(file);
    }
    /* 设置文件的权限为0640 */
    if (chmod(SAVE_FILE_NAME, FILE_AUTHORITY) == -1) {
        KNET_ERR("K-NET telemetry statistic persist set save file authority failed with errno %d", errno);
        return -1;
    }
    return 0;
}

/**
 * @brief 处理文件删除后的进程信息刷新
 * 当文件被删除后，重新整理进程信息数组，去除中间空白项。
 * 使用临时数组顺序保存存活的进程信息，然后复制回原数组。
 * @return int 执行结果
 *   @retval 0 成功
 *   @retval -1 内存拷贝失败
 *
 * @note 该函数会获取写锁保护共享数据，操作完成后释放锁
 * @note 进程数限制为MAX_PROCESS_NUM，非高频操作，故使用数组而非链表
 */
int TelemetryPersistDealFileDelete(void)
{
    KNET_RwlockWriteLock(&g_processInfo.lock);
    /*
    文件删除后，需要重新刷新一遍进程相关信息:
    为了防止删除后的中间空白造成一些不必要的bug，这里新建一个数组顺序保存还在的进程信息
    因为进程只有32个，并且删除文件不是频繁操作，整个过程开销不大就不用链表去存了
    */
    struct ProcessInfo tempbuff[MAX_PROCESS_NUM] = {0};
    int curPos = 0;

    for (int i = 0; i < MAX_PROCESS_NUM; i++) {
        if (g_processInfo.processInfo[i].alive == true && g_processInfo.processInfo[i].pid != 0) {
            /* curPos <= i */
            tempbuff[curPos].pid = g_processInfo.processInfo[i].pid;
            tempbuff[curPos].alive = true;
            tempbuff[curPos].offset = 0;
            tempbuff[curPos].clientID = g_processInfo.processInfo[i].clientID;
            curPos++;
        }
    }
    size_t ProcessInfoSize = sizeof(g_processInfo.processInfo);
    (void)memset_s(g_processInfo.processInfo, ProcessInfoSize, 0, ProcessInfoSize);
    if (memcpy_s(g_processInfo.processInfo, ProcessInfoSize, tempbuff, curPos * sizeof(struct ProcessInfo)) != 0) {
        KNET_ERR("K-NET telemetry statistic persist deal file delete failed. Memcpy failed with errno %d", errno);
        KNET_RwlockWriteUnlock(&g_processInfo.lock);
        return -1;
    }

    g_processInfo.curProcessNum = curPos;
    g_processInfo.totalProcessNum = curPos;
    g_processInfo.writeBitMap = 0;
    KNET_RwlockWriteUnlock(&g_processInfo.lock);
    return 0;
}

FILE *OpenFileWithRWB(const char *filePath, const char *filename)
{
    FILE *fp = fopen(filename, "r+b");
    if (fp == NULL) {
        /* 文件不存在说明之前的数据丢失了，此时需要处理相关进程信息 */
        if (TelemetryPersistDealFileDelete() != 0) {
            return NULL;
        }
        /* 文件不存在则创建 */
        fp = fopen(filename, "w+b");
        if (fp == NULL) {
            KNET_ERR("K-NET open telemetry persist file failed with errno %d", errno);
            return NULL;
        }
        fclose(fp);
        /* 设置文件的权限为0640 */
        if (chmod(SAVE_FILE_NAME, FILE_AUTHORITY) == -1) {
            KNET_ERR("K-NET open telemetry persist file failed. Set persist file authority failed with errno %d",
                     errno);
            return NULL;
        }
        fp = fopen(filename, "r+b");
        if (fp == NULL) {
            KNET_ERR("K-NET open telemetry persist file failed with errno %d", errno);
            return NULL;
        }
    }
    return fp;
}

int WriteDataToFile(FILE *file, char *data, size_t len, int offset)
{
    if (file == NULL) {
        KNET_ERR("K-NET telemetry statistic persist write file failed, file is null");
        return -1;
    }
    /* 防止写入空字符,做一下校验 */
    size_t strLen = strlen(data);
    if (strLen < len) {
        KNET_ERR("K-NET telemetry statistic persist write file failed, data length does not match expected length %d",
                 len);
        return -1;
    }
    if (fseek(file, offset, SEEK_SET) != 0) {
        KNET_ERR("K-NET telemetry statistic persist seek file failed with errno %d", errno);
        return -1;
    }
    size_t written = fwrite(data, 1, len, file);
    if (written != len) {
        KNET_ERR("K-NET telemetry statistic persist write file failed with errno %d: %s", errno, strerror(errno));
        return -1;
    }
    return len;
}

int GetCurrentTime(char *buffer, size_t size)
{
    time_t now = time(NULL);
    struct tm *tnInfo = localtime(&now);
    size_t ret = strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tnInfo);
    if (ret == 0) {
        KNET_ERR("K-NET telemetry stats persist get time failed with errno %d", errno);
        return -1;
    }
    return 0;
}

int WriteDpdkXstats(FILE *file, int offset)
{
    uint16_t portID = 0;
    char *singleOutput = g_knetTeleToFileFormatOutput;
    int outputLeftLen = 0;
    int fileOffset = offset;
    int dataOffset = 0;

    RTE_ETH_FOREACH_DEV(portID)
    {
        (void)memset_s(singleOutput, SINGLE_BLOCK_SIZE, 0, SINGLE_BLOCK_SIZE);
        /* 缓冲区底部保留SINGLE_BLOCK_SIZE个字节防止写超出.后面会保证一次写不会超过这么多字节 */
        outputLeftLen = SINGLE_BLOCK_SIZE - SINGLE_BLOCK_RESERVE;
        dataOffset = FormatXstatsDataByPortId(singleOutput, &outputLeftLen, portID);
        if (dataOffset < 0) {
            KNET_ERR("K-NET telemetry persist format xstats data by port %d failed", portID);
            return -1;
        }
        /* 数据写入文件 */
        FORMAT_FUNC_SUCCESS(WriteDataToFile(file, singleOutput, dataOffset, fileOffset), fileOffset);
        if (fileOffset < 0) {
            KNET_ERR("K-NET telemetry persist write xstats failed");
            return -1;
        }
    }
    fileOffset -= offset; // 这里减去初始offset,得到实际写入的长度
    return fileOffset;
}

int RefreshSingleProcessData(char *output, int *outputLeftLen, pid_t pid, uint64_t sequence)
{
    /* 构造单进程统计数据头 */
    int offset = 0;
    int leftLen = *outputLeftLen;
    char curTime[TIME_FORMAT_LEN] = {0};
    int ret = GetCurrentTime(curTime, TIME_FORMAT_LEN);
    if (ret < 0) {
        KNET_ERR("K-NET telemetry stats persistence format time failed");
        return -1;
    }
    FORMAT_FUNC_SUCCESS(FormatingInCustom(output + offset, &leftLen,
                                          "\"pstats%0*llu\" : {\n\"pid\": %*d,\"date\": \"%s\",\n", FORMAT_INT_WIDTH,
                                          sequence, FORMAT_INT_WIDTH, pid, curTime),
                        offset);
    if (offset < 0) {
        return offset;
    }
    /* 构造dp的统计数据 */
    FORMAT_FUNC_SUCCESS(FormatEveryDpStats(output + offset, &leftLen), offset);
    if (offset < 0) {
        return offset;
    }
    /* 构造数据尾 */
    offset -= ROLLBACK_STR_LEN; // 回退最后的",\n"
    leftLen += ROLLBACK_STR_LEN;
    FORMAT_FUNC_SUCCESS(FormatingInCustom(output + offset, &leftLen, "},\n"), offset);
    *outputLeftLen = leftLen;
    return offset;
}

int WriteJsonHead(FILE *file, int offset)
{
    char temp[TEMP_BUFF_LEN + 1] = {0};
    int tempLeftLen = TEMP_BUFF_LEN;
    int len = FormatingInCustom(temp, &tempLeftLen, "{\n");
    if (len < 0) {
        KNET_ERR("K-NET telemetry persist write json head failed. Format head failed");
        return -1;
    }
    int ret = WriteDataToFile(file, temp, len, offset);
    if (ret < 0) {
        KNET_ERR("K-NET telemetry persist write json head failed. Write head failed");
        return -1;
    }
    return ret;
}


int WriteJsonTail(FILE *file, int offset)
{
    char temp[TEMP_BUFF_LEN + 1] = {0};
    int tempLeftLen = TEMP_BUFF_LEN;
    int tailOffset = offset - 2; // 回退最后的",\n"
    int len = FormatingInCustom(temp, &tempLeftLen, "\n}");
    if (len < 0) {
        KNET_ERR("K-NET telemetry persist write json tail failed. Format tail failed");
        return -1;
    }
    int ret = WriteDataToFile(file, temp, len, tailOffset);
    if (ret < 0) {
        KNET_ERR("K-NET telemetry persist write json tail failed. Write tail failed");
        return -1;
    }
    return ret;
}

/**
 * @brief 刷新并持久化单个进程的telemetry数据到文件
 *
 * @param file 目标文件指针，用于写入数据
 * @param knetProcessInfo 进程信息结构体指针，包含进程相关信息
 * @param sequence 序列号指针，用于记录和更新数据序列号
 * @return int 成功返回写入的文件偏移量，失败返回负数错误码
 */
int TelemetryRefreshDataSingle(FILE *file, struct KnetProcessInfo *knetProcessInfo, uint64_t *sequence)
{
    uint64_t curSequence = *sequence;
    struct ProcessInfo *processInfo = knetProcessInfo->processInfo;
    pid_t pid = processInfo[0].pid; // 单进程情况直接获取到第一个pid

    int fileOffset = 0;
    // 构造json头格式并写入文件
    FORMAT_FUNC_SUCCESS(WriteJsonHead(file, fileOffset), fileOffset);
    if (fileOffset < 0) {
        goto END;
    }
    /* 构造dpdk xstats数据并写入到文件 */
    FORMAT_FUNC_SUCCESS(WriteDpdkXstats(file, fileOffset), fileOffset);
    if (fileOffset < 0) {
        KNET_ERR("K-NET telemetry persist write xstats to file failed");
        goto END;
    }
    
    /* 构造单个进程统计json数据并写入文件 */
    char *singleOutput = g_knetTeleToFileFormatOutput;
    (void)memset_s(singleOutput, SINGLE_BLOCK_SIZE, 0, SINGLE_BLOCK_SIZE);
    int outputLeftLen = SINGLE_BLOCK_SIZE - SINGLE_BLOCK_RESERVE;
    int dataOffset = RefreshSingleProcessData(singleOutput, &outputLeftLen, pid, curSequence);
    if (dataOffset < 0) {
        KNET_ERR("K-NET telemetry persist get dp stats failed");
        goto END;
    }
    FORMAT_FUNC_SUCCESS(WriteDataToFile(file, singleOutput, dataOffset, fileOffset), fileOffset);
    if (fileOffset < 0) {
        KNET_ERR("K-NET telemetry persist write dp stats failed");
        goto END;
    }
    /* 构造json尾格式并写入文件.尾部写失败没办法，里面会打印errlog */
    FORMAT_FUNC_SUCCESS(WriteJsonTail(file, fileOffset), fileOffset);
END:
    curSequence += 1;
    *sequence = curSequence;
    return fileOffset;
}

int GetSingleProcessDpStatsMulti(char *singleOutput, int *outputLeftLen, int pid, bool formatLastTail,
                                 uint64_t sequence)
{
    int processDataOffset = 0;
    /* 提前通知子进程刷新所有统计信息 */
    NotifySubprocessRefreshDpState(pid);
    /* 存在一种情况:最后一个进程是死的, 需要处理文件中写好的json尾部, 并添加",\n" */
    if (formatLastTail) {
        FORMAT_FUNC_SUCCESS(FormatingInCustom(singleOutput, outputLeftLen, ",\n"), processDataOffset);
        if (processDataOffset < 0) {
            KNET_ERR("K-NET telemetry persist format last process tail failed, pid %d", pid);
            return -1;
        }
    }
    /* 刷新单个进程统计信息 */
    FORMAT_FUNC_SUCCESS(RefreshSingleProcessData(singleOutput + processDataOffset, outputLeftLen, pid, sequence),
                        processDataOffset);
    if (processDataOffset < 0) {
        KNET_ERR("K-NET telemetry persist refresh process %d failed", pid);
        return -1;
    }
    return processDataOffset;
}
/**
 * @brief 刷新每个子进程的遥测数据到文件
 * @param file 要写入的文件指针
 * @param fileOffset 文件起始偏移量
 * @param knetProcessInfo 进程信息结构体指针
 * @param sequence 序列号指针，用于生成唯一序列号
 * @return int 成功写入的总字节数，失败返回负值
 *
 * @note 函数会维护writeBitMap标记已写入的进程，避免重复写入
 * @note 每个进程数据写入前会保留SINGLE_BLOCK_RESERVE字节防止缓冲区溢出
 */
int TelemetryRefreshPerSubprocess(FILE *file, int fileOffset, struct KnetProcessInfo *knetProcessInfo,
                                  uint64_t *sequence)
{
    struct ProcessInfo *processInfo = knetProcessInfo->processInfo;
    uint64_t curSequence = *sequence;
     /* 缓冲区buff保留SINGLE_BLOCK_RESERVE个字节防止写超出，后面会保证一次写不会超过这么多字节 */
    char *singleOutput = g_knetTeleToFileFormatOutput;
    int outputLeftLen = SINGLE_BLOCK_SIZE - SINGLE_BLOCK_RESERVE;
    int processDataOffset = 0;
    int offset = fileOffset;
    bool formatLastTail = false;
    int ret = 0;
    // 遍历所有进程，插入式刷新活的进程的统计数据
    for (int i = 0; i < MAX_PROCESS_NUM; i++) {
        // 当前pid为0.那么后面的一定也都是0
        if (processInfo[i].pid == 0) {
            break;
        }
        // 只刷新活的进程，进程的死活通过rpc消息来通知维护
        if (!processInfo[i].alive) {
            // 死进程且写过文件，跳过
            if (BIT_TEST(knetProcessInfo->writeBitMap, i)) {
                offset += processInfo[i].offset; // 偏移到下一个进程尾部
                /* 第i+1位bit是0,表示第i个进程是写入文件里的最后一个进程
                    最后一个进程是dead进程则当前文件是最后的符号是json的尾部
                    当有新的进程需要写入时需要处理这个尾部
                */
                formatLastTail = !BIT_TEST(knetProcessInfo->writeBitMap, i + 1);
                continue;
            }
        }
        (void)memset_s(singleOutput, SINGLE_BLOCK_SIZE, 0, SINGLE_BLOCK_SIZE);
        outputLeftLen = SINGLE_BLOCK_SIZE - SINGLE_BLOCK_RESERVE;
        processDataOffset = GetSingleProcessDpStatsMulti(singleOutput, &outputLeftLen, processInfo[i].pid,
                                                         formatLastTail, curSequence++);
        if (processDataOffset < 0) {
            offset += processInfo[i].offset; // 偏移到下一个进程开始的地方
            continue;                        // 这个进程刷新失败了，继续刷新下一个进程
        }
        processInfo[i].offset = processDataOffset;
        if (formatLastTail) {
            /* 当前文件是{\n...,\n"pstats0":{}\n} 回退最后两个字符，加上前缀,\n 后面补新进程数据
               即将写入文件的内容是 ,\n"pstats1":{...},\n
            */
            offset -= ROLLBACK_STR_LEN;
            processInfo[i].offset = processDataOffset - ROLLBACK_STR_LEN; // 多算了一个前缀,\n, 不属于本进程，offset减去
        }
        // 写入单个进程数据
        ret = WriteDataToFile(file, singleOutput, processDataOffset, offset);
        if (ret < 0) {
            KNET_ERR("K-NET telemetry persist write process %d to file failed", processInfo[i].pid);
            offset += processInfo[i].offset; // 偏移到下一个进程开始的地方
            continue;
        }
        knetProcessInfo->writeBitMap |= (1 << i); // 标记当前进程已写入文件
        offset += processDataOffset;
    }
    offset -= fileOffset; // 这里减去初始offset,得到实际写入的长度
    *sequence = curSequence;
    return offset;
}

/**
 * @brief 刷新并写入多进程的DPDK统计数据到文件
 * @param file 目标文件指针，用于写入统计数据
 * @param knetProcessInfo 进程信息结构体指针，包含当前进程信息
 * @param sequence 序列号指针，用于跟踪数据更新
 * @return int 成功写入的字节数，负数表示错误
 * @warning 如果中间步骤失败会立即终止，但JSON尾部写入失败不会影响返回值
 */
int TelemetryRefreshDataMulti(FILE *file, struct KnetProcessInfo *knetProcessInfo, uint64_t *sequence)
{
    /* 如果当前没有进程则直接返回 */
    if (knetProcessInfo->curProcessNum == 0) {
        return 0;
    }
    int fileOffset = 0;
    /* 构造json头格式并写入文件 */
    FORMAT_FUNC_SUCCESS(WriteJsonHead(file, fileOffset), fileOffset);
    if (fileOffset < 0) {
        goto END;
    }
    /* 构造dpdk xstats数据并写入到文件 */
    FORMAT_FUNC_SUCCESS(WriteDpdkXstats(file, fileOffset), fileOffset);
    if (fileOffset < 0) {
        goto END;
    }
    /* 遍历所有子进程，依次写入活的进程的dp统计数据到文件里 */
    FORMAT_FUNC_SUCCESS(TelemetryRefreshPerSubprocess(file, fileOffset, knetProcessInfo, sequence), fileOffset);
    if (fileOffset < 0) {
        goto END;
    }
    /* 构造json尾格式并写入文件.尾部写失败没办法，里面会打印errlog */
    FORMAT_FUNC_SUCCESS(WriteJsonTail(file, fileOffset), fileOffset);
END:
    return fileOffset;
}

/**
 * @brief 初始化遥测数据持久化线程
 * 根据运行模式（单进程/多进程）初始化不同的数据刷新函数，并执行必要的初始化操作：
 * - 单进程模式：直接获取当前进程信息
 * - 多进程主进程模式：注册RPC事件通知和共享内存池
 * @return int 成功返回0，失败返回-1
 * @note 函数内部会打印错误日志
 */
int TelemetryPersistThreadInit(void)
{
    KNET_RwlockInit(&g_processInfo.lock);
    int32_t mode = KNET_GetCfg(CONF_COMMON_MODE)->intValue;
    int procType = KNET_GetCfg(CONF_INNER_PROC_TYPE)->intValue;
    int ret = 0;
    if (mode == KNET_RUN_MODE_SINGLE) {
        /* 单进程情况这里直接获取进程信息 */
        TelemetrySetNewProcess(0, getpid());
        g_refreshDataFuncByRunMode = TelemetryRefreshDataSingle;
    } else if (mode == KNET_RUN_MODE_MULTIPLE && procType == KNET_PROC_TYPE_PRIMARY) {
        /* 多进程情况需要向rpc注册事件通知函数 */
        ret = RegEventNotifyToRpc();
        if (ret != 0) {
            KNET_ERR("K-NET telemetry persistence reg rpc notify failed");
            return -1;
        }
        /* 注册共享内存池 */
        ret = TelemetryPersistMzInit();
        if (ret != 0) {
            KNET_ERR("K-NET telemetry persistence mz init failed");
            return -1;
        }
        g_refreshDataFuncByRunMode = TelemetryRefreshDataMulti;
    } else {
        return -1;
    }
    ret = TelemetryPersistInitGetDpStatFunc(mode);
    if (ret != 0) {
        return -1; /* 里面有log打印 */
    }
    /* 初始化dp json数据 */
    ret = TelemetryPersistInitDpJson();
    if (ret != 0) {
        return -1; /* 里面有log打印 */
    }
    return 0;
}

void KNET_TelemetrySetPersistThreadExit(void)
{
    g_persistThreadExit = true;
    return;
}

/**
 * @brief 遥测数据持久化线程主函数
 * 该线程负责周期性地将遥测数据持久化到文件中，包括初始化、数据刷新和资源清理。
 * @warning 线程内部使用1秒的刷新间隔，修改需谨慎
 */
void *KNET_TelemetryPersistThread(void *arg)
{
    (void)arg;
    int ret = TelemetryPersistThreadInit();
    if (ret != 0) {
        return NULL;
    }
    /* 转储旧文件 */
    if (DumpOldFile() != 0) {
        KNET_ERR("K-NET telemetry persist dump old file failed");
        TelemetryPersistUninitDpJson();
        return NULL;
    }
    sleep(3); // 等待3秒等待其他线程都起来
    FILE *file = NULL;
    uint64_t sequence = 0;
    do {
        if (g_persistThreadExit) {
            break;
        }
        sequence += 1; // 用于区分每个进程,不太可能溢出
        file = OpenFileWithRWB(SAVE_FILE_PATH, SAVE_FILE_NAME);
        if (file == NULL) {
            sleep(3); // 获取文件失败等待3秒再获取
            continue; // 里面会打印log
        }
        KNET_RwlockReadLock(&g_processInfo.lock);
        if (g_refreshDataFuncByRunMode != NULL) {
            g_refreshDataFuncByRunMode(file, &g_processInfo, &sequence);
        }
        KNET_RwlockReadUnlock(&g_processInfo.lock);
        (void)fclose(file);
        sleep(1); // 1s刷新一次
    } while (1);
    /* 去初始化dp json数据 */
    TelemetryPersistUninitDpJson();
    return NULL;
}

int32_t KNET_TelemetryStartPersistThread(int procType, int processMode)
{
    // 多进程下从进程不创建线程，此时为正常情况返回0
    if (processMode == KNET_RUN_MODE_MULTIPLE && procType == KNET_PROC_TYPE_SECONDARY) {
        return 0;
    }
    // 创建telemetry持久化线程
    pthread_t tid;
    if (KNET_CreateThread(&tid, KNET_TelemetryPersistThread, NULL) != 0) {
        KNET_ERR("K-NET reg telemetry Persist thread failed, errno %d", errno);
        return KNET_ERROR;
    }
    KNET_ThreadNameSet(tid, "knetPersist");
    return 0;
}