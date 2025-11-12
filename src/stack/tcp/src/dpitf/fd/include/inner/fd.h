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

#ifndef FD_H
#define FD_H

#include <stdint.h>

#include "dp_types.h"
#include "utils_base.h"
#include "utils_atomic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FD_TYPE_SOCKET 0
#define FD_TYPE_EPOLL  1

typedef struct {
    int (*close)(void* priv);
} FdOps_t;

typedef struct FileDesc {
    int              type;
    int              fdIdx;
    void*            priv;
    atomic32_t       ref;
    struct FileDesc* next;
    FdOps_t*         ops;
} Fd_t;

extern int g_fdOffset;

/**
 * @brief fd 模块初始化
 *
 * @return 成功返回 0 ，失败返回 -1
 */
int FD_Init(void);

/**
 * @brief fd 模块去初始化
 *
 * @return 成功返回 0 ，失败返回 -1
 */
void FD_Deinit(void);

/**
 * @brief 分配一个 fd 对象
 *
 * @return 成功返回 fd 对象，失败返回 NULL
 */
Fd_t* FD_Alloc(void);

/**
 * @brief 释放一个 fd 对象
 *
 */
void FD_Free(Fd_t* file);

/**
 * @brief 根据 fd 获取到 Fd_t 对象，会增加 Fd_t 的引用计数
 *
 * @return 失败返回 NULL
 */
int FD_Get(int fd, int type, Fd_t** file);

/**
 * @brief 减少 fd 的引用计数，当引用计数为 0 时，调用 release
 *
 * @return 失败返回 NULL
 */
void FD_Put(Fd_t* file);

/**
 * @brief 关闭 fd
 *
 * @param fd
 * @return 成功返回 0，错误返回 -1
 */
int FD_Close(int fd);

/**
 * @brief 返回文件句柄的最大数量
 *
 */
int FD_GetFileLimit(void);

static inline int FD_GetFdOffset(void)
{
    return g_fdOffset;
}

static inline int FD_GetRealFd(int fd)
{
    return fd - g_fdOffset;
}

static inline int FD_GetUserFd(Fd_t* file)
{
    return file->fdIdx + g_fdOffset;
}

#ifdef __cplusplus
}
#endif
#endif
