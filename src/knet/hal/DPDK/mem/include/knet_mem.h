/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * K-NET is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: 内存管理相关操作
 */
#ifndef __KNET_MEM_H__
#define __KNET_MEM_H__

#define OSALMEM_TYPE "OSAL_MEM"

/**
 * @brief ut测试用修改线程g_runMode
 *
 * @param mode [IN] 参数类型 int8_t。要修改的运行模式
 */
#ifdef KNET_TEST
void KNET_SetRunMode(int8_t mode);
#endif

/**
 * @brief 内存分配
 *
 * @param size [IN] 参数类型 size_t。要分配的内存大小
 * @return void* 成功：分配内存块的首地址；失败：NULL
 */
void *KNET_MemAlloc(size_t size);

/**
 * @brief 内存释放
 *
 * @param ptr [IN] 参数类型 void*。要释放的内存块首地址
 */
void KNET_MemFree(void *ptr);

/**
 * @brief 内存模块设置退出标志
 * @param
 */
void KNET_MemSetFlagInSignalQuiting(void);
#endif