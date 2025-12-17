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

#ifndef K_NET_KNET_RAND_H
#define K_NET_KNET_RAND_H

#include "knet_types.h"

/**
 * @brief 获取随机数
 *
 * @param data [IN/OUT] 参数类型 uint8_t*。指向提供的缓冲区，read()会读取数据填充到data中
 * @param len [IN] 参数类型 uint32_t。指定最多读多少字节
 * @return int64_t 成功获取的随机数；失败时返回-1
 */
int64_t KNET_GetRandomNum(uint8_t *data, uint32_t len);

#endif // K_NET_KNET_RAND_H
