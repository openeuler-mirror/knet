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
#ifndef KNET_TUN_H
#define KNET_TUN_H

#include <stdint.h>

#define IF_NAME_SIZE 16
#define INVALID_FD 0xFFFFFFFF
#define TUN_PRE_NAME "knet_tap"

/**
 * @brief 获取网络接口的索引值
 *
 * @param ifName [IN] 参数类型 const char*。网络接口的名称
 * @param ifNameLen [IN] 参数类型 size_t。接口名称字符串的长度
 * @param ifIndex [IN/OUT] 参数类型 int*。返回接口的索引值
 * @return int32_t 0：成功；-1：失败
 */
int32_t KNET_FetchIfIndex(const char *ifName, size_t ifNameLen, int *ifIndex);

/**
 * @brief 关闭TAP口
 *
 * @param fd [IN] 参数类型 int32_t。释放的文件描述符
 * @return int32_t 0：成功；-1：失败
 */
int32_t KNET_TapFree(int32_t fd);

/**
 * @brief 创建并初始化TAP口
 *
 * @param fd [IN] 参数类型 int32_t。创建TAP口时打开文件设备描述符的fd
 * @param tapIfIndex [IN] 参数类型 int*。TAP设备接口索引
 * @return int 0：成功；-1：失败
 */
int KNET_TAPCreate(int32_t *fd, int *tapIfIndex);

#endif