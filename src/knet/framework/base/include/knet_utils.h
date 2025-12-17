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

#ifndef __KNET_UTILS_H__
#define __KNET_UTILS_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KNET_THREAD_NAME_LEN 128

/**
 * @brief 获取cpu是否存在
 *
 * @param lcoreld [IN] 参数类型 int。cpu核心id
 * @return int 0：成功；-1：失败
 */
int KNET_CpuDetected(int lcoreld);

/**
 * @brief 正则匹配
 *
 * @param pattern [IN] 参数类型 const char*。指向正则表达式字符串的指针
 * @param string [IN] 参数类型 const char*。指向匹配字符串的指针
 * @return true 匹配
 * @return false 非法参数/编译失败/匹配失败/其他错误
 */
bool KNET_RegMatch(const char *pattern, const char *string);

/**
 * @brief 获取当前线程名
 *
 * @param name [OUT] 参数类型 char*。指向线程名的指针
 * @param len [IN] 参数类型 size_t。线程名长度
 * @return const char* 返回线程名，非法线程名时，返回"invalid thread name"，
 * 非法入参时，返回"invalid parameter"
 */
const char *KNET_GetSelfThreadName(char *name, size_t len);

/**
 * @brief 字符串转uint32_t类型数字
 * @param str 用于转数字的字符串
 * @param num 储存字符串转换后的数字
 * @return int 0：成功；-1：失败
 */
int KNET_TransStrToNum(const char *str, uint32_t *num);

#ifdef __cplusplus
}
#endif

#endif /* __KNET_UTILS_H__ */